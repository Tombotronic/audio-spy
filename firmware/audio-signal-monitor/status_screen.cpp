#include "status_screen.h"

#include <M5Cardputer.h>

#include "app_state.h"
#include "storage.h"

#define UPDATE_INTERVAL_MS 500

static M5Canvas canvas(&M5Cardputer.Display);
static bool s_wasStealth = false;

void statusScreenInit() {
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);
    canvas.setColorDepth(8);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
}

void statusScreenUpdate() {
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
    lastUpdate = now;

    bool stealth, paused, isRecording, wifiConnected;
    float liveRms;
    uint64_t freeBytes, totalBytes;
    char ip[16];
    {
        AppStateLock lock;
        stealth = g_state.stealthMode;
        paused = g_state.paused;
        isRecording = g_state.isRecording;
        wifiConnected = g_state.wifiConnected;
        liveRms = g_state.liveRms;
        freeBytes = g_state.freeBytes;
        totalBytes = g_state.totalBytes;
        strncpy(ip, g_state.ipAddress, sizeof(ip));
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

    canvas.fillSprite(BLACK);

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

    // Live RMS meter bar
    int barX = 4, barY = 28, barW = canvas.width() - 8, barH = 14;
    canvas.drawRect(barX, barY, barW, barH, DARKGREY);
    int fillW = (int)(liveRms / 0.05f * barW);
    if (fillW > barW - 2) fillW = barW - 2;
    if (fillW < 0) fillW = 0;
    canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, GREENYELLOW);

    canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(4, 52);
    canvas.printf("WiFi:%s", wifiConnected ? ip : " none");

    canvas.setCursor(4, 76);
    uint64_t freeMB = freeBytes / (1024 * 1024);
    uint64_t totalMB = totalBytes / (1024 * 1024);
    canvas.printf("%llu/%lluMB free", freeMB, totalMB);

    canvas.pushSprite(0, 0);
}
