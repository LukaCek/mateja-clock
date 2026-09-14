#!/usr/bin/env python3
"""Synthesize a gentle nature/birdsong alarm WAV for the Mateja Clock.

The alarm firmware plays /clock/audio/alarm.wav through the built-in I2S DAC.
It requires 22050 Hz, 16-bit, mono PCM with an even number of data bytes.
This tool produces exactly that format, seeded for deterministic output.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import wave
from pathlib import Path

SAMPLE_RATE = 22050
CHANNELS = 1
SAMPLE_WIDTH = 2
BYTE_RATE = SAMPLE_RATE * CHANNELS * SAMPLE_WIDTH
BLOCK_ALIGN = CHANNELS * SAMPLE_WIDTH
DEFAULT_DURATION_SECONDS = 20.0
DEFAULT_SEED = 7


def _fade_sample(sample: float) -> int:
    """Clamp a normalized sample to signed 16-bit and clip softly."""
    clipped = max(-1.0, min(1.0, sample))
    return int(clipped * 32767)


def _sine_wave(phase: float) -> float:
    return math.sin(2.0 * math.pi * phase)


def _ambient_bed(samples: int, rng: random.Random,
                 duration_seconds: float) -> list[float]:
    """A soft loopable breezy-forest bed.

    Slow low-frequency tones (integer cycles over the duration) with gentle
    random-walk amplitude modulation plus a low-level filtered-noise rustle.
    """
    bed = [0.0] * samples

    drift = 0.0
    drift_target = 0.0
    drift_phase = 1.0
    noise_state = 0.0

    mod_frequencies = (1.0, 2.0, 3.0)
    mod_phases = [rng.uniform(0.0, 1.0) for _ in mod_frequencies]
    mod_weights = (0.5, 0.3, 0.2)
    drone_frequencies = (110.0, 165.0, 220.0)

    for index in range(samples):
        phase = index / SAMPLE_RATE

        # One-pole filtered noise for a wind/leaf rustle.
        noise_state = noise_state * 0.985 + rng.uniform(-1.0, 1.0) * 0.015

        # Slow random-walk amplitude shaping.
        if drift_phase <= 0.0:
            drift_target = rng.uniform(0.75, 1.0)
            drift_phase = rng.uniform(2.0, 6.0)
        drift_phase -= 1.0 / SAMPLE_RATE
        drift += (drift_target - drift) * 0.002

        mod = 0.0
        for value, weight, mod_phase in zip(mod_phases, mod_weights, mod_frequencies):
            mod += (0.5 + 0.5 * _sine_wave(mod_phase * duration_seconds / 4.0 +
                                           value)) * weight
        drone = sum(0.18 * _sine_wave((freq * phase) + 0.1) for freq in drone_frequencies)

        bed[index] = 0.09 * mod * drift * drone
        bed[index] += noise_state * 0.06 * drift

    return bed


def _birdsong_events(samples: int, rng: random.Random,
                     duration_seconds: float) -> list[float]:
    """Return a list of bird chirp windows as (sample_start, buffer)."""
    events: list[tuple[int, list[float]]] = []
    time = rng.uniform(0.5, 3.0)
    while time < duration_seconds - 1.5:
        if rng.random() < 0.75:
            syllable_count = 1 + rng.randint(0, 2)
            for syllable in range(syllable_count):
                events.append(_bird_syllable(int(time * SAMPLE_RATE), rng))
                time += rng.uniform(0.12, 0.35)
        time += rng.uniform(1.2, 3.2)
    return [item for item in events if item[0] + len(item[1]) < samples]


def _bird_syllable(offset: int, rng: random.Random) -> tuple[int, list[float]]:
    """A single bird call syllable: an FM sweep with harmonic timbre."""
    duration = rng.uniform(0.045, 0.095)
    length = int(duration * SAMPLE_RATE)
    start_hz = rng.uniform(1900.0, 3600.0)
    end_hz = start_hz * rng.uniform(1.25, 2.2)
    bend_amount = rng.uniform(0.7, 1.6)
    base_amplitude = rng.uniform(0.20, 0.30)
    harmonic = rng.choice((2.0, 3.0))
    buffer = [0.0] * length
    phase = rng.uniform(0.0, 1.0)

    for index in range(length):
        progress = index / length
        frequency = start_hz * math.pow(end_hz / start_hz,
                                        math.pow(progress, 1.0 / bend_amount))
        phase += frequency / SAMPLE_RATE
        attack = min(1.0, progress / 0.08)
        decay = math.exp(-progress * 5.0)
        envelope = attack * decay
        fundamental = _sine_wave(phase)
        overtone = _sine_wave(phase * harmonic)
        buffer[index] = (
            base_amplitude
            * (0.85 * fundamental + 0.25 * overtone)
            * envelope
        )
    return offset, buffer


def _crossfade(buffer: list[float], window: int) -> None:
    """Blend the end into the beginning to make the loop click-free."""
    window = min(window, len(buffer) // 4)
    for index in range(window):
        weight = index / window
        end_index = len(buffer) - window + index
        start_value = buffer[index]
        end_value = buffer[end_index]
        buffer[index] = start_value * weight + end_value * (1.0 - weight)


def synthesize(output_path: Path, duration_seconds: float = DEFAULT_DURATION_SECONDS,
               seed: int = DEFAULT_SEED) -> float:
    """Generate the alarm WAV and return the peak sample magnitude."""
    total_samples = int(duration_seconds * SAMPLE_RATE)
    rng = random.Random(seed)

    buffer = _ambient_bed(total_samples, rng, duration_seconds)
    for offset, chirp in _birdsong_events(total_samples, rng, duration_seconds):
        for index, value in enumerate(chirp):
            pos = offset + index
            if 0 <= pos < total_samples:
                buffer[pos] += value

    _crossfade(buffer, int(0.05 * SAMPLE_RATE))

    peak = 0.0
    with wave.open(str(output_path), "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH)
        wav.setframerate(SAMPLE_RATE)
        frame_count = 0
        chunk_size = 8192
        for start in range(0, total_samples, chunk_size):
            frames = bytearray()
            for sample in buffer[start:start + chunk_size]:
                instance = _fade_sample(sample)
                peak = max(peak, abs(instance) / 32767.0)
                frames += struct.pack("<h", instance)
            wav.writeframes(bytes(frames))
            frame_count += len(frames) // (CHANNELS * SAMPLE_WIDTH)
    return peak


def validate_wav(path: Path) -> None:
    """Verify the generated file matches the firmware's strict expectations."""
    with wave.open(str(path), "rb") as source:
        assert source.getnchannels() == CHANNELS
        assert source.getsampwidth() == SAMPLE_WIDTH
        assert source.getframerate() == SAMPLE_RATE
        assert source.getnframes() > 0
    data = path.read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    assert data[20:22] == struct.pack("<h", 1)  # PCM
    assert data[22:24] == struct.pack("<h", CHANNELS)
    assert data[24:28] == struct.pack("<i", SAMPLE_RATE)
    assert data[28:32] == struct.pack("<i", BYTE_RATE)
    assert data[32:34] == struct.pack("<h", BLOCK_ALIGN)
    assert data[34:36] == struct.pack("<h", 16)  # bits per sample
    data_size = int(struct.unpack("<i", data[40:44])[0])
    assert data_size > 0 and data_size % 2 == 0


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Synthesize the nature/birdsong alarm WAV for the Mateja Clock."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("alarm_output/alarm.wav"),
        help="Destination WAV path (default: alarm_output/alarm.wav).",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=DEFAULT_DURATION_SECONDS,
        help=f"WAV duration in seconds (default: {DEFAULT_DURATION_SECONDS}).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=DEFAULT_SEED,
        help=f"Random seed for deterministic output (default: {DEFAULT_SEED}).",
    )
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    peak = synthesize(args.output, args.duration, args.seed)
    validate_wav(args.output)
    print(f"wrote {args.output} peak={peak:.3f}")


if __name__ == "__main__":
    main()