#!/usr/bin/env python3
"""Subset icon codepoints into assets/littlefs_assets/fonts/font_awesome_6.ttf."""

from __future__ import annotations

import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_HPP = os.path.join(ROOT, "main", "app", "ebook", "gfx", "icon.hpp")
FA_SRC = os.path.join(
    ROOT, "others", "fontawesome-free-6.4.0-web", "webfonts", "fa-solid-900.ttf"
)
OUT_DIR = os.path.join(ROOT, "assets", "littlefs_assets", "fonts")
OUT_PATH = os.path.join(OUT_DIR, "font_awesome_6.ttf")


def parse_codepoints() -> list[int]:
    with open(ICON_HPP, "r", encoding="utf-8") as f:
        text = f.read()
    return sorted({int(m, 16) for m in re.findall(r"0x[0-9A-Fa-f]+", text)})


def try_subset(src: str, dst: str, codepoints: list[int]) -> bool:
    try:
        from fontTools import subset
    except ImportError:
        return False

    opts = subset.Options()
    opts.drop_tables += ["GSUB", "GPOS", "GDEF"]
    opts.recalc_bounds = True
    opts.recalc_timestamp = True

    unicodes = "".join(chr(cp) for cp in codepoints)
    subsetter = subset.Subsetter(options=opts)
    subsetter.populate(unicodes=unicodes)
    font = subset.load_font(src, options=subset.Options())
    subsetter.subset(font)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    font.save(dst)
    return True


def main() -> None:
    if not os.path.isfile(FA_SRC):
        print(f"Font Awesome source missing: {FA_SRC}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)
    cps = parse_codepoints()
    priv = [cp for cp in cps if cp < 0xF000]

    ok = False
    try:
        ok = try_subset(FA_SRC, OUT_PATH, cps)
    except Exception as exc:
        print(f"subset failed ({exc}), using full fa-solid-900", file=sys.stderr)

    if not ok:
        shutil.copy2(FA_SRC, OUT_PATH)

    size = os.path.getsize(OUT_PATH)
    mode = f"{len(cps)} codepoints" if ok else "full fa-solid-900"
    print(f"Wrote {OUT_PATH}: {mode} ({size // 1024} KiB)")

    if priv:
        print(
            "Note: private icons (U+E001~) are not in FA source; "
            "merge them into font_awesome_6.ttf manually if missing.",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()
