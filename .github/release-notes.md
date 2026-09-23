## Which file do I need?

| You want to… | Download | Flash at |
|---|---|---|
| **Install** Audio Spy on a Cardputer Adv for the first time (or it runs other firmware) | `audio-spy-VERSION-full.bin` | `0x0` |
| **Update** a Cardputer that already runs Audio Spy | `audio-spy-VERSION-update.bin` | `0x10000` |

The full image replaces everything on the chip, including the saved WiFi network: after flashing, the device asks for WiFi on its keyboard. The update keeps it. Recordings and settings on the SD card are untouched either way.

`SHA256SUMS.txt` lets you check the download (`shasum -a 256 -c SHA256SUMS.txt`). You only need the *Source code* archives to build the firmware yourself.

## How to flash

Connect the Cardputer by USB. If it isn't detected, hold the **G0** button while plugging in the cable (download mode).

**In the browser** (Chrome or Edge, nothing to install): open [esptool-js](https://espressif.github.io/esptool-js/), click **Connect** and pick the Cardputer's port. Enter the address from the table above, choose the file, then click **Program**.

**From a terminal** ([esptool](https://docs.espressif.com/projects/esptool/): `pip install esptool`):

```
esptool.py --chip esp32s3 --port <port> write_flash 0x0 audio-spy-VERSION-full.bin
esptool.py --chip esp32s3 --port <port> write_flash 0x10000 audio-spy-VERSION-update.bin
```

(one or the other; `<port>` is e.g. `/dev/cu.usbmodem2101` on macOS, `COM5` on Windows)

Then insert a FAT32 SD card and follow [WiFi setup](https://github.com/Tombotronic/audio-spy#wifi-setup) in the README.
