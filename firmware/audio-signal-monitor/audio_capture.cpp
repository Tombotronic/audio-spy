#include "audio_capture.h"

#include <M5Cardputer.h>
#include <SD.h>

#include "app_state.h"
#include "storage.h"

// Small streaming buffer: filled by the mic, written to the slot's temp
// file, then reused. Keeps RAM use tiny regardless of chunk length.
static constexpr size_t STREAM_BUF_SAMPLES = 512;

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

        double sumSquares = 0.0;
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
            }
            sumSquares += blockSquares;
            recorded += want;
            {
                AppStateLock lock;
                g_state.levelRms = (float)(sqrt(blockSquares / want) / 32768.0);
                g_state.chunkSoFarRms = (float)(sqrt(sumSquares / recorded) / 32768.0);
            }
        }
        tmp.close();

        float rms = (float)(sqrt(sumSquares / CHUNK_SAMPLES) / 32768.0);
        Serial.printf("[audio] chunk rms=%.5f\n", rms);
        FilledChunk chunk{slotIndex, rms};
        xQueueSend(s_filledQueue, &chunk, portMAX_DELAY);
    }
}

void audioCaptureStart() {
    M5Cardputer.Speaker.end();  // mic and speaker are mutually exclusive on this hardware
    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[audio] Mic.begin() failed");
    }
    xTaskCreatePinnedToCore(captureTask, "audioCapture", 8192, nullptr, 3, nullptr, 1);
}
