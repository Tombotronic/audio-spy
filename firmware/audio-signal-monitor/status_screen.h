#pragma once

void statusScreenInit();

// Shows a boot-progress line (e.g. "WiFi...") before the main screen takes
// over, so a slow boot step doesn't look like a hung black screen.
void statusScreenBootStep(const char* step);

// Call periodically from loop(). Redraws recording state, a dB level meter
// and running chunk RMS (both with a threshold marker), and free SD space;
// blanks the screen entirely under stealth mode.
void statusScreenUpdate();

// Replaces the status screen with the device's IP address (where the web
// UI is served) for a few seconds. Ignored under stealth mode.
void statusScreenShowIp();
