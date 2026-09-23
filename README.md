# Audio Spy

Always-on audio monitor built on the [M5Stack Cardputer Adv](https://docs.m5stack.com/en/core/Cardputer-Adv). Records continuously, keeps only the segments that contain sound, and exposes a web page to browse, audition, and collect the captured files.

> **Legal note:** Recording conversations without the consent of everyone
> involved is illegal in many countries (in Germany, for example, under § 201
> StGB). Only use this device where you're allowed to record, and inform the
> people around it. You are responsible for how you use it.

## Install

Download `audio-spy-vX.Y.Z.bin` from the [latest release](https://github.com/Tombotronic/audio-spy/releases/latest) and flash it at address `0x0`, both for a first install and for updates. It erases the saved WiFi network, so you set that up again on the device afterwards.

Flash it in the browser with [esptool-js](https://espressif.github.io/esptool-js/) (Chrome/Edge) or with `esptool.py`. The release notes have step-by-step instructions. Then insert a FAT32 SD card and set up [WiFi](#wifi-setup) on the device. To build it yourself instead, see [Build](#build).

## How it works

- Audio is captured continuously in ~10 second chunks, measuring the RMS level of every second.
- Seconds at/above the threshold are kept, plus 2 seconds before and after (to protect soft onsets/tails). Silent pauses of up to 5 seconds between kept parts are kept too, so one conversation stays in one WAV file; longer silence ends the file.
- A continuous recording is split into a new file every 10 minutes (~19 MB), since the web UI downloads a whole file over WiFi to play it.
- If the device reboots mid-recording, the file is repaired on the next boot (everything that reached the card is kept). A file that holds no audio at all is deleted.
- Capture never pauses for these decisions: each chunk is judged one chunk later (the pre-roll needs to know what comes next), so nothing is skipped live, only discarded in hindsight. As a result, the REC state appears 10–20 seconds after a sound starts; the file itself still begins 2 seconds before it.

## Hardware

- M5Stack Cardputer Adv (built-in SD slot; this unit has no PSRAM)
- Arduino framework, with these libraries (tested versions; install with `arduino-cli lib install`):
  - `M5Cardputer` 1.1.1
  - `M5Unified` 0.2.23 and `M5GFX` 0.2.30
  - `ArduinoJson` 7.4.3
- The ES8311 mic codec's analog gain (PGA) is raised to +18 dB after `Mic.begin()`; M5Unified leaves it at 0 dB, which makes recordings very quiet

## Build

Uses `arduino-cli` with board FQBN `esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB`:

```
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB firmware/audio-spy
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB firmware/audio-spy
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

### Versioned binary

The firmware version is `FIRMWARE_VERSION` in `firmware/audio-spy/version.h`.
It's shown on the boot screen, in the web UI's settings and in `/status`. To
build a flashable image:

```
arduino-cli compile --fqbn esp32:esp32:m5stack_cardputer:CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB --output-dir firmware/build firmware/audio-spy
```

`firmware/build/audio-spy.ino.merged.bin` is the full image (bootloader,
partition table and app), flashed at offset `0x0`:

```
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0x0 audio-spy-v0.9.0.bin
```

It covers the whole flash, so it also erases the saved WiFi network.
Recordings and `config.json` on the SD card are untouched.

Pushing a tag `vX.Y.Z` builds the firmware on GitHub Actions
(`.github/workflows/release.yml`) and publishes the full image as a release
(`audio-spy-vX.Y.Z.bin`, with the notes from `.github/release-notes.md`). The
tag must match `FIRMWARE_VERSION`.

The time zone (`TZ_INFO`, a POSIX rule so daylight saving switches
automatically) and NTP server are set in `firmware/audio-spy/config.h`.

## WiFi setup

WiFi credentials are entered on the device, not compiled in. On first boot it
scans and lists nearby networks (strongest first): press **1–6** to pick one, or
**0** to type a hidden SSID (confirm with **ok**, the enter key), then type the
password and press **ok** (leave it empty for an open network). They're saved in
NVS flash (plaintext, never sent over the network) and survive reflashing.

If connecting fails at boot (wrong password, different network), press **W**
within 10 seconds to pick a network again; any other key or waiting it out
boots offline and keeps recording. The setup screens themselves give up after 2
minutes without a key press and boot offline too, so an unattended restart
(e.g. after a power cut) never stops recording.

To switch networks while it's connected, use **Settings → WiFi → Forget…** in
the web UI. It clears the saved network and restarts into the setup screen. A
recording in progress is repaired on boot, the same as after a power cut.

## Audio format

16kHz / 16-bit / mono WAV.

## Storage

Files are written to `/audio-spy/` on the SD card, named after the NTP-synced time their audio starts (e.g. `2026-09-22_14-05-30.wav`). Before the first NTP sync, files are named `unsynced-000000.wav` etc. When the card fills up, the oldest kept files are automatically deleted to make room — recording never stops. `unsynced-*` files count as the oldest, since their real time is unknown.

Settings live in `/audio-spy/config.json` on the same card (created with defaults on first boot): `threshold` (linear RMS, 0–1), `webPassword`, `sessionSecret` (random, see [Web interface](#web-interface)), and `stealthMode` (kept across reboots; boot progress still shows, then the screen goes dark). If the file can't be parsed, it's kept as `config.json.bad` and defaults are used, so check the password after hand-editing it.

## Web interface

Password-protected (basic HTTP auth) page served over the local WiFi network at **http://audio-spy.local** (mDNS), or at the device's IP (press **I** on the device to see it; some Android browsers don't resolve `.local` names). User `admin`, password from `webPassword` in `config.json` (default `cardputer`).

After logging in once, the browser keeps a login cookie for a year and isn't asked again, which also keeps the iPhone home-screen app logged in (iOS forgets basic auth whenever it restarts the app). The cookie is derived from `webPassword` and a random `sessionSecret` in `config.json`: changing either one logs every browser out.

**Change the default password** before using it on a shared network: edit
`webPassword` in `/audio-spy/config.json` on the SD card and reboot. The page
uses plain HTTP, so it's meant for your own local network only; don't expose
it to the internet. Every POST must carry the header `X-Audio-Spy: 1` (the page
sends it), so other websites can't make your logged-in browser change settings or
delete recordings.

- List recordings (shown as `dd.mm.yyyy hh:mm:ss`) with a coarse waveform, play in-browser (normalised loudness), download, and delete
- Threshold slider in 1 dB steps under a live dB level meter
- Pause / resume recording (takes effect immediately; audio from before and after a pause never ends up in the same file). While paused nothing is recorded, but the level meters stay live, and the Resume button is highlighted in yellow like an active Bypass
- "Bypass Threshold" switch: while on, everything is recorded regardless of the threshold (off again after a reboot). Greyed out while paused
- Settings panel: address (`audio-spy.local` and IP), firmware version, storage (bar showing recordings vs. other used space, and what's free), appearance (System / Light / Dark), stealth mode (blanks the on-device screen; stays on after a reboot) and "delete all recordings" with confirmation

Works as an iPhone home screen app: in Safari, open `http://audio-spy.local`, then Share → Add to Home Screen (added via the name, it keeps working if the router hands out a different IP). It runs full screen as "Audio Spy" with its own icon (`/icon.png` and `/manifest.json` are served without login so iOS can fetch them).

## On-device display

Live status screen by default: recording state, a fast dB level meter with peak hold and threshold marker, the loudest second of the current chunk, the threshold, and free SD space. The dB number is averaged over 250 ms so it stays readable. Shows boot progress and the firmware version while starting (about 20 seconds, mostly WiFi and NTP). If WiFi fails, it's about 25 seconds: the 15 second connection timeout plus 10 seconds to press **W** (the NTP wait is skipped offline, see [WiFi setup](#wifi-setup)). Goes dark when stealth mode is enabled.

Press **I** to show the device's IP address and `audio-spy.local` (where the web interface is served) for 5 seconds, then the status screen returns. Does nothing while stealth mode is on.

## Status

Firmware implemented and running on the device.

## License

[MIT](LICENSE)
