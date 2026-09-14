from __future__ import annotations

import hashlib
import struct
import wave
from pathlib import Path

from tools.generate_alarm_wav import (
    BYTE_RATE,
    DEFAULT_SEED,
    SAMPLE_RATE,
    validate_wav,
)
from tools.generate_alarm_wav import synthesize


def _hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_synthesizes_valid_deterministic_wav(tmp_path: Path) -> None:
    output = tmp_path / "alarm.wav"
    peak = synthesize(output, duration_seconds=3.0, seed=DEFAULT_SEED)

    validate_wav(output)

    with wave.open(str(output), "rb") as wav:
        assert wav.getnchannels() == 1
        assert wav.getsampwidth() == 2
        assert wav.getframerate() == SAMPLE_RATE
        assert wav.getnframes() == 3 * SAMPLE_RATE

    assert peak > 0.0
    assert peak < 1.0
    assert _hash(output) == _hash(output)

    second = tmp_path / "identical.wav"
    synthesize(second, duration_seconds=3.0, seed=DEFAULT_SEED)
    assert _hash(output) == _hash(second)

    data = output.read_bytes()[44:]
    assert data != b"\x00" * len(data)
    samples = wave.open(str(output), "rb")
    frames = samples.readframes(samples.getnframes())
    samples.close()
    all_samples = struct.unpack(f"<{len(frames)//2}h", frames)
    assert any(abs(s) > 0 for s in all_samples[:100])
    assert BYTE_RATE == SAMPLE_RATE * 2


def test_different_seeds_change_output(tmp_path: Path) -> None:
    first = tmp_path / "first.wav"
    second = tmp_path / "second.wav"
    synthesize(first, duration_seconds=3.0, seed=1)
    synthesize(second, duration_seconds=3.0, seed=2)
    assert _hash(first) != _hash(second)