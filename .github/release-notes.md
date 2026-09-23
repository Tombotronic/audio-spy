## Flashing

Download `audio-spy-VERSION.bin` and flash it at address `0x0`, both for a first install and for updates. It replaces everything on the chip, including the saved WiFi network: after flashing, the device asks for WiFi on its keyboard. Recordings and settings on the SD card are untouched.

Connect the Cardputer by USB. If it isn't detected, hold the **G0** button while plugging in the cable (download mode).

**In the browser** (Chrome or Edge, nothing to install): open [esptool-js](https://espressif.github.io/esptool-js/), click **Connect** and pick the Cardputer's port. Enter `0x0` as the address, choose the file, then click **Program**.

**From a terminal** ([esptool](https://docs.espressif.com/projects/esptool/): `pip install esptool`):

```
esptool.py --chip esp32s3 --port <port> write_flash 0x0 audio-spy-VERSION.bin
```

(`<port>` is e.g. `/dev/cu.usbmodem2101` on macOS, `COM5` on Windows)

An SD card formatted as FAT32 must be in the slot (cards over 32 GB usually come as exFAT and need reformatting), or the device stops at boot. Then follow [WiFi setup](https://github.com/Tombotronic/audio-spy#wifi-setup) in the README. You only need the *Source code* archives to build the firmware yourself.
