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

## Planned ESP32-C3 UART

No C3 wiring is assigned yet. GPIO selection will be made only after the onboard CYD connections are verified, avoiding boot-strapping pins and conflicts with the TFT, touch, SD, audio, and sensors.
