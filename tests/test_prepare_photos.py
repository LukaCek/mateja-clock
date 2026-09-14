from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image

from tools.prepare_photos import prepare_photos


def _hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_prepares_deterministic_photos_and_skips_unsuitable_inputs(tmp_path: Path) -> None:
    source = tmp_path / "input"
    output = source / "prepared"
    nested = source / "nested"
    nested.mkdir(parents=True)

    landscape = Image.new("RGB", (900, 300), "green")
    landscape.paste("red", (0, 0, 300, 300))
    landscape.paste("blue", (600, 0, 900, 300))
    landscape.save(source / "a_landscape.png")

    Image.new("RGB", (300, 750), (220, 50, 40)).save(source / "b_portrait.webp")
    rotated = Image.new("RGB", (640, 400), (30, 180, 80))
    exif = Image.Exif()
    exif[274] = 6
    rotated.save(nested / "c_rotated.jpg", exif=exif)
    Image.new("RGB", (500, 300), "yellow").save(source / "holiday_screenshot.png")
    Image.new("RGB", (64, 64), "purple").save(source / "tiny.jpeg")
    (source / "broken.jpg").write_bytes(b"not an image")

    source_hashes = {path: _hash(path) for path in source.rglob("*") if path.is_file()}
    count, skipped = prepare_photos(source, output)

    assert count == 3
    assert len(skipped) == 3
    assert any("screenshot" in reason for reason in skipped)
    assert any("too small" in reason for reason in skipped)
    assert any("unreadable image" in reason for reason in skipped)
    assert source_hashes == {path: _hash(path) for path in source_hashes}

    manifest = json.loads((output / "manifest.json").read_text())
    assert manifest == {
        "version": 1,
        "count": 3,
        "photos": [
            {
                "id": 1,
                "filename": "photo_0001.jpg",
                "source_width": 900,
                "source_height": 300,
                "orientation": "landscape",
            },
            {
                "id": 2,
                "filename": "photo_0002.jpg",
                "source_width": 300,
                "source_height": 750,
                "orientation": "portrait",
            },
            {
                "id": 3,
                "filename": "photo_0003.jpg",
                "source_width": 400,
                "source_height": 640,
                "orientation": "portrait",
            },
        ],
    }
    assert str(source) not in json.dumps(manifest)

    source_manifest = json.loads((output / "source_manifest.json").read_text())
    assert source_manifest["count"] == 3
    assert all(Path(photo["source_path"]).is_absolute() for photo in source_manifest["photos"])
    assert [path.name for path in (output / "contact_sheets").iterdir()] == [
        "contact_sheet_001.jpg"
    ]
    with Image.open(output / "contact_sheets/contact_sheet_001.jpg") as sheet:
        assert sheet.size == (640, 710)

    generated = [output / f"photo_{index:04d}.jpg" for index in range(1, 4)]
    for path in generated:
        with Image.open(path) as image:
            assert image.size == (320, 240)
            assert image.mode == "RGB"
            assert image.format == "JPEG"
            assert not image.info.get("progressive", False)
            assert not image.info.get("progression", False)

    with Image.open(generated[0]) as image:
        center = image.getpixel((160, 120))
        assert center[1] > center[0] * 2 and center[1] > center[2] * 2
    with Image.open(generated[1]) as image:
        center = image.getpixel((160, 120))
        edge = image.getpixel((10, 120))
        assert sum(center) > sum(edge) + 40

    generated_hashes = {
        path.relative_to(output): _hash(path)
        for path in output.rglob("*")
        if path.is_file()
    }
    second_count, second_skipped = prepare_photos(source, output)
    assert second_count == count
    assert second_skipped == skipped
    assert generated_hashes == {
        path.relative_to(output): _hash(path)
        for path in output.rglob("*")
        if path.is_file()
    }
    assert source_hashes == {path: _hash(path) for path in source_hashes}


def test_rejects_output_that_contains_input(tmp_path: Path) -> None:
    source = tmp_path / "source"
    source.mkdir()

    try:
        prepare_photos(source, tmp_path)
    except ValueError as error:
        assert "must not" in str(error)
    else:
        raise AssertionError("unsafe output location was accepted")


def test_contact_sheets_are_paginated(tmp_path: Path) -> None:
    source = tmp_path / "source"
    output = tmp_path / "output"
    source.mkdir()
    for index in range(21):
        Image.new("RGB", (400, 400), (index * 10, 80, 120)).save(
            source / f"image_{index:02d}.png"
        )

    count, skipped = prepare_photos(source, output)

    assert count == 21
    assert skipped == []
    assert sorted(path.name for path in (output / "contact_sheets").iterdir()) == [
        "contact_sheet_001.jpg",
        "contact_sheet_002.jpg",
    ]
