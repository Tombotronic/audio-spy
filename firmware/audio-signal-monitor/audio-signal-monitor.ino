#include <M5Cardputer.h>

#include "app_state.h"
#include "audio_capture.h"
#include "chunk_pipeline.h"
#include "config.h"
#include "net.h"
#include "status_screen.h"
#include "storage.h"
#include "wav_writer.h"
#include "web_server.h"

Config g_config;

void setup() {
    Serial.begin(115200);
    delay(200);

    auto cfg = M5.config();
    cfg.internal_spk = false;  // physical speaker is never used; mic/speaker share the codec
    M5Cardputer.begin(cfg);
    appStateInit();
    statusScreenInit();

    statusScreenBootStep("SD card");
    if (!storageInit()) {
        statusScreenBootStep("SD card missing!");
        Serial.println("[boot] halting: SD card required");
        while (true) delay(1000);
    }

    wavRecoverInterruptedRun();
    g_config = configLoad();
    // Always boot with the screen on; stealth is a per-session choice.
    g_config.stealthMode = false;
    {
        AppStateLock lock;
        g_state.thresholdRms = g_config.threshold;
        g_state.stealthMode = g_config.stealthMode;
    }
    Serial.printf("[boot] config loaded: threshold=%.4f stealth=%d\n", g_config.threshold, g_config.stealthMode);

    statusScreenBootStep("WiFi");
    netConnectWifi();
    statusScreenBootStep("NTP time");
    netSyncTime();
    webServerStart();

    Serial.println("[boot] bootstrap complete");

    if (!audioCaptureInit()) {
        Serial.println("[boot] halting: failed to set up audio capture");
        while (true) delay(1000);
    }
    statusScreenBootStep("Mic");
    audioCaptureStart();
    chunkPipelineStart();

    Serial.println("[boot] recording pipeline started");
}

void loop() {
    M5Cardputer.update();
    webServerHandle();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        for (char c : M5Cardputer.Keyboard.keysState().word) {
            if (c == 'i' || c == 'I') {
                statusScreenShowIp();
                break;
            }
        }
    }
    statusScreenUpdate();
    delay(2);
}
