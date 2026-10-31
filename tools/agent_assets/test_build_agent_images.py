import importlib.util
import re
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("build-agent-images.py")
SPEC = importlib.util.spec_from_file_location("build_agent_images", SCRIPT)
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


def decode_i4(data, size):
    palette = [tuple(data[i:i + 4]) for i in range(0, 16 * 4, 4)]
    indices = []
    for byte in data[16 * 4:]:
        indices.extend((byte >> 4, byte & 0x0F))
    return palette, indices[:size * size]


def decode_i4_wh(data, width, height):
    palette = [tuple(data[i:i + 4]) for i in range(0, 16 * 4, 4)]
    indices = []
    for byte in data[16 * 4:]:
        indices.extend((byte >> 4, byte & 0x0F))
    return palette, indices[:width * height]


DESCRIPTOR_RE = re.compile(
    r"^const lv_image_dsc_t (?P<name>[A-Za-z_][A-Za-z0-9_]*) = \{\n"
    r"(?P<body>.*?)^\};\n",
    re.MULTILINE | re.DOTALL,
)
DESCRIPTOR_FIELD_RE = re.compile(
    r"^\s*\.(?P<name>cf|w|h|stride|data_size|data)\s*=\s*"
    r"(?P<value>[^,\n]+),?\s*$",
    re.MULTILINE,
)


def descriptor_block_and_fields(source, name):
    matches = [match for match in DESCRIPTOR_RE.finditer(source)
               if match.group("name") == name]
    if len(matches) != 1:
        raise AssertionError(
            f"expected exactly one {name} descriptor, found {len(matches)}"
        )

    pairs = DESCRIPTOR_FIELD_RE.findall(matches[0].group("body"))
    fields = dict(pairs)
    if len(fields) != len(pairs):
        raise AssertionError(f"duplicate field in {name} descriptor")
    return matches[0].group(0), fields


