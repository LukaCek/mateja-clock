#!/usr/bin/env python3
"""Stream a generated alarm WAV into the Mateja Clock over USB serial.

The firmware WavUploader receiver exposes a raw-data protocol:

    !wavraw <byteCount>
            device answers "WAVREADY <byteCount>" at the control baud,
            then streams byteCount payload bytes followed by a
            CRC16-XModem trailer (big-endian). It answers
            "[WAV_UPLOAD] done ..." or "[WAV_UPLOAD] fail reason=..." at
            the control baud when finished. The payload is shipped at the
            control baud (115200), gently paced so the target keeps up.

Run with the project venv (pyserial required):

    .venv/bin/python tools/deploy_alarm_wav_serial.py --wav alarm_output/alarm.wav
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import Optional

_PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

import serial  # pyserial

from tools.generate_alarm_wav import validate_wav

CONTROL_BAUD = 115200
TRANSFER_BAUD = 115200
DEFAULT_PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
CHUNK_SIZE = 2048
TARGET_BYTES_PER_SECOND = 40000


def crc16_xmodem(data: bytes, crc: int = 0) -> int:
    """CRC-16/XMODEM (poly 0x1021, init 0, MSB first), matching the firmware."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def make_handshake(byte_count: int) -> bytes:
    if byte_count < 100 or byte_count > 8 * 1024 * 1024:
        raise ValueError(f"byte count {byte_count} out of supported range")
    return f"!wavraw {byte_count}\r\n".encode("ascii")


def read_until_marker(port: serial.Serial, marker: str, timeout_s: float,
                      prefixes: tuple[str, ...] = ()) -> str:
    """Read buffered lines until one contains marker; return that line.

    Stops early if a line starts with any prefix from `prefixes`. Raises
    TimeoutError if `timeout_s` elapses before a match.
    """
    deadline = time.monotonic() + timeout_s
    buffer = b""
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"no '{marker}' line within {timeout_s}s")
        port.timeout = min(0.4, remaining)
        chunk = port.read(max(1, min(4096, port.in_waiting or 4096)))
        if not chunk:
            continue
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            text = line.decode("utf-8", "replace")
            if marker in text:
                return text
            if any(text.startswith(prefix) for prefix in prefixes):
                return text


def parse_result_line(line: str) -> tuple[str, str]:
    """Return ('done'|'fail', detail) from a device result line."""
    if "[WAV_UPLOAD] done" in line:
        detail = line.split("path=", 1)[1].strip() if "path=" in line else ""
        return "done", detail
    if "[WAV_UPLOAD] fail" in line:
        reason = line.split("reason=", 1)[1].split()[0] if "reason=" in line else "unknown"
        return "fail", reason
    return "unknown", line.strip()


def open_port(path: str, baud: int) -> serial.Serial:
    port = serial.Serial()
    port.port = path
    port.baudrate = baud
    port.timeout = 0.4
    port.write_timeout = 5
    port.rtscts = False
    port.dsrdtr = False
    port.rts = False
    port.dtr = False
    port.open()
    return port


def stream_wav(port: serial.Serial, data: bytes, bridge_delay_s: float) -> None:
    """Ship payload + CRC16 trailer at the control baud, gently paced."""
    port.flushInput()
    time.sleep(bridge_delay_s)
    payload = data + crc16_xmodem(data).to_bytes(2, "big")
    port.reset_input_buffer()
    chunk_time_s = CHUNK_SIZE / TARGET_BYTES_PER_SECOND
    for start in range(0, len(payload), CHUNK_SIZE):
        before = time.monotonic()
        port.write(payload[start:start + CHUNK_SIZE])
        port.flush()
        elapsed = time.monotonic() - before
        remainder = chunk_time_s - elapsed
        if remainder > 0:
            time.sleep(remainder)
    port.flush()
    time.sleep(bridge_delay_s)


def run_upload(path: str, data: bytes, *, attempts: int,
               progress_target: bool = True) -> bool:
    command = make_handshake(len(data))
    port = open_port(path, CONTROL_BAUD)
    try:
        for attempt in range(1, attempts + 1):
            if progress_target:
                print(f"[try {attempt}/{attempts}] sending handshake, "
                      f"{len(data)} bytes", flush=True)
            port.reset_input_buffer()
            port.write(command)
            port.flush()
            try:
                line = read_until_marker(port, "WAVREADY", timeout_s=5.0,
                                         prefixes=("[WAV_UPLOAD] fail",))
            except TimeoutError as error:
                print(f"[try {attempt}/{attempts}] handshake timeout: {error}")
                continue
            if "WAV_UPLOAD] fail" in line:
                print(f"[try {attempt}/{attempts}] device refused: {line.strip()}")
                continue

            ready_count = int(line.split("WAVREADY", 1)[1].split()[0])
            if ready_count != len(data):
                print(f"[try {attempt}/{attempts}] device expects {ready_count} "
                      f"bytes, have {len(data)}")
                continue

            started = time.monotonic()
            stream_wav(port, data, bridge_delay_s=0.3)

            elapsed = time.monotonic() - started
            if progress_target:
                print(f"[try {attempt}/{attempts}] shipped in {elapsed:.1f}s",
                      flush=True)
            try:
                line = read_until_marker(port, "[WAV_UPLOAD] done", timeout_s=15.0,
                                         prefixes=("[WAV_UPLOAD] fail",))
            except TimeoutError:
                print(f"[try {attempt}/{attempts}] no result line within 15s")
                continue
            kind, detail = parse_result_line(line)
            if kind == "done":
                print(f"[done] alarm WAV deployed to {detail} "
                      f"({len(data)} bytes, {elapsed:.1f}s)")
                return True
            print(f"[try {attempt}/{attempts}] device reported fail "
                  f"reason={detail}")
        print("[failed] all attempts exhausted")
        return False
    finally:
        port.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Deploy the alarm WAV to the clock SD card over USB serial."
    )
    parser.add_argument(
        "--wav", type=Path, required=True, help="WAV to deploy (22050 Hz, 16-bit, mono)."
    )
    parser.add_argument(
        "--port", type=str, default=DEFAULT_PORT,
        help="Serial device (default: the CH340 dongle the clock uses).",
    )
    parser.add_argument(
        "--attempts", type=int, default=3,
        help="Retry attempts on a failed transfer (default: 3).",
    )
    args = parser.parse_args()

    if not args.wav.is_file():
        print(f"error: {args.wav} does not exist (run tools/generate_alarm_wav.py)")
        return 2
    try:
        validate_wav(args.wav)
    except Exception as error:  # noqa: BLE001 - surface any format issue
        print(f"error: {args.wav} is not a valid alarm WAV: {error}")
        return 2

    payload = args.wav.read_bytes()
    ok = run_upload(args.port, payload, attempts=max(1, args.attempts))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())