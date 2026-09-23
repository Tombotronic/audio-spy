#include "net.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "wifi_setup.h"

static bool tryConnect(const String& ssid, const String& pass, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect();
    delay(100);
    Serial.printf("[net] connecting to SSID \"%s\"\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    wl_status_t lastStatus = (wl_status_t)255;
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        wl_status_t s = WiFi.status();
        if (s != lastStatus) {
            Serial.printf("[net] status=%d\n", (int)s);
            lastStatus = s;
        }
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

bool netConnectWifi(uint32_t timeoutMs) {
    // Before the WiFi stack starts (the setup scan starts it too), or DHCP
    // doesn't send it.
    WiFi.setHostname(HOSTNAME);
    String ssid, pass;
    bool haveCreds = wifiLoadCreds(ssid, pass) || wifiPromptCreds(ssid, pass);

    bool connected = haveCreds && tryConnect(ssid, pass, timeoutMs);
    // Wrong password or a different network: let the user fix it on the
    // spot. No key within the timeout = boot on offline (keeps recording).
    while (haveCreds && !connected && wifiAskReenter(10000)) {
        if (!wifiPromptCreds(ssid, pass)) break;
        connected = tryConnect(ssid, pass, timeoutMs);
    }
    netRefreshState();

    // Started once; it follows reconnects and IP changes by itself. Without
    // saved WiFi the network stack never starts, so there's nothing to do.
    if (haveCreds) {
        if (MDNS.begin(HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[net] mDNS: http://" HOSTNAME ".local");
        } else {
            Serial.println("[net] mDNS failed to start");
        }
    }

    if (connected) {
        Serial.printf("[net] WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[net] WiFi connect failed/timed out");
    }
    return connected;
}

static bool clockIsSet(const struct tm& t) {
    // before NTP sync the clock starts at 1970
    return t.tm_year + 1900 >= 2020;
}

bool netSyncTime(uint32_t timeoutMs) {
    configTzTime(TZ_INFO, NTP_SERVER);
    // Offline there's nothing to wait for; SNTP keeps retrying in the
    // background and netRefreshState() notices once the clock is set.
    if (WiFi.status() != WL_CONNECTED) timeoutMs = 0;

    struct tm timeinfo;
    uint32_t start = millis();
    bool synced = false;
    while (millis() - start < timeoutMs) {
        if (getLocalTime(&timeinfo, 250)) {
            // getLocalTime can return true with an unset (1970) clock while
            // NTP is still in flight; only trust a post-2020 year.
            if (clockIsSet(timeinfo)) {
                synced = true;
                break;
            }
        }
    }

    {
        AppStateLock lock;
        g_state.timeSynced = synced;
    }

    if (synced) {
        Serial.println("[net] NTP time synced");
    } else {
        Serial.println("[net] NTP sync failed/timed out, will use fallback filenames");
    }
    return synced;
}

void netRefreshState() {
    static uint32_t lastRefresh = 0;
    static bool first = true;
    if (!first && millis() - lastRefresh < 2000) return;
    first = false;
    lastRefresh = millis();

    bool connected = WiFi.status() == WL_CONNECTED;
    String ip = connected ? WiFi.localIP().toString() : String();
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    AppStateLock lock;
    g_state.wifiConnected = connected;
    strlcpy(g_state.ipAddress, ip.c_str(), sizeof(g_state.ipAddress));
    g_state.timeSynced = clockIsSet(timeinfo);
}

String netTimestampFilename(const char* extension, time_t when) {
    if (when == 0) time(&when);
    struct tm timeinfo;
    localtime_r(&when, &timeinfo);
    if (clockIsSet(timeinfo)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &timeinfo);
        return String(buf) + "." + extension;
    }

    static uint32_t fallbackCounter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "unsynced-%06lu.%s", (unsigned long)fallbackCounter++, extension);
    return String(buf);
}
