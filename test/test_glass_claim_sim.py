"""Needs You reaches the glass when VibePulse is not in front, and leaves it.

The takeover is painted inside VibePulse's own page tree, so with another app
(or the launcher) in front it used to render into a hidden root and time out
unseen. The platform's glass claim now brings VibePulse forward while the
takeover is visible and returns afterwards. This drives the real renderer."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class GlassClaimSimTests(unittest.TestCase):
    def test_alert_comes_forward_and_the_launcher_returns(self):
        subprocess.run(["cmake", "-S", "sim", "-B", "sim/build", "-G", "Ninja"],
                       cwd=ROOT, check=True, capture_output=True)
        subprocess.run(["cmake", "--build", "sim/build"], cwd=ROOT,
                       check=True, capture_output=True)
        with tempfile.TemporaryDirectory(prefix="vp-claim-") as temporary:
            env = dict(os.environ, TORGET_CAPTURE_DIR=temporary)
            run = subprocess.run([str(ROOT / "sim/build/torget-sim"),
                                  "--glass-claim-qa"],
                                 env=env, capture_output=True, text=True,
                                 timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            frames = {}
            for tag in ("before", "alert", "payoff", "returned"):
                with Image.open(Path(temporary) / f"torget-glass-claim-{tag}.bmp") as im:
                    im = im.convert("RGB")
                    self.assertEqual(im.size, (480, 480))
                    frames[tag] = hashlib.sha256(im.tobytes()).hexdigest()
            self.assertNotEqual(frames["alert"], frames["before"],
                                "the Needs You takeover did not reach the glass")
            self.assertNotEqual(frames["payoff"], frames["before"],
                                "the payoff beat must still own the glass")
            self.assertEqual(frames["returned"], frames["before"],
                             "after the answer the launcher must come back")

            # The waiting pulse (Claude finished and waits for you) claims the
            # glass for its 45 s pulse, then hands it back; the static card
            # stays inside VibePulse (2026-09-30, owner).
            pulse = {}
            for tag in ("before", "alert", "held", "returned"):
                with Image.open(Path(temporary) / f"torget-glass-pulse-{tag}.bmp") as im:
                    im = im.convert("RGB")
                    self.assertEqual(im.size, (480, 480))
                    pulse[tag] = hashlib.sha256(im.tobytes()).hexdigest()
            self.assertNotEqual(pulse["alert"], pulse["before"],
                                "the waiting pulse did not reach the glass")
            self.assertNotEqual(pulse["held"], pulse["before"],
                                "the pulse must hold the glass for its 45 s")
            self.assertEqual(pulse["returned"], pulse["before"],
                             "after the 45 s pulse the launcher must come back")

            # Ready to merge borrows the glass the same way, in green, and
            # hands it back at 45 s with no animation left running.
            merge = {}
            for tag in ("before", "alert", "held", "returned"):
                with Image.open(Path(temporary) / f"torget-glass-merge-{tag}.bmp") as im:
                    im = im.convert("RGB")
                    self.assertEqual(im.size, (480, 480))
                    merge[tag] = hashlib.sha256(im.tobytes()).hexdigest()
                    if tag == "alert":
                        self.assertEqual(im.getpixel((240, 10)), (63, 185, 80))
            self.assertNotEqual(merge["alert"], merge["before"],
                                "the merge card did not reach the glass")
            self.assertNotEqual(merge["held"], merge["before"],
                                "the merge pulse must hold the glass for 45 s")
            self.assertEqual(merge["returned"], merge["before"],
                             "after the merge pulse the launcher must return")


if __name__ == "__main__":
    unittest.main()
