#include "wifi_setup.h"

#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>

#include <vector>

// Size 2 text: ~20 chars per line, 8 lines on the 240x135 screen.
#define SETUP_TEXT_SIZE 2
#define MAX_SHOWN_NETWORKS 6  // keys 1-6, plus 0 for manual entry
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 63       // WPA passphrase limit

static Preferences s_prefs;

bool wifiLoadCreds(String& ssid, String& pass) {
    s_prefs.begin("wifi", true);
    ssid = s_prefs.getString("ssid", "");
    pass = s_prefs.getString("pass", "");
    s_prefs.end();
    return ssid.length() > 0;
}

void wifiSaveCreds(const String& ssid, const String& pass) {
    s_prefs.begin("wifi", false);
    s_prefs.putString("ssid", ssid);
    s_prefs.putString("pass", pass);
    s_prefs.end();
}

static void clearScreen() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(BLACK);
    d.setTextSize(SETUP_TEXT_SIZE);
    d.setTextColor(WHITE, BLACK);
    d.setCursor(0, 0);
}

// Reads one line from the keyboard. Enter confirms (only once there's
// content, unless allowEmpty), backspace edits. mask echoes '*'.
static String promptLine(const char* label, bool mask, bool allowEmpty, size_t maxLen) {
    String value;
    auto redraw = [&]() {
        clearScreen();
        M5Cardputer.Display.println(label);
        M5Cardputer.Display.println();
        M5Cardputer.Display.print("> ");
        for (size_t i = 0; i < value.length(); i++) {
            M5Cardputer.Display.print(mask ? '*' : value[i]);
        }
    };
    redraw();

    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto status = M5Cardputer.Keyboard.keysState();
            bool changed = false;
            for (char c : status.word) {
                if (value.length() >= maxLen) continue;
                value += c;
                changed = true;
            }
            if (status.del && value.length() > 0) {
                // drop the whole last UTF-8 character, not just its last byte
                size_t cut = value.length() - 1;
                while (cut > 0 && (((uint8_t)value[cut]) & 0xC0) == 0x80) cut--;
                value.remove(cut);
                changed = true;
            }
            if (status.enter && (allowEmpty || value.length() > 0)) return value;
            if (changed) redraw();
        }
        delay(10);
    }
}

// Scans and lists networks strongest first, deduped by name (mesh/repeater
// nodes show up once each). 1-6 picks one, 0 types a (hidden) SSID.
static String pickSsid() {
    clearScreen();
    M5Cardputer.Display.println("Scanning WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();

    std::vector<String> names;
    std::vector<int32_t> rssis;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;  // hidden: manual entry only
        int32_t r = WiFi.RSSI(i);
        size_t j = 0;
        while (j < names.size() && names[j] != s) j++;
        if (j < names.size()) {
            if (r > rssis[j]) rssis[j] = r;
        } else {
            names.push_back(s);
            rssis.push_back(r);
        }
    }
    WiFi.scanDelete();

    // selection sort, the list is tiny
    for (size_t i = 0; i < names.size(); i++) {
        size_t best = i;
        for (size_t j = i + 1; j < names.size(); j++) {
            if (rssis[j] > rssis[best]) best = j;
        }
        std::swap(names[i], names[best]);
        std::swap(rssis[i], rssis[best]);
    }

    int shown = min((int)names.size(), MAX_SHOWN_NETWORKS);
    clearScreen();
    M5Cardputer.Display.println(shown == 0 ? "No networks found." : "Select WiFi:");
    for (int i = 0; i < shown; i++) {
        String name = names[i];
        if (name.length() > 18) name = name.substring(0, 17) + ".";  // "N:" + 18 chars
        M5Cardputer.Display.printf("%d:%s\n", i + 1, name.c_str());
    }
    M5Cardputer.Display.println("0:Manual entry");

    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            for (char c : M5Cardputer.Keyboard.keysState().word) {
                if (c == '0') return promptLine("WiFi SSID:", false, false, MAX_SSID_LEN);
                if (c >= '1' && c < '1' + shown) return names[c - '1'];
            }
        }
        delay(10);
    }
}

void wifiPromptCreds(String& ssid, String& pass) {
    ssid = pickSsid();
    pass = promptLine("WiFi password:", true, true, MAX_PASS_LEN);  // empty = open network
    wifiSaveCreds(ssid, pass);
    Serial.printf("[wifi] saved credentials for SSID \"%s\"\n", ssid.c_str());
}

bool wifiAskReenter(uint32_t timeoutMs) {
    clearScreen();
    M5Cardputer.Display.println("WiFi failed.");
    M5Cardputer.Display.println();
    M5Cardputer.Display.println("W: change WiFi");
    M5Cardputer.Display.println("Other key: skip");

    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto status = M5Cardputer.Keyboard.keysState();
            for (char c : status.word) {
                if (c == 'w' || c == 'W') return true;
            }
            return false;
        }
        delay(10);
    }
    return false;
}
