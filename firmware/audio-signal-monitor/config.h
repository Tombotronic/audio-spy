#pragma once

#include <Arduino.h>

#define CONFIG_PATH "/audio-signal-monitor/config.json"

struct Config {
    float threshold = 0.02f;
    bool stealthMode = false;
    String webPassword = "cardputer";
};

// Loads config from CONFIG_PATH, creating the file with defaults if it
// doesn't exist yet. Returns the loaded (or newly-created default) config.
Config configLoad();

// Persists the given config to CONFIG_PATH.
bool configSave(const Config& cfg);
