# Development Progress

## 2026-09-14 - Phase 0 Started

- Identified the attached CH340 serial adapter at `/dev/ttyUSB0` with a stable by-id path.
- Identified an ESP32-D0WD-V3 revision 3.1, 40 MHz crystal, and 4 MB flash without writing flash.
- Backed up the complete original 4 MB flash before any upload.
- Verified backup size and recorded SHA-256 `b18a96a28402d2d4e3305364424b2a41cf7daedd68d59543d3cefeb706653d7a`.
- Created a pinned PlatformIO diagnostic environment.
- Corrected the panel's one-bit read framing and identified an ST7789-family controller from ID `85 85 52`.
- Visually confirmed 320 x 240 landscape output at rotation 3.
- Completed four-corner touch calibration and confirmed mapped touches track correctly.
- Confirmed GPIO21 backlight control, GPIO34 ambient-light response, and GPIO26 amplified audio output.
- Preserved touch calibration across diagnostic resets: horizontal raw Y 429..3592 and vertical raw X 510..3555.
- Confirmed SD SPI communication, but the inserted card has no recognized filesystem and fails low-level writes during authorized formatting, including at 1 MHz.
- Tested the card independently through Linux. The kernel reported a medium error and peripheral write fault at sector 3,833,848, confirming that the card is unsuitable.
- Recorded the physical PCB marking `ESP32-2432S028`, its Micro-USB plus USB-C variant, and RX/TX connector.
- Prepared a replacement 8 GB card as a single MBR FAT32 volume labeled `MATEJA_CLK`.
- Passed Linux write/read/hash/delete verification on the replacement card.
- Passed CYD mount and temporary-file write/read/delete verification at 10 MHz; reported capacity is 7,580 MB.
- Hardened diagnostic serial commands with a `!` prefix and newline framing so UART noise cannot alter state or invoke formatting.

Phase 0 is complete. All required onboard hardware has been exercised on the connected unit.

## 2026-09-14 - Phase 1 Complete

- Added `tools/prepare_photos.py` using Pillow with deterministic recursive discovery, EXIF correction, 4:3 landscape crop, premium blurred portrait composition, baseline RGB JPEG output, manifests, and paginated contact sheets.
- Added nine passing automated tests covering deterministic output, source immutability, EXIF orientation, exact JPEG properties, portrait/landscape composition, filtering, manifest privacy, pagination, and safe SD deployment.
- Prepared 130 display assets from `~/Downloads`: 54 landscape and 76 portrait, all exactly 320 x 240 at JPEG quality 85.
- Manually reviewed seven contact sheets. Three confirmed phone UI screenshots were excluded; uncertain images were retained.
- Added `tools/copy_photos_to_sd.py` with exact label/removable/FAT checks, staging, explicit `--replace`, per-file size and SHA-256 verification, and filesystem sync.
- Deployed only `/clock/manifest.json` and 130 referenced JPEGs under `/clock/photos/`; no source paths or PC-only contact sheets were copied.
- Added `PhotoService` using pinned TJpg_Decoder 1.1.0 and ArduinoJson 6.21.6. JPEG MCU blocks stream directly from SD to the display without a full-screen framebuffer.
- Preserved the verified ST7789 pins, 320 x 240 rotation 3, SD pins, backlight, and touch mapping.
- Corrected the two-USB ST7789 panel polarity with `invertDisplay(false)`; source photos were not altered to compensate.
- Physically confirmed natural skin tones, correct colors, full-screen geometry, portrait and landscape composition, and exact one-step left/right touch navigation.
- Verified no-SD boot behavior on hardware: the firmware remained alive and displayed `SD kartica ni na voljo`.
- Visually confirmed the malformed-library fallback text `Napaka knjižnice slik`, including correct rendering of Slovenian `ž`.
- Verified missing JPEG, corrupt JPEG, and malformed manifest rejection using temporary test files. Failures were logged, skipped, and cleaned up without a crash.
- Final hardware workload completed 28 successful renders plus two intentionally rejected assets. Average was 242 ms, maximum 285 ms, and observed render times ranged from 144 to 285 ms.
- Free heap was 338,728 bytes at boot, 310,760 bytes after SD/JPEG initialization, and stabilized at 310,560 bytes after the first SD write. Repeated renders and four repeated failure cycles showed no continual heap loss.
- Final firmware size: 26,128 bytes static RAM and 383,705 bytes flash.

Phase 1 is complete. Development stops here before the Home UI milestone.

## 2026-09-14 - Phase 2 Complete

