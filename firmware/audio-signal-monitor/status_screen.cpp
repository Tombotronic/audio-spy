#include "status_screen.h"

#include <M5Cardputer.h>

#include "app_state.h"
#include "storage.h"

#define UPDATE_INTERVAL_MS 50

// Meter scale in dBFS. The mic is quiet (room ~-65, voice ~-45..-25), so a
// linear scale would leave the bar almost empty; dB spreads it out.
#define METER_MIN_DB -70.0f
#define METER_MAX_DB -10.0f
#define PEAK_HOLD_MS 1000

static M5Canvas canvas(&M5Cardputer.Display);
static bool s_wasStealth = false;

void statusScreenInit() {
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);
    canvas.setColorDepth(8);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    canvas.setTextDatum(top_left);
}

static float toDb(float rms) {
    return rms > 0 ? 20.0f * log10f(rms) : METER_MIN_DB;
}

// Maps an RMS value to a 0..width pixel offset on the dB scale.
static int meterX(float rms, int width) {
    float t = (toDb(rms) - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return (int)(t * width);
}

static void drawThresholdMarker(int x, int y, int h) {
    canvas.drawFastVLine(x, y - 3, h + 6, YELLOW);
    canvas.drawFastVLine(x + 1, y - 3, h + 6, YELLOW);
}

void statusScreenUpdate() {
    static uint32_t lastUpdate = 0;
    static float peakRms = 0;
    static uint32_t peakAt = 0;
    uint32_t now = millis();
    if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
    lastUpdate = now;

    bool stealth, paused, isRecording;
    float levelRms, chunkRms, threshold;
    uint64_t freeBytes;
    {
        AppStateLock lock;
        stealth = g_state.stealthMode;
        paused = g_state.paused;
        isRecording = g_state.isRecording;
        levelRms = g_state.levelRms;
        chunkRms = g_state.chunkSoFarRms;
        threshold = g_state.thresholdRms;
        freeBytes = g_state.freeBytes;
    }

    if (stealth) {
        if (!s_wasStealth) {
            M5Cardputer.Display.fillScreen(BLACK);
            M5Cardputer.Display.setBrightness(0);
        }
        s_wasStealth = true;
        return;
    }
    if (s_wasStealth) {
        M5Cardputer.Display.setBrightness(128);
        s_wasStealth = false;
    }

    if (paused) levelRms = chunkRms = 0;
    if (levelRms >= peakRms || now - peakAt > PEAK_HOLD_MS) {
        peakRms = levelRms;
        peakAt = now;
    }

    canvas.fillSprite(BLACK);

    // Header: state (left), current level in dB (right)
    canvas.setTextSize(2);
    canvas.setCursor(4, 4);
    if (paused) {
        canvas.setTextColor(YELLOW, BLACK);
        canvas.print("PAUSED");
    } else if (isRecording) {
        canvas.setTextColor(RED, BLACK);
        canvas.print("REC");
    } else {
        canvas.setTextColor(GREEN, BLACK);
        canvas.print("listening");
    }
    canvas.setTextColor(WHITE, BLACK);
    canvas.setTextDatum(top_right);
    canvas.drawString(String(toDb(levelRms), 0) + " dB", canvas.width() - 4, 4);
    canvas.setTextDatum(top_left);

    const int x0 = 4, w = canvas.width() - 8;
    const int thrX = x0 + meterX(threshold, w);

    // Main level meter: follows the voice in ~32ms steps
    const int levelY = 34, levelH = 34;
    canvas.drawRect(x0 - 1, levelY - 1, w + 2, levelH + 2, DARKGREY);
    int fill = meterX(levelRms, w);
    canvas.fillRect(x0, levelY, fill, levelH, levelRms >= threshold ? GREEN : DARKGREEN);
    int peakX = x0 + meterX(peakRms, w);
    canvas.drawFastVLine(peakX, levelY, levelH, WHITE);
    drawThresholdMarker(thrX, levelY, levelH);

    // dB scale ticks
    canvas.setTextSize(1);
    canvas.setTextColor(DARKGREY, BLACK);
    canvas.setTextDatum(top_center);
    for (int db = -60; db <= -20; db += 10) {
        int x = x0 + (int)((db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB) * w);
        canvas.drawFastVLine(x, levelY + levelH + 2, 3, DARKGREY);
        canvas.drawString(String(db), x, levelY + levelH + 6);
    }
    canvas.setTextDatum(top_left);

    // Chunk bar: running RMS of the current 10s chunk. This is the value the
    // keep/discard decision actually compares against the threshold.
    const int chunkY = 96, chunkH = 12;
    canvas.setTextColor(LIGHTGREY, BLACK);
    canvas.setCursor(x0, chunkY - 11);
    canvas.print("chunk avg");
    bool willKeep = chunkRms >= threshold;
    canvas.setTextColor(willKeep ? GREEN : DARKGREY, BLACK);
    canvas.setTextDatum(top_right);
    canvas.drawString(willKeep ? "KEEP" : "below threshold", x0 + w, chunkY - 11);
    canvas.setTextDatum(top_left);
    canvas.drawRect(x0 - 1, chunkY - 1, w + 2, chunkH + 2, DARKGREY);
    canvas.fillRect(x0, chunkY, meterX(chunkRms, w), chunkH, willKeep ? GREEN : DARKGREEN);
    drawThresholdMarker(thrX, chunkY, chunkH);

    // Footer: threshold value and free space
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(x0, canvas.height() - 10);
    canvas.printf("thr %.0f dB", toDb(threshold));
    canvas.setTextColor(LIGHTGREY, BLACK);
    canvas.setTextDatum(top_right);
    canvas.drawString(String((uint32_t)(freeBytes / (1024ULL * 1024 * 1024))) + " GB free", x0 + w, canvas.height() - 10);
    canvas.setTextDatum(top_left);

    canvas.pushSprite(0, 0);
}
