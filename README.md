# Mateja Clock

Romantic bedside alarm clock and offline photo frame for the two-USB ESP32-2432S028 CYD.

Phases 0 through 4 are complete. The verified platform is an ESP32-D0WD-V3 revision 3.1 with 4 MB flash, ST7789 display at 320 x 240 landscape rotation 3, calibrated resistive touch, and an 8 GB FAT32 card labeled `MATEJA_CLK`.

The clock runs an offline photo slideshow with NTP time, a fully local persistent weekday alarm that plays WAV over the speaker, and a persistent love-message inbox fed by a self-hosted ntfy server and shown as popups, a list, and full-text detail on the display.

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

Tap the right half for the next photo or the left half for the previous photo. The top-right mail control opens the message list.

## RTC Coprocessor (DS1302)

The main board never bit-bangs an RTC. A tiny ESP32-C3 SuperMini owns a DS1302 (3-wire) and streams the UTC time to the CYD over a fixed UART link once per second. NTP stays the master time source when Wi-Fi is available; the coprocessor keeps the clock correct across reboots and Wi-Fi outages, so the display never shows `--:--`.

Wiring (P3 header, common ground, framed binary `rtclink-common` at 115200 8N1):

| C3 SuperMini | CYD (ESP32-2432S028) |
| --- | --- |
| GPIO21 (U0TXD) | GPIO35 (UART RX, input-only) |
| GPIO20 (U0RXD) | GPIO22 (UART TX) |
| GND | GND |

CYD P3 GPIO21 is the backlight and is not used by the link. The old P1/UART0
(GPIO3/GPIO1) route was rejected: the CH340 shares those nets and holds GPIO3
HIGH whenever the CYD 5 V rail is powered. The C3 sends `SOF 0xAA | VERSION=1 |
TYPE | LENGTH | PAYLOAD | CRC16` frames: `RTC_TIME`/`RTC_INVALID`,
`ALARM_SWITCH_*`, `VOLUME_CHANGED`, `SNOOZE_PRESSED`; the CYD sends
`SET_RTC_TIME`, `REQUEST_STATUS`, `PING`. C3 firmware builds, all 16 self tests,
and the full link specification live in the separate `/home/luka/Work/ds1302-test` project (CYDTest P3 bench verification: electrical GPIO, raw UART, framed protocol, and reset recovery all passed).

The CYD `RtcLinkService` runs this framed protocol on P3 (GPIO35/GPIO22 over `Serial1`), so the physical snooze button, alarm switch, volume pot, and DS1302 time all reach the main firmware without sharing the USB console UART.

While NTP is unavailable the TimeService falls back to the coprocessor epoch, so Home, the alarm, and message time labels all keep working offline.

## Messages (Phase 4)

For ntfy pushes, create the ignored local credentials header from the example
and fill in your self-hosted server details:

```bash
cp include/ntfy_credentials.example.h include/ntfy_credentials.h
```

`MATEJA_NTFY_BASE_URL` is your ntfy origin (no trailing slash), `MATEJA_NTFY_INBOX_TOPIC` is the love-message topic, `MATEJA_NTFY_ACK_TOPIC` is the seen-ack topic, and `MATEJA_NTFY_ACCESS_TOKEN` is the optional Bearer token. TLS is always verified: the firmware embeds the GTS Root R4 CA that signs the certificate chain of `ntfy.cekluka.com`, and fails closed if no CA is available. A `MATEJA_NTFY_CA_CERT` macro (a raw PEM string) overrides the embedded trust anchor for other servers. The firmware also builds without this file; the device then stays inert (`host=unset`).

The clock streams `GET <base>/<topic>/json?since=<lastId>` over TLS and only ingests `event=="message"` events, deduplicating by ntfy `id`. Resuming with `since=<id>` makes the stream lossless across reconnects and reboots; an HTTP 400 "invalid since" (e.g. a stale local checkpoint) automatically falls back to `since=latest`. Messages are stored newest-first in `/clock/messages/messages.json` (up to 100, oldest-READ-first pruning) and survive reboot. When a new unread message arrives while Home is idle, a 4.5-second popup appears near the bottom edge. Tap the top-right corner to open the list, then tap a row for the full text; opening the detail marks the message read and posts a "seen" acknowledgement (`POST <ackTopic>` with `X-Sequence-ID`) back to your ntfy server. Because the CYD cannot keep two TLS connections at once, the stream pauses briefly for the ack POST and resumes from `since=<id>`, and any ack still pending at boot is re-queued automatically.

The alarm always outranks messages: it never shows a popup, and a ringing alarm overrides everything.

## Alarm (Phase 3)

Tap the bottom-right alarm control to open the Slovenian settings screen. Toggle the enable switch, wrap hour/minute with `+`/`−`, select weekdays with the P–N buttons, and press `Shrani` to save. Settings persist to `/clock/config/settings.json` on the SD card with a `.tmp`/`.bak` recovery scheme.

At the configured time the clock shows `Dobro jutro ♥` with the current local clock time and plays `/clock/audio/alarm.wav`. Press **Dremež +10 min** to snooze (re-rings after 10 minutes) or **Ugasni** to stop. While Snooze is active, Home shows a compact coral `Zzz` pill at the right-center; tap it to cancel Snooze and resolve the original scheduled occurrence. Stopping keeps the alarm enabled and resolves that exact local scheduled occurrence (local date plus configured HH:MM). A later alarm-time edit on the same day produces a different occurrence and can ring normally. A debug `!alarm reset-day` clears the handled occurrence for an immediate re-test.

