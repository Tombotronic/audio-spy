#include "storage.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <vector>

bool storageInit() {
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        Serial.println("[storage] SD card mount failed");
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        Serial.println("[storage] no SD card attached");
        return false;
    }

    // One-time move from the project's old name, so recordings and
    // config.json survive the rename.
    if (!SD.exists(RECORDINGS_DIR) && SD.exists(LEGACY_RECORDINGS_DIR)) {
        if (SD.rename(LEGACY_RECORDINGS_DIR, RECORDINGS_DIR)) {
            Serial.println("[storage] moved " LEGACY_RECORDINGS_DIR " to " RECORDINGS_DIR);
        } else {
            Serial.println("[storage] failed to move " LEGACY_RECORDINGS_DIR);
        }
    }

    // Idempotent: SD.mkdir returns true if the dir already exists too.
    if (!SD.exists(RECORDINGS_DIR)) {
        if (!SD.mkdir(RECORDINGS_DIR)) {
            Serial.println("[storage] failed to create recordings dir");
            return false;
        }
    }

    Serial.printf("[storage] mounted, %llu MB total, %llu MB free\n",
                  storageTotalBytes() / (1024 * 1024), storageFreeBytes() / (1024 * 1024));
    return true;
}

uint64_t storageFreeBytes() {
    return SD.totalBytes() - SD.usedBytes();
}

uint64_t storageTotalBytes() {
    return SD.totalBytes();
}

// Filenames are NTP timestamps (e.g. 2026-09-22_14-05-30.wav), so
// lexicographic order is chronological order.
static std::vector<String> listRecordingsOldestFirst() {
    std::vector<String> names;
    File dir = SD.open(RECORDINGS_DIR);
    if (!dir) return names;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".wav")) names.push_back(name);
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    std::sort(names.begin(), names.end());
    return names;
}

void storageEnforceRollingLimit(const String& keepName) {
    if (storageFreeBytes() >= LOW_SPACE_MARGIN_BYTES) return;

    std::vector<String> names = listRecordingsOldestFirst();
    for (const String& name : names) {
        if (storageFreeBytes() >= LOW_SPACE_MARGIN_BYTES) break;
        if (name == keepName) continue;

        String path = String(RECORDINGS_DIR) + "/" + name;
        Serial.printf("[storage] low space, deleting oldest recording: %s\n", path.c_str());
        SD.remove(path);
    }
}
