#include "storage.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <vector>

static constexpr size_t WAV_HEADER_BYTES = 44;

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

void storageRemoveEmptyRecordings() {
    std::vector<String> empty;
    File dir = SD.open(RECORDINGS_DIR);
    if (!dir) return;
    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        if (!entry.isDirectory() && name.endsWith(".wav") && entry.size() <= WAV_HEADER_BYTES) {
            empty.push_back(name);
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    for (const String& name : empty) {
        String path = String(RECORDINGS_DIR) + "/" + name;
        Serial.printf("[storage] removing empty recording: %s\n", path.c_str());
        SD.remove(path);
    }
}

void storageEnforceRollingLimit() {
    if (storageFreeBytes() >= LOW_SPACE_MARGIN_BYTES) return;

    std::vector<String> names = listRecordingsOldestFirst();
    for (const String& name : names) {
        if (storageFreeBytes() >= LOW_SPACE_MARGIN_BYTES) break;

        String path = String(RECORDINGS_DIR) + "/" + name;
        Serial.printf("[storage] low space, deleting oldest recording: %s\n", path.c_str());
        SD.remove(path);
    }
}
