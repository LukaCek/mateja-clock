#!/usr/bin/env python3
"""Safely deploy prepared photos to a mounted Mateja Clock SD card."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import uuid
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


VOLUME_LABEL = "MATEJA_CLK"
MANIFEST_VERSION = 1
PHOTO_NAME_RE = re.compile(r"photo_\d{4}\.jpg")
ALARM_WAV_TARGET = Path("clock/audio/alarm.wav")


@dataclass(frozen=True)
class MountedVolume:
    mount: Path
    device: str
    label: str
    removable: bool
    filesystem: str


def _bool_value(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    return str(value).lower() in {"1", "true", "yes"}


def _mounted_volumes(devices: list[dict], inherited_removable: bool = False) -> list[MountedVolume]:
    volumes: list[MountedVolume] = []
    for device in devices:
        removable = inherited_removable or _bool_value(device.get("rm", False))
        mountpoints = device.get("mountpoints")
        if mountpoints is None:
            mountpoints = [device.get("mountpoint")]
        elif isinstance(mountpoints, str):
            mountpoints = [mountpoints]
        for mountpoint in mountpoints:
            if mountpoint:
                volumes.append(
                    MountedVolume(
                        mount=Path(mountpoint).resolve(),
                        device=str(device.get("path") or device.get("name") or "unknown"),
                        label=str(device.get("label") or ""),
                        removable=removable,
                        filesystem=str(device.get("fstype") or ""),
                    )
                )
        volumes.extend(_mounted_volumes(device.get("children", []), removable))
    return volumes


def read_block_devices() -> list[dict]:
    result = subprocess.run(
        [
            "lsblk",
            "--json",
            "--paths",
            "--output",
            "NAME,PATH,LABEL,MOUNTPOINTS,RM,TYPE,FSTYPE",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)["blockdevices"]


def identify_mount(explicit_mount: Path | None, devices: list[dict] | None = None) -> MountedVolume:
    if explicit_mount is not None:
        explicit_mount = explicit_mount.expanduser().resolve()
        if not explicit_mount.is_dir():
            raise ValueError(f"Mount directory does not exist: {explicit_mount}")

    volumes = _mounted_volumes(read_block_devices() if devices is None else devices)
    if explicit_mount is None:
        candidates = [volume for volume in volumes if volume.label == VOLUME_LABEL]
    else:
        candidates = [volume for volume in volumes if volume.mount == explicit_mount]

    if not candidates:
        if explicit_mount is None:
            raise ValueError(f"No mounted volume labeled exactly {VOLUME_LABEL} was found")
        raise ValueError(f"{explicit_mount} is not a mounted block-device filesystem")
    if len(candidates) != 1:
        raise ValueError("More than one matching mounted volume was found; use an unambiguous mount")

    volume = candidates[0]
    if not volume.mount.is_dir():
        raise ValueError(f"Mounted volume path does not exist: {volume.mount}")
    if volume.label != VOLUME_LABEL:
        raise ValueError(
            f"Volume label must be exactly {VOLUME_LABEL}, found {volume.label or '<none>'}"
        )
    if not volume.removable:
        raise ValueError(f"Refusing non-removable device {volume.device}")
    if volume.filesystem != "vfat":
        raise ValueError(
            f"Volume filesystem must be FAT, found {volume.filesystem or '<unknown>'}"
        )
    return volume


def _load_manifest(source_dir: Path) -> tuple[dict, list[str]]:
    manifest_path = source_dir / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read prepared manifest: {error}") from error

    if manifest.get("version") != MANIFEST_VERSION or not isinstance(manifest.get("photos"), list):
        raise ValueError("Unsupported or malformed prepared manifest")
    count = manifest.get("count")
    if isinstance(count, bool) or not isinstance(count, int) or count != len(manifest["photos"]):
        raise ValueError("Manifest count does not match its photo list")

    filenames: list[str] = []
    ids: set[int] = set()
    for expected_id, record in enumerate(manifest["photos"], start=1):
        if not isinstance(record, dict):
            raise ValueError("Manifest photo entries must be objects")
        photo_id = record.get("id")
        filename = record.get("filename")
        if (
            isinstance(photo_id, bool)
            or not isinstance(photo_id, int)
            or photo_id < 1
            or not isinstance(filename, str)
            or not PHOTO_NAME_RE.fullmatch(filename)
        ):
            raise ValueError("Manifest contains an invalid photo id or filename")
        if photo_id in ids or filename in filenames:
            raise ValueError("Manifest contains duplicate photo ids or filenames")
        if photo_id != expected_id or filename != f"photo_{expected_id:04d}.jpg":
            raise ValueError("Manifest photo ids and filenames must be sequential")
        photo_path = source_dir / filename
        if not photo_path.is_file():
            raise ValueError(f"Manifest-referenced photo is missing: {filename}")
        ids.add(photo_id)
        filenames.append(filename)
    return manifest, filenames


def _remove_path(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _copy_file_synced(source: Path, destination: Path) -> None:
    with source.open("rb") as source_file, destination.open("xb") as destination_file:
        shutil.copyfileobj(source_file, destination_file)
        destination_file.flush()
        os.fsync(destination_file.fileno())
    if source.stat().st_size != destination.stat().st_size:
        raise OSError(f"Copied file size mismatch: {source.name}")
    source_hash = hashlib.sha256(source.read_bytes()).digest()
    destination_hash = hashlib.sha256(destination.read_bytes()).digest()
    if source_hash != destination_hash:
        raise OSError(f"Copied file checksum mismatch: {source.name}")


def deploy_photos(
    source_dir: Path,
    explicit_mount: Path | None = None,
    replace: bool = False,
    *,
    devices: list[dict] | None = None,
    sync_fn: Callable[[], None] = os.sync,
) -> tuple[int, MountedVolume]:
    source_dir = source_dir.expanduser().resolve()
    if not source_dir.is_dir():
        raise ValueError(f"Prepared photo directory does not exist: {source_dir}")
    manifest, filenames = _load_manifest(source_dir)
    volume = identify_mount(explicit_mount, devices)

    clock_dir = volume.mount / "clock"
    if source_dir == clock_dir or source_dir.is_relative_to(clock_dir):
        raise ValueError("Prepared source directory must not be inside the target clock directory")
    if clock_dir.exists() and not clock_dir.is_dir():
        raise ValueError(f"Cannot create clock directory over non-directory: {clock_dir}")
    clock_dir.mkdir(exist_ok=True)

    photos_dir = clock_dir / "photos"
    target_manifest = clock_dir / "manifest.json"
    if (photos_dir.exists() or target_manifest.exists()) and not replace:
        raise ValueError("A clock photo dataset already exists; pass --replace to replace it")

    token = uuid.uuid4().hex
    stage_dir = clock_dir / f".mateja_staging_{token}"
    backup_photos = clock_dir / f".mateja_backup_photos_{token}"
    backup_manifest = clock_dir / f".mateja_backup_manifest_{token}"
    stage_photos = stage_dir / "photos"
    stage_dir.mkdir()
    stage_photos.mkdir()

    old_photos_moved = False
    old_manifest_moved = False
    new_photos_moved = False
    new_manifest_moved = False
    try:
        for filename in filenames:
            _copy_file_synced(source_dir / filename, stage_photos / filename)
        _copy_file_synced(source_dir / "manifest.json", stage_dir / "manifest.json")
        _fsync_directory(stage_photos)
        _fsync_directory(stage_dir)

        if photos_dir.exists() or photos_dir.is_symlink():
            os.replace(photos_dir, backup_photos)
            old_photos_moved = True
        if target_manifest.exists() or target_manifest.is_symlink():
            os.replace(target_manifest, backup_manifest)
            old_manifest_moved = True
        os.replace(stage_photos, photos_dir)
        new_photos_moved = True
        os.replace(stage_dir / "manifest.json", target_manifest)
        new_manifest_moved = True
    except Exception:
        if new_manifest_moved:
            _remove_path(target_manifest)
        if new_photos_moved:
            _remove_path(photos_dir)
        if old_manifest_moved:
            os.replace(backup_manifest, target_manifest)
        if old_photos_moved:
            os.replace(backup_photos, photos_dir)
        raise
    finally:
        _remove_path(stage_dir)

    _remove_path(backup_manifest)
    _remove_path(backup_photos)
    _fsync_directory(clock_dir)
    sync_fn()
    return manifest["count"], volume


def _valid_alarm_wav(path: Path) -> bool:
    """Return whether the file matches the firmware's strict WAV expectations."""
    try:
        with wave.open(str(path), "rb") as source:
            if (
                source.getnchannels() != 1
                or source.getsampwidth() != 2
                or source.getframerate() != 22050
            ):
                return False
    except (OSError, wave.Error):
        return False
    data = path.read_bytes()[44:]
    return bool(data) and len(data) % 2 == 0


