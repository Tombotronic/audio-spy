#include "web_server.h"

#include <ArduinoJson.h>
#include <M5Unified.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <esp_random.h>
#include <mbedtls/md.h>

#include "app_state.h"
#include "config.h"
#include "icon_png.h"
#include "net.h"
#include "storage.h"
#include "version.h"
#include "wav_writer.h"
#include "web_ui.h"
#include "wifi_setup.h"

extern Config g_config;

static WebServer server(80);

// Login cookie. An iPhone home-screen app forgets Basic Auth credentials
// whenever iOS restarts it, but keeps persistent cookies. So a successful
// Basic Auth login also sets this cookie, and while it's there the password
// isn't asked for again. Its value is HMAC-SHA256(sessionKey, webPassword):
// nothing to store per login, it survives reboots, and changing either value
// logs every browser out. Lax (not Strict) so it's
// sent when the home-screen app launches; cross-site POSTs don't get it,
// and they fail the CSRF header check anyway.
static const char* SESSION_COOKIE = "audio_spy_session";
static constexpr uint32_t SESSION_MAX_AGE_S = 365UL * 24 * 3600;
static String s_sessionToken;

static String toHex(const uint8_t* bytes, size_t len) {
    String hex;
    char pair[3];
    for (size_t i = 0; i < len; i++) {
        sprintf(pair, "%02x", bytes[i]);
        hex += pair;
    }
    return hex;
}

// Needs WiFi running: esp_random() is only truly random while the radio is
// on (before that ESP-IDF calls it pseudo-random), and the key must not be
// predictable. Without WiFi the page can't be reached anyway, so the cookie
// is simply off for that boot. Returns "" then.
static String computeSessionToken() {
    if (g_config.sessionKey.length() == 0) {
        if (WiFi.getMode() == WIFI_OFF) return "";
        uint8_t key[32];
        esp_fill_random(key, sizeof(key));
        g_config.sessionKey = toHex(key, sizeof(key));
        configSave(g_config);
        Serial.println("[web] generated a new session key");
    }
    uint8_t mac[32];
    const String& key = g_config.sessionKey;
    const String& msg = g_config.webPassword;
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)key.c_str(), key.length(),
                    (const uint8_t*)msg.c_str(), msg.length(), mac);
    return toHex(mac, sizeof(mac));
}

// Takes as long for every wrong token, wherever it differs.
static bool tokensEqual(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

static bool hasSessionCookie() {
    if (s_sessionToken.length() == 0) return false;  // cookie off: never match an empty value
    String cookies = server.header("Cookie");
    String prefix = String(SESSION_COOKIE) + "=";
    int start = cookies.indexOf(prefix);
    // must be the cookie's own name, not the tail of another one
    while (start > 0 && cookies[start - 1] != ' ' && cookies[start - 1] != ';') {
        start = cookies.indexOf(prefix, start + 1);
    }
    if (start < 0) return false;
    start += prefix.length();
    int end = cookies.indexOf(';', start);
    String value = cookies.substring(start, end < 0 ? cookies.length() : end);
    value.trim();
    return tokensEqual(value, s_sessionToken);
}

static bool requireAuth() {
    if (hasSessionCookie()) return true;
    if (!server.authenticate("admin", g_config.webPassword.c_str())) {
        server.requestAuthentication();
        return false;
    }
    if (s_sessionToken.length() > 0) server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=" + s_sessionToken + "; Max-Age=" +
                                        SESSION_MAX_AGE_S + "; Path=/; HttpOnly; SameSite=Lax");
    return true;
}

// Every POST must carry this header (the web UI's fetch() sets it). Browsers
// attach cached Basic Auth credentials to cross-site form posts, so without
// it any web page could make a logged-in browser delete all recordings or
// forget the WiFi. A form can't set custom headers, and a cross-origin
// fetch() with one needs a CORS preflight, which this server never allows.
static const char* CSRF_HEADER = "X-Audio-Spy";

