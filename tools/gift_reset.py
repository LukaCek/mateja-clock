#!/usr/bin/env python3
"""Reset user/test state on the Mateja Clock over USB serial.

FUTURE USE (no AI needed):

    Connect Mateja Clock over USB and run:

        python3 tools/gift_reset.py

    Confirm with y. Wait for "Gift reset: PASS".

For a stronger, destructive SD rebuild (photos preserved) run:

        python3 tools/gift_reset.py --full

    Read the FULL RESET warning and type "FULL RESET" to continue.

NORMAL mode invokes the firmware's authoritative `!gift-reset` command, which
clears only user/test state:

  * all local messages (read + unread, seen metadata lives in the store)
  * unread badge / popup / transient message state
  * Ringing, Snoozed, Zzz, and any active test-alarm state
  * the persisted handled_occurrence (so the next real alarm is not suppressed)

Everything production is untouched: Wi-Fi credentials, ntfy configuration,
alarm settings, volume, brightness, photos, RAW emoji assets, SD production
assets, RTC/C3 integration, touch/display configuration. The ntfy stream
checkpoint (processed.json) is preserved so old retained messages are not
replayed after the reset.

FULL mode rebuilds all NON-PHOTO SD content via the firmware's
`!gift-reset-full` wipe, then restores the required production assets from
the computers canonical copy in assets/sd/ (RAW emoji + alarm.wav), recreates
required directories, restores the logical ntfy checkpoint, re-applies the
alarm configuration, and verifies the clock is operational.

FULL mode preserves /clock/photos/, /clock/manifest.json, and the Wi-Fi
credentials. Runtime ntfy/admin configuration stays on the SD card. It deletes
messages, other SD config/state, audio, emoji, temporary and
recovery files (including FSCK*.REC fragments), then restores whatever firmware
requires from assets/sd/. Photo set equality before/after is enforced; a single
missing photo fails the whole reset.

The Wi-Fi config (/clock/config/wifi.json) is captured from the clock BEFORE
the destructive wipe and restored afterwards. If it cannot be captured and no
replacement is available from the local gitignored include/wifi_credentials.h,
the reset aborts before deleting anything. The payload is held in memory only,
never printed, and never written into the repository.

This script NEVER erases flash, formats the SD card, modifies the C3 firmware,
or touches the DS1302 RTC or display calibration.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("ERROR: pyserial is required. Install it with: "
             "pip install pyserial")

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.deploy_alarm_wav_serial import (  # noqa: E402
    crc16_xmodem,
    read_until_marker,
    stream_wav,
)

DEFAULT_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD = 115200
STARTUP_SETTLE_SECONDS = 12.0
QUIET_GAP_SECONDS = 0.4
QUERY_TIMEOUT_SECONDS = 10.0
OBSOLETE_EMOJI_DIRS = ["/emoji/16", "/emoji/32", "/emoji/48"]

BANNER = "Mateja Clock Gift Reset"

FULL_WARNING = (
    "FULL RESET\n"
    "----------\n"
    "This will delete all non-photo data from the Mateja Clock SD card.\n"
    "\n"
    "PRESERVED:\n"
    "- Mateja photos\n"
    "- Wi-Fi credentials (captured, then restored)\n"
    "- ntfy/admin runtime configuration\n"
    "\n"
    "RESET/REPLACED:\n"
    "- messages\n"
    "- message metadata\n"
    "- alarm runtime history\n"
    "- SD config/state\n"
    "- audio assets\n"
    "- emoji assets\n"
    "- temporary/recovery files\n"
    "- other non-photo SD content\n"
    "\n"
    "Required production assets will then be restored from the computer."
)

ASSETS_DIR = os.path.join(PROJECT_ROOT, "assets", "sd")
EMOJI_ASSETS = {
    "32": {"1f44d.raw": 2052, "1f60a.raw": 2052, "1f618.raw": 2052,
           "1f970.raw": 2052, "2764.raw": 2052},
    "48": {"1f44d.raw": 4612, "1f60a.raw": 4612, "1f618.raw": 4612,
           "1f970.raw": 4612, "2764.raw": 4612},
}
AUDIO_ASSET = os.path.join(ASSETS_DIR, "audio", "alarm.wav")
WIFI_CONFIG_PATH = "/clock/config/wifi.json"
NTFY_CONFIG_PATH = "/clock/config/ntfy.json"
ADMIN_CONFIG_PATH = "/clock/config/admin.json"
OTA_CONFIG_PATH = "/clock/config/ota.json"
WIFI_HEADER_PATH = os.path.join(PROJECT_ROOT, "include", "wifi_credentials.h")


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

def open_serial(port):
    ser = serial.Serial(
        port,
        BAUD,
        timeout=0.2,
        write_timeout=2.0,
        dsrdtr=False,
        rtscts=False,
    )
    # CH340 may assert DTR/RTS on open and reboot the CYD; disable them so we
    # do not keep pulsing the reset line, then let the board finish booting.
    ser.dtr = False
    ser.rts = False
    return ser


def read_until_quiet(ser, timeout=None):
    buffer = b""
    last_activity = time.monotonic()
    deadline = time.monotonic() + (timeout or QUERY_TIMEOUT_SECONDS)
    while time.monotonic() < deadline:
        chunk = ser.read(512)
        if chunk:
            buffer += chunk
            last_activity = time.monotonic()
        elif time.monotonic() - last_activity >= QUIET_GAP_SECONDS:
            break
        else:
            time.sleep(0.05)
    return buffer.decode("utf-8", errors="replace")


def send_command(ser, command, timeout=None):
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()
    return read_until_quiet(ser, timeout=timeout)


def status_lines(text, prefix):
    return [line for line in text.splitlines() if line.startswith(prefix)]


def last_status_line(text, prefix):
    lines = status_lines(text, prefix)
    return lines[-1] if lines else None


def retry_parse(ser, command, extractor, attempts=3, pause=0.5):
    for _ in range(attempts):
        text = send_command(ser, command)
        result = extractor(text)
        if result is not None:
            return result
        time.sleep(pause)
    return None


def parse_msg_status(ser):
    def extract(text):
        line = last_status_line(text, "[MSG_STATUS]")
        if not line:
            return None
        match = re.search(r"count=(\d+) unread=(\d+) last_processed=(\S+)",
                          line)
        if not match:
            return None
        return {
            "count": int(match.group(1)),
            "unread": int(match.group(2)),
            "last_processed": match.group(3),
        }
    return retry_parse(ser, "!msg", extract)


def parse_alarm_status(ser):
    def extract(text):
        line = last_status_line(text, "[ALARM_STATUS]")
        if not line:
            return None
        match = re.search(
            r"software_enabled=(\w+).*?state=(\w+) origin=(\w+) "
            r"time=(\d+):(\d+) days=(\d+) snooze=(\d+) volume=(\d+) "
            r"last_handled_occurrence=(\S+)", line)
        if not match:
            return None
        return {
            "enabled": "yes" if match.group(1) == "yes" else False,
            "state": match.group(2),
            "origin": match.group(3),
            "hour": int(match.group(4)),
            "minute": int(match.group(5)),
            "days": int(match.group(6)),
            "snooze": int(match.group(7)),
            "volume": int(match.group(8)),
            "handled": match.group(9),
        }
    return retry_parse(ser, "!alarm", extract)


def parse_c3_status(ser):
    def extract(text):
        line = last_status_line(text, "[C3]")
        if not line:
            return None
        match = re.search(r"link=(connected|down).*?compat=(\w+).*?rtc=(\w+)",
                          line)
        if not match:
            return {"link": None, "rtc": None}
        return {"link": match.group(1), "rtc": match.group(3)}
    return retry_parse(ser, "!c3", extract)


def parse_rtc_status(ser):
    def extract(text):
        time_line = last_status_line(text, "[TIME_STATUS]")
        valid = None
        source = None
        if time_line:
            match = re.search(r"valid=(\w+) source=(\w+)", time_line)
            if match:
                valid = match.group(1)
                source = match.group(2)
        rtc_line = last_status_line(text, "[RTC]")
        if rtc_line:
            match = re.search(r"clock_source=(\w+)", rtc_line)
            if match:
                source = match.group(1)
        if valid is None and source is None:
            return None
        return {"valid": valid, "source": source}
    return retry_parse(ser, "!rtc", extract)


def parse_gift_reset(line):
    match = re.search(
        r"messages=(\d+) unread=(\d+) handled=(\S+) state=(\w+) origin=(\w+)",
        line)
    if not match:
        return None
    return {
        "messages": int(match.group(1)),
        "unread": int(match.group(2)),
        "handled": match.group(3),
        "state": match.group(4),
        "origin": match.group(5),
    }


# ---------------------------------------------------------------------------
# SD directory listing
# ---------------------------------------------------------------------------

def list_dir_entries(ser, directory):
    """Return (set_of_subdirs, dict_of_file_path -> byte size).

    Uses the marker-based [LS_END] line instead of a quiet-gap timeout so
    that concurrent firmware log output does not truncate the response.
    """
    prefix = directory.rstrip("/") + "/"
    subdirs = set()
    files = {}
    ser.reset_input_buffer()
    ser.write(("!ls %s\r\n" % directory).encode("ascii"))
    ser.flush()
    deadline = time.monotonic() + 15.0
    buffer = b""
    got_end = False
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        ser.timeout = min(0.4, remaining)
        chunk = ser.read(max(1, min(4096, ser.in_waiting or 4096)))
        if not chunk:
            continue
        buffer += chunk
        if b"[LS_END]" in buffer:
            got_end = True
            break
    if not got_end:
        return subdirs, files
    text = buffer.decode("utf-8", "replace")
    for line in text.splitlines():
        if not line.startswith("[LS] "):
            continue
        entry = line[len("[LS] "):]
        if entry.endswith("/"):
            path = entry[:-1]
            if path.startswith(prefix):
                subdirs.add(path)
        else:
            match = re.match(r"(.*) bytes=(\d+)$", entry)
            if not match:
                continue
            path, size = match.group(1), int(match.group(2))
            if path.startswith(prefix):
                files[path] = size
    return subdirs, files


def collect_sd_files(ser, root):
    """Recursively collect every file path under root as a {path: bytes} dict."""
    collected = {}
    queue = [root]
    while queue:
        current = queue.pop(0)
        subdirs, files = list_dir_entries(ser, current)
        collected.update(files)
        queue.extend(sorted(subdirs))
    return collected


# ---------------------------------------------------------------------------
# Reset + verification
# ---------------------------------------------------------------------------

def run_gift_reset(ser):
    text = send_command(ser, "!gift-reset")
    line = last_status_line(text, "[GIFT_RESET]")
    if line is None:
        return None
    return parse_gift_reset(line)


def validate(before, after, reset_line):
    problems = []
    before_msg = before.get("msg") if before else None
    after_msg = after.get("msg") if after else None
    alarm_after = after.get("alarm") if after else None
    if before_msg is None or after_msg is None:
        problems.append("message status could not be parsed")
    else:
        if after_msg.get("count", -1) != 0:
            problems.append("messages not cleared (count=%d)" %
                            after_msg.get("count"))
        if after_msg.get("unread", -1) != 0:
            problems.append("unread not cleared (unread=%d)" %
                            after_msg.get("unread"))
    if alarm_after is None:
        problems.append("alarm status could not be parsed")
    else:
        if alarm_after.get("state") != "armed":
            problems.append("alarm not armed (state=%s)" %
                            alarm_after.get("state"))
        if alarm_after.get("handled") != "none":
            problems.append("handled occurrence not cleared (value=%s)" %
                            alarm_after.get("handled"))
    if reset_line is None:
        problems.append("firmware gift-reset did not report a result")
    else:
        if reset_line.get("messages") != 0 or reset_line.get("unread") != 0:
            problems.append("gift-reset reported lingering messages")
        if reset_line.get("state") != "armed":
            problems.append("gift-reset reported non-armed state")
        if reset_line.get("handled") != "none":
            problems.append("gift-reset reported handled occurrence set")
    return problems


# ---------------------------------------------------------------------------
# Full SD rebuild
# ---------------------------------------------------------------------------

def preflight_full_assets():
    """Verify every restore source exists locally. STOPS the wipe otherwise."""
    problems = []
    for size, expected in EMOJI_ASSETS.items():
        directory = os.path.join(ASSETS_DIR, "emoji", size)
        if not os.path.isdir(directory):
            problems.append("missing emoji asset dir: %s" % directory)
            continue
        present = set(os.listdir(directory))
        for name, size_bytes in expected.items():
            path = os.path.join(directory, name)
            if name not in present:
                problems.append("missing emoji asset: %s" % path)
            elif os.path.getsize(path) != size_bytes:
                problems.append(
                    "emoji asset size mismatch: %s (have %d, want %d)" %
                    (path, os.path.getsize(path), size_bytes))
        for name in sorted(present):
            if name not in expected:
                problems.append("unexpected file in %s: %s" % (directory,
                                                               name))
    if not os.path.isfile(AUDIO_ASSET):
        problems.append("missing audio asset: %s" % AUDIO_ASSET)
    else:
        try:
            with open(AUDIO_ASSET, "rb") as handle:
                audio_bytes = handle.read()
        except OSError as error:
            problems.append("cannot read audio asset %s: %s" %
                            (AUDIO_ASSET, error))
            audio_bytes = None
        if audio_bytes is not None:
            if audio_bytes[0:4] != b"RIFF" or audio_bytes[8:12] != b"WAVE":
                problems.append("audio asset is not a WAV file: %s" %
                                AUDIO_ASSET)
    return problems


def ensure_remote_dirs(ser, remote_path):
    """Create every ancestor directory of remote_path, shallowest first."""
    parts = remote_path.strip("/").split("/")
    current = ""
    for part in parts:
        if not part:
            continue
        current += "/" + part
        send_command(ser, "!mkdir %s" % current)


def upload_putfile(ser, local_bytes, remote_path, attempts=3):
    """Upload one small file via the firmware !putfile protocol."""
    parent = remote_path.rsplit("/", 1)[0]
    last_detail = "not attempted"
    for attempt in range(attempts):
        try:
            ensure_remote_dirs(ser, parent)
            ser.reset_input_buffer()
            ser.write(("!putfile %s %d\r\n" %
                       (remote_path, len(local_bytes))).encode("ascii"))
            ser.flush()
            try:
                ready = read_until_marker(ser, "FILEREADY", timeout_s=8.0,
                                          prefixes=("[FILE] failed",
                                                    "[FILE] sd"))
            except TimeoutError:
                last_detail = "no FILEREADY for %s" % remote_path
                time.sleep(1.0)
                continue
            if "FILEREADY" not in ready:
                last_detail = "no FILEREADY for %s" % remote_path
                time.sleep(1.0)
                continue
            # Gentle pacing at roughly the wire rate (115200 baud, 8N1).
            chunk_size = 1024
            for offset in range(0, len(local_bytes), chunk_size):
                chunk = local_bytes[offset:offset + chunk_size]
                ser.write(chunk)
                ser.flush()
                time.sleep(len(chunk) * 10.0 / BAUD)
            ser.flush()
            try:
                result = read_until_marker(
                    ser, "[FILE] received", timeout_s=20.0,
                    prefixes=("[FILE] failed", "[FILE] sd"))
            except TimeoutError:
                last_detail = "no result line for %s" % remote_path
                time.sleep(1.0)
                continue
            if "[FILE] received" in result:
                return True, remote_path
            last_detail = "upload failed for %s: %s" % (remote_path,
                                                       result.strip())
        except Exception as error:  # transient USB tcdrain/EIO
            last_detail = "serial error uploading %s: %s" % (remote_path,
                                                             error)
        time.sleep(1.0)
    return False, last_detail


def _upload_wav_once(ser, data):
    ser.reset_input_buffer()
    ser.write(("!wavraw %d\r\n" % len(data)).encode("ascii"))
    ser.flush()
    try:
        ready = read_until_marker(ser, "WAVREADY", timeout_s=8.0,
                                  prefixes=("[WAV_UPLOAD] fail",))
    except TimeoutError:
        return False, "no WAVREADY handshake"
    if "WAV_UPLOAD] fail" in ready:
        return False, ready.strip()
    expected = int(ready.split("WAVREADY", 1)[1].split()[0])
    if expected != len(data):
        return False, "device expects %d bytes, have %d" % (expected,
                                                            len(data))
    stream_wav(ser, data, bridge_delay_s=0.3)
    try:
        result = read_until_marker(ser, "[WAV_UPLOAD] done", timeout_s=120.0,
                                   prefixes=("[WAV_UPLOAD] fail",))
    except TimeoutError:
        return False, "no upload result line"
    if "[WAV_UPLOAD] done" in result:
        return True, result.strip()
    return False, result.strip()


def upload_wav(ser, data, attempts=3):
    """Upload the alarm WAV via the firmware !wavraw protocol."""
    last_detail = "not attempted"
    for attempt in range(attempts):
        try:
            ok, detail = _upload_wav_once(ser, data)
            if ok:
                return ok, detail
            last_detail = detail
        except Exception as error:  # transient USB tcdrain/EIO
            last_detail = "serial error uploading WAV: %s" % error
        time.sleep(1.5)
    return False, last_detail


def restore_emoji_assets(ser):
    """Upload every committed RAW emoji asset; return (ok, problems)."""
    problems = []
    for size, expected in EMOJI_ASSETS.items():
        directory = os.path.join(ASSETS_DIR, "emoji", size)
        for name in sorted(expected):
            path = os.path.join(directory, name)
            with open(path, "rb") as handle:
                payload = handle.read()
            ok, detail = upload_putfile(ser, payload, "/emoji/%s/%s" %
                                        (size, name))
            if not ok:
                problems.append(detail)
    return (not problems), problems


def restore_alarm_wav(ser):
    with open(AUDIO_ASSET, "rb") as handle:
        payload = handle.read()
    return upload_wav(ser, payload)


def read_wifi_header(path=None):
    """Return (ssid, password) from a local gitignored credential header."""
    path = WIFI_HEADER_PATH if path is None else path
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    def macro(name):
        match = re.search(r'#define\s+%s\s+"((?:[^"\\]|\\.)*)"' % name, text)
        if match is None:
            return None
        return match.group(1).replace('\\"', '"').replace("\\\\", "\\")

    ssid = macro("MATEJA_WIFI_SSID")
    if not ssid:
        raise ValueError("MATEJA_WIFI_SSID missing/empty in %s" % path)
    password = macro("MATEJA_WIFI_PASSWORD")
    return ssid, ("" if password is None else password)


def build_wifi_payload(ssid, password):
    return json.dumps({"ssid": ssid, "password": password},
                      separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def host_wifi_payload():
    """Best-effort replacement config from the local header, else None."""
    try:
        ssid, password = read_wifi_header()
    except (OSError, ValueError):
        return None
    return build_wifi_payload(ssid, password)


def decode_wifi_dump(lines):
    expected = None
    chunks = []
    for text in lines:
        if text.startswith("[WIFICFG] bytes="):
            try:
                expected = int(text.split("=", 1)[1])
            except ValueError:
                return None
        elif re.fullmatch(r"[0-9a-fA-F]+", text):
            chunks.append(text)
    if expected is None:
        return None
    try:
        data = bytes.fromhex("".join(chunks))
    except ValueError:
        return None
    return data if len(data) == expected else None


def capture_wifi_config(ser):
    """Read back /clock/config/wifi.json via the dedicated !wificfg command.

    Returns (data, detail); data is None when the file is absent or the dump
    cannot be decoded. The payload is kept in memory only and never printed.
    """
    ser.reset_input_buffer()
    ser.write(b"!wificfg get\r\n")
    ser.flush()
    deadline = time.monotonic() + 10.0
    buffer = b""
    lines = []
    while time.monotonic() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip()
                if text.startswith("[WIFICFG] missing"):
                    return None, "missing"
                if text.startswith("[WIFICFG] end"):
                    data = decode_wifi_dump(lines)
                    return (data, "captured") if data is not None \
                        else (None, "undecodable")
                lines.append(text)
        else:
            time.sleep(0.02)
    return None, "timeout"


def verify_wifi_config(ser, expected):
    data, detail = capture_wifi_config(ser)
    if data is None:
        return ["Wi-Fi config not readable after restore: %s" % detail]
    if data != expected:
        return ["Wi-Fi config on SD differs from the expected payload"]
    return []


def verify_wifi_online(ser):
    status = last_status_line(send_command(ser, "!d"), "[TIME_STATUS]")
    if not status:
        return ["Wi-Fi status not reported"]
    if "wifi=connected" not in status:
        return ["Wi-Fi not connected after full reset"]
    return []


def protected_config_sizes(ser):
    _, files = list_dir_entries(ser, "/clock/config")
    return {path: files[path] for path in
            (NTFY_CONFIG_PATH, ADMIN_CONFIG_PATH, OTA_CONFIG_PATH)
            if path in files}


def verify_protected_configs(ser, expected):
    problems = []
    _, files = list_dir_entries(ser, "/clock/config")
    if NTFY_CONFIG_PATH not in files:
        problems.append("ntfy config missing after full reset")
    for path, size in expected.items():
        if files.get(path) != size:
            problems.append("protected config changed: %s (%s != %s)" %
                            (path, files.get(path), size))
    return problems


def restore_processed_checkpoint(ser, last_processed):
    if last_processed is None:
        return False, "no checkpoint value captured"
    if last_processed == "(none)":
        result = send_command(ser, "!msg processed clear")
        ok = "[MSGRO] processed cleared" in result
        return ok, "cleared" if ok else result.strip()
    result = send_command(ser, "!msg processed %s" % last_processed)
    if "[MSGRO] processed set id=%s" % last_processed not in result:
        return False, "checkpoint restore failed: %s" % result.strip()
    return True, last_processed


def reapply_alarm_config(ser, alarm):
    """Re-apply the captured production alarm config after the wipe."""
    problems = []
    if alarm is None:
        problems.append("no alarm config captured; cannot re-apply")
        return problems
    days_mask = alarm.get("days", 127)
    days_bits = "".join("1" if days_mask & (1 << index) else "0"
                        for index in range(7))
    commands = [
        "!alarm set %02d %02d" % (alarm.get("hour", 7),
                                  alarm.get("minute", 0)),
        "!alarm days %s" % days_bits,
        "!alarm snooze-min %d" % alarm.get("snooze", 10),
        "!v %d" % alarm.get("volume", 65),
    ]
    if alarm.get("enabled"):
        commands.append("!alarm on")
    for command in commands:
        result = send_command(ser, command)
        if "[ALARM] settings save failed" in result:
            problems.append("command failed: %s" % command)
    return problems


def verify_emoji_on_sd(ser):
    problems = []
    for size, expected in EMOJI_ASSETS.items():
        _, files = list_dir_entries(ser, "/emoji/%s" % size)
        for name, size_bytes in expected.items():
            remote = "/emoji/%s/%s" % (size, name)
            if remote not in files:
                problems.append("emoji missing after restore: %s" % remote)
            elif files[remote] != size_bytes:
                problems.append("emoji size mismatch on SD: %s (%d != %d)" %
                                (remote, files[remote], size_bytes))
        for path, file_size in files.items():
            if path not in ["/emoji/%s/%s" % (size, name)
                            for name in expected]:
                problems.append("unexpected emoji on SD: %s" % path)
    return problems


def verify_audio_on_sd(ser):
    with open(AUDIO_ASSET, "rb") as handle:
        expected_size = len(handle.read())
    _, files = list_dir_entries(ser, "/clock/audio")
    path = "/clock/audio/alarm.wav"
    if path not in files:
        return ["audio missing after restore: %s" % path]
    if files[path] != expected_size:
        return ["audio size mismatch on SD: %d != %d" % (files[path],
                                                         expected_size)]
    return []


def full_reset_diagnostics(ser, photo_count):
    problems = []
    text = send_command(ser, "!d")
    stats = last_status_line(text, "[PHOTO_STATS]")
    if not stats:
        problems.append("no photo stats in diagnostics")
    else:
        match = re.search(r"count=(\d+).*?failed=(\d+)", stats)
        if not match:
            problems.append("photo stats unparsable")
        else:
            if int(match.group(1)) != photo_count:
                problems.append("photo stats count %s != %d" %
                                (match.group(1), photo_count))
            if int(match.group(2)) != 0:
                problems.append("photo render failures detected")
    screen = last_status_line(text, "[SCREEN]")
    if screen:
        match = re.search(r"mode=(\d+)", screen)
        if match and match.group(1) != "0":
            problems.append("not on home screen (mode=%s)" % match.group(1))
    return problems


def run_full_reset(ser, before_photos, before_msg, alarm_before, wifi_payload,
                   protected_configs):
    """Execute and verify the full SD rebuild. Returns (ok, problems, report)."""
    problems = []
    report = {}
    photo_before_set = set(before_photos)
    report["photo_before"] = len(photo_before_set)

    print("Inspecting SD card...")
    print("  photos: %d" % len(photo_before_set))

    print("Wiping non-photo SD content (!gift-reset-full)...")
    # The wipe runs silently for several seconds before printing its result,
    # which is longer than the quiet-gap reader waits, so wait for the marker.
    ser.reset_input_buffer()
    ser.write(b"!gift-reset-full\r\n")
    ser.flush()
    try:
        line = read_until_marker(ser, "[FULL_RESET]", timeout_s=120.0,
                                 prefixes=("[FULL_RESET] fail",))
    except TimeoutError:
        problems.append("no [FULL_RESET] response within 120 s")
        return False, problems, report
    line = line.strip()
    if line is None or "fail" in line:
        problems.append("firmware full reset failed: %s" %
                        (line or "no [FULL_RESET] line"))
        return False, problems, report
    match = re.search(r"removed=(\d+) kept=(\d+) photos_kept=(\d+) "
                      r"manifest_kept=(\d+)", line)
    if not match:
        problems.append("firmware full reset line unparsable: %s" % line)
        return False, problems, report
    report["removed"] = int(match.group(1))
    report["kept"] = int(match.group(2))
    report["photos_kept"] = int(match.group(3))
    print("  removed=%s kept=%s photos_kept=%s" %
          (match.group(1), match.group(2), match.group(3)))

    print("Verifying photos survived the wipe...")
    after_wipe = collect_sd_files(ser, "/clock/photos")
    if set(after_wipe) != photo_before_set:
        missing = sorted(photo_before_set - set(after_wipe))
        added = sorted(set(after_wipe) - photo_before_set)
        problems.append(
            "PHOTOS DAMAGED by wipe: %d missing, %d added" %
            (len(missing), len(added)))
        return False, problems, report

    print("Recreating required directories...")
    for directory in ["/clock/messages", "/clock/config", "/clock/audio",
                      "/emoji", "/emoji/32", "/emoji/48"]:
        ensure_remote_dirs(ser, directory)

    print("Restoring Wi-Fi config...")
    ok, detail = upload_putfile(ser, wifi_payload, WIFI_CONFIG_PATH)
    if not ok:
        problems.append("wifi config restore failed: %s" % detail)
    report["wifi"] = ("%d bytes" % len(wifi_payload)) if ok \
        else ("FAILED: %s" % detail)

    print("Restoring emoji assets (%d files)..." %
          sum(len(expected) for expected in EMOJI_ASSETS.values()))
    ok, emoji_problems = restore_emoji_assets(ser)
    if not ok:
        problems.extend(emoji_problems)

    print("Restoring alarm audio...")
    ok, detail = restore_alarm_wav(ser)
    if not ok:
        problems.append("audio restore failed: %s" % detail)
    report["audio"] = detail if ok else ("FAILED: %s" % detail)

    print("Restoring ntfy checkpoint...")
    ok, detail = restore_processed_checkpoint(
        ser, before_msg.get("last_processed") if before_msg else None)
    if not ok:
        problems.append("checkpoint restore failed: %s" % detail)
    report["checkpoint"] = before_msg.get("last_processed") if before_msg \
        else None

    print("Running firmware gift-reset...")
    reset_line = run_gift_reset(ser)

    print("Re-applying alarm configuration...")
    alarm_problems = reapply_alarm_config(ser, alarm_before)
    problems.extend(alarm_problems)

    # ------------------------------------------------------------------
    # Verification
    # ------------------------------------------------------------------
    print("Verifying final state...")
    after_photos = collect_sd_files(ser, "/clock/photos")
    photo_ok = set(after_photos) == photo_before_set
    if not photo_ok:
        problems.append(
            "photo set changed during rebuild: %d -> %d files" %
            (len(photo_before_set), len(after_photos)))

    msg_after = parse_msg_status(ser)
    if msg_after is None:
        problems.append("message status could not be parsed")
    else:
        if msg_after.get("count", -1) != 0:
            problems.append("messages not zero (count=%d)" %
                            msg_after.get("count"))
        if msg_after.get("unread", -1) != 0:
            problems.append("unread not zero (unread=%d)" %
                            msg_after.get("unread"))
        expected_checkpoint = before_msg.get("last_processed") if before_msg \
            else None
        if (expected_checkpoint is not None and
                msg_after.get("last_processed") != expected_checkpoint):
            problems.append("ntfy checkpoint changed: %s -> %s" %
                            (expected_checkpoint,
                             msg_after.get("last_processed")))

    alarm_after = parse_alarm_status(ser)
    if alarm_after is None:
        problems.append("alarm status could not be parsed")
    else:
        if alarm_after.get("state") != "armed":
            problems.append("alarm not armed (state=%s)" %
                            alarm_after.get("state"))
        if alarm_after.get("handled") != "none":
            problems.append("handled occurrence not cleared (value=%s)" %
                            alarm_after.get("handled"))
        if alarm_before is not None:
            expected = [[alarm_before.get("hour"),
                         alarm_before.get("minute")],
                        alarm_before.get("days"),
                        alarm_before.get("snooze"),
                        alarm_before.get("volume"),
                        alarm_before.get("enabled")]
            actual = [[alarm_after.get("hour"), alarm_after.get("minute")],
                      alarm_after.get("days"),
                      alarm_after.get("snooze"),
                      alarm_after.get("volume"),
                      alarm_after.get("enabled")]
            if expected != actual:
                problems.append("alarm config diverged after rebuild: %s" %
                                (actual,))

    problems.extend(verify_emoji_on_sd(ser))
    problems.extend(verify_audio_on_sd(ser))
    problems.extend(verify_wifi_config(ser, wifi_payload))
    problems.extend(verify_protected_configs(ser, protected_configs))
    problems.extend(verify_wifi_online(ser))
    problems.extend(full_reset_diagnostics(ser, len(photo_before_set)))

    c3 = parse_c3_status(ser)
    report["c3"] = (c3.get("link") if c3 else "-")
    if c3 is None:
        problems.append("C3 link could not be verified")
    elif c3.get("link") != "connected":
        problems.append("C3 not connected (link=%s)" % c3.get("link"))
    rtc = parse_rtc_status(ser)
    report["rtc"] = (rtc.get("source") if rtc else "-")

    report["photo_count"] = len(after_photos)
    report["messages"] = (msg_after.get("count") if msg_after else "-")
    report["unread"] = (msg_after.get("unread") if msg_after else "-")
    report["alarm_state"] = (alarm_after.get("state") if alarm_after else "-")
    report["handled"] = (alarm_after.get("handled") if alarm_after else "-")
    report["reset_line"] = reset_line
    return (not problems), problems, report


# ---------------------------------------------------------------------------
# Optional one-time SD cleanup (obsolete emoji PNGs only)
# ---------------------------------------------------------------------------

def cleanup_obsolete_emoji(ser, verbose=True):
    removed = 0
    skipped = 0
    for directory in OBSOLETE_EMOJI_DIRS:
        text = send_command(ser, "!ls %s" % directory)
        png_files = []
        for line in text.splitlines():
            match = re.match(r"\[LS\] (\S+) bytes=(\d+)$", line)
            if match and match.group(1).endswith(".png") and \
                    match.group(1).startswith(directory + "/"):
                png_files.append(match.group(1))
        for path in png_files:
            result = send_command(ser, "!rmfile %s" % path)
            if "[RMFILE] removed " in result:
                removed += 1
            else:
                skipped += 1
                print("  WARNING: could not remove %s" % path)
    # Verify nothing obsolete remains.
    remaining = 0
    for directory in OBSOLETE_EMOJI_DIRS:
        text = send_command(ser, "!ls %s" % directory)
        for line in text.splitlines():
            match = re.match(r"\[LS\] (\S+) bytes=(\d+)$", line)
            if match and match.group(1).endswith(".png") and \
                    match.group(1).startswith(directory + "/"):
                remaining += 1
                print("  WARNING: still present %s" % match.group(1))
    if verbose:
        if removed or skipped:
            print("SD cleanup: %d obsolete PNG removed (%d failed)" %
                  (removed, skipped))
        else:
            print("SD cleanup: none")
    return removed, remaining


# ---------------------------------------------------------------------------
# Optional --flash (rebuild + flash + settle)
# ---------------------------------------------------------------------------

def find_pio():
    venv_pio = os.path.join(PROJECT_ROOT, ".venv", "bin", "pio")
    if os.path.exists(venv_pio):
        return venv_pio
    return "pio"


def flash_firmware():
    pio = find_pio()
    steps = [
        [pio, "test", "-e", "native"],
        [pio, "run", "-e", "cyd_photo_test"],
        [pio, "run", "-e", "cyd_photo_test", "-t", "upload"],
    ]
    for step in steps:
        print("» %s" % " ".join(step))
        result = subprocess.run(step, cwd=PROJECT_ROOT)
        if result.returncode != 0:
            print("ERROR: firmware step failed: %s" % " ".join(step))
            return False
    return True


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def format_pair(prev, cur):
    if prev is None:
        return str(cur) if cur is not None else "-"
    if cur is None:
        return "%s -> -" % prev
    return "%s -> %s" % (prev, cur)


def print_report(before, after, reset_line, c3, rtc, cleanup_result):
    after_msg = after.get("msg") if after else None
    before_msg = before.get("msg") if before else None
    alarm_after = after.get("alarm") if after else None
    alarm_before = before.get("alarm") if before else None
    print()
    print("Messages:           %s" % format_pair(
        before_msg.get("count") if before_msg else None,
        after_msg.get("count") if after_msg else None))
    print("Unread:             %s" % format_pair(
        before_msg.get("unread") if before_msg else None,
        after_msg.get("unread") if after_msg else None))
    print("Handled occurrence: %s" % format_pair(
        alarm_before.get("handled") if alarm_before else None,
        alarm_after.get("handled") if alarm_after else None))
    print("Alarm runtime:      %s" % (alarm_after.get("state")
                                      if alarm_after else "-"))
    print("Snooze:             %s" % ("inactive" if alarm_after and
                                     alarm_after.get("state") == "armed"
                                     else "ACTIVE"))
    if c3:
        print("C3:                 %s (rtc=%s)" %
              (c3.get("link") or "-", c3.get("rtc") or "-"))
    else:
        print("C3:                 -")
    if rtc:
        print("RTC:                %s (source=%s)" %
              (rtc.get("valid") or "-", rtc.get("source") or "-"))
    else:
        print("RTC:                -")
    if cleanup_result is not None:
        print("SD cleanup:         %d PNG removed, %d remaining" %
              (cleanup_result[0], cleanup_result[1]))


def print_full_report(report):
    print()
    print("Photos:             %d (was %d before wipe, unchanged)" %
          (report.get("photo_count", 0), report.get("photo_before", 0)))
    print("Messages:           %s" % report.get("messages", "-"))
    print("Unread:             %s" % report.get("unread", "-"))
    print("Handled occurrence: %s" % report.get("handled", "-"))
    print("Alarm runtime:      %s" % report.get("alarm_state", "-"))
    print("Emoji:              %d files restored" %
          sum(len(expected) for expected in EMOJI_ASSETS.values()))
    print("Audio:              %s" % report.get("audio", "-"))
    print("Wi-Fi config:       %s" % report.get("wifi", "-"))
    print("Checkpoint:         %s" % report.get("checkpoint", "-"))
    print("C3:                 %s" % report.get("c3", "-"))
    print("RTC:                %s" % report.get("rtc", "-"))
    if report.get("reset_line"):
        reset = report["reset_line"]
        print("Gift reset:         messages=%s unread=%s handled=%s state=%s" %
              (reset.get("messages"), reset.get("unread"),
               reset.get("handled"), reset.get("state")))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="tools/gift_reset.py",
        description=(
            "Reset user/test state on the Mateja Clock.\n"
            "\n"
            "NORMAL MODE (default): safe user-state reset. Clears messages and "
            "alarm runtime history while preserving every production asset and "
            "configuration on the SD card.\n"
            "\n"
            "FULL MODE (--full): destructive SD rebuild. Deletes all NON-PHOTO "
            "content on the SD card (messages, config/state, audio, emoji, "
            "temporary/recovery files), then restores the required production "
            "assets from assets/sd/ and verifies the clock is operational. "
            "Photos, the photo manifest, and protected SD runtime config are "
            "preserved."
        ),
        epilog=(
            "Normal use: connect the clock over USB and run "
            "'python3 tools/gift_reset.py', confirm with y, wait for "
            "'Gift reset: PASS'.\n"
            "Full rebuild: 'python3 tools/gift_reset.py --full' (DESTRUCTIVE; "
            "type 'FULL RESET' to confirm). Photos are preserved."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial device (default: %s)" % DEFAULT_PORT,
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Skip the confirmation prompt (non-interactive use).",
    )
    parser.add_argument(
        "--flash",
        action="store_true",
        help="Also rebuild and reflash the CYD firmware first (requires a "
             "clean git tree).",
    )
    parser.add_argument(
        "--cleanup-obsolete",
        action="store_true",
        help="One-time maintenance: remove obsolete emoji PNG files from the "
             "SD card (RAW emoji assets and all other files are preserved).",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="DESTRUCTIVE SD rebuild: delete all non-photo content, then "
             "restore production assets from assets/sd/ and verify. Only "
             "/clock/photos/ and the photo manifest survive.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=QUERY_TIMEOUT_SECONDS,
        help=argparse.SUPPRESS,
    )
    return parser.parse_args(argv)


def confirm_full_reset(args):
    print(FULL_WARNING)
    if args.yes:
        print()
        print("!!! AUTOMATIC CONTINUATION (--yes): this WILL delete all "
              "non-photo SD data.")
        return True
    answer = input("\nType FULL RESET to continue: ").strip()
    if answer != "FULL RESET":
        print("Aborted (exact text 'FULL RESET' required).")
        return False
    return True


def main(argv):
    args = parse_args(argv)
    port = args.port if args.port else DEFAULT_PORT
    if not os.path.exists(port):
        print("ERROR: serial device not found: %s" % port)
        if not args.port:
            print("       Use --port /dev/ttyUSB0 to select the correct "
                  "device.")
        return 1

    if args.full:
        print(BANNER)
        print("-" * len(BANNER))
        print("\nPreparing full SD rebuild... verifying local restore assets")
        problems = preflight_full_assets()
        if problems:
            print("\nFULL RESET: ABORT - required production assets are "
                  "missing on this computer:")
            for problem in problems:
                print("  - %s" % problem)
            print("\nNothing was changed on the clock. Fix the assets and "
                  "re-run.")
            return 2
        print("  assets ok: 10 emoji RAW + alarm WAV found in assets/sd/")
        if not confirm_full_reset(args):
            return 1
    else:
        if not args.yes:
            print(BANNER)
            print("-" * len(BANNER))
            print("This will permanently delete all local messages and reset "
                  "alarm runtime history.")
            if args.cleanup_obsolete:
                print("It will also delete obsolete emoji PNG files from the "
                      "SD card.")
            print("Photos, Wi-Fi, ntfy, alarm settings and emoji RAW assets "
                  "will be preserved.")
            answer = input("Continue? [y/N] ").strip().lower()
            if answer not in ("y", "yes"):
                print("Aborted.")
                return 1

    print(BANNER)
    print("-" * len(BANNER))
    print("Device: %s" % port)

    if args.flash:
        print()
        if not flash_firmware():
            return 1

    print()
    print("Connecting... (waiting up to %d s for the board to settle)"
          % STARTUP_SETTLE_SECONDS)
    try:
        ser = open_serial(port)
    except serial.SerialException as exc:
        print("ERROR: cannot open %s: %s" % (port, exc))
        return 1

    try:
        time.sleep(STARTUP_SETTLE_SECONDS)
        ser.reset_input_buffer()

        if args.full:
            ok, problems, report = _run_full(ser)
            print_full_report(report)
            print()
            if ok:
                print("FULL RESET: PASS")
                return 0
            print("FULL RESET: FAIL")
            for problem in problems:
                print("  - %s" % problem)
            return 1

        print("Resetting user state...")
        before = {"msg": parse_msg_status(ser), "alarm": parse_alarm_status(ser)}
        reset_line = run_gift_reset(ser)
        after = {"msg": parse_msg_status(ser), "alarm": parse_alarm_status(ser)}
        c3 = parse_c3_status(ser)
        rtc = parse_rtc_status(ser)

        cleanup_result = None
        if args.cleanup_obsolete:
            cleanup_result = cleanup_obsolete_emoji(ser)

        check_problems = validate(before, after, reset_line)
        print_report(before, after, reset_line, c3, rtc, cleanup_result)

        print()
        if check_problems:
            print("Gift reset: FAIL")
            for problem in check_problems:
                print("  - %s" % problem)
            return 1
        print("Gift reset: PASS")
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass


def _run_full(ser):
    print("Capturing current state...")
    before_msg = parse_msg_status(ser)
    before_alarm = parse_alarm_status(ser)
    if before_msg is None:
        return False, ["could not capture message status"], {}
    if before_alarm is None:
        return False, ["could not capture alarm status before wipe"], {}
    print("  messages=%s unread=%s last_processed=%s" %
          (before_msg.get("count"), before_msg.get("unread"),
           before_msg.get("last_processed")))
    print("  alarm: %02d:%02d days=%d snooze=%d volume=%d enabled=%s" %
          (before_alarm.get("hour"), before_alarm.get("minute"),
           before_alarm.get("days"), before_alarm.get("snooze"),
           before_alarm.get("volume"), "yes" if before_alarm.get("enabled")
           else "no"))

    print("Capturing Wi-Fi config...")
    wifi_payload, wifi_detail = capture_wifi_config(ser)
    if wifi_payload is None:
        replacement = host_wifi_payload()
        if replacement is None:
            return False, ["cannot capture %s (%s) and no host replacement "
                           "available; aborting before wipe" %
                           (WIFI_CONFIG_PATH, wifi_detail)], {}
        wifi_payload = replacement
        print("  capture unavailable (%s); using host replacement" %
              wifi_detail)
    else:
        print("  captured %d bytes (in memory only)" % len(wifi_payload))

    print("Collecting photo inventory...")
    before_photos = collect_sd_files(ser, "/clock/photos")

    protected_configs = protected_config_sizes(ser)
    if NTFY_CONFIG_PATH not in protected_configs:
        return False, ["required ntfy config missing before wipe"], {}
    print("  protected configs: %s" %
          ", ".join(sorted(protected_configs)))

    ok, problems, report = run_full_reset(
        ser, before_photos, before_msg, before_alarm, wifi_payload,
        protected_configs)
    return ok, problems, report


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
