#!/usr/bin/env python3
"""Prepare emoji PNG assets from NotoColorEmoji.ttf for the Mateja Clock.

Usage:
    python3 tools/prepare_emojis.py --sizes 32 48 --emoji ❤ 😊 🥰 😘 👍
"""

import argparse
import io
import os
import sys

from PIL import Image
from fontTools.ttLib import TTFont


def codepoint_from_char(ch):
    """Get the primary codepoint from a character (skip VS16)."""
    cp = ord(ch[0])
    return cp


def build_emoji_sequences(emoji_strs):
    """Build dictionary of codepoint sequences from input strings."""
    sequences = {}
    for s in emoji_strs:
        codepoints = [ord(c) for c in s]
        hex_parts = []
        for cp in codepoints:
            if cp == 0x200D:  # ZWJ
                continue
            if 0xFE00 <= cp <= 0xFE0F:
                continue
            if 0xE0100 <= cp <= 0xE01EF:
                continue
            hex_parts.append(f"{cp:x}")
        filename = "-".join(hex_parts) + ".png"
        sequences[s] = (codepoints, filename)
    return sequences


def extract_emoji_png(font_path, codepoint):
    """Extract color emoji bitmap from font CBDT table."""
    font = TTFont(font_path)
    cmap = font.getBestCmap()
    cbdt = font["CBDT"]

    result = None
    if codepoint in cmap:
        gid = cmap[codepoint]
        for strike in cbdt.strikeData:
            if gid in strike:
                glyph = strike[gid]
                if hasattr(glyph, "imageData"):
                    img = Image.open(io.BytesIO(glyph.imageData))
                    # Convert to RGBA if needed
                    if img.mode == "P":
                        img = img.convert("RGBA")
                    elif img.mode in ("RGB", "L", "LA"):
                        img = img.convert("RGBA")
                    result = img
                    break
        font.close()
        return result
    font.close()
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Prepare emoji PNGs for Mateja Clock"
    )
    parser.add_argument(
        "--sizes",
        nargs="+",
        type=int,
        default=[32, 48],
        help="Pixel sizes (e.g. 32 48)",
    )
    parser.add_argument(
        "--emoji",
        nargs="+",
        required=True,
        help="Emoji characters to extract",
    )
    parser.add_argument(
        "--font",
        default="/usr/share/fonts/noto/NotoColorEmoji.ttf",
        help="Path to NotoColorEmoji.ttf",
    )
    parser.add_argument(
        "--output",
        default="/tmp/sd_card/emoji",
        help="Output base directory",
    )
    args = parser.parse_args()

    if not os.path.exists(args.font):
        sys.exit(f"Font not found: {args.font}")

    sequences = build_emoji_sequences(args.emoji)
    successes = 0

    for emoji_str, (codepoints, filename) in sequences.items():
        primary_cp = codepoints[0]
        print(f"{emoji_str}  U+{primary_cp:04X} -> {filename}")

        # Extract full-size bitmap from font
        full = extract_emoji_png(args.font, primary_cp)
        if full is None:
            print(f"  [FAIL] Could not extract from font")
            continue

        w, h = full.size
        # Crop to square centered on the largest dimension
        if w != h:
            side = max(w, h)
            square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
            x = (side - w) // 2
            y = (side - h) // 2
            square.paste(full, (x, y), full)
            full = square

        for size in args.sizes:
            dir_path = os.path.join(args.output, str(size))
            os.makedirs(dir_path, exist_ok=True)
            out_path = os.path.join(dir_path, filename)

            resized = full.resize((size, size), Image.LANCZOS)
            resized.save(out_path, "PNG")
            print(f"  [{size}px] saved -> {out_path}")

        successes += 1

    print(f"\n{successes}/{len(sequences)} emojis extracted successfully")
    print(f"Output: {os.path.abspath(args.output)}")


if __name__ == "__main__":
    main()