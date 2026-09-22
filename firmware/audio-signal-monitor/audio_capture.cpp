#include "audio_capture.h"

#include <M5Cardputer.h>
#include <SD.h>

#include "app_state.h"
#include "storage.h"

// Small streaming buffer: filled by the mic, written to the slot's temp
// file, then reused. Keeps RAM use tiny regardless of chunk length.
static constexpr size_t STREAM_BUF_SAMPLES = 512;

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

static void captureTask(void*) {
    static int16_t streamBuf[STREAM_BUF_SAMPLES];

    for (;;) {
        bool paused;
        {
            AppStateLock lock;
            paused = g_state.paused;
        }
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint8_t slotIndex;
        if (xQueueReceive(s_freeQueue, &slotIndex, portMAX_DELAY) != pdTRUE) continue;

        File tmp = SD.open(audioCaptureSlotPath(slotIndex), FILE_WRITE);
        if (!tmp) {
            Serial.println("[audio] failed to open chunk temp file");
            xQueueSend(s_freeQueue, &slotIndex, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        time_t chunkStart = time(nullptr);
        double secondSquares[CHUNK_SECONDS] = {0};
        float loudestSecond = 0.0f;  // of the seconds completed so far
        size_t recorded = 0;
        while (recorded < CHUNK_SAMPLES) {
            size_t want = min(STREAM_BUF_SAMPLES, (size_t)(CHUNK_SAMPLES - recorded));
            // Blocks briefly on the mic's DMA queue; the gap before the next
            // call is just this small file.write(), well inside the queue's
            // buffering margin, so no samples are dropped between reads.
            M5Cardputer.Mic.record(streamBuf, want, CHUNK_SAMPLE_RATE);
            tmp.write((const uint8_t*)streamBuf, want * sizeof(int16_t));
            double blockSquares = 0.0;
            for (size_t i = 0; i < want; i++) {
                double s = streamBuf[i];
                blockSquares += s * s;
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
                g_state.levelRms = (float)(sqrt(blockSquares / want) / 32768.0);
                g_state.chunkLoudestSecondRms = loudestSecond;
            }
        }
        tmp.close();

        FilledChunk chunk;
        chunk.slotIndex = slotIndex;
        chunk.startTime = chunkStart;
        chunk.rms = 0.0f;
        for (int sec = 0; sec < CHUNK_SECONDS; sec++) {
            chunk.secondRms[sec] = (float)(sqrt(secondSquares[sec] / CHUNK_SAMPLE_RATE) / 32768.0);
            if (chunk.secondRms[sec] > chunk.rms) chunk.rms = chunk.secondRms[sec];
        }
        Serial.printf("[audio] chunk loudest second rms=%.5f\n", chunk.rms);
        xQueueSend(s_filledQueue, &chunk, portMAX_DELAY);
    }
}

void audioCaptureStart() {
    M5Cardputer.Speaker.end();  // mic and speaker are mutually exclusive on this hardware
    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[audio] Mic.begin() failed");
    }
    // Mic.begin() writes the codec's registers synchronously, so this sticks.
    uint8_t pga = 0x10 | (MIC_PGA_GAIN_DB / 3);
    M5.In_I2C.writeRegister8(ES8311_I2C_ADDR, ES8311_REG_ADC_PGA, pga, 100000);
    Serial.printf("[audio] mic PGA reg=0x%02X (wrote 0x%02X)\n",
                  M5.In_I2C.readRegister8(ES8311_I2C_ADDR, ES8311_REG_ADC_PGA, 100000), pga);
    xTaskCreatePinnedToCore(captureTask, "audioCapture", 8192, nullptr, 3, nullptr, 1);
}
