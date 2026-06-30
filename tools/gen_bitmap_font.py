#!/usr/bin/env python3
"""Offline: tools/fonts/simhei.ttf → assets/littlefs_assets/fonts/simhei_XX.bin"""

from __future__ import annotations

import argparse
import os
import struct
import sys

try:
    import freetype
except ImportError:
    print("Install freetype-py: pip install freetype-py", file=sys.stderr)
    sys.exit(1)

MAGIC = b"EBF1"
VERSION = 1
LUT_ENTRIES = 65536

LEAD_MIN, LEAD_MAX = 0x81, 0xFE
TRAILS = [t for t in range(0x40, 0x100) if t != 0x7F]

EXTRA_CODEPOINTS = [
    0x00A0,
    0x00B7,
    0x2014,
    0x2018,
    0x2019,
    0x201C,
    0x201D,
    0x2026,
    0x3000,
    0x3001,
    0x3002,
    0x300A,
    0x300B,
    0xFF01,
    0xFF08,
    0xFF09,
    0xFF0C,
    0xFF1A,
    0xFF1B,
    0xFF1F,
]


def collect_codepoints() -> list[int]:
    cps: set[int] = set(range(0x20, 0x7F))
    cps.update(EXTRA_CODEPOINTS)
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        for trail in TRAILS:
            try:
                uni = ord(bytes([lead, trail]).decode("gbk"))
            except UnicodeDecodeError:
                continue
            if uni >= 0x80:
                cps.add(uni)
    return sorted(cps)


def render_glyph(face: freetype.Face, cp: int) -> dict | None:
    try:
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    except freetype.FT_Exception:
        return None

    g = face.glyph
    bm = g.bitmap
    width = bm.width
    rows = bm.rows
    pitch = (width + 7) // 8 if width > 0 else 0
    data = bytearray(pitch * rows)

    if bm.buffer and width > 0 and rows > 0:
        if bm.pixel_mode == freetype.FT_PIXEL_MODE_MONO:
            for r in range(rows):
                src = bm.buffer[r * bm.pitch : r * bm.pitch + pitch]
                data[r * pitch : r * pitch + len(src)] = src
        else:
            for r in range(rows):
                for c in range(width):
                    if bm.buffer[r * bm.pitch + c] >= 128:
                        data[r * pitch + (c >> 3)] |= 0x80 >> (c & 7)

    return {
        "advance": g.advance.x >> 6,
        "bearing_x": g.bitmap_left,
        "bearing_y": g.bitmap_top,
        "width": width,
        "height": rows,
        "pitch": pitch,
        "data": bytes(data),
    }


def build_font(ttf_path: str, out_path: str, size_px: int) -> None:
    face = freetype.Face(ttf_path)
    face.set_pixel_sizes(0, size_px)

    ascent = face.size.ascender >> 6
    line_height = face.size.height >> 6
    if line_height == 0:
        line_height = size_px
    if ascent == 0:
        ascent = size_px

    lut = [0] * LUT_ENTRIES
    dir_entries: list[dict] = []
    bitmap_pool = bytearray()
    glyph_count = 0

    for cp in collect_codepoints():
        if cp >= LUT_ENTRIES:
            continue
        glyph = render_glyph(face, cp)
        if glyph is None:
            continue

        glyph_count += 1
        lut[cp] = glyph_count
        glyph["bitmap_ofs"] = len(bitmap_pool)
        dir_entries.append(glyph)
        bitmap_pool.extend(glyph["data"])

    header_size = 28
    lut_ofs = header_size
    dir_ofs = lut_ofs + LUT_ENTRIES * 2
    bitmap_ofs = dir_ofs + glyph_count * 16
    file_size = bitmap_ofs + len(bitmap_pool)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HBBBBH", VERSION, size_px, ascent, line_height, 0, glyph_count))
        f.write(struct.pack("<III", lut_ofs, dir_ofs, bitmap_ofs))
        f.write(struct.pack("<I", file_size))

        for gid in lut:
            f.write(struct.pack("<H", gid))

        for g in dir_entries:
            f.write(
                struct.pack(
                    "<HhhHHHI",
                    g["advance"],
                    g["bearing_x"],
                    g["bearing_y"],
                    g["width"],
                    g["height"],
                    g["pitch"],
                    g["bitmap_ofs"],
                )
            )

        f.write(bitmap_pool)

    print(
        f"Wrote {out_path}: {glyph_count} glyphs @ {size_px}px, "
        f"{file_size // 1024} KiB (ascent={ascent}, line={line_height})"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate EBF1 bitmap font for userdata")
    parser.add_argument(
        "--size",
        type=int,
        nargs="+",
        default=[12, 14, 16],
        help="Pixel sizes to build (default: 12 14 16)",
    )
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ttf = os.path.join(root, "tools", "fonts", "simhei.ttf")
    if not os.path.isfile(ttf):
        print(f"TTF not found: {ttf}", file=sys.stderr)
        sys.exit(1)

    for size_px in args.size:
        if size_px < 8 or size_px > 32:
            print(f"Skip invalid size: {size_px}", file=sys.stderr)
            continue
        out = os.path.join(
            root, "assets", "littlefs_assets", "fonts", f"simhei_{size_px}.bin"
        )
        build_font(ttf, out, size_px)


if __name__ == "__main__":
    main()
