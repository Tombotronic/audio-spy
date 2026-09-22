#include "net.h"

#include <WiFi.h>
#include <time.h>

#include "app_state.h"
#include "secrets.h"

bool netConnectWifi(uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.printf("[net] connecting to SSID \"%s\"\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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

    bool connected = WiFi.status() == WL_CONNECTED;
    {
        AppStateLock lock;
        g_state.wifiConnected = connected;
        if (connected) {
            strncpy(g_state.ipAddress, WiFi.localIP().toString().c_str(), sizeof(g_state.ipAddress) - 1);
        }
    }

    if (connected) {
        Serial.printf("[net] WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[net] WiFi connect failed/timed out");
    }
    return connected;
}

bool netSyncTime(uint32_t timeoutMs) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    struct tm timeinfo;
    uint32_t start = millis();
    bool synced = false;
    while (millis() - start < timeoutMs) {
        if (getLocalTime(&timeinfo, 250)) {
            // getLocalTime can return true with an unset (1970) clock while
            // NTP is still in flight; only trust a post-2020 year.
            if (timeinfo.tm_year + 1900 >= 2020) {
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

String netLocalIp() {
    AppStateLock lock;
    return String(g_state.ipAddress);
}

String netTimestampFilename(const char* extension) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &timeinfo);
        return String(buf) + "." + extension;
    }

    static uint32_t fallbackCounter = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "unsynced-%06lu.%s", (unsigned long)fallbackCounter++, extension);
    return String(buf);
}
