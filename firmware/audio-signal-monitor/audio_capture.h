#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define CHUNK_SECONDS 10
#define CHUNK_SAMPLE_RATE 16000
#define CHUNK_SAMPLES (CHUNK_SECONDS * CHUNK_SAMPLE_RATE)
#define CHUNK_BYTES (CHUNK_SAMPLES * sizeof(int16_t))

// This hardware has no PSRAM (confirmed via psramFound()/getPsramSize() at
// boot), and one raw chunk (~320KB) doesn't fit in the ~277KB of free
// internal RAM. So each in-flight chunk lives in a small rotating temp
// file on the SD card instead of a RAM buffer; only a small streaming
// I/O buffer (a couple KB) is ever held in RAM. NUM_CHUNK_SLOTS temp
// files give the consumer (chunk_pipeline) slack to lag behind the
// producer (mic capture) without the capture task ever blocking on it.
#define NUM_CHUNK_SLOTS 4

// Path of the temp file used for a given chunk slot.
String audioCaptureSlotPath(uint8_t slotIndex);

// Allocates the free/filled index queues. Must be called once before
// audioCaptureStart().
bool audioCaptureInit();

// Starts the capture task pinned to core 1. It continuously records
// CHUNK_SAMPLES-sample chunks into a temp file per chunk (pausing/idling
// per AppState.paused), computing each chunk's RMS as it streams, then
// pushes {slotIndex, rms} onto the filled queue.
void audioCaptureStart();

struct FilledChunk {
    uint8_t slotIndex;
    float rms;  // normalized 0.0-1.0
};

// Items are FilledChunk structs.
QueueHandle_t audioCaptureFilledQueue();

// Returns a slot to the free pool once the pipeline is done with it
// (its temp file has been copied into a run or discarded).
void audioCaptureReleaseSlot(uint8_t slotIndex);
