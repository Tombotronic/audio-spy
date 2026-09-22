#pragma once

void statusScreenInit();

// Call periodically from loop(). Redraws recording state, live RMS meter,
// WiFi IP, and free SD space; blanks the screen entirely under stealth mode.
void statusScreenUpdate();
