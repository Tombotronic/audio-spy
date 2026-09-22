#include <M5Cardputer.h>

#include "app_state.h"
#include "config.h"
#include "net.h"
#include "storage.h"

Config g_config;

void setup() {
    Serial.begin(115200);
    delay(200);

    M5Cardputer.begin();
    appStateInit();

    if (!storageInit()) {
        Serial.println("[boot] halting: SD card required");
        while (true) delay(1000);
    }

    g_config = configLoad();
    {
        AppStateLock lock;
        g_state.thresholdRms = g_config.threshold;
        g_state.stealthMode = g_config.stealthMode;
    }
    Serial.printf("[boot] config loaded: threshold=%.4f stealth=%d\n", g_config.threshold, g_config.stealthMode);

    netConnectWifi();
    netSyncTime();

    Serial.println("[boot] bootstrap complete");
}

void loop() {
    M5Cardputer.update();
    delay(10);
}
