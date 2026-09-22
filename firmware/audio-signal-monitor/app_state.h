#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Runtime state shared between the core-1 capture task and the core-0
// web server / status screen loop. All access must go through the lock
// helpers below.
struct AppState {
    SemaphoreHandle_t mutex = nullptr;

    float thresholdRms = 0.02f;  // normalized 0.0-1.0, compared against chunk RMS
    bool stealthMode = false;
    bool paused = false;
    bool forceKeepRequested = false;

    bool isRecording = false;   // a merge run is currently open
    float liveRms = 0.0f;       // most recently measured chunk RMS
    float levelRms = 0.0f;      // RMS of the latest ~32ms mic block (fast meter)
    float chunkSoFarRms = 0.0f; // running RMS of the chunk being recorded
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
    char currentFile[64] = {0};
    char ipAddress[16] = {0};
    bool wifiConnected = false;
    bool timeSynced = false;
};

extern AppState g_state;

void appStateInit();

// RAII-style lock guard: `AppStateLock lock;` holds the mutex until it
// goes out of scope.
class AppStateLock {
public:
    AppStateLock() { xSemaphoreTake(g_state.mutex, portMAX_DELAY); }
    ~AppStateLock() { xSemaphoreGive(g_state.mutex); }
    AppStateLock(const AppStateLock&) = delete;
    AppStateLock& operator=(const AppStateLock&) = delete;
};
