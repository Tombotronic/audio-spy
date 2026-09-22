#include "web_server.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <WebServer.h>
#include <math.h>
#include <vector>

#include "app_state.h"
#include "config.h"
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

static bool isCurrentRecording(const String& name) {
    AppStateLock lock;
    return name == g_state.currentFile;
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
                o["recording"] = isCurrentRecording(name);
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

// Coarse waveform for the UI: max |sample| of a short window at each of n
// evenly spaced positions. Reads a few hundred KB at most from the SD card
// and sends ~n numbers, instead of the whole WAV going over WiFi.
static void handlePeaks() {
    if (!requireAuth()) return;
    if (!server.hasArg("name") || !isSafeRecordingName(server.arg("name"))) {
        server.send(400, "text/plain", "bad name");
        return;
    }
    int n = server.hasArg("n") ? server.arg("n").toInt() : 100;
    n = constrain(n, 10, 400);

    File file = SD.open(String(RECORDINGS_DIR) + "/" + server.arg("name"), FILE_READ);
    if (!file) {
        server.send(404, "text/plain", "not found");
        return;
    }

    static constexpr size_t HEADER_BYTES = 44;
    static constexpr size_t WINDOW_SAMPLES = 512;  // 32 ms at 16 kHz
    static int16_t window[WINDOW_SAMPLES];
    size_t size = file.size();
    uint32_t totalSamples = size > HEADER_BYTES ? (size - HEADER_BYTES) / 2 : 0;

    String out = "[";
    for (int i = 0; i < n && totalSamples > 0; i++) {
        uint32_t start = (uint64_t)totalSamples * i / n;
        size_t want = min((uint32_t)WINDOW_SAMPLES, totalSamples - start);
        file.seek(HEADER_BYTES + start * 2);
        size_t got = file.read((uint8_t*)window, want * 2) / 2;
        int peak = 0;
        for (size_t j = 0; j < got; j++) {
            int v = abs((int)window[j]);
            if (v > peak) peak = v;
        }
        if (i > 0) out += ',';
        out += peak;
    }
    out += "]";
    file.close();
    server.send(200, "application/json", out);
}

static void handleDelete() {
    if (!requireAuth()) return;
    if (!server.hasArg("name") || !isSafeRecordingName(server.arg("name"))) {
        server.send(400, "text/plain", "bad name");
        return;
    }

    if (isCurrentRecording(server.arg("name"))) {
        server.send(409, "text/plain", "recording in progress");
        return;
    }

    String path = String(RECORDINGS_DIR) + "/" + server.arg("name");
    if (SD.remove(path)) {
        server.send(200, "text/plain", "ok");
    } else {
        server.send(500, "text/plain", "delete failed");
    }
}

// Deletes every recording except the one currently being written (if any).
static void handleDeleteAll() {
    if (!requireAuth()) return;

    std::vector<String> names;
    File dir = SD.open(RECORDINGS_DIR);
    if (dir) {
        File entry = dir.openNextFile();
        while (entry) {
            String name = entry.name();
            if (!entry.isDirectory() && name.endsWith(".wav")) names.push_back(name);
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }

    int deleted = 0, skipped = 0;
    for (const String& name : names) {
        if (isCurrentRecording(name)) {
            skipped++;
            continue;
        }
        if (SD.remove(String(RECORDINGS_DIR) + "/" + name)) deleted++;
    }

    JsonDocument doc;
    doc["deleted"] = deleted;
    doc["skipped"] = skipped;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
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
        doc["bypassThreshold"] = g_state.bypassThreshold;
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
    if (!isfinite(value) || value < 0 || value > 1) {
        server.send(400, "text/plain", "value must be 0..1");
        return;
    }

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

// The lock is released before replying: a slow client must not hold up
// the capture task, which takes it for every mic block.
static void setPaused(bool paused) {
    if (!requireAuth()) return;
    {
        AppStateLock lock;
        g_state.paused = paused;
    }
    server.send(200, "text/plain", "ok");
}

static void handleSetBypass() {
    if (!requireAuth()) return;
    bool on = server.hasArg("on") && server.arg("on") == "1";
    {
        AppStateLock lock;
        g_state.bypassThreshold = on;
    }
    server.send(200, "text/plain", "ok");
}

void webServerStart() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/files", HTTP_GET, handleFiles);
    server.on("/stream", HTTP_GET, []() { handleStream(false); });
    server.on("/download", HTTP_GET, []() { handleStream(true); });
    server.on("/peaks", HTTP_GET, handlePeaks);
    server.on("/delete", HTTP_POST, handleDelete);
    server.on("/deleteall", HTTP_POST, handleDeleteAll);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/threshold", HTTP_POST, handleSetThreshold);
    server.on("/stealth", HTTP_POST, handleSetStealth);
    server.on("/pause", HTTP_POST, []() { setPaused(true); });
    server.on("/resume", HTTP_POST, []() { setPaused(false); });
    server.on("/bypass", HTTP_POST, handleSetBypass);
    server.begin();
    Serial.println("[web] server started on port 80");
}

void webServerHandle() {
    server.handleClient();
}
