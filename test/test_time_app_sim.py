"""TID's LVGL half through the real shared renderer.

TID is opt-in, so the default simulator never compiles it. Build a TID variant
in its own directory (CMake caches the option; sim/build is shared with the
other renderer tests and must stay VibePulse-only) and run its captures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BUILD = "sim/build-time"
FRAMES = [
    "clock", "clock-unset",
    "pomodoro-idle", "pomodoro-running", "pomodoro-paused", "pomodoro-done",
    "done-over-clock", "pomodoro-break",
    "timer-select", "timer-running", "timer-done",
    "clock-attention", "clock-sound-off",
]


class TimeAppSimTests(unittest.TestCase):
    def test_every_state_renders_at_native_size(self):
        configure = ["cmake", "-S", "sim", "-B", BUILD, "-G", "Ninja",
                     "-DTORGET_WITH_TIME=ON"]
        # Reuse the LVGL checkout the default simulator already fetched (the
        # renderer tests above build sim/build first) instead of downloading a
        # second copy; a cold tree simply falls back to FetchContent.
        shared_lvgl = ROOT / "sim/build/_deps/lvgl-src"
        if shared_lvgl.is_dir():
            configure.append(f"-DFETCHCONTENT_SOURCE_DIR_LVGL={shared_lvgl}")
        subprocess.run(configure, cwd=ROOT, check=True, capture_output=True)
        subprocess.run(["cmake", "--build", BUILD], cwd=ROOT,
                       check=True, capture_output=True)
        with tempfile.TemporaryDirectory(prefix="vp-time-") as temporary:
            env = dict(os.environ, TORGET_CAPTURE_DIR=temporary)
            run = subprocess.run([str(ROOT / BUILD / "torget-sim"),
                                  "--time-app-captures"],
                                 env=env, capture_output=True, text=True,
                                 timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            images = {}
            for tag in FRAMES:
                with self.subTest(frame=tag):
                    with Image.open(Path(temporary) / f"torget-time-{tag}.bmp") as im:
                        im = im.convert("RGB")
                        self.assertEqual(im.size, (480, 480))
                        lit = sum(p != (0, 0, 0) for p in im.get_flattened_data())
                        self.assertGreater(lit, 500, "frame is blank")
                        images[tag] = im.tobytes()
            self.assertNotEqual(images["clock"], images["clock-unset"],
                                "an unset clock must not look like a set one")
            self.assertNotEqual(images["clock-attention"], images["clock"],
                                "Claude waiting must show its icon on the clock")
            self.assertNotEqual(images["clock-sound-off"], images["clock"],
                                "sound off must show a crossed speaker")
            # A timer that ends while VibePulse is in front must still chime.
            out = run.stdout
            marker = out.find("qa: hidden-timer")
            self.assertGreaterEqual(marker, 0, out)
            over = out.find("qa: break-over")
            self.assertGreaterEqual(over, 0, out)
            # A break that ends has its own cue, and never the DONE chime too.
            self.assertEqual(out[over:].count("audio: BREAK_OVER"), 1, out[over:])
            self.assertNotIn("audio: DONE", out[over:])
            self.assertNotIn("audio: BREAK_OVER", out[:over])
            self.assertEqual(out[marker:].count("audio: DONE"), 1,
                             "a TID timer ending behind VibePulse must chime "
                             "exactly once, also across the return to TID")
            # Turning sound back on plays the chime once as a confirmation.
            on = out.find("qa: sound-on")
            self.assertGreaterEqual(on, 0, out)
            self.assertIn("audio: DONE", out[on:marker])


if __name__ == "__main__":
    unittest.main()
