#pragma once

#include <Arduino.h>

// WiFi credentials live in NVS flash (namespace "wifi"), entered on the
// device itself with its keyboard, not compiled in. Stored as plaintext
// (no flash encryption): never sent over the network, but readable by
// anyone with USB access to the flash chip.

// Returns false if nothing is saved yet.
bool wifiLoadCreds(String& ssid, String& pass);
void wifiSaveCreds(const String& ssid, const String& pass);

// Blocking setup screens: scan and pick a network (or type a hidden one),
// then type the password. Saves the result.
void wifiPromptCreds(String& ssid, String& pass);

// Shows "WiFi failed" and waits up to timeoutMs for a key. Returns true if
// W was pressed (user wants to pick another network / retype the password).
bool wifiAskReenter(uint32_t timeoutMs);