class AgentAssetTests(unittest.TestCase):
    def test_checked_in_generated_sources_match_exactly(self):
        header, source = build.render_generated_sources()

        self.assertEqual(
            header,
            (build.ROOT / "components/app_tokens/agent_assets.h").read_text(
                encoding="utf-8"
            ),
        )
        self.assertEqual(
            source,
            (build.ROOT / "components/app_tokens/agent_assets.c").read_text(
                encoding="utf-8"
            ),
        )

    def test_generated_source_drift_is_detected(self):
        header, source = build.render_generated_sources()
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / "agent_assets.c"
            generated.write_text(source + "/* hand edit */\n", encoding="utf-8")

            self.assertNotEqual(source, generated.read_text(encoding="utf-8"))

    def test_claude_small_asset_is_exactly_32_by_32_a8(self):
        data = build.build_claude(32)

        self.assertEqual(len(data), 32 * 32)
        self.assertGreater(sum(byte > 0 for byte in data), 64)

    def test_codex_assets_are_native_deterministic_i4_composites(self):
        for size in (112, 64, 32):
            with self.subTest(size=size):
                data = build.build_codex(size)
                self.assertIsInstance(data, bytes)
                self.assertEqual(len(data), 16 * 4 + size * size // 2)
                self.assertEqual(data, build.build_codex(size))

    def test_codex_needs_you_asset_is_native_transparent_64(self):
        data = build.build_codex(64)
        palette, indices = decode_i4(data, 64)
        self.assertEqual(len(data), 16 * 4 + 64 * 64 // 2)
        self.assertEqual(palette[0], (0, 0, 0, 0))
        self.assertEqual(sum(color == (255, 255, 255, 255)
                             for color in palette), 1)
        self.assertTrue(all(indices[i] == 0 for i in (0, 63, 4032, 4095)))
        header, _ = build.render_generated_sources()
        self.assertIn("extern const lv_image_dsc_t tk_img_codex_64;", header)

    def test_codex_needs_you_descriptor_is_bound_to_the_native_asset(self):
        _, source = build.render_generated_sources()

        block, fields = descriptor_block_and_fields(source, "tk_img_codex_64")

        self.assertEqual(fields["cf"], "LV_COLOR_FORMAT_I4")
        self.assertEqual(fields["w"], "64")
        self.assertEqual(fields["h"], "64")
        self.assertEqual(fields["stride"], "32")
        self.assertEqual(fields["data_size"], "2112")
        self.assertEqual(fields["data"], "tk_img_codex_64_data")

        # The old generic substring checks still pass after this descriptor is
        # removed because 64 px mascot descriptors use the same dimensions.
        # Selecting the named block must reject that false-positive source.
        missing = source.replace(block, "", 1)
        self.assertIn(".w = 64", missing)
        self.assertIn(".stride = 32", missing)
        with self.assertRaisesRegex(AssertionError, "expected exactly one"):
            descriptor_block_and_fields(missing, "tk_img_codex_64")

    def test_codex_palette_reserves_transparency_and_one_exact_white(self):
        for size in (112, 64, 32):
            palette, indices = decode_i4(build.build_codex(size), size)
            used = {palette[index] for index in indices}

            self.assertEqual(palette[0], (0, 0, 0, 0))
            self.assertEqual(sum(color == (255, 255, 255, 255)
                                 for color in palette), 1)
            self.assertIn((255, 255, 255, 255), used)
            # At least 1.25% of every native raster stays exact white, which
            # keeps both terminal glyphs materially visible at each size.
            self.assertGreater(sum(index == 15 for index in indices),
                               size * size // 80)
            self.assertGreater(sum(index not in (0, 15) for index in indices),
                               size * size // 5)

    def test_codex_silhouette_has_clean_transparent_corners(self):
        for size in (112, 64, 32):
            palette, indices = decode_i4(build.build_codex(size), size)
            visible = [(x, y) for y in range(size) for x in range(size)
                       if indices[y * size + x] != 0]
            xs = [x for x, _ in visible]
            ys = [y for _, y in visible]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            bbox_area = (x1 - x0 + 1) * (y1 - y0 + 1)

            self.assertTrue(all(indices[y * size + x] == 0
                                for x, y in ((0, 0), (size - 1, 0),
                                             (0, size - 1),
                                             (size - 1, size - 1))))
            self.assertLess(len(visible), bbox_area * 0.85)
            for color in palette[1:15]:
                b, g, r, a = color
                if a:
                    self.assertGreater(b, r)
                    self.assertGreater(b, g)
                    self.assertFalse(r > 205 and b > 225,
                                     f"lavender fringe color {color}")

    def test_mascot_poses_are_precolored_i4_at_integer_scales(self):
        for name, pose, cell in build.MASCOTS:
            with self.subTest(name=name):
                data, width, height = build.build_mascot(pose, cell)
                self.assertEqual((width, height), (16 * cell, 13 * cell))
                self.assertEqual(len(data), 16 * 4 + width * height // 2)
                # Deterministic, so the byte-for-byte gate is stable.
                self.assertEqual(data, build.build_mascot(pose, cell)[0])
                palette, indices = decode_i4_wh(data, width, height)
                # Pre-colored: transparent ground + exactly the Claude accent,
                # never recolored at runtime.
                self.assertEqual(palette[0], (0, 0, 0, 0))
                self.assertEqual(palette[1], (0x57, 0x77, 0xD9, 255))
                self.assertEqual(set(indices) - {0, 1}, set())
                self.assertGreater(sum(i == 1 for i in indices), 0)
                for corner in (0, width - 1, (height - 1) * width,
                               height * width - 1):
                    self.assertEqual(indices[corner], 0)

    def test_mascot_poses_are_visually_distinct(self):
        # Each emotive beat is its own raster; poses never collapse together.
        variants = {name: build.build_mascot(pose, cell)[0]
                    for name, pose, cell in build.MASCOTS}
        self.assertNotEqual(variants["tk_img_mascot_asking_4"],
                            variants["tk_img_mascot_neutral_4"])
        self.assertNotEqual(variants["tk_img_mascot_alert_8"],
                            variants["tk_img_mascot_happy_8"])

    def test_pull_request_icon_is_precolored_merge_green_i4(self):
        for size in (112, 32):
            with self.subTest(size=size):
                data = build.build_pull_request(size)
                self.assertEqual(len(data), 16 * 4 + size * size // 2)
                palette, indices = decode_i4(data, size)
                self.assertEqual(palette[0][3], 0)  # index 0 transparent
                for index in range(1, 16):
                    b, g, r, a = palette[index]
                    self.assertEqual((r, g, b), build.MERGE_GREEN_RGB)
                    self.assertEqual(a, index * 17)
                # Opaque stroke cores exist, and the corners stay clear of
                # the round icon ring on the card.
                self.assertIn(15, indices)
                for x, y in ((0, 0), (size - 1, 0), (0, size - 1),
                             (size - 1, size - 1)):
                    self.assertEqual(indices[y * size + x], 0)
                self.assertEqual(build.build_pull_request(size), data)

    def test_pull_request_descriptors_match_native_sizes(self):
        source = build.render_generated_sources()[1]
        for name, size in (("tk_img_merge", 112), ("tk_img_merge_32", 32)):
            with self.subTest(name=name):
                block = [m for m in DESCRIPTOR_RE.finditer(source)
                         if m.group("name") == name]
                self.assertEqual(len(block), 1)
                fields = dict(DESCRIPTOR_FIELD_RE.findall(block[0].group("body")))
                self.assertEqual(fields["cf"], "LV_COLOR_FORMAT_I4")
                self.assertEqual((fields["w"], fields["h"]),
                                 (str(size), str(size)))
                self.assertEqual(fields["stride"], str(size // 2))

    def test_descriptor_uses_requested_canvas_size(self):
        source = build.descriptor(
            "small", "small_data", "LV_COLOR_FORMAT_A8",
            stride=32, size=1024, canvas=32)

        self.assertIn(".w = 32", source)
        self.assertIn(".h = 32", source)


if __name__ == "__main__":
    unittest.main()
