#!/usr/bin/env python3
"""Upload emoji PNG assets to the CYD SD card over serial.

Usage:
    .venv/bin/python tools/upload_emoji.py
"""

import os
import sys
import time

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PROJECT_ROOT)

import serial  # pyserial

EMOJI_DIR = "/tmp/sd_card/emoji"
PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD = 115200


def send_command(port, cmd, timeout=5):
    port.write((cmd + "\r\n").encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    resp = b""
    while time.monotonic() < deadline:
        resp += port.read(port.in_waiting or 1)
        if b"\r\n" in resp:
            break
    return resp.decode("ascii", errors="replace")


def upload_file(port, local_path, remote_path):
    with open(local_path, "rb") as f:
        data = f.read()
    size = len(data)
    print(f"Uploading {size}B -> {remote_path}")

    # Create parent directory
    parent = os.path.dirname(remote_path)
    if parent:
        resp = send_command(port, f"!mkdir {parent}")
        print(f"  mkdir: {resp.strip()}")

    # Send putfile command
    port.write(f"!putfile {remote_path} {size}\r\n".encode("ascii"))
    port.flush()

    # Wait for FILEREADY
    deadline = time.monotonic() + 10
    resp = b""
    while time.monotonic() < deadline:
        resp += port.read(port.in_waiting or 1)
        if b"FILEREADY" in resp:
            break
    print(f"  server ready")

    # Send data
    CHUNK = 4096
    for offset in range(0, size, CHUNK):
        chunk = data[offset : offset + CHUNK]
        port.write(chunk)
        port.flush()
        time.sleep(0.01 * (len(chunk) / 1024))

    # Receive response
    deadline = time.monotonic() + 15
    resp = b""
    while time.monotonic() < deadline:
        resp += port.read(port.in_waiting or 1)
        if b"[FILE]" in resp or b"\r\n" in resp:
            break
    print(f"  result: {resp.decode('ascii', errors='replace').strip()}")


def main():
    if not os.path.exists(EMOJI_DIR):
        sys.exit(f"Emoji directory not found: {EMOJI_DIR}")

    ser = serial.Serial(PORT, BAUD, timeout=0.5, dsrdtr=False, rtscts=False)
    ser.dtr = False
    ser.rts = False
    time.sleep(2)
    ser.reset_input_buffer()

    # Clear any pending
    send_command(ser, "")
    time.sleep(0.5)

    for size_dir in ["32", "48"]:
        local_dir = os.path.join(EMOJI_DIR, size_dir)
        if not os.path.isdir(local_dir):
            continue
        for fname in sorted(os.listdir(local_dir)):
            if not fname.endswith(".png"):
                continue
            local_path = os.path.join(local_dir, fname)
            remote_path = f"/emoji/{size_dir}/{fname}"
            upload_file(ser, local_path, remote_path)
            time.sleep(0.3)

    ser.close()
    print("\nDone. Emoji files uploaded.")


if __name__ == "__main__":
    main()