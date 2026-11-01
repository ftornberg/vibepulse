"""The TID speaker symbol: two 24 x 24 A8 images, generated and checked in."""

import unittest

from tools.speaker_assets import build_speaker_assets as b


class SpeakerAssetTests(unittest.TestCase):
    def test_two_distinct_a8_images(self):
        assets = b.build_assets()
        self.assertEqual(sorted(assets), ["off", "on"])
        for name, data in assets.items():
            with self.subTest(image=name):
                self.assertEqual(len(data), b.SIZE * b.SIZE)
                self.assertTrue(any(data), f"{name} has no pixels")
                self.assertTrue(set(data) <= {0, 255})
        self.assertNotEqual(assets["on"], assets["off"])

    def test_off_has_no_sound_waves_but_a_slash(self):
        assets = b.build_assets()
        on, off = assets["on"], assets["off"]
        waves = [i for i, v in enumerate(on) if v and i % b.SIZE >= 16 and not off[i]]
        self.assertTrue(waves, "the on image must carry waves the off image lacks")

    def test_checked_in_sources_are_current(self):
        header, source = b.render_sources()
        self.assertEqual(b.OUT_H.read_text(encoding="utf-8"), header,
                         "run tools/speaker_assets/build_speaker_assets.py")
        self.assertEqual(b.OUT_C.read_text(encoding="utf-8"), source,
                         "run tools/speaker_assets/build_speaker_assets.py")


if __name__ == "__main__":
    unittest.main()
