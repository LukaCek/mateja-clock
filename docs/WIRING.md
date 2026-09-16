# Wiring

These onboard connections were exercised by the diagnostic on the attached ESP32-2432S028. The replacement 8 GB SD card passed mount and read/write verification at 10 MHz.

| Function | GPIO | Notes |
| --- | ---: | --- |
| TFT SCLK | 14 | HSPI |
| TFT MISO | 12 | Used for controller ID probe where the panel exposes SDO |
| TFT MOSI | 13 | HSPI |
| TFT CS | 15 | Active low |
| TFT D/C | 2 | Command/data select |
| TFT reset | -1 | Tied to board reset on this unit |
| Touch SCLK | 25 | Bit-banged during diagnostics to avoid SD bus contention |
| Touch MISO | 39 | Input-only GPIO |
| Touch MOSI | 32 |  |
| Touch CS | 33 | Active low |
| Touch IRQ | 36 | Input-only GPIO, active low |
| microSD SCLK | 18 | VSPI |
| microSD MISO | 19 | VSPI |
| microSD MOSI | 23 | VSPI |
| microSD CS | 5 | Active low |
| Backlight | 21 | Active-high, 5 kHz, 8-bit PWM verified |
| Ambient light sensor | 34 | ADC input |
| Audio amplifier input | 26 | Test tone output verified |

The fitted display is ST7789-family. The confirmed final orientation is Adafruit GFX rotation 3 at 320 x 240.

## ESP32-C3 UART link (production)

The ESP32-C3 SuperMini coprocessor (DS1302 RTC, snooze, alarm switch, volume)
talks to the CYD over a dedicated UART on the P3 header, 115200 8N1, framed
binary protocol (`rtclink-common`):

| C3 SuperMini | CYD | P3 header |
| --- | --- | --- |
| GPIO21 (U0TXD) | GPIO35 (UART RX, input-only) | P3 |
| GPIO20 (U0RXD) | GPIO22 (UART TX) | P3 |
| GND | GND | |

CYD P3 GPIO21 is the backlight and is **not** used by the link. GPIO35 is
input-only with no internal pull; it must never be configured OUTPUT or with a
pull in firmware, and it is not shared with the CH340 (UART0).

### Rejected: P1 / UART0 / GPIO3 route

Diagnosed and discarded (2026-09-16). The P1 header shares UART0 nets with the
onboard CH340. Whenever the CYD 5 V rail is powered, the CH340 holds P1
RX / GPIO3 HIGH no matter what the C3 drives — measured 100% HIGH
(`docs/test1.log`, `docs/test2_full.log`) even with the CYD USB unplugged.
Not fixable by pulls, software, or trace/CH340 modifications.

See `/home/luka/Work/ds1302-test/README.md` for the full link spec and bench
verification. The CYD side is wired on `Serial1` (GPIO35 RX / GPIO22 TX) with the
framed binary protocol; the CH340 USB console stays on UART0 unimpeded.
