# Audio Signal Monitor

Always-on audio monitor built on the [M5Stack Cardputer Adv](https://docs.m5stack.com/en/core/Cardputer-Adv). Records continuously, keeps only the segments that contain sound, and exposes a web page to browse, audition, and collect the captured files.

## How it works

- Audio is captured continuously in ~10 second chunks, measuring the RMS level of every second.
- Seconds at/above the threshold are kept, plus 2 seconds before and after (to protect soft onsets/tails). Silent pauses of up to 5 seconds between kept parts are kept too, so one conversation stays in one WAV file; longer silence ends the file.
- If the device reboots mid-recording, the file is repaired on the next boot (everything that reached the card is kept).
- Capture never pauses for these decisions: each chunk is judged one chunk later (the pre-roll needs to know what comes next), so nothing is skipped live, only discarded in hindsight. As a result, the REC state appears 10–20 seconds after a sound starts; the file itself still begins 2 seconds before it.

## Hardware

- M5Stack Cardputer Adv (built-in SD slot; this unit has no PSRAM)
- Arduino framework + M5Unified/M5Cardputer library
- The ES8311 mic codec's analog gain (PGA) is raised to +18 dB after `Mic.begin()`; M5Unified leaves it at 0 dB, which makes recordings very quiet

## Build

Uses `arduino-cli` with board FQBN `esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB`:

```
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB firmware/audio-signal-monitor
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB firmware/audio-signal-monitor
```

**Keep USB CDC On Boot disabled** (`CDCOnBoot=default`; the board's default is
enabled). With it enabled, `Serial` goes over native USB, and after an upload
reset the port can look "open" to the chip while nobody reads it. Every
`Serial.print` then blocks and the device hangs on a black screen until it's
power-cycled. With it disabled, `Serial` goes to UART0 (not visible over USB).

The Cardputer's ESP32-S3 has 8MB of flash; the board default only uses a 4MB
layout (1.2MB app). `FlashSize=8M,PartitionScheme=default_8MB` gives a 3MB app
partition.

**Must use `esp32:esp32` core version 3.2.0, not newer.** Core versions built on
ESP-IDF v5.5.x (3.3.x and later) have a known regression where MCLK doesn't
reach the Cardputer ADV's ES8311 mic codec, so `M5.Mic.record()` silently
returns constant/near-zero samples (no error, no crash — it just looks like
silence forever). 3.2.0 is built on IDF v5.4.1, which is unaffected.
Install with: `arduino-cli core install esp32:esp32@3.2.0`

The time zone (`TZ_INFO`, a POSIX rule so daylight saving switches
automatically) and NTP server are set in `firmware/audio-signal-monitor/config.h`.

## WiFi setup

WiFi credentials are entered on the device, not compiled in. On first boot it
scans and lists nearby networks (strongest first): press **1–6** to pick one, or
**0** to type a hidden SSID (confirm with **ok**, the enter key), then type the
password and press **ok** (leave it empty for an open network). They're saved in
NVS flash (plaintext, never sent over the network) and survive reflashing.

If connecting fails at boot (wrong password, different network), press **W**
within 10 seconds to pick a network again; any other key or waiting it out
boots offline and keeps recording.

## Audio format

16kHz / 16-bit / mono WAV.

## Storage

Files are written to `/audio-signal-monitor/` on the SD card, named after the NTP-synced time their audio starts (e.g. `2026-09-22_14-05-30.wav`). Before the first NTP sync, files are named `unsynced-000000.wav` etc. When the card fills up, the oldest kept files are automatically deleted to make room — recording never stops.

Settings live in `/audio-signal-monitor/config.json` on the same card (created with defaults on first boot): `threshold` (linear RMS, 0–1), `webPassword`, and `stealthMode` (ignored at boot; the device always starts with the screen on).

## Web interface

Password-protected (basic HTTP auth) page served over the local WiFi network at the device's IP (press **I** on the device to see it). User `admin`, password from `webPassword` in `config.json` (default `cardputer`).

- List recordings (shown as `dd.mm.yyyy hh:mm:ss`) with a coarse waveform, play in-browser (normalised loudness), download, and delete
- Threshold slider in 1 dB steps under a live dB level meter
- Pause / resume recording (takes effect immediately; audio from before and after a pause never ends up in the same file). While paused nothing is recorded, but the level meters stay live
- "Bypass Threshold" switch: while on, everything is recorded regardless of the threshold (off again after a reboot). Greyed out while paused
- Settings panel: IP address, storage (bar showing recordings vs. other used space, and what's free), appearance (System / Light / Dark), stealth mode (blanks the on-device screen; off again after a reboot) and "delete all recordings" with confirmation

Works as an iPhone home screen app: in Safari, Share → Add to Home Screen. It runs full screen as "Audio Monitor" with its own icon (`/icon.png` and `/manifest.json` are served without login so iOS can fetch them).

## On-device display

Live status screen by default: recording state, a fast dB level meter with peak hold and threshold marker, the loudest second of the current chunk, the threshold, and free SD space. The dB number is averaged over 250 ms so it stays readable. Shows boot progress while starting (about 20 seconds, mostly WiFi and NTP; 10 seconds more if WiFi fails, see [WiFi setup](#wifi-setup)). Goes dark when stealth mode is enabled.

Press **I** to show the device's IP address (where the web interface is served) for 5 seconds, then the status screen returns. Does nothing while stealth mode is on.

## Status

Firmware implemented and running on the device.
