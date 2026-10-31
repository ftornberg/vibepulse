#!/usr/bin/env python3
"""The OTA pusher must report what the panel answered, not what it hoped for.

2026-10-07: three uploads in a row ended in HTTP 408 `upload interrupted`, and
tools/ota-flash.sh printed its "202 = avbilden vald" line and exited 0 each
time. This runs the real script against a local stand-in for the panel and
pins the exit code and the message for a success and for each failure seen."""

import http.server
import os
import shutil
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOKEN = "ab" * 32
VERSION = b"v9.9.9-1-g0123abc"


class Panel(http.server.BaseHTTPRequestHandler):
    upload_status = 202
    upload_body = b'{"sha256":"x","version":"v9.9.9-1-g0123abc"}'

    def log_message(self, *args):
        pass

    def do_GET(self):
        body = b'{"maintenance_open":true}'
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        self.send_response(type(self).upload_status)
        self.send_header("Content-Length", str(len(type(self).upload_body)))
        self.end_headers()
        self.wfile.write(type(self).upload_body)


class SenderResultTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Panel)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.tree = Path(tempfile.mkdtemp(prefix="torget-sender-"))
        (cls.tree / "tools").mkdir()
        shutil.copy(ROOT / "tools" / "ota-flash.sh", cls.tree / "tools")
        (cls.tree / "secrets.h").write_text(
            f'#define TG_OTA_TOKEN "{TOKEN}"\n', encoding="utf-8")
        (cls.tree / "build").mkdir()
        image = bytearray(4096)
        image[48:48 + len(VERSION)] = VERSION
        (cls.tree / "build" / "torget.bin").write_bytes(image)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        shutil.rmtree(cls.tree, ignore_errors=True)

    def send(self, status, body):
        Panel.upload_status = status
        Panel.upload_body = body
        host = f"127.0.0.1:{self.server.server_address[1]}"
        return subprocess.run(
            ["sh", str(self.tree / "tools" / "ota-flash.sh"), host, "build"],
            env={**os.environ, "TG_OTA_ALLOW_NO_CI": "1"},
            capture_output=True, text=True, timeout=60)

    def test_accepted_image_is_a_success(self):
        run = self.send(202, b'{"sha256":"x","version":"v9.9.9-1-g0123abc"}')
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("HTTP 202", run.stdout)
        self.assertIn("avbilden vald", run.stdout)
        self.assertNotIn("MISSLYCKADES", run.stderr)

    def test_interrupted_upload_fails_and_never_claims_the_image_was_chosen(self):
        run = self.send(408, b'{"error":"upload interrupted"}')
        self.assertNotEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("upload interrupted", run.stdout)
        self.assertIn("HTTP 408", run.stdout)
        self.assertNotIn("avbilden vald", run.stdout + run.stderr)
        self.assertIn("MISSLYCKADES", run.stderr)
        self.assertIn("408 = ", run.stderr)

    def test_other_refusals_fail_too(self):
        for status in (400, 401, 403, 500):
            with self.subTest(status=status):
                run = self.send(status, b'{"error":"no"}')
                self.assertNotEqual(run.returncode, 0)
                self.assertNotIn("avbilden vald", run.stdout + run.stderr)
                self.assertIn(f"HTTP {status}", run.stdout)


if __name__ == "__main__":
    unittest.main()
