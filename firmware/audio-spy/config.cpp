#include "config.h"

#include <ArduinoJson.h>
#include <SD.h>

Config configLoad() {
    Config cfg;

    // A save was cut off between removing the old file and the rename.
    if (!SD.exists(CONFIG_PATH) && SD.exists(CONFIG_TMP_PATH)) SD.rename(CONFIG_TMP_PATH, CONFIG_PATH);

    File file = SD.open(CONFIG_PATH, FILE_READ);
    if (!file) {
        Serial.println("[config] no config.json found, creating defaults");
        configSave(cfg);
        return cfg;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) {
        Serial.printf("[config] failed to parse config.json (%s), using defaults; kept as config.json.bad\n",
                      err.c_str());
        SD.remove(CONFIG_BAD_PATH);
        SD.rename(CONFIG_PATH, CONFIG_BAD_PATH);
        configSave(cfg);
        return cfg;
    }

    cfg.threshold = doc["threshold"] | cfg.threshold;
    cfg.stealthMode = doc["stealthMode"] | cfg.stealthMode;
    cfg.webPassword = doc["webPassword"] | cfg.webPassword;
    cfg.sessionKey = doc["sessionKey"] | cfg.sessionKey;
    return cfg;
}

bool configSave(const Config& cfg) {
    JsonDocument doc;
    doc["threshold"] = cfg.threshold;
    doc["stealthMode"] = cfg.stealthMode;
    doc["webPassword"] = cfg.webPassword;
    doc["sessionKey"] = cfg.sessionKey;

    File file = SD.open(CONFIG_TMP_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("[config] failed to open config.json.tmp for writing");
        return false;
    }

    bool ok = serializeJson(doc, file) > 0;
    file.close();
    if (!ok) {
        SD.remove(CONFIG_TMP_PATH);
        return false;
    }
    // FAT can't rename over an existing file.
    SD.remove(CONFIG_PATH);
    return SD.rename(CONFIG_TMP_PATH, CONFIG_PATH);
}
