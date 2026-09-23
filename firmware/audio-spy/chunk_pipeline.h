#pragma once

// Starts the pipeline task that consumes finished chunk buffers from the
// audio_capture filled queue, decides per second what to keep (loud seconds
// plus a little padding, bridging short pauses), writes the kept audio into
// one WAV per run, and honors pause/bypass from AppState.
void chunkPipelineStart();
