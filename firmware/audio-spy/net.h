#pragma once

#include <Arduino.h>
#include <time.h>

// Connects to WiFi (blocks up to timeoutMs). Returns true on success.
bool netConnectWifi(uint32_t timeoutMs = 15000);

// Syncs system time via NTP (blocks up to timeoutMs, not at all while WiFi
// is down). Returns true once getLocalTime() reports a plausible
// (post-2020) time.
bool netSyncTime(uint32_t timeoutMs = 10000);

// Call from loop(): keeps AppState's WiFi/IP/time-synced fields current
// (WiFi reconnects on its own, and NTP keeps retrying after boot).
void netRefreshState();

// Timestamp-based filename for `when` (default: now), e.g.
// "2026-09-22_14-05-30.wav". Falls back to
// an incrementing counter (e.g. "unsynced-000123.wav") if time hasn't been
// synced yet, so recording is never blocked on NTP.
String netTimestampFilename(const char* extension = "wav", time_t when = 0);