- Added a centralized `HomeScreen` over the streaming SD-photo renderer. The final layout uses large warm-white time, a lowercase Slovenian date, coral accents, small photo-blended top controls, an unread badge, and a rounded 24 px alarm-clock asset at the lower-right edge.
- Corrected U8g2 transparency after discovering that every font change resets its mode. Time, date, badges, fallback details, and alarm text now remain transparent across initial, minute, photo, and status redraws.
- Added the lower photo gradient directly inside JPEG MCU blocks. It begins at row 133 and reaches about 32% source brightness at the bottom without allocating a framebuffer.
- Added `TimeService` with asynchronous Wi-Fi association, bounded retries, NTP, and the POSIX `CET-1CEST,M3.5.0,M10.5.0/3` timezone rule.
- Verified NTP on hardware through a 2.4 GHz phone hotspot. The CYD displayed Monday 14 September at 10:17 CEST, exactly matching the computer's Europe/Ljubljana time. Later boots and minute changes remained correct.
- Verified the unavailable-network path on hardware. Home remained usable with `--:--`, timed out without blocking, and scheduled later retries.
- Added `BrightnessService` with backlight PWM, explicit manual levels, and passive one-second smoothing of GPIO34 ambient-light samples. Automatic ambient brightness is intentionally deferred until calibrated.
- Added provider interfaces plus non-persistent demo providers for message count and alarm status. Default production state is zero unread messages and alarm disabled.
- Added strict newline-framed commands for temporary message, alarm, and brightness testing. Malformed or oversized serial frames are discarded through their newline and cannot execute a suffix.
- Added full calibrated X/Y touch handling. Left/right photo navigation remains immediate; the reserved mail and alarm zones were physically tapped and both showed feedback without leaving Home.
- Added the 45-second randomized slideshow using a tested no-immediate-repeat selector. A hardware interval test kept the image during a minute refresh, then advanced from photo 61 to photo 121 at the slideshow deadline.
- Current-photo redraw now advances to another valid JPEG if the current asset disappears or becomes invalid. Failed automatic slideshow attempts are rate-limited by the normal interval.
- Re-ran temporary missing/corrupt JPEG and malformed-manifest tests on Phase 2. Recovery passed, temporary files were removed, and Home was restored.
- The existing no-SD boot was physically verified in Phase 1; the Phase 2 Home preserves that start result and now presents `--:--` with `SD kartica ni na voljo`. The invalid-library fallback remains `Napaka knjižnice slik`.
- Completed a 54-render Home workload with stable free heap near 218,980 bytes. Photo rendering averaged 250 ms with a 315 ms maximum; a later final regression averaged 270 ms with a 295 ms photo maximum and 309 ms complete-Home maximum. No continual heap loss occurred.
- Deliberately omitted an added fade animation: streaming already provides clean 200-300 ms changes, while hiding the render behind a fade would exceed the target and add backlight flicker risk.
- Added 15 passing native Unity cases for Slovenian mappings/formatting, unread badges, strict alarm parsing, and non-repeating random selection. The original nine Python photo/deployment tests also pass.
- Final firmware size is 49,728 bytes static RAM and 872,321 bytes flash.
- The local Wi-Fi credential header is mode-restricted and gitignored; a credential-free example header is tracked.

Phase 2 is complete. Development stops here before messages, ntfy integration, persistent alarms, or other Phase 3 work.

## 2026-09-14 - Phase 3 Local Alarm Complete

