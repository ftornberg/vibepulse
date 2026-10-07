import dataclasses
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.tokenserver import vibepulse_config as config_module
from tools.tokenserver.vibepulse_config import (
    ConfigError,
    VibePulseConfig,
    claude_project_key,
    config_lock,
    load,
    load_config,
    save,
    save_config,
)


class SavedConfigTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="vibepulse-config-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "state" / "config.json"

    def test_missing_file_is_fully_off_and_immutable(self):
        config = load_config(self.path)

        self.assertEqual(config, VibePulseConfig())
        self.assertFalse(config.claude_interactions)
        self.assertFalse(config.codex_interactions)
        self.assertFalse(config.interaction_detail)
        self.assertFalse(config.legacy_claude_panel_v1)
        self.assertFalse(config.interaction_relay)
        self.assertFalse(config.agent_status_relay)
        self.assertIsNone(config.interaction_relay_url)
        self.assertIsNone(config.interaction_mailbox)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            config.codex_interactions = True

    def test_direct_construction_rejects_every_non_boolean_field(self):
        for field in (
                "claude_interactions", "codex_interactions",
                "interaction_detail", "legacy_claude_panel_v1",
                "interaction_relay", "agent_status_relay"):
            for value in (0, 1, "yes", None):
                values = {field: value}
                with self.subTest(field=field, value=value):
                    with self.assertRaises(ConfigError):
                        VibePulseConfig(**values)

    def test_save_revalidates_even_a_forged_frozen_instance(self):
        forged = object.__new__(VibePulseConfig)
        object.__setattr__(forged, "claude_interactions", False)
        object.__setattr__(forged, "codex_interactions", 1)
        object.__setattr__(forged, "interaction_detail", False)
        object.__setattr__(forged, "legacy_claude_panel_v1", False)
        object.__setattr__(forged, "interaction_relay", False)
        object.__setattr__(forged, "agent_status_relay", False)
        object.__setattr__(forged, "interaction_relay_url", None)
        object.__setattr__(forged, "interaction_mailbox", None)

        with self.assertRaises(ConfigError):
            save_config(self.path, forged)

        self.assertFalse(self.path.exists())

    def test_valid_partial_and_complete_files_load_strict_booleans(self):
        self.path.parent.mkdir()
        self.path.write_text(
            '{"codex_interactions":true}', encoding="utf-8")
        self.assertEqual(load(self.path), VibePulseConfig(
            codex_interactions=True))

        self.path.write_text(json.dumps({
            "claude_interactions": True,
            "codex_interactions": False,
            "interaction_detail": True,
        }), encoding="utf-8")
        self.assertEqual(load_config(self.path), VibePulseConfig(
            claude_interactions=True, interaction_detail=True))

        self.path.write_text(
            '{"legacy_claude_panel_v1":true}', encoding="utf-8")
        self.assertEqual(load_config(self.path), VibePulseConfig(
            legacy_claude_panel_v1=True))

    def test_actual_bytes_are_capped_even_if_path_metadata_looks_small(self):
        self.path.parent.mkdir()
        self.path.write_bytes(
            b'{"codex_interactions":true}' + b" " * (16 * 1024))
        fake_stat = mock.Mock(st_size=1)

        with mock.patch.object(Path, "stat", return_value=fake_stat):
            with self.assertRaises(ConfigError):
                load_config(self.path)

    @unittest.skipUnless(
        os.name == "posix" and hasattr(os, "O_NOFOLLOW"),
        "descriptor-level symlink refusal needs POSIX O_NOFOLLOW")
    def test_symlink_is_rejected_instead_of_followed(self):
        target = Path(self.tmp.name) / "target.json"
        target.write_text(
            '{"codex_interactions":true}', encoding="utf-8")
        self.path.parent.mkdir()
        self.path.symlink_to(target)

        with self.assertRaises(ConfigError):
            load_config(self.path)

    @unittest.skipUnless(os.name == "posix", "POSIX symbolic links")
    def test_symlink_is_rejected_without_o_nofollow_support(self):
        target = Path(self.tmp.name) / "target.json"
        target.write_text(
            '{"codex_interactions":true}', encoding="utf-8")
        self.path.parent.mkdir()
        self.path.symlink_to(target)

        with mock.patch.object(os, "O_NOFOLLOW", 0, create=True):
            with self.assertRaises(ConfigError):
                load_config(self.path)

        self.assertEqual(json.loads(target.read_text(encoding="utf-8")), {
            "codex_interactions": True,
        })

    @unittest.skipUnless(
        os.name == "posix" and hasattr(os, "mkfifo"), "POSIX FIFOs")
    def test_fifo_without_writer_is_rejected_without_blocking(self):
        self.path.parent.mkdir()
        os.mkfifo(self.path, 0o600)
        program = (
            "from pathlib import Path; "
            "from tools.tokenserver.vibepulse_config import "
            "ConfigError, load_config; "
            "\ntry: load_config(Path(__import__('sys').argv[1]))"
            "\nexcept ConfigError: raise SystemExit(0)"
            "\nraise SystemExit(1)"
        )

        completed = subprocess.run(
            [sys.executable, "-c", program, str(self.path)],
            cwd=Path(__file__).resolve().parents[2],
            timeout=2,
            check=False,
        )

        self.assertEqual(completed.returncode, 0)

    def test_config_lock_is_private_and_released_after_an_error(self):
        with self.assertRaisesRegex(RuntimeError, "inside transaction"):
            with config_lock(self.path):
                mode = stat.S_IMODE(
                    self.path.with_name(
                        f".{self.path.name}.lock").stat().st_mode)
                if os.name == "posix":
                    self.assertEqual(mode, 0o600)
                raise RuntimeError("inside transaction")

        with config_lock(self.path):
            save_config(self.path, VibePulseConfig(codex_interactions=True))
        self.assertTrue(load_config(self.path).codex_interactions)

    def _run_lock_program(self, program):
        try:
            return subprocess.run(
                [sys.executable, "-c", program, str(self.path)],
                cwd=Path(__file__).resolve().parents[2],
                timeout=2,
                check=False,
                capture_output=True,
                text=True,
            )
        except subprocess.TimeoutExpired:
            self.fail("nested config lock deadlocked")

    def test_config_lock_is_reentrant_for_the_same_thread_and_path(self):
        program = (
            "from pathlib import Path; import sys; "
            "from tools.tokenserver.vibepulse_config import config_lock; "
            "path=Path(sys.argv[1]); "
            "\nwith config_lock(path):"
            "\n with config_lock(path): pass"
        )

        completed = self._run_lock_program(program)

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_nested_exception_unwinds_then_lock_can_be_reacquired(self):
        program = (
            "from pathlib import Path; import sys; "
            "from tools.tokenserver.vibepulse_config import config_lock; "
            "path=Path(sys.argv[1]); "
            "\nwith config_lock(path):"
            "\n try:"
            "\n  with config_lock(path): raise RuntimeError('nested')"
            "\n except RuntimeError: pass"
            "\nwith config_lock(path): pass"
        )

        completed = self._run_lock_program(program)

        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_config_lock_fails_before_mutation_without_an_os_backend(self):
        with mock.patch.object(config_module, "fcntl", None), \
                mock.patch.object(config_module, "msvcrt", None):
            with self.assertRaises(ConfigError):
                with config_lock(self.path):
                    self.fail("lock succeeded without an OS backend")

        self.assertFalse(self.path.parent.exists())

    @unittest.skipUnless(os.name == "posix", "POSIX symbolic links")
    def test_config_lock_never_follows_a_symlink(self):
        self.path.parent.mkdir()
        target = Path(self.tmp.name) / "unrelated"
        target.write_bytes(b"do not touch")
        self.path.with_name(f".{self.path.name}.lock").symlink_to(target)

        with self.assertRaises(ConfigError):
            with config_lock(self.path):
                self.fail("unsafe lock was acquired")

        self.assertEqual(target.read_bytes(), b"do not touch")

    @unittest.skipUnless(os.name == "posix", "POSIX file kinds")
    def test_non_regular_config_path_is_rejected(self):
        self.path.mkdir(parents=True)

        with self.assertRaises(ConfigError):
            load_config(self.path)

    def test_round_trip_uses_only_the_public_non_secret_schema(self):
        expected = VibePulseConfig(
            claude_interactions=True,
            codex_interactions=True,
            interaction_detail=True,
            legacy_claude_panel_v1=True,
            interaction_relay=True,
            agent_status_relay=True,
            interaction_relay_url="https://relay.example",
            interaction_mailbox="vp_A1b2C3d4E5f6G7h8",
        )

        save_config(self.path, expected)

        self.assertEqual(load_config(self.path), expected)
        self.assertEqual(json.loads(self.path.read_text(encoding="utf-8")), {
            "claude_interactions": True,
            "codex_interactions": True,
            "interaction_detail": True,
            "legacy_claude_panel_v1": True,
            "interaction_relay": True,
            "agent_status_relay": True,
            "interaction_relay_url": "https://relay.example",
            "interaction_mailbox": "vp_A1b2C3d4E5f6G7h8",
            "agent_status_ignore": [],
            "merge_queue_sources": [],
        })
        self.assertNotIn("key", self.path.read_text(encoding="utf-8").lower())

    def test_agent_status_ignore_round_trips_as_a_tuple(self):
        expected = VibePulseConfig(
            agent_status_ignore=("/agent-team-orchestrator/work/",))

        save_config(self.path, expected)
        loaded = load_config(self.path)

        self.assertEqual(loaded, expected)
        self.assertEqual(loaded.agent_status_ignore,
                         ("/agent-team-orchestrator/work/",))
        self.assertEqual(
            json.loads(self.path.read_text(encoding="utf-8"))[
                "agent_status_ignore"],
            ["/agent-team-orchestrator/work/"])

    def test_agent_status_ignore_rejects_unsafe_entries(self):
        for bad in (
                ("/",),                  # would hide every session
                ("a/b",),                # fewer than three letters/digits
                ("",),
                ("x" * 201,),
                ("ok-path\nnext",),
                ("dup-path", "dup-path"),
                tuple(f"path-{i}" for i in range(17)),
                ("ok-path", 3),
                "a-string-not-a-list"):
            with self.subTest(bad=bad), self.assertRaises(ConfigError):
                VibePulseConfig(agent_status_ignore=bad)

    def test_merge_queue_sources_accept_only_normalized_loopback(self):
        ok = VibePulseConfig(merge_queue_sources=[
            "http://127.0.0.1:4400", "http://127.0.0.1:4401"])
        self.assertEqual(ok.merge_queue_sources,
                         ("http://127.0.0.1:4400", "http://127.0.0.1:4401"))
        for bad in (
                ("http://localhost:4400",),       # setup normalizes first
                ("https://127.0.0.1:4400",),
                ("http://192.168.1.10:4400",),
                ("http://127.0.0.1:4400/api",),
                ("http://127.0.0.1:0",),
                ("http://127.0.0.1:70000",),
                ("http://127.0.0.1:04400",),
                ("http://127.0.0.1:4400", "http://127.0.0.1:4400"),
                tuple(f"http://127.0.0.1:{4400 + i}" for i in range(17)),
                "http://127.0.0.1:4400"):
            with self.subTest(bad=bad), self.assertRaises(ConfigError):
                VibePulseConfig(merge_queue_sources=bad)

    def test_claude_project_key_matches_claude_code_folder_names(self):
        self.assertEqual(
            claude_project_key("/Users/f/_src/kvitt/agent-team-orchestrator"),
            "-Users-f--src-kvitt-agent-team-orchestrator")

    def test_unknown_duplicate_malformed_and_non_object_json_are_rejected(self):
        invalid_documents = (
            '{"unknown":false}',
            '{"codex_interactions":true,"codex_interactions":false}',
            '{',
            '[]',
            'null',
            '"off"',
        )
        self.path.parent.mkdir()
        for document in invalid_documents:
            with self.subTest(document=document):
                self.path.write_text(document, encoding="utf-8")
                with self.assertRaises(ConfigError):
                    load_config(self.path)

    def test_every_non_boolean_value_is_rejected_without_truthiness(self):
        self.path.parent.mkdir()
        for field in (
                "claude_interactions", "codex_interactions",
                "interaction_detail", "legacy_claude_panel_v1",
                "interaction_relay", "agent_status_relay"):
            for value in (0, 1, "true", [], {}, None):
                with self.subTest(field=field, value=value):
                    self.path.write_text(
                        json.dumps({field: value}), encoding="utf-8")
                    with self.assertRaises(ConfigError):
                        load_config(self.path)

    def test_relay_url_and_mailbox_are_strict_nullable_public_fields(self):
        valid = VibePulseConfig(
            interaction_relay_url="https://relay.example",
            interaction_mailbox="vp_A1b2C3d4E5f6G7h8")
        save_config(self.path, valid)
        self.assertEqual(load_config(self.path), valid)

        for field, values in (
                ("interaction_relay_url", (
                    "", "http://relay.example", "https://user@relay.example",
                    "https://relay.example/path", 1, True, [], {})),
                ("interaction_mailbox", (
                    "", "vp_short", "vp_A1b2C3d4E5f6G7h8=", 1, True, [], {}))):
            for value in values:
                with self.subTest(field=field, value=value):
                    with self.assertRaises(ConfigError):
                        VibePulseConfig(**{field: value})

    def test_save_creates_private_directory_and_file_where_supported(self):
        save(self.path, VibePulseConfig(codex_interactions=True))

        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(self.path.parent.stat().st_mode),
                             0o700)
            self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)

    def test_save_atomically_replaces_the_destination_from_same_directory(self):
        self.path.parent.mkdir()
        self.path.write_text("old", encoding="utf-8")
        real_replace = os.replace
        calls = []

        def observed_replace(source, destination):
            calls.append((Path(source), Path(destination)))
            self.assertTrue(Path(source).exists())
            self.assertEqual(Path(source).parent, self.path.parent)
            return real_replace(source, destination)

        with mock.patch(
                "tools.tokenserver.vibepulse_config.os.replace",
                side_effect=observed_replace):
            save_config(self.path, VibePulseConfig(
                claude_interactions=True))

        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1], self.path)
        self.assertEqual(load_config(self.path), VibePulseConfig(
            claude_interactions=True))
        self.assertEqual(list(self.path.parent.glob(".config.json.*")), [])

    def test_failed_replace_preserves_old_file_and_removes_temporary_file(self):
        self.path.parent.mkdir()
        self.path.write_text(
            '{"codex_interactions":true}', encoding="utf-8")

        with mock.patch(
                "tools.tokenserver.vibepulse_config.os.replace",
                side_effect=OSError("replace failed")):
            with self.assertRaises(ConfigError):
                save_config(self.path, VibePulseConfig(
                    claude_interactions=True))

        self.assertEqual(load_config(self.path), VibePulseConfig(
            codex_interactions=True))
        self.assertEqual(list(self.path.parent.glob(".config.json.*")), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