static bool requireAuthPost() {
    if (!requireAuth()) return false;
    if (server.header(CSRF_HEADER) != "1") {
        server.send(403, "text/plain", "missing " + String(CSRF_HEADER) + " header");
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

static String currentRecording() {
    AppStateLock lock;
    return String(g_state.currentFile);
}

static void handleIndex() {
    if (!requireAuth()) return;
    // Revalidate every load, so a flash shows up without a hard refresh.
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", WEB_INDEX_HTML);
}

// Icon and manifest are served without auth: iOS fetches them for Add to
// Home Screen without the page's credentials, and they reveal nothing.
static void handleIcon() {
    server.sendHeader("Cache-Control", "max-age=604800");
    server.send_P(200, "image/png", (const char*)ICON_PNG, ICON_PNG_LEN);
}

static void handleManifest() {
    server.send(200, "application/manifest+json",
                "{\"name\":\"Audio Spy\",\"short_name\":\"Audio Spy\","
                "\"start_url\":\"/\",\"display\":\"standalone\","
                "\"background_color\":\"#111111\",\"theme_color\":\"#111111\","
                "\"icons\":[{\"src\":\"/icon.png\",\"sizes\":\"180x180\",\"type\":\"image/png\"}]}");
}

// Streamed as chunked JSON a few entries at a time: the card can hold
// thousands of recordings, far more than fit in RAM as one document.
static void handleFiles() {
    if (!requireAuth()) return;

    String current = currentRecording();
    // Live data: Safari otherwise reuses a stale list (e.g. after Delete all).
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    String out = "[";
    bool first = true;
    storageForEachRecording([&](File& entry) {
        JsonDocument doc;
        doc["name"] = entry.name();
        doc["size"] = entry.size();
        doc["recording"] = current == entry.name();
        if (!first) out += ',';
        first = false;
        String item;
        serializeJson(doc, item);
        out += item;
        if (out.length() >= 1024) {
            server.sendContent(out);
            out = "";
        }
    });
    out += "]";
    server.sendContent(out);
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

    static constexpr size_t HEADER_BYTES = WAV_HEADER_BYTES;
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
    if (!requireAuthPost()) return;
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
// Names are collected in small batches (not all at once, see handleFiles),
// and deleted after the directory is closed, not while listing it.
static void handleDeleteAll() {
    if (!requireAuthPost()) return;

    static constexpr int BATCH = 32;
    int deleted = 0, skipped = 0;
    for (;;) {
        String current = currentRecording();
        String batch[BATCH];
        int count = 0;
        skipped = 0;
        storageForEachRecording([&](File& entry) {
            String name = entry.name();
            if (name == current) skipped++;
            else if (count < BATCH) batch[count++] = name;
        });
        int removed = 0;
        for (int i = 0; i < count; i++) {
            if (isCurrentRecording(batch[i])) continue;  // a run started meanwhile
            if (SD.remove(String(RECORDINGS_DIR) + "/" + batch[i])) removed++;
        }
        deleted += removed;
        if (count < BATCH || removed == 0) break;  // done, or stuck on undeletable files
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
        doc["hostname"] = netMdnsName();
        doc["ssid"] = WiFi.SSID();
        doc["wifiConnected"] = g_state.wifiConnected;
        doc["timeSynced"] = g_state.timeSynced;
        doc["uptimeS"] = millis() / 1000;
        doc["version"] = FIRMWARE_VERSION;
    }
    doc["battery"] = M5.Power.getBatteryLevel();  // 0-100, -1 if unknown

    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

static void handleSetThreshold() {
    if (!requireAuthPost()) return;
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
    if (!requireAuthPost()) return;
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
    if (!requireAuthPost()) return;
    {
        AppStateLock lock;
        g_state.paused = paused;
    }
    server.send(200, "text/plain", "ok");
}

static void handleSetBypass() {
    if (!requireAuthPost()) return;
    bool on = server.hasArg("on") && server.arg("on") == "1";
    {
        AppStateLock lock;
        g_state.bypassThreshold = on;
    }
    server.send(200, "text/plain", "ok");
}

// Clears the saved network and reboots into WiFi setup on the device.
// An interrupted recording is repaired at boot like after a power cut.
static void handleForgetWifi() {
    if (!requireAuthPost()) return;
    wifiForgetCreds();
    server.send(200, "text/plain", "ok");
    Serial.println("[web] WiFi credentials cleared, restarting");
    delay(500);  // let the response go out
    ESP.restart();
}

void webServerStart() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/icon.png", HTTP_GET, handleIcon);
    server.on("/manifest.json", HTTP_GET, handleManifest);
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
    server.on("/forgetwifi", HTTP_POST, handleForgetWifi);
    const char* headers[] = {CSRF_HEADER, "Cookie"};
    server.collectHeaders(headers, 2);
    s_sessionToken = computeSessionToken();
    server.begin();
    Serial.println("[web] server started on port 80");
}

void webServerHandle() {
    server.handleClient();
}
