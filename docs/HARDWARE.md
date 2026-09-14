# Hardware Identification

Last updated: 2026-09-14

## Confirmed Before Flashing

| Item | Result | Method |
| --- | --- | --- |
| USB bridge | QinHeng CH340, USB ID `1a86:7523` | `lsusb` |
| Stable serial path | `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` | udev symlink inspection |
| Serial device | `/dev/ttyUSB0` | udev symlink inspection |
| MCU | ESP32-D0WD-V3, revision 3.1 | esptool 5.1.0 `chip-id` |
| CPU features | Dual core, Wi-Fi, Bluetooth, 240 MHz capable | esptool 5.1.0 `chip-id` |
| Crystal | 40 MHz | esptool 5.1.0 `chip-id` |
| Flash | 4 MB, manufacturer `0x5e`, device `0x4016`, 3.3 V | esptool 5.1.0 `flash-id` |
| MAC | `d4:8a:fc:a6:11:a4` | esptool 5.1.0 `chip-id` |
| Original flash backup | 4,194,304 bytes, SHA-256 `b18a96a28402d2d4e3305364424b2a41cf7daedd68d59543d3cefeb706653d7a` | esptool 5.1.0 `read-flash` |
| PCB marking | `ESP32-2432S028` | Physical inspection |
| PCB variant | Micro-USB plus USB-C, RX/TX connector | Physical inspection |

The backup is stored at `backups/cyd_original_flash.bin` and intentionally ignored by Git.

## Confirmed On The Attached Unit

The model name was not used as proof of the fitted peripherals. These results came from register reads, serial diagnostics, and physical observation on the attached unit:

| Peripheral | Result | Evidence |
| --- | --- | --- |
| TFT controller | ST7789-family | `RDDID(0x04)` = `85 85 52`; `RDDID4(0xD3)` does not return the ILI9341 `00 93 41` signature |
| Display orientation | 320 x 240 landscape, rotation 3 | Correct dimensions reported and user visually confirmed upright full-screen output |
| Touch controller | XPT2046-compatible resistive controller | Four-corner calibration passed and subsequent touch dots tracked correctly |
| Touch mapping | Horizontal uses raw Y: left 429, right 3592; vertical raw X: top 510, bottom 3555 | Stored diagnostic calibration and reboot report |
| Backlight | GPIO21, active-high PWM | User observed off/dim/full brightness changes; diagnostic step transitions were intentionally abrupt |
| Ambient light | ADC on GPIO34 | User confirmed covered/illuminated response; live readings also changed in serial output |
| Audio amplifier | GPIO26 | User heard the 880 Hz diagnostic tone |

## microSD Verification

The GPIO5/18/19/23 SPI bus communicates with the inserted card and FatFs consistently reports `FR_NO_FILESYSTEM`. The card was then tested directly in Linux through a USB reader. It contained a GPT with an unformatted 16 MB first partition and an empty FAT32 second partition. The user authorized reformatting, but writing the end of the device failed.

Linux reported:

```text
Medium Error
Peripheral device write fault
I/O error, dev sdc, sector 3833848, WRITE
```

This independently confirms the ESP32 `FR_DISK_ERR` at both 10 MHz and 1 MHz. The failed card's partition metadata is now partially erased and the card must be discarded.

A replacement 8 GB card was then prepared as one MBR FAT32 partition labeled `MATEJA_CLK`. It passed a Linux write/read/hash/delete test and the CYD diagnostic at 10 MHz:

```text
mount=PASS rw=PASS card_mb=7580 type=3
```

The CYD microSD interface and replacement card are confirmed for production development.

## Safety Notes

- No flash erase or firmware upload was performed before the full backup completed successfully.
- The display read protocol has a one-bit dummy period. Correcting that framing changed the raw `42 C2 A9` stream into the documented ST7789 `85 85 52` ID.
- The SD test writes, reads, and removes only `/.cyd_diagnostic.tmp`.
- SD formatting is never automatic. It requires the explicit serial `f` command.
