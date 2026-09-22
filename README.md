# Audio Signal Monitor

Always-on audio monitor built on the [M5Stack Cardputer Adv](https://docs.m5stack.com/en/core/Cardputer). Records continuously, keeps only the segments that contain sound, and exposes a web page to browse, audition, and collect the captured files.

## How it works

- Audio is captured continuously in ~10 second chunks.
- Each chunk's RMS level is checked against a threshold. Chunks below threshold are discarded; chunks at/above threshold are kept, along with their immediate neighbor chunks (to protect soft onsets/tails). Consecutive kept chunks are merged into one continuous WAV file.
- This guarantees nothing is ever missed — nothing is skipped live, only discarded in hindsight.

## Hardware

- M5Stack Cardputer Adv (built-in SD slot; this unit has no PSRAM)
- Arduino framework + M5Unified/M5Cardputer library

## Build

Uses `arduino-cli` with board FQBN `esp32:esp32:m5stack_cardputer:CDCOnBoot=default`:

```
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default firmware/audio-signal-monitor
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default firmware/audio-signal-monitor
```

**Keep USB CDC On Boot disabled** (`CDCOnBoot=default`; the board's default is
enabled). With it enabled, `Serial` goes over native USB, and after an upload
reset the port can look "open" to the chip while nobody reads it. Every
`Serial.print` then blocks and the device hangs on a black screen until it's
power-cycled. With it disabled, `Serial` goes to UART0 (not visible over USB).

**Must use `esp32:esp32` core version 3.2.0, not newer.** Core versions built on
ESP-IDF v5.5.x (3.3.x and later) have a known regression where MCLK doesn't
reach the Cardputer ADV's ES8311 mic codec, so `M5.Mic.record()` silently
returns constant/near-zero samples (no error, no crash — it just looks like
silence forever). 3.2.0 is built on IDF v5.4.1, which is unaffected.
Install with: `arduino-cli core install esp32:esp32@3.2.0`

Copy `firmware/audio-signal-monitor/secrets.h.example` to `secrets.h` (gitignored)
and fill in your WiFi credentials before flashing.

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
