#!/usr/bin/env python3
"""Build flash-only LVGL images for VibePulse's Claude and Codex pets."""

from __future__ import annotations

import math
from itertools import pairwise
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/app_tokens/assets/source"
OUT_C = ROOT / "components/app_tokens/agent_assets.c"
OUT_H = ROOT / "components/app_tokens/agent_assets.h"
CANVAS = 180


def fit_canvas(image: Image.Image, crop: tuple[int, int, int, int],
               canvas_size: int,
               resample: Image.Resampling = Image.Resampling.NEAREST) -> Image.Image:
    cropped = image.crop(crop)
    scale = min(canvas_size / cropped.width, canvas_size / cropped.height)
    size = (max(1, round(cropped.width * scale)),
            max(1, round(cropped.height * scale)))
    resized = cropped.resize(size, resample)
    canvas = Image.new(image.mode, (canvas_size, canvas_size), 0)
    canvas.paste(resized, ((canvas_size - size[0]) // 2,
                           (canvas_size - size[1]) // 2))
    return canvas


def components(mask: Image.Image, bounds: tuple[int, int, int, int]):
    pixels = mask.load()
    x0, y0, x1, y1 = bounds
    remaining = {(x, y) for y in range(y0, y1) for x in range(x0, x1)
                 if pixels[x, y]}
    found = []
    while remaining:
        start = remaining.pop()
        queue = deque([start])
        part = [start]
        while queue:
            x, y = queue.popleft()
            for point in ((x - 1, y), (x + 1, y),
                          (x, y - 1), (x, y + 1)):
                if point in remaining:
                    remaining.remove(point)
                    queue.append(point)
                    part.append(point)
        found.append(part)
    return found


def build_claude(canvas_size: int = CANVAS) -> bytes:
    source = Image.open(SOURCE / "claude-pet-white.png").convert("RGBA")
    alpha = source.getchannel("A")
    crop = alpha.getbbox()
    if crop is None:
        raise ValueError("Claude source has no visible pixels")
    return bytes(fit_canvas(alpha, crop, canvas_size).get_flattened_data())


def build_codex(canvas_size: int = CANVAS) -> bytes:
    source = Image.open(SOURCE / "codex-icon.png").convert("RGBA")
    src = source.load()
    colored = Image.new("L", source.size, 0)
    colored_px = colored.load()
    for y in range(source.height):
        for x in range(source.width):
            r, g, b, a = src[x, y]
            if a and max(r, g, b) - min(r, g, b) > 8:
                colored_px[x, y] = 255

    crop = colored.getbbox()
    if crop is None:
        raise ValueError("Codex source has no colored cloud")

    # The white app plate touches the crop edges; the two enclosed white
    # components are the real terminal glyphs. This derives > and _ from the
    # supplied artwork instead of redrawing approximations.
    white = Image.new("L", source.size, 0)
    white_px = white.load()
    x0, y0, x1, y1 = crop
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = src[x, y]
            if a > 200 and min(r, g, b) >= 235:
                white_px[x, y] = 255

    glyph_parts = []
    for part in components(white, crop):
        if len(part) < 100:
            continue
        if any(x in (x0, x1 - 1) or y in (y0, y1 - 1) for x, y in part):
            continue
        glyph_parts.append(part)
    if len(glyph_parts) != 2:
        raise ValueError(f"expected two Codex glyphs, found {len(glyph_parts)}")
    glyph_parts.sort(key=lambda part: max(y for _, y in part) -
                     min(y for _, y in part), reverse=True)

    # Compose the source-derived cloud and both enclosed white terminal
    # components before scaling. Pixels from the rounded white application
    # plate never enter this image, so its edge cannot create a pale fringe.
    composite = Image.new("RGBA", source.size, (0, 0, 0, 0))
    composite_px = composite.load()
    glyph_points = {point for part in glyph_parts for point in part}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if colored_px[x, y]:
                composite_px[x, y] = src[x, y]
            elif (x, y) in glyph_points:
                composite_px[x, y] = (255, 255, 255, src[x, y][3])

    # Pillow's RGBa mode is premultiplied-alpha. Resampling it prevents color
    # from transparent source pixels bleeding into the downsampled silhouette.
    fitted = fit_canvas(composite.convert("RGBa"), crop, canvas_size,
                        Image.Resampling.LANCZOS).convert("RGBA")
    fitted_pixels = list(fitted.get_flattened_data())
    cloud_pixels = [(r, g, b) for r, g, b, a in fitted_pixels
                    if a >= 96 and not (min(r, g, b) >= 235)]
    if not cloud_pixels:
        raise ValueError("Codex source has no cloud pixels after scaling")
    strip = Image.new("RGB", (len(cloud_pixels), 1))
    strip.putdata(cloud_pixels)
    quantized = strip.quantize(colors=14, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.NONE)
    palette_raw = quantized.getpalette()
    quantized_pixels = list(quantized.get_flattened_data())
    used = sorted(set(quantized_pixels))
    colors = [(palette_raw[i * 3], palette_raw[i * 3 + 1],
               palette_raw[i * 3 + 2]) for i in used]
    remap = {old: new + 1 for new, old in enumerate(used)}
    color_to_index = {}
    for color, old_index in zip(cloud_pixels, quantized_pixels, strict=True):
        color_to_index[color] = remap[old_index]

    palette = bytearray(16 * 4)
    for index, (r, g, b) in enumerate(colors, start=1):
        palette[index * 4:index * 4 + 4] = bytes((b, g, r, 255))
    palette[15 * 4:16 * 4] = bytes((255, 255, 255, 255))

    indices = []
    for r, g, b, a in fitted_pixels:
        if a < 96:
            indices.append(0)
        elif min(r, g, b) >= 235:
            indices.append(15)
        else:
            indices.append(color_to_index[(r, g, b)])
    packed = bytearray()
    for offset in range(0, len(indices), 2):
        packed.append((indices[offset] << 4) | indices[offset + 1])

    return bytes(palette + packed)


# The Needs You mascot: the shipped Claude pet, cell-accurate to the 16x16
# grid in tools/mockups/needsyou_mascot.py, rasterized at integer pixel scales
# with nearest-neighbor edges. Emitted PRE-COLORED (no runtime recolor, per the
# AMOLED skill invariant): an I4 image whose one non-transparent palette entry
# is the Claude accent. Private dims it with object opacity, never a recolor.
MASCOT_CLAUDE_BGRA = bytes((0x57, 0x77, 0xD9, 255))  # #D97757


def build_mascot(pose: str, cell: int) -> tuple[bytes, int, int]:
    width, height = 16 * cell, 13 * cell
    on = bytearray(width * height)

    def rect(cx: float, cy: float, cw: float, ch: float, value: int) -> None:
        x0, y0 = round(cx * cell), round(cy * cell)
        x1, y1 = round((cx + cw) * cell), round((cy + ch) * cell)
        for y in range(max(0, y0), min(height, y1)):
            base = y * width
            for x in range(max(0, x0), min(width, x1)):
                on[base + x] = value

    # Body, full-width arm band, four legs — the shipped silhouette.
    rect(2, 3, 12, 8, 1)
    rect(0, 7, 16, 2, 1)
    for leg_x in (3, 5, 10, 12):
        rect(leg_x, 11, 1, 2, 1)
    if pose == "asking":
        rect(14, 4, 2, 3, 1)  # one raised arm, the head-tilt look without a tilt

    # Eyes are punched back to transparent, per pose.
    if pose == "neutral":
        rect(4, 5, 1, 2, 0)
        rect(11, 5, 1, 2, 0)
    elif pose == "asking":
        rect(4, 5, 1, 2, 0)
        rect(10.5, 4.5, 2, 2.5, 0)
    elif pose == "alert":
        rect(3.5, 4.5, 2, 2.5, 0)
        rect(10.5, 4.5, 2, 2.5, 0)
    elif pose == "happy":
        for eye_x in (3.2, 10.2):  # delighted ^ ^ chevrons, not sleepy slits
            rect(eye_x, 6.0, 0.9, 0.9, 0)
            rect(eye_x + 0.85, 5.2, 0.9, 0.9, 0)
            rect(eye_x + 1.7, 6.0, 0.9, 0.9, 0)
    else:
        raise ValueError(f"unknown mascot pose {pose!r}")

    palette = bytearray(16 * 4)
    palette[4:8] = MASCOT_CLAUDE_BGRA  # index 1; index 0 stays transparent
    packed = bytearray()
    for offset in range(0, len(on), 2):
        packed.append((on[offset] << 4) | on[offset + 1])
    return bytes(palette + packed), width, height


# "Ready to merge": the pull request icon in assets/source/pull-request.svg
# (24 x 24 viewBox, 1.5 stroke, round caps and joins), drawn here from the
# same geometry so the build needs no SVG renderer. Strokes are stamped as
# discs along the flattened path — exactly a round-capped, round-joined
# stroke — at 8x and box-filtered down to the native size, then stored as
# I4 with fifteen pre-colored alpha steps of the merge green: smooth edges,
# no runtime recolor or scaling.
MERGE_GREEN_RGB = (0x3F, 0xB9, 0x50)  # #3FB950
PR_STROKE = 1.5
# Cropped viewBox (x, y, size): the drawing spans 3.25..20.75 with its stroke;
# the margin keeps the nodes clear of the round icon ring on the card.
PR_VIEW = (1.0, 1.0, 22.0)


def _cubic(p0, p1, p2, p3, steps: int = 48):
    return [tuple((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b +
                  3 * (1 - t) * t * t * c + t ** 3 * d
                  for a, b, c, d in zip(p0, p1, p2, p3, strict=True))
            for t in (i / steps for i in range(steps + 1))]


def _line(p0, p1, steps: int = 32):
    return [(p0[0] + (p1[0] - p0[0]) * i / steps,
             p0[1] + (p1[1] - p0[1]) * i / steps) for i in range(steps + 1)]


def _circle(center, radius: float, steps: int = 96):
    return [(center[0] + radius * math.cos(2 * math.pi * i / steps),
             center[1] + radius * math.sin(2 * math.pi * i / steps))
            for i in range(steps + 1)]


def pull_request_paths():
    corner = (18, 12)
    return [
        _line((6, 8), (6, 16)),
        _line((18, 16), corner)
        + _cubic(corner, (18, 9.17156), (18, 7.75735), (17.1213, 6.87867))
        + _cubic((17.1213, 6.87867), (16.2426, 5.99999), (14.8284, 5.99999),
                 (12, 5.99999))
        + _line((12, 5.99999), (11, 5.99999)),
        _cubic((11, 5.99999), (11, 5.29976), (12.9943, 3.99152),
               (13.5, 3.49999)),
        _cubic((11, 5.99999), (11, 6.70022), (12.9943, 8.00846),
               (13.5, 8.49999)),
        _circle((6, 18), 2),
        _circle((6, 6), 2),
        _circle((18, 18), 2),
    ]


def build_pull_request(size: int, supersample: int = 8) -> bytes:
    x0, y0, view = PR_VIEW
    scale = size * supersample / view
    radius = PR_STROKE / 2 * scale
    big = Image.new("L", (size * supersample, size * supersample), 0)
    draw = ImageDraw.Draw(big)
    for path in pull_request_paths():
        points = [((x - x0) * scale, (y - y0) * scale) for x, y in path]
        for (ax, ay), (bx, by) in pairwise(points):
            steps = max(1, int(math.hypot(bx - ax, by - ay) / (radius / 4)))
            for i in range(steps + 1):
                x = ax + (bx - ax) * i / steps
                y = ay + (by - ay) * i / steps
                draw.ellipse([x - radius, y - radius, x + radius, y + radius],
                             fill=255)
    alpha = big.resize((size, size), Image.Resampling.BOX)
    palette = bytearray(16 * 4)
    for index in range(1, 16):  # index 0 stays fully transparent
        r, g, b = MERGE_GREEN_RGB
        palette[index * 4:index * 4 + 4] = bytes((b, g, r, index * 17))
    levels = [round(value / 17) for value in alpha.tobytes()]
    packed = bytearray()
    for offset in range(0, len(levels), 2):
        packed.append((levels[offset] << 4) | levels[offset + 1])
    return bytes(palette + packed)


MASCOTS = (
    ("tk_img_mascot_asking_4", "asking", 4),
    ("tk_img_mascot_neutral_4", "neutral", 4),
    ("tk_img_mascot_alert_8", "alert", 8),
    ("tk_img_mascot_neutral_7", "neutral", 7),
    ("tk_img_mascot_happy_8", "happy", 8),
)


def c_array(name: str, data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return f"static const uint8_t {name}[] = {{\n" + "\n".join(rows) + "\n};\n"


def descriptor(name: str, data_name: str, color_format: str, stride: int,
               size: int, canvas: int = CANVAS,
               height: int | None = None) -> str:
    return f"""const lv_image_dsc_t {name} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = {color_format},
    .flags = 0,
    .w = {canvas},
    .h = {canvas if height is None else height},
    .stride = {stride},
  }},
  .data_size = {size},
  .data = {data_name},
}};
"""


def render_generated_sources() -> tuple[str, str]:
    claude = build_claude()
    codex = build_codex(112)
    codex_64 = build_codex(64)
    claude_32 = build_claude(32)
    codex_32 = build_codex(32)
    merge = build_pull_request(112)
    merge_32 = build_pull_request(32)
    mascots = [(name, *build_mascot(pose, cell))
               for name, pose, cell in MASCOTS]
    mascot_externs = "".join(
        f"extern const lv_image_dsc_t {name};\n" for name, _, _, _ in mascots)
    header = f"""#ifndef AGENT_ASSETS_H
#define AGENT_ASSETS_H

#include "lvgl.h"

extern const lv_image_dsc_t tk_img_claude;
extern const lv_image_dsc_t tk_img_codex;
extern const lv_image_dsc_t tk_img_codex_64;
extern const lv_image_dsc_t tk_img_claude_32;
extern const lv_image_dsc_t tk_img_codex_32;

/* Merge queue card: the pull request icon, pre-colored merge green. */
extern const lv_image_dsc_t tk_img_merge;
extern const lv_image_dsc_t tk_img_merge_32;

/* Needs You takeover: the Claude pet, one pose per emotive beat, pre-colored
 * at integer pixel scales. */
{mascot_externs}
#endif
"""
    source = "#include \"agent_assets.h\"\n\n"
    source += c_array("tk_img_claude_data", claude)
    source += c_array("tk_img_codex_data", codex)
    source += c_array("tk_img_codex_64_data", codex_64)
    source += c_array("tk_img_claude_32_data", claude_32)
    source += c_array("tk_img_codex_32_data", codex_32)
    source += c_array("tk_img_merge_data", merge)
    source += c_array("tk_img_merge_32_data", merge_32)
    for name, data, _, _ in mascots:
        source += c_array(f"{name}_data", data)
    source += "\n"
    source += descriptor("tk_img_claude", "tk_img_claude_data",
                         "LV_COLOR_FORMAT_A8", CANVAS, len(claude))
    source += descriptor("tk_img_codex", "tk_img_codex_data",
                         "LV_COLOR_FORMAT_I4", 112 // 2, len(codex), 112)
    source += descriptor("tk_img_codex_64", "tk_img_codex_64_data",
                         "LV_COLOR_FORMAT_I4", 64 // 2, len(codex_64), 64)
    source += descriptor("tk_img_claude_32", "tk_img_claude_32_data",
                         "LV_COLOR_FORMAT_A8", 32, len(claude_32), 32)
    source += descriptor("tk_img_codex_32", "tk_img_codex_32_data",
                         "LV_COLOR_FORMAT_I4", 16, len(codex_32), 32)
    source += descriptor("tk_img_merge", "tk_img_merge_data",
                         "LV_COLOR_FORMAT_I4", 112 // 2, len(merge), 112)
    source += descriptor("tk_img_merge_32", "tk_img_merge_32_data",
                         "LV_COLOR_FORMAT_I4", 32 // 2, len(merge_32), 32)
    for name, data, width, height in mascots:
        source += descriptor(name, f"{name}_data", "LV_COLOR_FORMAT_I4",
                             width // 2, len(data), width, height)
    return header, source


def main() -> None:
    header, source = render_generated_sources()
    OUT_H.write_text(header, encoding="utf-8")
    OUT_C.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
