#include "web_server.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <WebServer.h>

#include "app_state.h"
#include "config.h"
#include "net.h"
#include "storage.h"
#include "web_ui.h"

extern Config g_config;

static WebServer server(80);

static bool requireAuth() {
    if (!server.authenticate("admin", g_config.webPassword.c_str())) {
        server.requestAuthentication();
        return false;
    }
    return true;
}

// Recording filenames are plain "<stuff>.wav" with no path separators;
// reject anything else so a query param can never escape RECORDINGS_DIR.
static bool isSafeRecordingName(const String& name) {
    if (name.length() == 0 || name.length() > 64) return false;
    if (!name.endsWith(".wav")) return false;
    if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) return false;
    return true;
}

static void handleIndex() {
    if (!requireAuth()) return;
    server.send_P(200, "text/html", WEB_INDEX_HTML);
}

static void handleFiles() {
    if (!requireAuth()) return;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    File dir = SD.open(RECORDINGS_DIR);
    if (dir) {
        File entry = dir.openNextFile();
        while (entry) {
            String name = entry.name();
            if (!entry.isDirectory() && name.endsWith(".wav")) {
                JsonObject o = arr.add<JsonObject>();
                o["name"] = name;
                o["size"] = entry.size();
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleStream(bool download) {
    if (!requireAuth()) return;
    if (!server.hasArg("name") || !isSafeRecordingName(server.arg("name"))) {
        server.send(400, "text/plain", "bad name");
        return;
    }

    String path = String(RECORDINGS_DIR) + "/" + server.arg("name");
    File file = SD.open(path, FILE_READ);
    if (!file) {
        server.send(404, "text/plain", "not found");
        return;
    }

    if (download) {
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + server.arg("name") + "\"");
    }
    server.streamFile(file, "audio/wav");
    file.close();
}

static void handleDelete() {
    if (!requireAuth()) return;
    if (!server.hasArg("name") || !isSafeRecordingName(server.arg("name"))) {
        server.send(400, "text/plain", "bad name");
        return;
    }

    String path = String(RECORDINGS_DIR) + "/" + server.arg("name");
    if (SD.remove(path)) {
        server.send(200, "text/plain", "ok");
    } else {
        server.send(500, "text/plain", "delete failed");
    }
}

static void handleStatus() {
    if (!requireAuth()) return;

    JsonDocument doc;
    {
        AppStateLock lock;
        doc["isRecording"] = g_state.isRecording;
        doc["paused"] = g_state.paused;
        doc["liveRms"] = g_state.liveRms;
        doc["levelRms"] = g_state.levelRms;
        doc["threshold"] = g_state.thresholdRms;
        doc["stealthMode"] = g_state.stealthMode;
        doc["freeBytes"] = g_state.freeBytes;
        doc["totalBytes"] = g_state.totalBytes;
        doc["ip"] = g_state.ipAddress;
        doc["wifiConnected"] = g_state.wifiConnected;
        doc["timeSynced"] = g_state.timeSynced;
        doc["uptimeS"] = millis() / 1000;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleSetThreshold() {
    if (!requireAuth()) return;
    if (!server.hasArg("value")) {
        server.send(400, "text/plain", "missing value");
        return;
    }

    float value = server.arg("value").toFloat();
    if (value < 0) value = 0;

    g_config.threshold = value;
    configSave(g_config);
    {
        AppStateLock lock;
        g_state.thresholdRms = value;
    }
    server.send(200, "text/plain", "ok");
}

static void handleSetStealth() {
    if (!requireAuth()) return;
    bool on = server.hasArg("on") && server.arg("on") == "1";

    g_config.stealthMode = on;
    configSave(g_config);
    {
        AppStateLock lock;
        g_state.stealthMode = on;
    }
    server.send(200, "text/plain", "ok");
}

static void handlePause() {
    if (!requireAuth()) return;
    AppStateLock lock;
    g_state.paused = true;
    server.send(200, "text/plain", "ok");
}

static void handleResume() {
    if (!requireAuth()) return;
    AppStateLock lock;
    g_state.paused = false;
    server.send(200, "text/plain", "ok");
}

static void handleForceKeep() {
    if (!requireAuth()) return;
    AppStateLock lock;
    g_state.forceKeepRequested = true;
    server.send(200, "text/plain", "ok");
}

void webServerStart() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/files", HTTP_GET, handleFiles);
    server.on("/stream", HTTP_GET, []() { handleStream(false); });
    server.on("/download", HTTP_GET, []() { handleStream(true); });
    server.on("/delete", HTTP_POST, handleDelete);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/threshold", HTTP_POST, handleSetThreshold);
    server.on("/stealth", HTTP_POST, handleSetStealth);
    server.on("/pause", HTTP_POST, handlePause);
    server.on("/resume", HTTP_POST, handleResume);
    server.on("/forcekeep", HTTP_POST, handleForceKeep);
    server.begin();
    Serial.println("[web] server started on port 80");
}

void webServerHandle() {
    server.handleClient();
}
