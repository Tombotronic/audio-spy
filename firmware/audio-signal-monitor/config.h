#pragma once

#include <Arduino.h>

#define CONFIG_PATH "/audio-signal-monitor/config.json"

struct Config {
    // Normalized RMS (0.0-1.0), compared against each second's RMS. Only the
    // first-boot default; normally set from the web UI's threshold slider.
    float threshold = 0.0015f;
    bool stealthMode = false;
    String webPassword = "cardputer";
};

// Loads config from CONFIG_PATH, creating the file with defaults if it
// doesn't exist yet. Returns the loaded (or newly-created default) config.
Config configLoad();

// Persists the given config to CONFIG_PATH.
bool configSave(const Config& cfg);
