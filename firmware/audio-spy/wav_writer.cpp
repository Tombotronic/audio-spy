#include "wav_writer.h"

#include <SD.h>

#include "storage.h"

static const char* RUN_MARKER_PATH = RECORDINGS_DIR "/_current_run";

struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunkSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;  // PCM
    uint16_t numChannels = WAV_CHANNELS;
    uint32_t sampleRate = WAV_SAMPLE_RATE;
    uint32_t byteRate = WAV_SAMPLE_RATE * WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8);
    uint16_t blockAlign = WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8);
    uint16_t bitsPerSample = WAV_BITS_PER_SAMPLE;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
static_assert(sizeof(WavHeader) == WAV_HEADER_BYTES, "WAV header must be 44 bytes, unpadded");

bool WavWriter::beginRun(const String& path) {
    if (_open) endRun();

    // Marker first: a reset right after the WAV is created must still find
    // it at boot (otherwise an empty file stays on the card).
    File marker = SD.open(RUN_MARKER_PATH, FILE_WRITE);
    if (marker) {
        marker.print(path);
        marker.close();
    }

    _file = SD.open(path, FILE_WRITE);
    if (!_file) {
        Serial.printf("[wav] failed to open %s for writing\n", path.c_str());
        SD.remove(RUN_MARKER_PATH);
        return false;
    }

    WavHeader header;  // placeholder sizes, fixed up in endRun()
    _file.write((const uint8_t*)&header, sizeof(header));

    _path = path;
    _dataBytesWritten = 0;
    _open = true;
    Serial.printf("[wav] started run: %s\n", path.c_str());
    return true;
}

void WavWriter::appendFromFile(File& source, size_t byteCount) {
    if (!_open) return;
    static uint8_t buf[512];
    size_t remaining = byteCount;
    while (remaining > 0) {
        size_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t got = source.read(buf, toRead);
        if (got == 0) break;
        // Only count what reached the card (a full card writes short), so
        // the header never claims more audio than the file holds.
        size_t written = _file.write(buf, got);
        _dataBytesWritten += written;
        if (written < got) {
            Serial.printf("[wav] short write to %s\n", _path.c_str());
            break;
        }
        remaining -= got;
    }
}

static void writeHeaderTo(File& file, uint32_t dataBytes) {
    WavHeader header;
    header.chunkSize = 36 + dataBytes;
    header.dataSize = dataBytes;
    file.seek(0);
    file.write((const uint8_t*)&header, sizeof(header));
}

void WavWriter::writeHeader(uint32_t dataBytes) {
    writeHeaderTo(_file, dataBytes);
}

void WavWriter::checkpoint() {
    if (!_open) return;
    writeHeader(_dataBytesWritten);
    _file.seek(sizeof(WavHeader) + _dataBytesWritten);
    _file.flush();
}

void WavWriter::endRun() {
    if (!_open) return;
    writeHeader(_dataBytesWritten);
    _file.close();
    SD.remove(RUN_MARKER_PATH);
    Serial.printf("[wav] finalized run: %s (%lu bytes audio)\n", _path.c_str(),
                  (unsigned long)_dataBytesWritten);
    _open = false;
}

static void recoverMarkedRun();

// At boot nothing is being recorded, so a WAV holding no audio is left over
// from a reset (e.g. before the run marker existed). Removed in batches, like
// the web server's delete all, so the name list stays small.
static void removeEmptyRecordings() {
    static constexpr int BATCH = 16;
    for (;;) {
        String batch[BATCH];
        int count = 0;
        storageForEachRecording([&](File& entry) {
            if (count < BATCH && entry.size() <= sizeof(WavHeader)) batch[count++] = entry.name();
        });
        for (int i = 0; i < count; i++) {
            SD.remove(String(RECORDINGS_DIR) + "/" + batch[i]);
            Serial.printf("[wav] removed empty recording: %s\n", batch[i].c_str());
        }
        if (count < BATCH) return;
    }
}

void wavRecoverInterruptedRun() {
    recoverMarkedRun();
    removeEmptyRecordings();
}

static void recoverMarkedRun() {
    File marker = SD.open(RUN_MARKER_PATH, FILE_READ);
    if (!marker) return;
    String path = marker.readString();
    marker.close();
    SD.remove(RUN_MARKER_PATH);
    path.trim();
    if (path.length() == 0) return;

    File file = SD.open(path, "r+");
    if (!file) return;
    size_t size = file.size();
    if (size <= sizeof(WavHeader)) {
        file.close();
        SD.remove(path);
        Serial.printf("[wav] removed empty interrupted run: %s\n", path.c_str());
        return;
    }
    // Whole 16-bit samples only; the tail of a cut-off write may be odd.
    uint32_t dataBytes = (size - sizeof(WavHeader)) & ~(uint32_t)1;
    writeHeaderTo(file, dataBytes);
    file.close();
    Serial.printf("[wav] recovered interrupted run: %s (%lu bytes audio)\n", path.c_str(),
                  (unsigned long)dataBytes);
}
