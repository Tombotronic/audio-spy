# Audio Signal Monitor

Always-on audio monitor built on the [M5Stack Cardputer Adv](https://docs.m5stack.com/en/core/Cardputer). Records continuously, keeps only the segments that contain sound, and exposes a web page to browse, audition, and collect the captured files.

## How it works

- Audio is captured continuously in ~10 second chunks.
- Each chunk's RMS level is checked against a threshold. Chunks below threshold are discarded; chunks at/above threshold are kept, along with their immediate neighbor chunks (to protect soft onsets/tails). Consecutive kept chunks are merged into one continuous WAV file.
- This guarantees nothing is ever missed — nothing is skipped live, only discarded in hindsight.

## Hardware

- M5Stack Cardputer Adv (built-in SD slot)
- Arduino framework + M5Unified library

## Audio format

16kHz / 16-bit / mono WAV.

## Storage

Files are written to `/audio-signal-monitor/` on the SD card, named by NTP-synced timestamp (e.g. `2026-09-22_14-05-30.wav`). When the card fills up, the oldest kept files are automatically deleted to make room — recording never stops.

## Web interface

Password-protected (basic HTTP auth) page served over the local WiFi network:

- List, play in-browser, download, and delete recordings
- RMS threshold slider with a live level readout
- Stealth mode toggle — blanks the on-device screen and mutes LED/beep feedback
- Pause / resume recording
- Force-keep the current in-progress chunk

## On-device display

Live status screen by default (recording state, live RMS meter, WiFi IP address, free SD space). Goes dark and silent when stealth mode is enabled.

## Status

Design phase complete. Firmware implementation not yet started.
