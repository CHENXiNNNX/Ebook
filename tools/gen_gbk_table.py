#!/usr/bin/env python3
"""Generate main/app/ebook/text/gbk_table.inc for path_encoding.cc."""

from __future__ import annotations

import os

LEAD_MIN, LEAD_MAX = 0x81, 0xFE
TRAILS = [t for t in range(0x40, 0x100) if t != 0x7F]
LEAD_COUNT = LEAD_MAX - LEAD_MIN + 1
TRAIL_COUNT = len(TRAILS)
TRAIL_TO_IDX = {t: i for i, t in enumerate(TRAILS)}


def main() -> None:
    grid = [0xFFFF] * (LEAD_COUNT * TRAIL_COUNT)
    mapped = 0
    for lead in range(LEAD_MIN, LEAD_MAX + 1):
        for trail in TRAILS:
            try:
                uni = ord(bytes([lead, trail]).decode("gbk"))
            except UnicodeDecodeError:
                continue
            if uni < 0x80:
                continue
            li = lead - LEAD_MIN
            ti = TRAIL_TO_IDX[trail]
            grid[li * TRAIL_COUNT + ti] = uni
            mapped += 1

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "main", "app", "ebook", "text", "gbk_table.inc")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    with open(out, "w", encoding="utf-8") as f:
        f.write(f"// Auto-generated GBK dense grid ({mapped} mapped, {len(grid)} slots)\n")
        f.write(f"static constexpr uint8_t kGbkLeadMin = 0x{LEAD_MIN:02X};\n")
        f.write(f"static constexpr uint8_t kGbkLeadMax = 0x{LEAD_MAX:02X};\n")
        f.write(f"static constexpr uint8_t kGbkTrailCount = {TRAIL_COUNT};\n")
        f.write("static const uint16_t kGbkGrid[] = {\n")
        for i in range(0, len(grid), 16):
            chunk = grid[i : i + 16]
            line = ", ".join(f"0x{v:04X}" for v in chunk)
            comma = "," if i + 16 < len(grid) else ""
            f.write(f"    {line}{comma}\n")
        f.write("};\n")

    print(f"wrote {out} ({mapped} entries, {len(grid) * 2} bytes)")


if __name__ == "__main__":
    main()
