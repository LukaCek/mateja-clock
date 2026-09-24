#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
from pathlib import Path


VERSION_PATTERN = re.compile(
    r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
)


def valid_version(version: str) -> bool:
    if not VERSION_PATTERN.fullmatch(version) or len(version) >= 24:
        return False
    return all(int(component) <= 65535 for component in version.split("."))


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a firmware release manifest.")
    parser.add_argument("--version", required=True)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if not valid_version(args.version):
        parser.error(f"invalid semantic version: {args.version!r}")
    if not args.firmware.is_file():
        parser.error(f"firmware file does not exist: {args.firmware}")

    digest = hashlib.sha256()
    with args.firmware.open("rb") as firmware:
        for chunk in iter(lambda: firmware.read(1024 * 1024), b""):
            digest.update(chunk)

    manifest = {
        "product": "mateja-clock",
        "version": args.version,
        "firmware": args.firmware.name,
        "size": args.firmware.stat().st_size,
        "sha256": digest.hexdigest(),
    }
    args.output.write_text(
        json.dumps(manifest, separators=(",", ":")) + "\n", encoding="ascii"
    )


if __name__ == "__main__":
    main()
