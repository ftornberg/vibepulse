import io
import json
import unittest

from tools.tokenserver.merge_queue import (
    PUBLIC_PAYLOAD_BYTES,
    MergeQueueMonitor,
    disabled_snapshot,
    payload_bytes,
    normalize_source,
    parse_source_payload,
)


class _Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def opener_for(answers):
    """answers: port -> dict (JSON answer) or Exception."""
    calls = []

    def opener(request, timeout):
        calls.append(request.full_url)
        port = int(request.full_url.split(":")[2].split("/")[0])
        answer = answers[port]
        if isinstance(answer, Exception):
            raise answer
        return _Response(json.dumps(answer).encode())

    opener.calls = calls
    return opener


def answer(repo, *prs, paused=False, updated="2026-10-06T19:00:00.000Z"):
    return {"repo": repo, "paused": paused, "updatedAt": updated,
            "prs": [{"number": n, "title": t, "url": "u", "headRef": "h"}
                    for n, t in prs]}


class NormalizeSourceTests(unittest.TestCase):
    def test_port_and_loopback_forms(self):
        for value in ("4401", "http://127.0.0.1:4401", "http://localhost:4401",
                      " http://127.0.0.1:4401/ "):
            self.assertEqual(normalize_source(value), "http://127.0.0.1:4401")

    def test_rejects_anything_not_loopback(self):
        for value in ("", "0", "70000", "http://192.168.1.2:4401",
                      "https://127.0.0.1:4401", "http://127.0.0.1:4401/api",
                      "http://evil.example:4401", "file:///etc/passwd"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                normalize_source(value)


class ParseTests(unittest.TestCase):
    def test_project_is_repo_basename_and_titles_are_bounded(self):
        parsed = parse_source_payload(answer(
            "ftornberg/kvitt", (134, "x" * 200), (12, "  rad\nbrytning ")))
        self.assertEqual(parsed["project"], "kvitt")
        self.assertEqual(len(parsed["prs"][0]["title"]), 80)
        self.assertTrue(parsed["prs"][0]["title"].endswith("…"))
        self.assertEqual(parsed["prs"][1]["title"], "rad brytning")

    def test_malformed_answers_raise(self):
        for payload in ([], {"repo": "bad"}, {"repo": "o/r", "prs": {}},
                        {"repo": "o/r", "prs": [{"number": "1"}]},
                        {"repo": "o/r", "prs": [{"number": True}]},
                        {"repo": "o/r", "prs": [{"number": 0}]}):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                parse_source_payload(payload)


class MonitorTests(unittest.TestCase):
    def test_aggregates_sorted_and_counts_all(self):
        opener = opener_for({
            4401: answer("ftornberg/kvitt", (134, "feat: a"), (12, "fix: b")),
            4402: answer("ftornberg/gaffel", (7, "chore: c"), paused=True),
        })
        monitor = MergeQueueMonitor(["4401", "4402"], opener=opener)
        self.assertEqual(monitor.poll_once(), 2)
        snap = monitor.snapshot()
        self.assertEqual(snap["count"], 3)
        self.assertFalse(snap["incomplete"])
        self.assertEqual([(p["project"], p["number"]) for p in snap["prs"]],
                         [("gaffel", 7), ("kvitt", 12), ("kvitt", 134)])
        self.assertEqual(snap["sources"][1],
                         {"project": "gaffel", "up": True, "paused": True})
        self.assertEqual(opener.calls[0],
                         "http://127.0.0.1:4401/api/merge-queue")

    def test_down_source_is_left_out_and_flagged_not_zeroed(self):
        opener = opener_for({
            4401: answer("ftornberg/kvitt", (134, "feat: a")),
            4402: ConnectionRefusedError(61, "refused"),
        })
        monitor = MergeQueueMonitor(["4401", "4402"], opener=opener)
        self.assertEqual(monitor.poll_once(), 1)
        snap = monitor.snapshot()
        self.assertEqual(snap["count"], 1)
        self.assertTrue(snap["incomplete"])
        self.assertEqual(snap["sources"][1],
                         {"project": None, "up": False, "paused": False})

    def test_source_that_has_not_read_its_prs_yet_is_not_up(self):
        opener = opener_for({4401: answer("o/kvitt", (1, "t"), updated=None)})
        monitor = MergeQueueMonitor(["4401"], opener=opener)
        monitor.poll_once()
        snap = monitor.snapshot()
        self.assertEqual(snap["count"], 0)
        self.assertTrue(snap["incomplete"])

    def test_before_first_poll_is_incomplete(self):
        snap = MergeQueueMonitor(["4401"], opener=opener_for({})).snapshot()
        self.assertTrue(snap["incomplete"])
        self.assertEqual(snap["count"], 0)

    def test_list_is_capped_but_count_is_not(self):
        opener = opener_for({4401: answer(
            "o/kvitt", *((n, f"pr {n}") for n in range(1, 13)))})
        monitor = MergeQueueMonitor(["4401"], opener=opener)
        monitor.poll_once()
        snap = monitor.snapshot()
        self.assertEqual(snap["count"], 12)
        self.assertEqual(len(snap["prs"]), 8)

    def test_fullest_answer_fits_the_panel_buffer(self):
        # Sixteen sources (the configuration's limit), 100-character names,
        # eight pull requests with titles that escape to 12 bytes a glyph.
        ports = [str(4400 + i) for i in range(16)]
        answers = {}
        for i, port in enumerate(ports):
            name = f"{i:02d}" + "r" * 98
            answers[int(port)] = answer(
                f"o/{name}",
                *((2_147_483_000 + n, "\U0001F680" * 80)
                  for n in range(1, 9)))
        monitor = MergeQueueMonitor(ports, opener=opener_for(answers))
        monitor.poll_once()
        snap = monitor.snapshot()
        self.assertEqual(snap["count"], 128)
        self.assertEqual(len(snap["prs"]), 8)
        self.assertLessEqual(payload_bytes(snap), PUBLIC_PAYLOAD_BYTES)
        self.assertLess(PUBLIC_PAYLOAD_BYTES, 4096)

    def test_titles_give_way_last_first(self):
        # Escaped titles that only just overflow: the first ones survive.
        opener = opener_for({4401: answer(
            "o/kvitt", *((n, "ö" * 80) for n in range(1, 9)))})
        monitor = MergeQueueMonitor(["4401"], opener=opener)
        monitor.poll_once()
        snap = monitor.snapshot()
        titles = [pr["title"] for pr in snap["prs"]]
        self.assertLessEqual(payload_bytes(snap), PUBLIC_PAYLOAD_BYTES)
        self.assertIsNotNone(titles[0])
        self.assertIsNone(titles[-1])
        kept = [t is not None for t in titles]
        self.assertEqual(kept, sorted(kept, reverse=True))

    def test_small_answer_keeps_every_title(self):
        opener = opener_for({4401: answer("o/kvitt", (1, "feat: a"))})
        monitor = MergeQueueMonitor(["4401"], opener=opener)
        monitor.poll_once()
        self.assertEqual(monitor.snapshot()["prs"][0]["title"], "feat: a")

    def test_disabled_snapshot(self):
        self.assertEqual(disabled_snapshot(), {"v": 1, "enabled": False})


if __name__ == "__main__":
    unittest.main()
