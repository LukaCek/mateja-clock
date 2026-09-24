#!/usr/bin/env python3
"""Migrate the local ntfy credential header to the clock's SD card.

Reads the gitignored include/ntfy_credentials.h and uploads
/clock/config/ntfy.json through the firmware !putfile protocol. The access
token is never printed and no local JSON file is created.
"""

import argparse
import ast
import json
import os
import re
import sys
import time
import urllib.parse

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from tools.gift_reset import (  # noqa: E402
    DEFAULT_PORT,
    STARTUP_SETTLE_SECONDS,
    list_dir_entries,
    open_serial,
    upload_putfile,
)

NTFY_CONFIG_PATH = "/clock/config/ntfy.json"
NTFY_HEADER_PATH = os.path.join(PROJECT_ROOT, "include",
                                "ntfy_credentials.h")


def _logical_lines(text):
    lines = []
    pending = ""
    for line in text.splitlines():
        pending += line
        if pending.rstrip().endswith("\\"):
            pending = pending.rstrip()[:-1] + " "
        else:
            lines.append(pending)
            pending = ""
    if pending:
        lines.append(pending)
    return lines


def _decode_c_strings(expression, name):
    expression = expression.strip()
    raw = re.fullmatch(r'R"([^ ()\\\t]{0,16})\((.*)\)\1"', expression,
                       flags=re.DOTALL)
    if raw is not None:
        return raw.group(2)

    tokens = re.findall(r'"(?:[^"\\]|\\.)*"', expression)
    remainder = re.sub(r'"(?:[^"\\]|\\.)*"', "", expression)
    remainder = re.sub(r'/\*.*?\*/|//.*', "", remainder,
                       flags=re.DOTALL).strip()
    if not tokens or remainder:
        raise ValueError("%s must be a C string literal" % name)
    try:
        return "".join(ast.literal_eval(token) for token in tokens)
    except (SyntaxError, ValueError) as error:
        raise ValueError("invalid C string literal for %s" % name) from error


def read_ntfy_header(path):
    with open(path, "r", encoding="utf-8") as handle:
        logical_lines = _logical_lines(handle.read())

    definitions = {}
    for line in logical_lines:
        match = re.match(r'^\s*#\s*define\s+(MATEJA_NTFY_[A-Z_]+)\s+(.*)$',
                         line, flags=re.DOTALL)
        if match is not None:
            definitions[match.group(1)] = match.group(2)

    def value(name, required=True):
        expression = definitions.get(name)
        if expression is None:
            if required:
                raise ValueError("%s missing in %s" % (name, path))
            return ""
        return _decode_c_strings(expression, name)

    config = {
        "base_url": value("MATEJA_NTFY_BASE_URL"),
        "inbox_topic": value("MATEJA_NTFY_INBOX_TOPIC"),
        "ack_topic": value("MATEJA_NTFY_ACK_TOPIC"),
        "access_token": value("MATEJA_NTFY_ACCESS_TOKEN"),
    }
    ca_cert = value("MATEJA_NTFY_CA_CERT", required=False)
    if ca_cert:
        config["ca_cert"] = ca_cert
    return config


def validate_config(config):
    base_url = config["base_url"]
    try:
        parsed = urllib.parse.urlsplit(base_url)
        port = parsed.port
    except ValueError as error:
        raise ValueError("MATEJA_NTFY_BASE_URL is invalid") from error
    if (parsed.scheme != "https" or not parsed.hostname or
            parsed.username is not None or parsed.password is not None or
            parsed.path or parsed.query or parsed.fragment or
            parsed.netloc.startswith("[") or parsed.netloc.endswith(":") or
            port == 0):
        raise ValueError("MATEJA_NTFY_BASE_URL must be an HTTPS authority "
                         "without a path")
    if not config["inbox_topic"]:
        raise ValueError("MATEJA_NTFY_INBOX_TOPIC must not be empty")
    if len(base_url.encode("utf-8")) >= 128:
        raise ValueError("MATEJA_NTFY_BASE_URL is too long")
    for key, macro in (("inbox_topic", "MATEJA_NTFY_INBOX_TOPIC"),
                       ("ack_topic", "MATEJA_NTFY_ACK_TOPIC")):
        if len(config[key].encode("utf-8")) >= 65:
            raise ValueError("%s is too long" % macro)
        if any(ord(char) <= 0x20 or char in "/?#\x7f"
               for char in config[key]):
            raise ValueError("%s contains an unsafe character" % macro)
    if len(config["access_token"].encode("utf-8")) >= 129:
        raise ValueError("MATEJA_NTFY_ACCESS_TOKEN is too long")
    if any(ord(char) < 0x20 or ord(char) == 0x7f
           for char in config["access_token"]):
        raise ValueError("MATEJA_NTFY_ACCESS_TOKEN contains a control character")
    if len(config.get("ca_cert", "").encode("utf-8")) >= 2049:
        raise ValueError("MATEJA_NTFY_CA_CERT is too long")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Upload /clock/config/ntfy.json to the Mateja Clock.")
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="CYD serial port (default: %s)" % DEFAULT_PORT)
    parser.add_argument("--header", default=NTFY_HEADER_PATH,
                        help="credential header to migrate (default: %s)" %
                             NTFY_HEADER_PATH)
    args = parser.parse_args(argv)

    try:
        config = read_ntfy_header(args.header)
        validate_config(config)
    except (OSError, ValueError) as error:
        sys.exit("ERROR: %s" % error)

    payload = json.dumps(config, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    if len(payload) > 4096:
        sys.exit("ERROR: generated ntfy.json exceeds 4096 bytes")

    ser = open_serial(args.port)
    try:
        time.sleep(STARTUP_SETTLE_SECONDS)
        ser.reset_input_buffer()
        ok, detail = upload_putfile(ser, payload, NTFY_CONFIG_PATH)
        if not ok:
            sys.exit("ERROR: ntfy.json upload failed: %s" % detail)
        _, files = list_dir_entries(ser, "/clock/config")
        if files.get(NTFY_CONFIG_PATH) != len(payload):
            sys.exit("ERROR: ntfy.json size mismatch on SD (%s != %d)" %
                     (files.get(NTFY_CONFIG_PATH), len(payload)))
    finally:
        ser.close()

    effective_ack = config["ack_topic"] or config["inbox_topic"]
    print("ntfy config uploaded")
    print("  base URL: %s" % config["base_url"])
    print("  inbox topic: %s" % config["inbox_topic"])
    print("  ack topic: %s" % effective_ack)
    return 0


if __name__ == "__main__":
    sys.exit(main())
