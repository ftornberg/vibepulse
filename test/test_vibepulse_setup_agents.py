"""`vibepulse_setup.py agents`: the agent-monitor ignore list.

Run directly (python test/test_vibepulse_setup_agents.py), like the other
setup tests in test/run.sh.
"""
from __future__ import annotations

import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]


def _load_setup():
    spec = importlib.util.spec_from_file_location(
        "vibepulse_setup", REPO_ROOT / "tools" / "vibepulse_setup.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["vibepulse_setup"] = module
    spec.loader.exec_module(module)
    return module


setup = _load_setup()


def _no_run(argv, **kwargs):
    raise AssertionError(f"agents must not run commands: {argv!r}")


class AgentsIgnoreList(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.config = Path(self._tmp.name) / "config.json"
        self.addCleanup(self._tmp.cleanup)

    def _run(self, *argv):
        out = io.StringIO()
        code = setup.main(
            list(argv), config_path=self.config, codex=None, run=_no_run,
            stdout=out, stdin_isatty=False)
        return code, out.getvalue()

    def _saved(self):
        return json.loads(self.config.read_text())

    def test_ignore_saves_and_keeps_every_other_switch(self):
        self.config.write_text(json.dumps({
            "claude_interactions": True, "interaction_detail": True}))

        code, text = self._run("agents", "ignore",
                               "/agent-team-orchestrator/work/")

        self.assertEqual(code, 0, text)
        self.assertIn("Restart the tokenserver", text)
        saved = self._saved()
        self.assertEqual(saved["agent_status_ignore"],
                         ["/agent-team-orchestrator/work/"])
        self.assertTrue(saved["claude_interactions"])
        self.assertTrue(saved["interaction_detail"])

    def test_ignore_twice_is_idempotent(self):
        self._run("agents", "ignore", "/agent-team-orchestrator/work/")
        code, text = self._run("agents", "ignore",
                               "/agent-team-orchestrator/work/")
        self.assertEqual(code, 0, text)
        self.assertEqual(self._saved()["agent_status_ignore"],
                         ["/agent-team-orchestrator/work/"])

    def test_unignore_removes_and_unknown_fails(self):
        self._run("agents", "ignore", "/agent-team-orchestrator/work/")
        code, _ = self._run("agents", "unignore",
                            "/agent-team-orchestrator/work/")
        self.assertEqual(code, 0)
        self.assertEqual(self._saved()["agent_status_ignore"], [])
        code, text = self._run("agents", "unignore", "/nope/")
        self.assertEqual(code, 1)
        self.assertIn("FIX", text)

    def test_too_broad_path_is_refused_and_nothing_saved(self):
        code, text = self._run("agents", "ignore", "/")
        self.assertEqual(code, 1)
        self.assertIn("FIX", text)
        self.assertFalse(self.config.exists())

    def test_list_and_status_show_the_entries(self):
        code, text = self._run("agents", "list")
        self.assertEqual(code, 0)
        self.assertIn("No ignored paths", text)
        self._run("agents", "ignore", "/agent-team-orchestrator/work/")
        _, text = self._run("agents", "list")
        self.assertEqual(text.strip(), "/agent-team-orchestrator/work/")
        _, text = self._run("status")
        self.assertIn("Agent monitor ignores: 1 path(s)", text)


class MergeQueueSources(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.config = Path(self._tmp.name) / "config.json"
        self.addCleanup(self._tmp.cleanup)

    def _run(self, *argv):
        out = io.StringIO()
        code = setup.main(
            list(argv), config_path=self.config, codex=None, run=_no_run,
            stdout=out, stdin_isatty=False)
        return code, out.getvalue()

    def test_add_normalizes_a_port_and_keeps_other_settings(self):
        self._run("agents", "ignore", "/agent-team-orchestrator/work/")
        code, text = self._run("merge-queue", "add", "4401")
        self.assertEqual(code, 0, text)
        saved = json.loads(self.config.read_text())
        self.assertEqual(saved["merge_queue_sources"],
                         ["http://127.0.0.1:4401"])
        self.assertEqual(saved["agent_status_ignore"],
                         ["/agent-team-orchestrator/work/"])
        # and the agents command keeps the sources in turn
        self._run("agents", "unignore", "/agent-team-orchestrator/work/")
        self.assertEqual(json.loads(self.config.read_text())[
            "merge_queue_sources"], ["http://127.0.0.1:4401"])

    def test_non_loopback_is_refused_and_nothing_saved(self):
        code, text = self._run("merge-queue", "add", "http://192.168.1.2:4401")
        self.assertEqual(code, 1)
        self.assertIn("FIX", text)
        self.assertFalse(self.config.exists())

    def test_remove_list_and_status(self):
        self._run("merge-queue", "add", "http://localhost:4400")
        _, text = self._run("merge-queue", "list")
        self.assertEqual(text.strip(), "http://127.0.0.1:4400")
        _, text = self._run("status")
        self.assertIn("Merge queue: 1 source(s)", text)
        code, _ = self._run("merge-queue", "remove", "4400")
        self.assertEqual(code, 0)
        _, text = self._run("merge-queue", "list")
        self.assertIn("No merge-queue sources", text)


if __name__ == "__main__":
    unittest.main()
