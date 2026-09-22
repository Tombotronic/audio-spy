#include "wav_writer.h"

#include <SD.h>

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

bool WavWriter::beginRun(const String& path) {
    if (_open) endRun();

    _file = SD.open(path, FILE_WRITE);
    if (!_file) {
        Serial.printf("[wav] failed to open %s for writing\n", path.c_str());
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

void WavWriter::appendSamples(const int16_t* data, size_t sampleCount) {
    if (!_open) return;
    size_t bytes = sampleCount * sizeof(int16_t);
    _file.write((const uint8_t*)data, bytes);
    _dataBytesWritten += bytes;
}

void WavWriter::appendFromFile(File& source, size_t byteCount) {
    if (!_open) return;
    static uint8_t buf[512];
    size_t remaining = byteCount;
    while (remaining > 0) {
        size_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t got = source.read(buf, toRead);
        if (got == 0) break;
        _file.write(buf, got);
        _dataBytesWritten += got;
        remaining -= got;
    }
}

void WavWriter::writeHeader(uint32_t dataBytes) {
    WavHeader header;
    header.chunkSize = 36 + dataBytes;
    header.dataSize = dataBytes;
    _file.seek(0);
    _file.write((const uint8_t*)&header, sizeof(header));
}

void WavWriter::endRun() {
    if (!_open) return;
    writeHeader(_dataBytesWritten);
    _file.close();
    Serial.printf("[wav] finalized run: %s (%lu bytes audio)\n", _path.c_str(),
                  (unsigned long)_dataBytesWritten);
    _open = false;
}
