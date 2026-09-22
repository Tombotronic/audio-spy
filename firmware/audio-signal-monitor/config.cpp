#include "config.h"

#include <ArduinoJson.h>
#include <SD.h>

static void writeDefaults(Config& cfg) {
    cfg = Config();
}

Config configLoad() {
    Config cfg;

    File file = SD.open(CONFIG_PATH, FILE_READ);
    if (!file) {
        Serial.println("[config] no config.json found, creating defaults");
        writeDefaults(cfg);
        configSave(cfg);
        return cfg;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[config] failed to parse config.json (%s), using defaults\n", err.c_str());
        writeDefaults(cfg);
        configSave(cfg);
        return cfg;
    }

    cfg.threshold = doc["threshold"] | cfg.threshold;
    cfg.stealthMode = doc["stealthMode"] | cfg.stealthMode;
    cfg.webPassword = doc["webPassword"] | cfg.webPassword;
    return cfg;
}

bool configSave(const Config& cfg) {
    JsonDocument doc;
    doc["threshold"] = cfg.threshold;
    doc["stealthMode"] = cfg.stealthMode;
    doc["webPassword"] = cfg.webPassword;

    File file = SD.open(CONFIG_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("[config] failed to open config.json for writing");
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    return ok;
}