- Added the pure core `AlarmLogic` (config validation, weekday Monday-first mask, JSON settings codec, once-per-day dedupe, absolute snooze deadline, hardware gate, server clock sample) with 18 Unity cases.
- Extended `TimeService` snapshots with epoch seconds, local year, and year-of-day so alarm matching is independent of the second and deduplicates per day.
- Added `AlarmService` with persistent `/clock/config/settings.json` storage: tmp-write, verify, `.bak` backup, atomic rename, backup recovery on boot, and defaults on malformed config without hanging the clock.
- Added `AlarmScreens`: a Slovenian settings screen (enable toggle, wrapping hour/minute, P T S Č P S N day buttons, summary, `Shrani`) and a ringing screen (`Dobro jutro ♥`, `Dremež +10 min` / `Ugasni`). `handleTap` coordinates were repaired after a concurrent agent corrupted them (touch accumulation, coordinate addition, debounce increment, serial pointer arithmetic).
- Wired the alarm engine into the Home loop (`clockTime.update()` → `alarmService.update()` → `showRinging()`), pausing the slideshow only during Home and keeping it running while snoozed.
- Added serial commands `!alarm` status, `!alarm on/off`, `!alarm set HH MM`, `!alarm days` Monday-first, `!alarm test`, `!alarm snooze`, `!alarm stop`, `!alarm reset-day`, and `!v 0..100` persisted volume.
- Integrated the community `ESP32-audioI2S` library (`git#3.0.12`) driven by SD WAV through the internal DAC on GPIO26. Only `I2S_DAC_CHANNEL_LEFT_EN` is enabled so GPIO25 (touch clock) stays free; samples are duplicated into both frame slots.
- Root-caused earlier audio silence: GPIO26 stayed routed to the DAC after cleanup, so later PWM never reached the amplifier. `start()` now explicitly re-enables DAC2 before playback and `stop()` does `dacDisable`+LOW, giving clean idle silence after Snooze/Stop.
- Volume scaling reworked for the CYD's high-gain amplifier: `1 + volume*11/100` maps 0–100 to library level 0–11 (saved 85% ≈ level 10). 100% can no longer drive the amp into clipping/hum.
- Added `MelodyPlayer` fallback lullaby and a `wavIsPlayable` header check so a missing or corrupt `alarm.wav` plays the melody instead of an endless silent `audioHeader reading timeout` loop (verified on a deliberately corrupted file).
- Added `WavUploader` (`!wavraw BYTES`, CRC16-XModem) and `tools/deploy_alarm_wav_serial.py`; re-deployed the restored 882,044-byte ring over serial after the corrupt-file test.
- Switched to the `huge_app.csv` partition (3 MB app, no OTA) because the audio library exceeds the default app partition. Firmware: 54,692 bytes static RAM, 1,395,009 bytes flash (44.3%).
- Hardware verification (all on the connected CYD):
  - WAV audible through the amp with mild hum at library level 10; Snooze and Stop both silence fully and restore Home.
  - Natural scheduled fire: `!alarm reset-day` + `!alarm set 20 45` at 20:43, then at 20:45 the device rang by itself (`WAV started`, `[SCREEN] ringing`).
  - Touch Snooze on the real ring; then with no command, the snoozed alarm re-rang by itself at the 10-minute deadline (20:59, ring count 2).
  - Serial Stop leaves `state=armed enabled=yes` (once-per-day key recorded), and `!alarm reset-day` re-enables an immediate re-test.
  - Settings persisted across many reboots (time, days, volume, handled day); config and backup loading verified.
  - Fallback melody plays on a missing/corrupt WAV and touch Snooze still works on it; the real WAV was restored and re-verified.
  - Free heap: ~232 KB boot, ~137 KB Home, ~133 KB ringing, stable across slideshow and snoozed minutes.
  - One transient SD mount failure appeared right after the corrupt-file/firmware-flash sequence and cleared on the next boot; no data loss.
- Host tests: 34 native Unity cases (15 Home + 19 alarm) and all Python photo/WAV tests pass.
  - Settings UI verified end-to-end: opening via the bottom-right button, editing, and `Shrani` persisted new time/day-selection values (status reported `time=21:48 days=109` after the save plus a reboot). The on-screen "Shranjevanje ni uspelo" only appears via the last-day-to-disable guard or an SD I/O rejection.
- Final firmware size: 54,692 bytes static RAM and 1,395,009 bytes flash (44.3% of the 3 MB app partition).

Phase 3 is complete. Development stops here before the local messages phase; ntfy, C3 wiring, DS1302, OTA, and Immich remain out of scope.

## 2026-09-14 - Phase 4 Persistent Messages + Ntfy (in progress)

