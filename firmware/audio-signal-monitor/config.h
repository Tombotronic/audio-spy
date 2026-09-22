#pragma once

#include <Arduino.h>

#define CONFIG_PATH "/audio-signal-monitor/config.json"

struct Config {
    // Normalized RMS (0.0-1.0). This unit's ES8311 mic runs at minimum PGA
    // gain (M5Unified's default Cardputer ADV enable sequence), so ambient
    // room noise measures ~0.0002-0.001 in practice; 0.0015 sits just above
    // that. Recalibrate live once the web UI's level readout exists (#8).
    float threshold = 0.0015f;
    bool stealthMode = false;
    String webPassword = "cardputer";
};

// Loads config from CONFIG_PATH, creating the file with defaults if it
// doesn't exist yet. Returns the loaded (or newly-created default) config.
Config configLoad();

// Persists the given config to CONFIG_PATH.
bool configSave(const Config& cfg);
