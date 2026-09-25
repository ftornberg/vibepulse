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


if __name__ == "__main__":
    unittest.main()
