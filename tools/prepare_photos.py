#!/usr/bin/env python3
"""Prepare ordinary photos for the Mateja Clock display."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageOps


OUTPUT_SIZE = (320, 240)
SUPPORTED_EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp"}
MIN_SOURCE_SIDE = 240
KNOWN_SCREENSHOT_SIZES = {
    (739, 1600),
    (828, 1792),
}
KNOWN_SCREENSHOT_SHA256 = {
    "a3dd5f57798c8ed24bd83b986769b52f5f17ef3358cc6d5509a7274d93b9a630",
    "3583654685851e024f5d15ceee9dd9f9d2fd40e624009c25cfa847e277823014",
    "eb6cfa9bdca4f22a74113a6cb29d67bcb46539a7af3a79ddb4d2a1c3599d7897",
}
MANIFEST_VERSION = 1
CONTACT_SHEET_COLUMNS = 4
CONTACT_SHEET_ROWS = 5
CONTACT_THUMB_SIZE = (160, 120)
SKIP_NAME_RE = re.compile(
    r"(?:^|[\s_.-])(?:"
    r"screen[\s_-]?(?:shots?|captures?)(?=[\s_.-]|\d|$)"
    r"|(?:favicon|icon|thumbnail|thumb)(?=[\s_.-]|$)"
    r")",
    re.IGNORECASE,
)


def discover_images(input_dir: Path, output_dir: Path) -> list[Path]:
    """Return supported source images in a stable relative-path order."""
    input_dir = input_dir.resolve()
    output_dir = output_dir.resolve()
    candidates = []
    for path in input_dir.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SUPPORTED_EXTENSIONS:
            continue
        resolved = path.resolve()
        if resolved == output_dir or resolved.is_relative_to(output_dir):
            continue
        candidates.append(path)
    return sorted(
        candidates,
        key=lambda path: (
            path.relative_to(input_dir).as_posix().casefold(),
            path.relative_to(input_dir).as_posix(),
        ),
    )


def _as_rgb(image: Image.Image) -> Image.Image:
    if image.mode in {"RGBA", "LA"} or "transparency" in image.info:
        rgba = image.convert("RGBA")
        background = Image.new("RGBA", rgba.size, "white")
        return Image.alpha_composite(background, rgba).convert("RGB")
    return image.convert("RGB")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def compose_photo(image: Image.Image) -> tuple[Image.Image, str]:
    """Compose an EXIF-corrected image for the landscape display."""
    image = _as_rgb(image)
    width, height = image.size
    if width > height:
        return ImageOps.fit(image, OUTPUT_SIZE, method=Image.Resampling.LANCZOS), "landscape"

    background = ImageOps.fit(image, OUTPUT_SIZE, method=Image.Resampling.LANCZOS)
    background = ImageEnhance.Color(background).enhance(0.82)
    background = ImageEnhance.Brightness(background).enhance(0.72)
    background = background.filter(ImageFilter.GaussianBlur(radius=8))

    foreground = ImageOps.contain(image, OUTPUT_SIZE, method=Image.Resampling.LANCZOS)
    left = (OUTPUT_SIZE[0] - foreground.width) // 2
    top = (OUTPUT_SIZE[1] - foreground.height) // 2
    background.paste(foreground, (left, top))
    return background, "portrait" if width < height else "square"


def _write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def _make_contact_sheets(stage_dir: Path, photos: list[dict]) -> None:
    sheet_dir = stage_dir / "contact_sheets"
    sheet_dir.mkdir()
    per_page = CONTACT_SHEET_COLUMNS * CONTACT_SHEET_ROWS
    cell_width = CONTACT_THUMB_SIZE[0]
    cell_height = CONTACT_THUMB_SIZE[1] + 22
    font = ImageFont.load_default()

    for page_start in range(0, len(photos), per_page):
        page = Image.new(
            "RGB",
            (CONTACT_SHEET_COLUMNS * cell_width, CONTACT_SHEET_ROWS * cell_height),
            "white",
        )
        draw = ImageDraw.Draw(page)
        for offset, record in enumerate(photos[page_start : page_start + per_page]):
            row, column = divmod(offset, CONTACT_SHEET_COLUMNS)
            x = column * cell_width
            y = row * cell_height
            with Image.open(stage_dir / record["filename"]) as photo:
                thumbnail = photo.resize(CONTACT_THUMB_SIZE, Image.Resampling.LANCZOS)
                page.paste(thumbnail, (x, y))
            draw.text(
                (x + 4, y + CONTACT_THUMB_SIZE[1] + 4),
                record["filename"],
                fill="black",
                font=font,
            )
        page_number = page_start // per_page + 1
        page.save(
            sheet_dir / f"contact_sheet_{page_number:03d}.jpg",
            format="JPEG",
            quality=85,
            progressive=False,
            optimize=False,
        )


def _validate_staged_photos(stage_dir: Path, photos: list[dict]) -> None:
    for record in photos:
        path = stage_dir / record["filename"]
        with Image.open(path) as image:
            image.load()
            if image.format != "JPEG" or image.mode != "RGB" or image.size != OUTPUT_SIZE:
                raise RuntimeError(f"Invalid generated photo: {path.name}")
            if image.info.get("progressive") or image.info.get("progression"):
                raise RuntimeError(f"Generated JPEG is progressive: {path.name}")


def _install_staged_output(stage_dir: Path, output_dir: Path) -> None:
    for path in output_dir.iterdir():
        if path.is_file() and re.fullmatch(r"photo_\d{4}\.jpg", path.name):
            path.unlink()
    for name in ("manifest.json", "source_manifest.json"):
        path = output_dir / name
        if path.exists():
            path.unlink()
    contact_sheets = output_dir / "contact_sheets"
    if contact_sheets.exists():
        if not contact_sheets.is_dir():
            raise RuntimeError(f"Cannot replace non-directory: {contact_sheets}")
        shutil.rmtree(contact_sheets)

    for path in stage_dir.iterdir():
        path.replace(output_dir / path.name)


def prepare_photos(input_dir: Path, output_dir: Path, quality: int = 85) -> tuple[int, list[str]]:
    input_dir = input_dir.expanduser().resolve()
    output_dir = output_dir.expanduser().resolve()
    if not input_dir.is_dir():
        raise ValueError(f"Input directory does not exist: {input_dir}")
    if input_dir == output_dir or input_dir.is_relative_to(output_dir):
        raise ValueError("Output must not be the input directory or one of its parents")
    if not 1 <= quality <= 95:
        raise ValueError("JPEG quality must be between 1 and 95")

    output_dir.mkdir(parents=True, exist_ok=True)
    sources = discover_images(input_dir, output_dir)
    skipped: list[str] = []
    records: list[dict] = []

    stage_dir = Path(tempfile.mkdtemp(prefix=".prepare_photos_", dir=output_dir))
    try:
        for source in sources:
            relative_source = source.relative_to(input_dir).as_posix()
            if SKIP_NAME_RE.search(source.stem):
                skipped.append(f"{relative_source}: filename indicates a screenshot/icon/thumbnail")
                continue
            try:
                with Image.open(source) as opened:
                    opened.load()
                    corrected = ImageOps.exif_transpose(opened)
                    source_width, source_height = corrected.size
                    if min(source_width, source_height) < MIN_SOURCE_SIDE:
                        skipped.append(
                            f"{relative_source}: too small ({source_width}x{source_height})"
                        )
                        continue
                    if (
                        (source_width, source_height) in KNOWN_SCREENSHOT_SIZES
                        and _sha256(source) in KNOWN_SCREENSHOT_SHA256
                    ):
                        skipped.append(
                            f"{relative_source}: dimensions indicate a phone screenshot "
                            f"({source_width}x{source_height})"
                        )
                        continue
                    composed, orientation = compose_photo(corrected)
            except (OSError, SyntaxError, ValueError) as error:
                skipped.append(f"{relative_source}: unreadable image ({error})")
                continue

            photo_id = len(records) + 1
            if photo_id > 9999:
                raise ValueError("At most 9999 photos can be prepared")
            filename = f"photo_{photo_id:04d}.jpg"
            composed.save(
                stage_dir / filename,
                format="JPEG",
                quality=quality,
                progressive=False,
                optimize=False,
                subsampling=2,
            )
            records.append(
                {
                    "id": photo_id,
                    "filename": filename,
                    "source_width": source_width,
                    "source_height": source_height,
                    "orientation": orientation,
                    "source_path": str(source.resolve()),
                }
            )

        esp_records = [
            {key: value for key, value in record.items() if key != "source_path"}
            for record in records
        ]
        _write_json(
            stage_dir / "manifest.json",
            {"version": MANIFEST_VERSION, "count": len(records), "photos": esp_records},
        )
        _write_json(
            stage_dir / "source_manifest.json",
            {"version": MANIFEST_VERSION, "count": len(records), "photos": records},
        )
        _validate_staged_photos(stage_dir, records)
        _make_contact_sheets(stage_dir, records)
        _install_staged_output(stage_dir, output_dir)
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)

    return len(records), skipped


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("~/Downloads"))
    parser.add_argument("--output", type=Path, default=Path("~/Downloads/mateja_clock_photos"))
    parser.add_argument("--quality", type=int, default=85)
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        count, skipped = prepare_photos(args.input, args.output, args.quality)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Error: {error}")
        return 1
    for reason in skipped:
        print(f"Skipped {reason}")
    print(f"Prepared {count} photo(s) in {args.output.expanduser().resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
