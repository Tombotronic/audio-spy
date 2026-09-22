#pragma once

void statusScreenInit();

// Call periodically from loop(). Redraws recording state, a dB level meter
// and running chunk RMS (both with a threshold marker), and free SD space;
// blanks the screen entirely under stealth mode.
void statusScreenUpdate();
