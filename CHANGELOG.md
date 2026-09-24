# Changelog

Each release's section is copied into its GitHub release notes by
`.github/workflows/release.yml`; a tag without a section here isn't published.

## v1.0.0 (2026-09-24)

- **Fixed:** first-time WiFi setup is always visible. If no WiFi is saved (first boot, or after "Forget WiFi"), stealth mode is switched off so the screen stays on.
- **Fixed:** after entering the WiFi password, the screen shows "Connecting to …" right away instead of keeping the password prompt up while it connects.

## v0.10.0 (2026-09-23)

- **New:** the web UI is reachable at **http://audio-spy.local**, so you don't need the IP address anymore. The name is shown in the settings and on the device's **I** screen.
- **New:** you stay logged in for a year. The iPhone home-screen app no longer asks for the password every time iOS restarts it. After updating, log in once more.
- **New:** while recording is paused, the Resume button is highlighted in yellow.
- **Changed:** a continuous recording (e.g. with Bypass Threshold) now starts a new file every 10 minutes instead of every hour. That keeps files around 19 MB, so they load quickly in the web UI.
- **Fixed:** a recording that was just being started could briefly show up in the list as an empty file that could be deleted.
- **Fixed:** empty recordings left behind by a reset are cleaned up at boot.
- The flashing instructions now mention that the SD card must be FAT32 and in the slot before booting.

## v0.9.0 (2026-09-23)

First release.
