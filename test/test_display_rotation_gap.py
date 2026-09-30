#!/usr/bin/env python3
"""The rotation gap table must never put the panel gap on the swapped axis.

OBS-42 (2026-09-30): with the buttons down the 2.16 panel rendered every app
as horizontally smeared, dotted streaks. That pose selects MADCTL 0x60
(MV|MX), and the table gave it x_gap 6. The CO5300 driver adds x_gap straight
to CASET without knowing MV has swapped the axes, so the column window ran to
485 on an axis that ends at 479: the controller clamps the window while every
row still carries 480 pixels, and each row of a 12-row flush band spills six
pixels into the next. The x_gap in an MV mode is therefore always 0."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
main = (root / "main/main.c").read_text(encoding="utf-8")

madctl = re.search(r"static const uint8_t MADCTL\[4\] = \{([^}]*)\};", main)
gap = re.search(r"static const int GAP\[4\]\[2\] = \{(.*?)\n  \};", main, re.S)
assert madctl and gap, "rotation tables not found in main/main.c"
modes = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", madctl.group(1))]
gaps = [(int(x), int(y)) for x, y in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", gap.group(1))]
assert len(modes) == 4 and len(gaps) == 4, (modes, gaps)

for mode, (x_gap, _y_gap) in zip(modes, gaps, strict=True):
    if mode & 0x20:  # MV: the driver's x lands on the axis without spare RAM
        assert x_gap == 0, (
            f"MADCTL 0x{mode:02X} swaps the axes (MV) but has x_gap {x_gap}: "
            "the column window overruns and every flush band smears (OBS-42)"
        )

assert "0x0006..0x01DD" not in main, (
    "BSP 2.0.1 initialises CASET 0x0000..0x01DF; the old comment was wrong"
)
print("OK: no rotation mode puts the panel gap on the swapped axis")
