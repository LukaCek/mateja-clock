#!/usr/bin/env python3
"""Upload the public GitHub repository identifier used by OTA."""

import argparse
import json
import os
import re
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.gift_reset import DEFAULT_PORT, STARTUP_SETTLE_SECONDS, open_serial, upload_putfile

PATH = "/clock/config/ota.json"


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("repository", help="GitHub OWNER/REPO")
    parser.add_argument("--port", default=DEFAULT_PORT)
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        parser.error("repository must be OWNER/REPO")
    payload = json.dumps({"repository": args.repository}, separators=(",", ":")).encode()
    ser = open_serial(args.port)
    try:
        time.sleep(STARTUP_SETTLE_SECONDS)
        ser.reset_input_buffer()
        ok, detail = upload_putfile(ser, payload, PATH)
        if not ok:
            raise SystemExit("ERROR: %s" % detail)
    finally:
        ser.close()
    print("OTA repository configured: %s" % args.repository)
    return 0


if __name__ == "__main__":
    sys.exit(main())
