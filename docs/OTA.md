# GitHub Release OTA

## Version and release assets

`version.txt` is the only manually maintained firmware version. PlatformIO's
`tools/pio_version.py` compiles it as `MATEJA_CLOCK_VERSION`; the release
workflow requires tag `v<version>` and generates `mateja-clock.bin` plus a
compact `manifest.json` containing product, version, filename, size, and
SHA-256.

The clock rejects wrong products/filenames, malformed numeric semantic
versions, firmware smaller than 64 KiB, firmware larger than the inactive slot,
malformed SHA-256, equal versions for installation, and downgrades.

## Flash layout

The 4 MiB CYD uses `partitions/ota_4mb.csv`:

| Partition | Offset | Size |
| --- | ---: | ---: |
| NVS | `0x9000` | `0x5000` |
| OTA metadata | `0xE000` | `0x2000` |
| `ota_0` | `0x10000` | `0x1F0000` (2,031,616 bytes) |
| `ota_1` | `0x200000` | `0x1F0000` (2,031,616 bytes) |
| coredump | `0x3F0000` | `0x10000` |

There is no flash filesystem; persistent application data is on SD. The first
installation must be a CH340 serial upload because the old `huge_app.csv` table
has no inactive app slot. Never flash the ESP32-C3 `/dev/ttyACM0` device.

## SD configuration

Secrets are not compiled into release firmware. Runtime Wi-Fi, ntfy, and
optional admin settings live under `/clock/config/`. The public GitHub
repository identifier lives at `/clock/config/ota.json`:

```json
{"repository":"OWNER/REPO"}
```

Provision ntfy and OTA after the SD is mounted:

```bash
python3 tools/upload_ntfy_config.py
python3 tools/upload_ota_config.py OWNER/REPO
```

These files are protected by full SD reset and never belong in `assets/sd/`.

## Commands and safety

```text
!ota status
!ota check
!ota update
```

`check` downloads only the manifest. `update` explicitly streams a newer valid
release into the inactive slot in at most 1 KiB chunks. Installation is never
automatic at boot.

- TLS uses a Mozilla-derived CA bundle and hostname validation; there is no
  `setInsecure()` fallback.
- Redirects are HTTPS-only, limited to five, and restricted to `github.com`
  and `release-assets.githubusercontent.com`.
- Ntfy's stream is paused while OTA owns the constrained TLS heap, then resumed
  using its processed-ID checkpoint.
- The current boot slot is unchanged until byte count, release SHA-256, ESP
  image validation, and partition validation all succeed.
- OTA cannot start while Ringing/Snoozed, alarm audio is active, time is
  invalid, or a scheduled alarm is within 15 minutes.
- Alarm activity during transfer aborts OTA before boot selection. Reboot is
  deferred while alarm state/audio/safety window is unsafe.

## Rollback

Arduino's immediate first-boot confirmation is overridden. A newly installed
image remains pending until setup succeeds, SD and alarm WAV are available,
and the normal loop has run for at least 15 seconds. A definite local health
failure marks the image invalid and reboots to the previous slot. C3/network
absence is not a rollback criterion.

Framework support and implementation are present, but automatic rollback is
not verified until a real A -> deliberately unhealthy B -> A hardware test
passes.
