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

static bool bypassThreshold() {
    AppStateLock lock;
    return g_state.bypassThreshold;
}

static void publishLiveState(float rms, bool recording) {
    AppStateLock lock;
    g_state.liveRms = rms;
    g_state.isRecording = recording;
    g_state.freeBytes = storageFreeBytes();
    g_state.totalBytes = storageTotalBytes();
}

// Name (not path) of the WAV currently being written, or "" between runs,
// so the web server never deletes a file that's still open.
static void setCurrentFile(const String& name) {
    AppStateLock lock;
    strncpy(g_state.currentFile, name.c_str(), sizeof(g_state.currentFile) - 1);
    g_state.currentFile[sizeof(g_state.currentFile) - 1] = '\0';
}

// Per-second keep decision. A second is "loud" when its RMS reaches the
// threshold (force-keep marks a whole chunk loud). Kept: every second
// within PRE_ROLL_S before / POST_ROLL_S after a loud second, plus silent
// gaps of up to MERGE_GAP_S between kept seconds, so pauses in speech don't
// split a recording into many files. Deciding the pre-roll needs the next
// chunk, so each chunk is decided one chunk late, over a 3-chunk window.
static constexpr int PRE_ROLL_S = 2;
static constexpr int POST_ROLL_S = 2;
static constexpr int MERGE_GAP_S = 5;
static constexpr int WINDOW_S = 3 * CHUNK_SECONDS;

struct PendingChunk {
    bool valid = false;
    uint8_t slotIndex = 0;
    time_t startTime = 0;
    bool loud[CHUNK_SECONDS] = {};
};

static WavWriter s_writer;
static bool s_runActive = false;
static bool s_prevLoud[CHUNK_SECONDS] = {};  // chunk before the pending one
static PendingChunk s_pending;                // one chunk "ago", awaiting its decision

// Fills keep[] for the middle chunk of the prev/cur/next window.
static void decideSeconds(const bool* prev, const bool* cur, const bool* next, bool* keep) {
    bool loud[WINDOW_S];
    memcpy(loud, prev, CHUNK_SECONDS);
    memcpy(loud + CHUNK_SECONDS, cur, CHUNK_SECONDS);
    memcpy(loud + 2 * CHUNK_SECONDS, next, CHUNK_SECONDS);

    bool padded[WINDOW_S];
    for (int i = 0; i < WINDOW_S; i++) {
        padded[i] = false;
        for (int j = max(0, i - POST_ROLL_S); j <= min(WINDOW_S - 1, i + PRE_ROLL_S); j++) {
            if (loud[j]) {
                padded[i] = true;
                break;
            }
        }
    }

    for (int s = 0; s < CHUNK_SECONDS; s++) {
        int i = CHUNK_SECONDS + s;
        keep[s] = padded[i];
        if (keep[s]) continue;
        int left = i - 1, right = i + 1;
        while (left >= 0 && !padded[left]) left--;
        while (right < WINDOW_S && !padded[right]) right++;
        keep[s] = left >= 0 && right < WINDOW_S && right - left - 1 <= MERGE_GAP_S;
    }
}

// Named after when its first kept sample was recorded, not when it's written
// (that's a chunk or more later).
static void startRun(time_t audioStart) {
    storageEnforceRollingLimit();
    String runName = netTimestampFilename("wav", audioStart);
    s_runActive = s_writer.beginRun(String(RECORDINGS_DIR) + "/" + runName);
    if (s_runActive) setCurrentFile(runName);
}

static void endRun() {
    if (!s_runActive) return;
    s_writer.endRun();
    s_runActive = false;
    setCurrentFile("");
}

// Copies the kept seconds of a chunk's temp file into the current run
// (starting/ending runs at kept/dropped boundaries), then drops the temp
// file and frees the slot for the capture task to reuse.
static void applyChunk(const PendingChunk& chunk, const bool* keep) {
    uint8_t slotIndex = chunk.slotIndex;
    char mask[CHUNK_SECONDS + 1];
    for (int s = 0; s < CHUNK_SECONDS; s++) mask[s] = keep[s] ? '#' : '.';
    mask[CHUNK_SECONDS] = '\0';
    Serial.printf("[pipeline] slot=%d keep=%s\n", slotIndex, mask);

    String path = audioCaptureSlotPath(slotIndex);
    File src;
    int s = 0;
    while (s < CHUNK_SECONDS) {
        if (!keep[s]) {
            endRun();
            s++;
            continue;
        }
        int e = s;
        while (e < CHUNK_SECONDS && keep[e]) e++;
        if (!s_runActive) startRun(chunk.startTime + s);
        if (s_runActive) {
            if (!src) src = SD.open(path, FILE_READ);
            if (src) {
                src.seek(s * BYTES_PER_SECOND);
                s_writer.appendFromFile(src, (e - s) * BYTES_PER_SECOND);
            }
        }
        s = e;
    }
    if (s_runActive) s_writer.checkpoint();
    if (src) src.close();
    SD.remove(path);
    audioCaptureReleaseSlot(slotIndex);
}

// Called when pause is requested: there's no "next" chunk coming, so decide
// the pending chunk on what's already known and cleanly close any open run
// rather than leaving it dangling indefinitely.
static void flushOnPause() {
    if (s_pending.valid) {
        static const bool none[CHUNK_SECONDS] = {};
        bool keep[CHUNK_SECONDS];
        decideSeconds(s_prevLoud, s_pending.loud, none, keep);
        applyChunk(s_pending, keep);
        s_pending.valid = false;
    }
    endRun();
    memset(s_prevLoud, 0, sizeof(s_prevLoud));
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

        float threshold = currentThreshold();
        bool force = consumeForceKeep() || bypassThreshold();
        PendingChunk current;
        current.valid = true;
        current.slotIndex = chunk.slotIndex;
        current.startTime = chunk.startTime;
        for (int s = 0; s < CHUNK_SECONDS; s++) {
            current.loud[s] = force || chunk.secondRms[s] >= threshold;
        }

        if (s_pending.valid) {
            bool keep[CHUNK_SECONDS];
            decideSeconds(s_prevLoud, s_pending.loud, current.loud, keep);
            applyChunk(s_pending, keep);
            memcpy(s_prevLoud, s_pending.loud, sizeof(s_prevLoud));
        }
        s_pending = current;
        publishLiveState(chunk.rms, s_runActive);
    }
}

void chunkPipelineStart() {
    xTaskCreatePinnedToCore(pipelineTask, "chunkPipeline", 8192, nullptr, 2, nullptr, 0);
}
