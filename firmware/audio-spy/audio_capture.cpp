#include "audio_capture.h"

#include <M5Cardputer.h>
#include <SD.h>

#include "app_state.h"
#include "storage.h"

// Mic.record() only queues a buffer (the mic task has two request slots)
// and returns; the buffer is filled later. So three small buffers rotate:
// two are always queued, keeping capture continuous, while the oldest
// completed one is read. 500 samples (31.25 ms) divides a second evenly.
static constexpr size_t STREAM_BUF_SAMPLES = 500;
static constexpr int NUM_STREAM_BUFS = 3;
static_assert(CHUNK_SAMPLE_RATE % STREAM_BUF_SAMPLES == 0, "blocks must not straddle seconds");

// M5Unified leaves the ES8311's analog mic PGA at its minimum (0 dB), so
// recordings come out very quiet (quiet room ~-75 dBFS, voice peaks ~-20).
// Raising the analog gain lifts the signal above the ADC's own noise, which
// a digital boost can't do. REG14: bit4 selects Mic1p/Mic1n, bits[3:0] are
// the PGA gain in 3 dB steps (0..10 = 0..30 dB).
static constexpr int MIC_PGA_GAIN_DB = 18;
static constexpr uint8_t ES8311_I2C_ADDR = 0x18;
static constexpr uint8_t ES8311_REG_ADC_PGA = 0x14;

static QueueHandle_t s_freeQueue = nullptr;    // holds free slot indices (uint8_t)
static QueueHandle_t s_filledQueue = nullptr;  // holds FilledChunk structs

String audioCaptureSlotPath(uint8_t slotIndex) {
    return String(RECORDINGS_DIR) + "/_chunk" + slotIndex + ".tmp";
}

bool audioCaptureInit() {
    s_freeQueue = xQueueCreate(NUM_CHUNK_SLOTS, sizeof(uint8_t));
    s_filledQueue = xQueueCreate(NUM_CHUNK_SLOTS, sizeof(FilledChunk));
    if (!s_freeQueue || !s_filledQueue) {
        Serial.println("[audio] failed to create capture queues");
        return false;
    }

    for (uint8_t i = 0; i < NUM_CHUNK_SLOTS; i++) {
        xQueueSend(s_freeQueue, &i, 0);
    }

    Serial.printf("[audio] %d chunk slots ready (SD-backed, no PSRAM on this unit)\n", NUM_CHUNK_SLOTS);
    return true;
}

QueueHandle_t audioCaptureFilledQueue() {
    return s_filledQueue;
}

void audioCaptureReleaseSlot(uint8_t slotIndex) {
    xQueueSend(s_freeQueue, &slotIndex, portMAX_DELAY);
}

static int16_t s_streamBufs[NUM_STREAM_BUFS][STREAM_BUF_SAMPLES];
static int s_nextBuf = 0;        // buffer to queue next
static bool s_streaming = false; // two requests are queued (from then on, always)

// Returns the oldest completed block (valid until the next call) and queues
// another request, which waits until a request slot is free, i.e. until
// that block has been filled.
static const int16_t* nextBlock() {
    if (!s_streaming) {
        M5Cardputer.Mic.record(s_streamBufs[0], STREAM_BUF_SAMPLES, CHUNK_SAMPLE_RATE);
        M5Cardputer.Mic.record(s_streamBufs[1], STREAM_BUF_SAMPLES, CHUNK_SAMPLE_RATE);
        s_nextBuf = 2;
        s_streaming = true;
    }
    int done = (s_nextBuf + 1) % NUM_STREAM_BUFS;
    if (!M5Cardputer.Mic.record(s_streamBufs[s_nextBuf], STREAM_BUF_SAMPLES, CHUNK_SAMPLE_RATE)) {
        // Nothing was waited for: keep real-time pace and write silence.
        Serial.println("[audio] Mic.record() failed");
        vTaskDelay(pdMS_TO_TICKS(STREAM_BUF_SAMPLES * 1000 / CHUNK_SAMPLE_RATE));
        memset(s_streamBufs[done], 0, sizeof(s_streamBufs[done]));
    }
    s_nextBuf = (s_nextBuf + 1) % NUM_STREAM_BUFS;
    return s_streamBufs[done];
}

