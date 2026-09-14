# Mateja Clock

Romantic bedside alarm clock and offline photo frame for the two-USB ESP32-2432S028 CYD.

Phases 0 through 3 are complete. The verified platform is an ESP32-D0WD-V3 revision 3.1 with 4 MB flash, ST7789 display at 320 x 240 landscape rotation 3, calibrated resistive touch, and an 8 GB FAT32 card labeled `MATEJA_CLK`.

The clock runs an offline photo slideshow with NTP time and a fully local, persistent weekday alarm that plays a WAV file over the on-board amplified speaker.

## Photo Preparation

Install the pinned PC dependencies and prepare photos:

```bash
uv pip install --python .venv/bin/python -r tools/requirements.txt
./.venv/bin/python tools/prepare_photos.py \
  --input ~/Downloads \
  --output ~/Downloads/mateja_clock_photos
```

The tool never changes source images. It writes display JPEGs and the ESP manifest to the output root, a private `source_manifest.json`, and paginated sheets under `contact_sheets/`.

## SD Deployment

Mount the prepared FAT32 card, then run:

```bash
./.venv/bin/python tools/copy_photos_to_sd.py \
  --source ~/Downloads/mateja_clock_photos
```

Use `--replace` only when intentionally replacing an existing clock photo dataset. The helper requires a removable FAT volume labeled exactly `MATEJA_CLK`, preserves unrelated files, verifies every copied file, and synchronizes the filesystem. It never formats the card.

## Firmware

For NTP time, create the ignored local credential header from the example and
enter a WPA2 2.4 GHz network:

```bash
cp include/wifi_credentials.example.h include/wifi_credentials.h
```

The firmware also builds without this local file. It stays responsive offline,
shows `--:--`, and retries Wi-Fi asynchronously.

Build, upload, and monitor:

```bash
./.venv/bin/pio run
./.venv/bin/pio run --target upload
./.venv/bin/pio device monitor
```

The Phase 2 Home boots to a random SD photo, synchronizes Europe/Ljubljana time over NTP, and advances to a non-repeating random photo every 45 seconds. It renders a lower readability gradient while streaming each JPEG, so no full-screen framebuffer is required.

Tap the right half for the next photo or the left half for the previous photo. The top-right mail control provides press feedback but stays on Home until the messages phase.

## Alarm (Phase 3)

Tap the bottom-right alarm control to open the Slovenian settings screen. Toggle the enable switch, wrap hour/minute with `+`/`−`, select weekdays with the P–N buttons, and press `Shrani` to save. Settings persist to `/clock/config/settings.json` on the SD card with a `.tmp`/`.bak` recovery scheme.

At the configured time the clock shows `Dobro jutro ♥` with the alarm clock time and plays `/clock/audio/alarm.wav`. Press **Dremež +10 min** to snooze (re-rings after 10 minutes) or **Ugasni** to stop. Stopping keeps the alarm enabled, and the same-day alarm can only fire once; a debug `!alarm reset-day` re-enables it.

The audio path is the internal DAC on GPIO26 feeding the CYD amplifier. Only the left DAC channel is enabled so the touch clock pin (GPIO25) stays free. Software volume maps 0–100 to library level 0–11 to avoid amplifier clipping/hum; full idle silence is restored after Stop. Exactly one alarm is supported.

If `alarm.wav` is missing, the firmware falls back to a built-in melodic tone sequence.

Serial test commands require the `!` prefix and Enter:

| Command | Action |
| --- | --- |
| `!n` | Next photo |
| `!p` | Previous photo |
| `!r` | Random photo without immediate repeat |
| `!t` | Render ten complete Home screens and print statistics |
| `!x` | Test missing/corrupt JPEG and malformed-manifest recovery |
| `!e` | Show the Slovenian photo-library error screen |
| `!d` | Print photo, heap, SD, time, Wi-Fi, brightness, and alarm diagnostics |
| `!m COUNT` | Set the temporary unread-message demo count |
| `!b 0..255` | Set the current backlight PWM level |
| `!v 0..100` | Set the persisted alarm volume |
| `!alarm` | Print alarm status |
| `!alarm on` / `!alarm off` | Enable/disable the alarm (persisted) |
| `!alarm set HH MM` | Set the alarm time (persisted) |
| `!alarm days LMMJCSN` | Set days, Monday-first 7 digits, at least one on |
| `!alarm test` | Audition the ring immediately |
| `!alarm snooze` | Snooze an active ring |
| `!alarm stop` | Stop an active ring |
| `!alarm reset-day` | Debug: clear the once-per-day flag so the alarm can ring again today |
| `!wavraw BYTES` | Upload a WAV over serial after the `WAVREADY` prompt |
| `!q` / `!Q` / `!s` / `!i` | Audio diagnostics: 6-stage, focused tone, sweep, WAV header scan |
| `!h` | Print command help |

Alarm and volume changes are persisted to the SD card; demo `!m` message counts and `!b` brightness are not. Measured hardware performance and milestone details are recorded in `docs/PROGRESS.md`. The original 4 MB flash backup remains at `backups/cyd_original_flash.bin` with its checksum documented in `docs/HARDWARE.md`.

The firmware targets the `huge_app.csv` partition (3 MB application, no OTA) because the audio library exceeds the default app partition.