def deploy_alarm_wav(
    wav_path: Path,
    explicit_mount: Path | None = None,
    *,
    devices: list[dict] | None = None,
    sync_fn: Callable[[], None] = os.sync,
) -> MountedVolume:
    wav_path = wav_path.expanduser().resolve()
    if not wav_path.is_file():
        raise ValueError(f"Alarm WAV does not exist: {wav_path}")
    if not _valid_alarm_wav(wav_path):
        raise ValueError(
            "Alarm WAV must be 22050 Hz, 16-bit, mono PCM with an even data size"
        )
    volume = identify_mount(explicit_mount, devices)
    audio_dir = volume.mount / "clock" / "audio"
    if audio_dir.exists() and not audio_dir.is_dir():
        raise ValueError(f"Cannot create audio directory over non-directory: {audio_dir}")
    audio_dir.mkdir(parents=True, exist_ok=True)

    target = audio_dir / ALARM_WAV_TARGET.name
    token = uuid.uuid4().hex
    staging = audio_dir / f".mateja_staging_audio_{token}"
    _copy_file_synced(wav_path, staging)
    if target.exists() or target.is_symlink():
        backup = audio_dir / f".mateja_backup_alarm_wav_{token}"
        os.replace(target, backup)
        try:
            os.replace(staging, target)
        except Exception:
            os.replace(backup, target)
            raise
        _remove_path(backup)
    else:
        os.replace(staging, target)
    _fsync_directory(audio_dir)
    sync_fn()
    return volume


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=Path("~/Downloads/mateja_clock_photos")
    )
    parser.add_argument("--mount", type=Path)
    parser.add_argument("--replace", action="store_true")
    parser.add_argument(
        "--alarm-wav",
        type=Path,
        metavar="PATH",
        help="Deploy a generated 22050 Hz/16-bit/mono alarm WAV to /clock/audio/alarm.wav.",
    )
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        mounted: MountedVolume | None = None
        deployed: list[str] = []
        source_dir = args.source.expanduser().resolve()
        if source_dir.is_dir():
            count, mounted = deploy_photos(source_dir, args.mount, args.replace)
            deployed.append(f"Copied {count} photo(s) to {mounted.mount}/clock")
        elif args.alarm_wav is None:
            raise ValueError(f"Prepared photo directory does not exist: {source_dir}")
        if args.alarm_wav is not None:
            mounted = deploy_alarm_wav(args.alarm_wav, args.mount)
            deployed.append(f"Copied alarm WAV to {mounted.mount}/clock/audio/alarm.wav")
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"Error: {error}")
        return 1
    print(" and synchronized the card.".join(deployed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
