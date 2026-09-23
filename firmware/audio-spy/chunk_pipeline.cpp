#include "chunk_pipeline.h"

#include <SD.h>

#include "app_state.h"
#include "audio_capture.h"
#include "net.h"
#include "storage.h"
#include "wav_writer.h"

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
// threshold (Bypass Threshold marks every second loud). Kept: every second
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

// Consecutive chunks start CHUNK_SECONDS apart; more than this off means
// something (a pause) came between them.
static constexpr int CONTIGUOUS_TOLERANCE_S = 2;

static WavWriter s_writer;
static bool s_runActive = false;
static String s_runName;                      // file of the open run
static float s_lastRms = 0.0f;                // loudest second of the latest chunk
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

static String runPath(const String& name) {
    return String(RECORDINGS_DIR) + "/" + name;
}

// Opening a run truncates, so never reuse a name that's on the card.
static String unusedRunName(time_t audioStart) {
    String name = netTimestampFilename("wav", audioStart);
    // Unsynced names count up per call but restart at 000000 every boot.
    while (name.startsWith("unsynced-") && SD.exists(runPath(name))) {
        name = netTimestampFilename("wav", audioStart);
    }
    // Timestamps only repeat after the clock jumped back.
    String base = name.substring(0, name.length() - 4);
    for (int i = 1; SD.exists(runPath(name)); i++) name = base + "_" + i + ".wav";
    return name;
}

// Named after when its first kept sample was recorded, not when it's written
// (that's a chunk or more later).
static void startRun(time_t audioStart) {
    storageEnforceRollingLimit();
    s_runName = unusedRunName(audioStart);
    s_runActive = s_writer.beginRun(runPath(s_runName));
    if (s_runActive) setCurrentFile(s_runName);
}

static void endRun() {
    if (!s_runActive) return;
    s_writer.endRun();
    s_runActive = false;
    s_runName = "";
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
    if (s_runActive) {
        s_writer.checkpoint();
        // A long run (e.g. Bypass Threshold) must not fill the card either.
        storageEnforceRollingLimit(s_runName);
    }
    if (src) src.close();
    SD.remove(path);
    audioCaptureReleaseSlot(slotIndex);
}

// Decides the pending chunk without a "next" one (none follows directly:
// paused, or a gap in time) and closes any open run, rather than leaving
// it dangling or merging it with audio from after the gap.
static void flushPending() {
    if (s_pending.valid) {
        static const bool none[CHUNK_SECONDS] = {};
        bool keep[CHUNK_SECONDS];
        decideSeconds(s_prevLoud, s_pending.loud, none, keep);
        applyChunk(s_pending, keep);
        s_pending.valid = false;
    }
    endRun();
    memset(s_prevLoud, 0, sizeof(s_prevLoud));
    s_lastRms = 0.0f;
    publishLiveState(0.0f, false);
}

static void processChunk(const FilledChunk& chunk) {
    float threshold = currentThreshold();
    bool keepAll = bypassThreshold();
    PendingChunk current;
    current.valid = true;
    current.slotIndex = chunk.slotIndex;
    current.startTime = chunk.startTime;
    for (int s = 0; s < CHUNK_SECONDS; s++) {
        current.loud[s] = keepAll || chunk.secondRms[s] >= threshold;
    }

    if (s_pending.valid) {
        long gap = (long)(current.startTime - s_pending.startTime) - CHUNK_SECONDS;
        if (labs(gap) > CONTIGUOUS_TOLERANCE_S) flushPending();
    }
    if (s_pending.valid) {
        bool keep[CHUNK_SECONDS];
        decideSeconds(s_prevLoud, s_pending.loud, current.loud, keep);
        applyChunk(s_pending, keep);
        memcpy(s_prevLoud, s_pending.loud, sizeof(s_prevLoud));
    }
    s_pending = current;
    s_lastRms = chunk.rms;
    publishLiveState(s_lastRms, s_runActive);
}

static void pipelineTask(void*) {
    QueueHandle_t filled = audioCaptureFilledQueue();

    for (;;) {
        bool paused;
        {
            AppStateLock lock;
            paused = g_state.paused;
        }

        // Chunks captured before a pause are still processed while paused.
        FilledChunk chunk;
        if (xQueueReceive(filled, &chunk, pdMS_TO_TICKS(paused ? 100 : 500)) == pdTRUE) {
            processChunk(chunk);
            continue;
        }

        if (paused && (s_pending.valid || s_runActive)) {
            flushPending();
        } else {
            // Nothing new; still keep status (free space etc) fresh.
            publishLiveState(s_lastRms, s_runActive);
        }
    }
}

void chunkPipelineStart() {
    xTaskCreatePinnedToCore(pipelineTask, "chunkPipeline", 8192, nullptr, 2, nullptr, 0);
}
