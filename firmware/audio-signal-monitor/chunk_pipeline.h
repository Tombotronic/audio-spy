#pragma once

// Starts the pipeline task that consumes finished chunk buffers from the
// audio_capture filled queue, makes the RMS keep/delete decision with
// neighbor-keep, merges consecutive kept chunks into one WAV run, and
// honors pause/force-keep from AppState.
void chunkPipelineStart();