The audio path is the internal DAC on GPIO26 feeding the CYD amplifier. Only the left DAC channel is enabled so the touch clock pin (GPIO25) stays free. Software volume maps 0–100 to library level 0–11 to avoid amplifier clipping/hum; full idle silence is restored after Stop. Exactly one alarm is supported.

Tap the top-left Home heart to blank only the display backlight. The clock, alarms, C3 link, SD, messages, and slideshow remain active. The first touch anywhere wakes the backlight at the saved manual brightness and is consumed; a ringing alarm wakes the display automatically.

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
| `!msg` | Print message and ntfy status |
| `!msg list` | List the stored message index |
| `!msg unread` | Print unread/count totals |
| `!msg read INDEX` | Mark a message read and queue its seen-ack |
| `!msg inject <text>` | Ingest a synthetic message through the real pipeline |
| `!msg ack` | Queue the seen-acknowledgement POST |
| `!ntfy` | Print ntfy connection state |
| `!ntfy reconnect` | Force a stream reconnection (replays new messages) |
| `!b 0..255` | Set the current backlight PWM level |
| `!v 0..100` | Set the persisted alarm volume |
| `!alarm` | Print alarm status |
| `!rtc` | Print time source (NTP vs DS1302) and link status |
| `!rtc sync` | Push the current NTP time to the C3 DS1302 coprocessor |
| `!alarm on` / `!alarm off` | Enable/disable the alarm (persisted) |
| `!alarm set HH MM` | Set the alarm time (persisted) |
| `!alarm days LMMJCSN` | Set days, Monday-first 7 digits, at least one on |
| `!alarm test` | Audition the ring immediately |
| `!alarm snooze` | Snooze an active ring |
| `!alarm stop` | Stop an active ring |
| `!alarm reset-day` | Debug: clear the handled occurrence so it can be re-tested |
| `!gift-reset` | Clear all user/test state (messages, handled occurrence, Ringing/Snoozed); preserves all config and assets |
| `!gift-reset-full` | Wipe all NON-PHOTO SD content; preserves only `/clock/photos/` and the photo manifest |
| `!msg processed <id>` | Restore the ntfy stream checkpoint (used by the host to avoid replaying retained messages) |
| `!msg processed clear` | Clear the ntfy stream checkpoint (future messages start from latest) |
| `!ls [dir]` | Read-only SD directory listing |
| `!rmfile <path>` | Remove an obsolete emoji `.png` under `/emoji/` only (`.raw` blocked) |
| `!wavraw BYTES` | Upload a WAV over serial after the `WAVREADY` prompt |
| `!q` / `!Q` / `!s` / `!i` | Audio diagnostics: 6-stage, focused tone, sweep, WAV header scan |
| `!h` | Print command help |

## Gift / user-state reset

Connect the clock over USB and run:

```bash
python3 tools/gift_reset.py
```

Confirm with `y` and wait for `Gift reset: PASS`.

This invokes the firmware's `!gift-reset` command, which clears only user/test
state: all local messages (read + unread), Ringing/Snoozed/test-alarm state, and
the persisted handled occurrence. Wi-Fi, ntfy configuration, alarm settings
(08:00, all days, 10 min snooze), volume, brightness, photos, RAW emoji assets,
and RTC/C3 integration are all preserved. The ntfy stream checkpoint is kept so
old retained messages are not replayed. Use `--yes` to skip the prompt and
`--flash` to also rebuild + reflash the CYD first (requires a clean git tree).

Alarm and volume, message store, and processed-id checkpoint are persisted to the SD card; demo `!b` brightness is not. Measured hardware performance and milestone details are recorded in `docs/PROGRESS.md`. The original 4 MB flash backup remains at `backups/cyd_original_flash.bin` with its checksum documented in `docs/HARDWARE.md`.

## Full SD rebuild (destructive)

For a complete SD rebuild that wipes all non-photo content and restores production assets from the repository:

```bash
python3 tools/gift_reset.py --full
```

You must type `FULL RESET` at the prompt to confirm. With `--yes` the prompt is skipped but a prominent warning is printed.

What FULL mode preserves:
- `/clock/photos/` (all Mateja photos, byte-identical set)
- `/clock/manifest.json` (photo render manifest)

What FULL mode deletes and then restores:
- All messages and processed-id checkpoint (restored to the exact prior checkpoint after rebuild)
- Alarm runtime history (cleared; alarm settings re-applied from what the clock had before the wipe)
- SD config/state files
- Emoji RAW assets (restored from `assets/sd/emoji/`)
- Alarm audio (restored from `assets/sd/audio/alarm.wav`)
- Temporary and recovery files (including `FSCK*.REC` fragments)

What FULL mode never touches:
- Flash contents (no firmware erase/reflash)
- C3 coprocessor firmware or state
- DS1302 RTC or display calibration
- Wi-Fi/ntfy credentials (compiled into the firmware)

Production assets are restored from `assets/sd/` in the repository (`assets/sd/emoji/32/*.raw`, `assets/sd/emoji/48/*.raw`, `assets/sd/audio/alarm.wav`). The script aborts immediately — without modifying the clock — if any of these assets are missing locally.

Post-rebuild verification: photo set equality (must match byte-for-byte), messages == 0, unread == 0, ntfy checkpoint preserved, alarm state == armed with correct settings, all emoji present, alarm WAV restored, C3 connected, home screen displayed.

The firmware targets the `huge_app.csv` partition (3 MB application, no OTA) because the audio library exceeds the default app partition.
