# SD Card Layout

The production firmware will use this offline-first layout:

```text
/clock/
  photos/
    photo_0001.jpg
    photo_0002.jpg
  messages/
    messages.json
  config/
    settings.json
  audio/
    alarm.wav
  manifest.json
```

Photo assets are baseline RGB JPEG files at exactly 320 x 240 pixels. The ESP32 manifest contains only SD-relative metadata and never contains original computer filesystem paths.

The Phase 0 diagnostic does not create this structure. It verifies card read/write operation using `/.cyd_diagnostic.tmp` and removes the file immediately.

The installed 8 GB card is a single MBR FAT32 volume labeled `MATEJA_CLK`. It passed both Linux and CYD write/read/delete verification. The original approximately 2 GB card produced a kernel-confirmed medium/write fault and must be discarded.

## Phase 1 Deployment

The active card contains:

```text
/clock/
  manifest.json
  photos/
    photo_0001.jpg
    ...
    photo_0130.jpg
```

`manifest.json` has schema version 1 and contains 130 deterministic entries. It contains no PC paths. `source_manifest.json` and `contact_sheets/` stay in the PC output directory and are not deployed.

Use `tools/copy_photos_to_sd.py` for future updates. It requires a mounted removable FAT volume labeled exactly `MATEJA_CLK`. Existing `/clock/photos` or `/clock/manifest.json` require `--replace`; unrelated card contents are preserved.

## Phase 3 Addition

The active card additionally contains:

```text
/clock/
  audio/
    alarm.wav
  config/
    settings.json
```

`/clock/audio/alarm.wav` is the ringing sound. It must be mono, 16-bit, 22050 Hz PCM WAV; the nine deployed rings are 20 seconds (882,000 data bytes). Deploy over serial with `tools/deploy_alarm_wav_serial.py --wav alarm_output/alarm.wav`, which waits for the firmware `WAVREADY` prompt and sends a CRC-verified upload. The firmware plays exactly `/clock/audio/alarm.wav`; the companion `alarm_1.wav`...`alarm_7.wav` files are spare rings for later selection.

`/clock/config/settings.json` is created and owned by the firmware. It stores the alarm config and the last handled local scheduled occurrence (local date plus configured HH:MM):

```json
{"version":1,"alarm":{"enabled":true,"hour":20,"minute":45,"daysMask":127,"snoozeMinutes":10,"volume":85},"handled_occurrence":202609162045}
```

Writes go through `settings.tmp` with verification, a `.bak` backup, and rename. Legacy `lastHandledDayKey` files are accepted for their alarm configuration, but their day-only value is ignored because it cannot identify a safe exact occurrence. If the primary file is unreadable at boot the firmware tries the backup, and if neither loads it falls back to defaults without hanging the clock.

## Phase 4 Addition

The active card additionally contains the message store:

```text
/clock/
  messages/
    messages.json
    processed.json
```

`/clock/messages/messages.json` is owned by the firmware. It stores accepted messages newest-first, one JSON object per line, capped at 100. Prune drops the oldest READ message first and never silently deletes an unread one. Writes go through `messages.tmp`, whole-file verification, a `.bak` backup, and rename (atomic commit); boot recovery prefers the `.bak` if the primary is unreadable.

`/clock/messages/processed.json` records the `lastProcessedId` so a restart replays the ntfy stream from just after the last accepted message (`since=lastId`), not from `since=latest`:

```json
{"version":1,"lastProcessedId":"NTFY-message-id-example"}
```

Both files are created on first write by the firmware. A fresh card starts empty (`last_processed=(none)`); serial `!msg inject <text>` synthesizes a stream event that goes through the identical ingest/persist path as a live ntfy push.
