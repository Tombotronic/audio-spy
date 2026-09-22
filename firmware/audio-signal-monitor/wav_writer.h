#pragma once

#include <Arduino.h>
#include <FS.h>

#define WAV_SAMPLE_RATE 16000
#define WAV_BITS_PER_SAMPLE 16
#define WAV_CHANNELS 1

// Streaming WAV writer for a single continuous "run" (a merged sequence of
// kept chunks). The header's size fields are written as placeholders on
// beginRun() and rewritten with real values on endRun(), since the total
// length isn't known until the run stops.
class WavWriter {
public:
    bool beginRun(const String& path);
    void appendSamples(const int16_t* data, size_t sampleCount);
    // Streams byteCount bytes from an already-open source file into the
    // current run (used to move a chunk's SD-backed temp buffer into the
    // merged WAV without ever holding the whole chunk in RAM).
    void appendFromFile(File& source, size_t byteCount);
    // Rewrites the header with the current size and flushes, so a reboot
    // mid-run leaves a valid WAV (FAT only records the file size on
    // flush/close; without this an interrupted run ends up as 0 bytes).
    void checkpoint();
    void endRun();
    bool isOpen() const { return _open; }
    const String& path() const { return _path; }

private:
    File _file;
    String _path;
    uint32_t _dataBytesWritten = 0;
    bool _open = false;

    void writeHeader(uint32_t dataBytes);
};

// A run in progress is recorded in a marker file on the SD card (removed by
// endRun()). Call once at boot: if the marker is still there, that run was
// cut off by a reboot. Its header is then fixed to cover every byte that
// reached the card, or the file is deleted if it holds no audio.
void wavRecoverInterruptedRun();