- Added the pure core `MessageLogic` (hand-rolled JSON stream parsing, message ingest/dedupe, history cap/prune, seen-ack id/body codecs, `since` param, preview copying without splitting UTF-8 sequences, heart normalization into a PUA token, time labels, popup verdicts) with 25 Unity cases.
- Added `MessageService` with persistent `/clock/messages/messages.json` newest-first line-store: tmp-write, whole-file verify, `.bak` backup, atomic rename, backup recovery on boot, defaults on missing store. A RAM index of up to 100 summaries drives the badge/list/popup without re-reading SD; prune drops the oldest READ message first and refuses to evict unread ones.
- Added `TimeService::utcOffsetSeconds()` (minute-floor civil math) so message time labels render in local time.
- Added `NtfyClient` with two non-blocking FSMs (stream + ack). The stream FSM does a chunked-decoder `GET <base>/<topic>/json?since=latest|<lastId>` with a bounded record buffer, keepalive watchdog, exponential backoff cap 30 s, and a `reconnectNow()` for replays. Only `event=="message"` ingests; dedupe is by ntfy `id`. The ack FSM publishes `POST <base>/<ackTopic>` with the Bearer token, `X-Sequence-ID`, and a JSON body on the first pending unread.
- Added `MessageScreens`: a 4.5-second popup pinned to the bottom edge while Home is idle (dismissed by any navigation/alarm), a scrollable list using the reserved top-right touch zone, and a full-text detail screen that marks a message read and triggers the ntfy ack.
- Wired `main.cpp`: `ScreenMode` now includes MessagesList/MessageDetail; the popup policy shows only fresh unread on Home; alarm always outranks messages; the top-right zone opens the list; serial `!msg`/`!msg list|unread|ack`/`!msg inject <text>` and `!ntfy`/`!ntfy reconnect` replace the old `!m COUNT` demo.
- Set `-DARDUINO_LOOP_STACK_SIZE=16384`: the inject/addIncoming/persist path (which also runs from the real stream ingest) overflowed the default loop stack (canary panic); the larger stack plus an ASCII-only serial inject fixed it.
- Created `include/ntfy_credentials.example.h` (tracked) and gitignored `include/ntfy_credentials.h`; empty credential macros keep the device inert (`host=unset`) until a server is supplied.
- Hardware verification on the connected CYD (SD present, 130-photo library):
  - `!msg inject` persists; reboot restores all injected messages with correct `last_processed`.
  - `!msg list`/`!msg`/`!msg unread` report the live index.
  - Fixed a `last_processed=:` parse bug (off-by-17 quote scan) that dropped the id to a single colon after reboot.
  - SD mount flakiness recurs right after crash/reset sequences but clears on the next boot (same class as Phase 3's transient).
  - Fixed `!msg` dispatch: handler originally checked `command[1]==' '` which made `!msg` unreachable (command is `msg`, not `!msg`); switched to `strncmp(command,"msg",3)` with adjusted subcommand offsets.
  - Added `!msg read INDEX` serial command to mark a message read and queue its ack (useful for testing without the touchscreen).
  - Fixed stack overflow (`Stack canary watchpoint triggered (loopTask)`) from the inject/addIncoming/persist path; added `-DARDUINO_LOOP_STACK_SIZE=16384` to `platformio.ini`.
- TLS hardening and ntfy integration:
  - Embedded the self-signed GTS Root R4 CA certificate (`include/ntfy_ca_cert.h`) — the only trust anchor needed for ntfy.cekluka.com; validates the full chain (leaf cekluka.com → WE1 → GTS Root R4) via OpenSSL.
  - Fail-closed: both the stream and ack TLS contexts reject connections if no CA cert is present; `setInsecure()` removed entirely. Optional `MATEJA_NTFY_CA_CERT` macro in credentials overrides the default.
  - Fixed `since=` double-prefix bug: `makeSinceParam()` returns `since=<id>` including the prefix, but the stream request line was `?since=%s` → malformed `?since=since=<id>` → HTTP 400. Changed format to `?%s`.
  - HTTP 400 "invalid since" now auto-resets the persisted checkpoint to empty so the next stream attempt falls back to `since=latest` (protects against poisoned/fake IDs).
  - Ack heap fix: the ESP32-2432S028 cannot sustain two concurrent mbedTLS contexts (each needs ~32KB in/out buffers) with ~78KB fragmented free heap. The stream now pauses before the ack connect (`streamState_=kIdle`, `reconnectAtMs_=0`) and resumes with `since=<id>` after the synchronous ack POST finishes — lossless for live messages.
  - Pre-reboot ack re-queue: on startup `NtfyClient::begin()` scans for read-but-unacked messages and re-queues any that were lost to a prior reboot.
- Real ntfy push verified on hardware:
  - Received a live message from the user's phone (payload: "luka testera če lahko pošlje sporočilo s telefona", id `grMv21xi005M`).
  - `!msg read 0` → stream pauses → ack POST to `mateja-clock-seen` returns HTTP 200 (confirmed via curl) → stream resumes → `state=chunk size`.
  - Reboot: 4 messages restored, `last_processed=XPJURRtIpyIK`, stream reconnects with `since=<lastId>`, no message loss.
- Host tests: 59 native Unity cases (15 Home + 19 alarm + 25 message) and 21 Python tests pass.
- Firmware: 74,408 bytes static RAM and 1,419,041 bytes flash (45.1% of the 3 MB app partition).

Phase 4 is complete. Real-time push, persistence, and ack all verified end-to-end on hardware.
