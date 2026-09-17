#!/usr/bin/env python3
"""Upload Wi-Fi credentials to the Mateja Clock SD card.

Reads the gitignored local header include/wifi_credentials.h and writes
/clock/config/wifi.json on the clock with the firmware `!putfile` protocol.
The SSID is printed; the Wi-Fi password is never printed and the generated
wifi.json is never committed to the repository.

    python3 tools/upload_wifi_config.py

The clock reads the config at boot, so power-cycle it afterwards.
"""

import argparse
import os
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.gift_reset import (  # noqa: E402
    DEFAULT_PORT,
    STARTUP_SETTLE_SECONDS,
    WIFI_CONFIG_PATH,
    WIFI_HEADER_PATH,
    build_wifi_payload,
    list_dir_entries,
    open_serial,
    read_wifi_header,
    upload_putfile,
)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Upload /clock/config/wifi.json to the Mateja Clock.")
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="CYD serial port (default: %s)" % DEFAULT_PORT)
    parser.add_argument("--header", default=WIFI_HEADER_PATH,
                        help="credential header to read (default: %s)"
                             % WIFI_HEADER_PATH)
    parser.add_argument("--ssid", default=None,
                        help="override SSID instead of reading the header")
    parser.add_argument("--password", default=None,
                        help="override password instead of reading the header")
    args = parser.parse_args(argv)

    if args.ssid is not None or args.password is not None:
        ssid = args.ssid if args.ssid is not None else ""
        password = args.password if args.password is not None else ""
        if not ssid:
            sys.exit("ERROR: --ssid must not be empty when overriding")
    else:
        try:
            ssid, password = read_wifi_header(args.header)
        except (OSError, ValueError) as error:
            sys.exit("ERROR: %s" % error)

    payload = build_wifi_payload(ssid, password)

    ser = open_serial(args.port)
    try:
        time.sleep(STARTUP_SETTLE_SECONDS)
        ser.reset_input_buffer()
        ok, detail = upload_putfile(ser, payload, WIFI_CONFIG_PATH)
        if not ok:
            sys.exit("ERROR: wifi.json upload failed: %s" % detail)
        _, files = list_dir_entries(ser, "/clock/config")
        if files.get(WIFI_CONFIG_PATH) != len(payload):
            sys.exit("ERROR: wifi.json size mismatch on SD (%s != %d)" %
                     (files.get(WIFI_CONFIG_PATH), len(payload)))
    finally:
        ser.close()

    print("Wi-Fi config uploaded for SSID: %s" % ssid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
