#include "storage.h"

#include <SD.h>
#include <SPI.h>

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

void storageForEachRecording(const std::function<void(File& entry)>& fn) {
    File dir = SD.open(RECORDINGS_DIR);
    if (!dir) return;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) fn(entry);
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
}

// Filenames are NTP timestamps (e.g. 2026-09-22_14-05-30.wav), so
// lexicographic order is chronological order. Files named before the first
// NTP sync ("unsynced-...") have no known time; they count as older than
// every timestamped one, since otherwise ('u' sorts after digits) they
// would never be deleted.
static bool isOlder(const String& a, const String& b) {
    bool unsyncedA = a.startsWith("unsynced-"), unsyncedB = b.startsWith("unsynced-");
    if (unsyncedA != unsyncedB) return unsyncedA;
    return a < b;
}

// One pass over the directory instead of a sorted list of every name.
static String findOldestRecording(const String& keepName) {
    String oldest;
    storageForEachRecording([&](File& entry) {
        String name = entry.name();
        if (name == keepName) return;
        if (oldest.length() == 0 || isOlder(name, oldest)) oldest = name;
    });
    return oldest;
}

void storageEnforceRollingLimit(const String& keepName) {
    while (storageFreeBytes() < LOW_SPACE_MARGIN_BYTES) {
        String oldest = findOldestRecording(keepName);
        if (oldest.length() == 0) return;

        String path = String(RECORDINGS_DIR) + "/" + oldest;
        Serial.printf("[storage] low space, deleting oldest recording: %s\n", path.c_str());
        if (!SD.remove(path)) return;  // would pick the same file again
    }
}