static float blockRms(const int16_t* block) {
    double squares = 0.0;
    for (size_t i = 0; i < STREAM_BUF_SAMPLES; i++) squares += (double)block[i] * block[i];
    return (float)(sqrt(squares / STREAM_BUF_SAMPLES) / 32768.0);
}

static void captureTask(void*) {
    for (;;) {
        bool paused;
        {
            AppStateLock lock;
            paused = g_state.paused;
        }
        if (paused) {
            // Keep listening so the level meters stay live, but write
            // nothing: no chunk, no temp file, no recording.
            float level = blockRms(nextBlock());
            AppStateLock lock;
            g_state.levelRms = level;
            g_state.chunkLoudestSecondRms = 0.0f;
            continue;
        }

        uint8_t slotIndex;
        if (xQueueReceive(s_freeQueue, &slotIndex, portMAX_DELAY) != pdTRUE) continue;

        String slotPath = audioCaptureSlotPath(slotIndex);
        File tmp = SD.exists(slotPath) ? SD.open(slotPath, "r+") : SD.open(slotPath, FILE_WRITE);
        if (!tmp) {
            Serial.println("[audio] failed to open chunk temp file");
            xQueueSend(s_freeQueue, &slotIndex, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        time_t chunkStart = time(nullptr);
        double secondSquares[CHUNK_SECONDS] = {0};
        float loudestSecond = 0.0f;  // of the seconds completed so far
        bool bypass = false;
        size_t recorded = 0;
        while (recorded < CHUNK_SAMPLES) {
            const size_t want = STREAM_BUF_SAMPLES;
            // Waits for the next block; the gap before the next call is just
            // this small file.write(), well inside the time the other queued
            // request takes, so no samples are dropped between blocks.
            const int16_t* block = nextBlock();
            tmp.write((const uint8_t*)block, want * sizeof(int16_t));
            for (size_t i = 0; i < want; i++) {
                double s = block[i];
                secondSquares[(recorded + i) / CHUNK_SAMPLE_RATE] += s * s;
            }
            size_t secondsBefore = recorded / CHUNK_SAMPLE_RATE;
            recorded += want;
            for (size_t sec = secondsBefore; sec < recorded / CHUNK_SAMPLE_RATE; sec++) {
                float r = (float)(sqrt(secondSquares[sec] / CHUNK_SAMPLE_RATE) / 32768.0);
                if (r > loudestSecond) loudestSecond = r;
            }
            {
                AppStateLock lock;
                g_state.levelRms = blockRms(block);
                g_state.chunkLoudestSecondRms = loudestSecond;
                paused = g_state.paused;
                bypass = bypass || g_state.bypassThreshold;
            }
            // Pause takes effect now: the partial chunk is handed on as is
            // (seconds not recorded have an RMS of 0 and no bytes to copy).
            if (paused) break;
        }
        tmp.close();

        FilledChunk chunk;
        chunk.slotIndex = slotIndex;
        chunk.samples = recorded;
        chunk.startTime = chunkStart;
        chunk.bypass = bypass;
        chunk.rms = 0.0f;
        for (int sec = 0; sec < CHUNK_SECONDS; sec++) {
            chunk.secondRms[sec] = (float)(sqrt(secondSquares[sec] / CHUNK_SAMPLE_RATE) / 32768.0);
            if (chunk.secondRms[sec] > chunk.rms) chunk.rms = chunk.secondRms[sec];
        }
        Serial.printf("[audio] chunk loudest second rms=%.5f\n", chunk.rms);
        xQueueSend(s_filledQueue, &chunk, portMAX_DELAY);
    }
}

bool audioCaptureStart() {
    M5Cardputer.Speaker.end();  // mic and speaker are mutually exclusive on this hardware
    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[audio] Mic.begin() failed");
        return false;
    }
    // Mic.begin() writes the codec's registers synchronously, so this sticks.
    uint8_t pga = 0x10 | (MIC_PGA_GAIN_DB / 3);
    M5.In_I2C.writeRegister8(ES8311_I2C_ADDR, ES8311_REG_ADC_PGA, pga, 100000);
    Serial.printf("[audio] mic PGA reg=0x%02X (wrote 0x%02X)\n",
                  M5.In_I2C.readRegister8(ES8311_I2C_ADDR, ES8311_REG_ADC_PGA, 100000), pga);
    xTaskCreatePinnedToCore(captureTask, "audioCapture", 8192, nullptr, 3, nullptr, 1);
    return true;
}
