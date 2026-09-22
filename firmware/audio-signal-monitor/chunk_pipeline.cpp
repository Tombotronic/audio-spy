#include "chunk_pipeline.h"

#include <SD.h>

#include "app_state.h"
#include "audio_capture.h"
#include "net.h"
#include "storage.h"
#include "wav_writer.h"

// Consumes and clears AppState.forceKeepRequested (a one-shot flag set by
// the web UI's force-keep button).
static bool consumeForceKeep() {
    AppStateLock lock;
    if (g_state.forceKeepRequested) {
        g_state.forceKeepRequested = false;
        return true;
    }
    return false;
}

static float currentThreshold() {
    AppStateLock lock;
    return g_state.thresholdRms;
}

static void publishLiveState(float rms, bool recording) {
    AppStateLock lock;
    g_state.liveRms = rms;
    g_state.isRecording = recording;
    g_state.freeBytes = storageFreeBytes();
    g_state.totalBytes = storageTotalBytes();
}

struct DecisionSlot {
    bool valid = false;
    uint8_t slotIndex = 0;
    bool keepSelf = false;
};

static WavWriter s_writer;
static bool s_runActive = false;
static bool s_prevPrevKeepSelf = false;
static DecisionSlot s_pending;  // one chunk "ago", awaiting its neighbor-keep decision

// Applies the effective keep/discard decision for a chunk slot: on keep,
// streams its temp file's bytes into the current merge run (starting one
// if needed); on discard, just drops the temp file. Either way the slot
// is freed for the capture task to reuse.
static void applyDecision(uint8_t slotIndex, bool effectiveKeep) {
    Serial.printf("[pipeline] slot=%d effectiveKeep=%d\n", slotIndex, effectiveKeep);
    String path = audioCaptureSlotPath(slotIndex);
    if (effectiveKeep) {
        if (!s_runActive) {
            storageEnforceRollingLimit();
            String runPath = String(RECORDINGS_DIR) + "/" + netTimestampFilename("wav");
            s_runActive = s_writer.beginRun(runPath);
        }
        if (s_runActive) {
            File src = SD.open(path, FILE_READ);
            if (src) {
                s_writer.appendFromFile(src, CHUNK_BYTES);
                src.close();
            }
        }
    } else if (s_runActive) {
        s_writer.endRun();
        s_runActive = false;
    }
    SD.remove(path);
    audioCaptureReleaseSlot(slotIndex);
}

// Called when pause is requested: there's no "next" chunk coming, so decide
// the pending chunk on what's already known and cleanly close any open run
// rather than leaving it dangling indefinitely.
static void flushOnPause() {
    if (s_pending.valid) {
        bool effectiveKeep = s_prevPrevKeepSelf || s_pending.keepSelf;
        applyDecision(s_pending.slotIndex, effectiveKeep);
        s_pending.valid = false;
    }
    if (s_runActive) {
        s_writer.endRun();
        s_runActive = false;
    }
    s_prevPrevKeepSelf = false;
    publishLiveState(0.0f, false);
}

static void pipelineTask(void*) {
    QueueHandle_t filled = audioCaptureFilledQueue();
    bool wasPaused = false;

    for (;;) {
        bool paused;
        {
            AppStateLock lock;
            paused = g_state.paused;
        }
        if (paused) {
            if (!wasPaused) flushOnPause();
            wasPaused = true;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        wasPaused = false;

        FilledChunk chunk;
        if (xQueueReceive(filled, &chunk, pdMS_TO_TICKS(500)) != pdTRUE) {
            // Nothing new; still keep status (free space etc) fresh.
            publishLiveState(g_state.liveRms, s_runActive);
            continue;
        }

        bool keepSelf = (chunk.rms >= currentThreshold()) || consumeForceKeep();
        publishLiveState(chunk.rms, s_runActive);

        DecisionSlot current{true, chunk.slotIndex, keepSelf};

        if (s_pending.valid) {
            bool effectiveKeep = s_prevPrevKeepSelf || s_pending.keepSelf || current.keepSelf;
            applyDecision(s_pending.slotIndex, effectiveKeep);
            s_prevPrevKeepSelf = s_pending.keepSelf;
        }
        s_pending = current;
    }
}

void chunkPipelineStart() {
    xTaskCreatePinnedToCore(pipelineTask, "chunkPipeline", 8192, nullptr, 2, nullptr, 0);
}
