#!/usr/bin/env python3
"""Reset user/test state on the Mateja Clock over USB serial.

FUTURE USE (no AI needed):

    Connect Mateja Clock over USB and run:

        python3 tools/gift_reset.py

    Confirm with y. Wait for "Gift reset: PASS".

This invokes the firmware's authoritative `!gift-reset` command, which clears
only user/test state:

  * all local messages (read + unread)
  * unread badge / popup / transient message state
  * Ringing, Snoozed, Zzz, and any active test-alarm state
  * the persisted handled_occurrence (so the next real alarm is not suppressed)

Everything production is untouched: Wi-Fi credentials, ntfy configuration,
alarm settings, volume, brightness, photos, RAW emoji assets, SD production
assets, RTC/C3 integration, touch/display configuration.

The ntfy stream checkpoint (processed.json) is preserved so old retained
messages are not replayed after the reset.

This script NEVER erases flash, formats the SD card, deletes photos or RAW
emoji assets, or modifies the C3 firmware.
"""

import argparse
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
DEFAULT_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD = 115200
STARTUP_SETTLE_SECONDS = 12.0
QUIET_GAP_SECONDS = 0.4
QUERY_TIMEOUT_SECONDS = 10.0
OBSOLETE_EMOJI_DIRS = ["/emoji/16", "/emoji/32", "/emoji/48"]

BANNER = "Mateja Clock Gift Reset"


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


def read_until_quiet(ser):
    buffer = b""
    last_activity = time.monotonic()
    deadline = time.monotonic() + QUERY_TIMEOUT_SECONDS
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


def send_command(ser, command):
    ser.write((command + "\r\n").encode("ascii"))
    ser.flush()
    return read_until_quiet(ser)


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
        match = re.search(r"count=(\d+) unread=(\d+)", line)
        if not match:
            return None
        return {"count": int(match.group(1)), "unread": int(match.group(2))}
    return retry_parse(ser, "!msg", extract)


def parse_alarm_status(ser):
    def extract(text):
        line = last_status_line(text, "[ALARM_STATUS]")
        if not line:
            return None
        match = re.search(r"state=(\w+).*?origin=(\w+).*?" 
                          r"last_handled_occurrence=(\S+)", line)
        if not match:
            return None
        return {
            "state": match.group(1),
            "origin": match.group(2),
            "handled": match.group(3),
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


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="tools/gift_reset.py",
        description=(
            "Reset user/test state on the Mateja Clock (messages, alarm "
            "runtime history). Preserves photos, Wi-Fi/ntfy config, alarm "
            "settings and RAW emoji assets."
        ),
        epilog=(
            "Future use: connect the clock over USB and run "
            "'python3 tools/gift_reset.py', confirm with y, wait for "
            "'Gift reset: PASS'."
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
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    port = args.port if args.port else DEFAULT_PORT
    if not os.path.exists(port):
        print("ERROR: serial device not found: %s" % port)
        if not args.port:
            print("       Use --port /dev/ttyUSB0 to select the correct "
                  "device.")
        return 1

    if not args.yes:
        print(BANNER)
        print("-" * len(BANNER))
        print("This will permanently delete all local messages and reset "
              "alarm runtime history.")
        if args.cleanup_obsolete:
            print("It will also delete obsolete emoji PNG files from the SD "
                  "card.")
        print("Photos, Wi-Fi, ntfy, alarm settings and emoji RAW assets will "
              "be preserved.")
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

        print("Resetting user state...")
        before = {"msg": parse_msg_status(ser), "alarm": parse_alarm_status(ser)}
        reset_line = run_gift_reset(ser)
        after = {"msg": parse_msg_status(ser), "alarm": parse_alarm_status(ser)}
        c3 = parse_c3_status(ser)
        rtc = parse_rtc_status(ser)

        cleanup_result = None
        if args.cleanup_obsolete:
            cleanup_result = cleanup_obsolete_emoji(ser)

        problems = validate(before, after, reset_line)
        print_report(before, after, reset_line, c3, rtc, cleanup_result)

        print()
        if problems:
            print("Gift reset: FAIL")
            for problem in problems:
                print("  - %s" % problem)
            return 1
        print("Gift reset: PASS")
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))