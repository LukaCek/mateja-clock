from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.copy_photos_to_sd import deploy_alarm_wav, deploy_photos, identify_mount
from tools.generate_alarm_wav import synthesize


def _devices(
    mount: Path,
    *,
    label: str = "MATEJA_CLK",
    removable: int = 1,
    filesystem: str = "vfat",
) -> list[dict]:
    return [
        {
            "name": "/dev/sdz",
            "path": "/dev/sdz",
            "rm": removable,
            "children": [
                {
                    "name": "/dev/sdz1",
                    "path": "/dev/sdz1",
                    "label": label,
                    "mountpoints": [str(mount)],
                    "rm": removable,
                    "fstype": filesystem,
                }
            ],
        }
    ]


def _prepared_source(root: Path, content: bytes = b"jpeg data") -> Path:
    root.mkdir()
    (root / "photo_0001.jpg").write_bytes(content)
    (root / "unreferenced.jpg").write_bytes(b"do not copy")
    (root / "source_manifest.json").write_text('{"private": true}\n')
    contact_sheets = root / "contact_sheets"
    contact_sheets.mkdir()
    (contact_sheets / "contact_sheet_001.jpg").write_bytes(b"do not copy")
    manifest = {
        "version": 1,
        "count": 1,
        "photos": [
            {
                "id": 1,
                "filename": "photo_0001.jpg",
                "source_width": 640,
                "source_height": 480,
                "orientation": "landscape",
            }
        ],
    }
    (root / "manifest.json").write_text(json.dumps(manifest) + "\n")
    return root


def test_deploys_only_manifest_dataset_and_replaces_safely(tmp_path: Path) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    (mount / "unrelated.txt").write_text("keep me")
    source = _prepared_source(tmp_path / "prepared")
    sync_calls: list[bool] = []

    count, volume = deploy_photos(
        source,
        mount,
        devices=_devices(mount),
        sync_fn=lambda: sync_calls.append(True),
    )

    assert count == 1
    assert volume.mount == mount.resolve()
    assert sync_calls == [True]
    assert (mount / "unrelated.txt").read_text() == "keep me"
    assert (mount / "clock/photos/photo_0001.jpg").read_bytes() == b"jpeg data"
    assert json.loads((mount / "clock/manifest.json").read_text())["count"] == 1
    assert not (mount / "clock/source_manifest.json").exists()
    assert not (mount / "clock/contact_sheets").exists()
    assert not (mount / "clock/photos/unreferenced.jpg").exists()

    (source / "photo_0001.jpg").write_bytes(b"new jpeg data")
    with pytest.raises(ValueError, match="--replace"):
        deploy_photos(
            source,
            mount,
            devices=_devices(mount),
            sync_fn=lambda: sync_calls.append(True),
        )
    assert (mount / "clock/photos/photo_0001.jpg").read_bytes() == b"jpeg data"

    deploy_photos(
        source,
        mount,
        replace=True,
        devices=_devices(mount),
        sync_fn=lambda: sync_calls.append(True),
    )
    assert (mount / "clock/photos/photo_0001.jpg").read_bytes() == b"new jpeg data"
    assert (mount / "unrelated.txt").read_text() == "keep me"
    assert not list((mount / "clock").glob(".mateja_*"))
    assert sync_calls == [True, True]


@pytest.mark.parametrize(
    ("label", "removable", "message"),
    [
        ("WRONG", 1, "label must be exactly"),
        ("MATEJA_CLK", 0, "non-removable"),
    ],
)
def test_explicit_mount_still_requires_label_and_removable_media(
    tmp_path: Path, label: str, removable: int, message: str
) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    with pytest.raises(ValueError, match=message):
        identify_mount(mount, _devices(mount, label=label, removable=removable))


def test_explicit_mount_must_be_a_mounted_block_device(tmp_path: Path) -> None:
    requested = tmp_path / "ordinary_directory"
    requested.mkdir()
    other = tmp_path / "card"
    other.mkdir()

    with pytest.raises(ValueError, match="not a mounted block-device"):
        identify_mount(requested, _devices(other))


def test_rejects_non_fat_filesystem(tmp_path: Path) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    with pytest.raises(ValueError, match="filesystem must be FAT"):
        identify_mount(mount, _devices(mount, filesystem="ext4"))


def test_auto_detection_requires_exact_label(tmp_path: Path) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    with pytest.raises(ValueError, match="No mounted volume"):
        identify_mount(None, _devices(mount, label="mateja_clk"))


def test_deploys_alarm_wav_and_replaces_it(tmp_path: Path) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    source_wav = tmp_path / "alarm.wav"
    synthesize(source_wav, duration_seconds=1.0)
    original_bytes = source_wav.read_bytes()
    sync_calls: list[bool] = []

    volume = deploy_alarm_wav(
        source_wav,
        mount,
        devices=_devices(mount),
        sync_fn=lambda: sync_calls.append(True),
    )

    assert volume.mount == mount.resolve()
    assert sync_calls == [True]
    target = mount / "clock/audio/alarm.wav"
    assert target.is_file()
    assert target.read_bytes() == original_bytes
    assert not list((mount / "clock/audio").glob(".mateja_*"))

    (source_wav).write_bytes(b"replacement wav bytes")
    with pytest.raises(ValueError, match="22050 Hz"):
        deploy_alarm_wav(source_wav, mount, devices=_devices(mount))
    assert target.read_bytes() == original_bytes

    synthesize(source_wav, duration_seconds=1.0)
    deploy_alarm_wav(source_wav, mount, devices=_devices(mount))
    assert target.read_bytes() == source_wav.read_bytes()
    assert not list((mount / "clock/audio").glob(".mateja_*"))


def test_rejects_missing_or_malformed_alarm_wav(tmp_path: Path) -> None:
    mount = tmp_path / "card"
    mount.mkdir()
    missing = tmp_path / "missing.wav"
    with pytest.raises(ValueError, match="does not exist"):
        deploy_alarm_wav(missing, mount, devices=_devices(mount))

    bad = tmp_path / "bad.wav"
    bad.write_bytes(b"not a wav at all")
    with pytest.raises(ValueError, match="22050 Hz"):
        deploy_alarm_wav(bad, mount, devices=_devices(mount))
