from __future__ import annotations

import pytest

from tools.deploy_alarm_wav_serial import (
    crc16_xmodem,
    make_handshake,
    parse_result_line,
)
from tools.generate_alarm_wav import synthesize, validate_wav


def test_crc16_xmodem_known_vector() -> None:
    assert crc16_xmodem(b"") == 0x0000
    assert crc16_xmodem(b"123456789") == 0x31C3


def test_crc16_xmodem_incremental_matches_streaming() -> None:
    data = b"the quick brown fox jumps over the lazy dog" * 100
    assert crc16_xmodem(data) == crc16_xmodem(data[50:], crc16_xmodem(data[:50]))


def test_make_handshake_valid() -> None:
    assert make_handshake(100) == b"!wavraw 100\r\n"
    assert make_handshake(8 * 1024 * 1024) == b"!wavraw 8388608\r\n"


def test_make_handshake_rejects_out_of_range() -> None:
    with pytest.raises(ValueError):
        make_handshake(99)
    with pytest.raises(ValueError):
        make_handshake(0)
    with pytest.raises(ValueError):
        make_handshake(8 * 1024 * 1024 + 1)


def test_parse_result_line_done() -> None:
    kind, detail = parse_result_line("[WAV_UPLOAD] done path=/clock/audio/alarm.wav")
    assert kind == "done"
    assert detail == "/clock/audio/alarm.wav"


def test_parse_result_line_fail() -> None:
    kind, detail = parse_result_line("[WAV_UPLOAD] fail reason=crc path=/clock/audio/alarm.wav")
    assert kind == "fail"
    assert detail == "crc"


def test_parse_result_line_unknown() -> None:
    kind, detail = parse_result_line("[ALARM] something")
    assert kind == "unknown"


def test_synthesized_wav_is_deployable(tmp_path) -> None:
    wav = tmp_path / "alarm.wav"
    synthesize(wav, duration_seconds=1.0)
    validate_wav(wav)
    data = wav.read_bytes()
    assert len(data) > 0