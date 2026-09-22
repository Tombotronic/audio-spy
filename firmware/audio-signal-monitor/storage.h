#pragma once

#include <Arduino.h>

#define RECORDINGS_DIR "/audio-signal-monitor"

// SD SPI pins (M5Cardputer Adv built-in slot)
#define SD_SPI_SCK_PIN 40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN 12

// If free space drops below this, the oldest recordings are deleted.
#define LOW_SPACE_MARGIN_BYTES (200ULL * 1024 * 1024)

// Initializes SPI + SD, and idempotently creates RECORDINGS_DIR.
// Returns false if the card can't be mounted.
bool storageInit();

uint64_t storageFreeBytes();
uint64_t storageTotalBytes();

// Deletes the oldest *.wav files in RECORDINGS_DIR until free space is
// back above LOW_SPACE_MARGIN_BYTES, or there's nothing left to delete.
void storageEnforceRollingLimit();
