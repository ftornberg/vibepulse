#!/usr/bin/env python3
"""Repository-gate contract for the numbers relay JavaScript suites."""

import json
from pathlib import Path
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class NumbersRelayJsWiringTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = read("test/run.sh")
        workflow = yaml.safe_load(read(".github/workflows/ci.yml"))
        self.jobs = workflow["jobs"]

    @staticmethod
    def step_runs(job: dict) -> list[str]:
        return [
            step["run"]
            for step in job["steps"]
            if isinstance(step, dict) and "run" in step
        ]

    def test_full_runner_runs_each_numbers_relay_suite_once(self) -> None:
        merge_command = "node --test tools/relay/test.mjs"
        runtime_command = "(cd tools/relay && npm ci && npm test)"

        self.assertEqual(self.runner.count(merge_command), 1)
        self.assertEqual(self.runner.count(runtime_command), 1)
        self.assertEqual(
            self.runner.count(
                '"$PYTHON_BIN" test_numbers_relay_js_wiring.py'
            ),
            1,
        )

        node_branch = self.runner.split(
            "elif command -v node >/dev/null 2>&1 && "
            "command -v npm >/dev/null 2>&1; then",
            1,
        )[1].split('elif [ -n "${CI:-}" ]; then', 1)[0]
        self.assertIn(merge_command, node_branch)
        self.assertIn(runtime_command, node_branch)
        self.assertIn('if [ "$SKIP_JS" = 1 ]; then', self.runner)

    def test_ci_owns_relay_suites_in_one_non_matrix_job(self) -> None:
        merge_command = "node --test tools/relay/test.mjs"
        runtime_command = "(cd tools/relay && npm ci && npm test)"
        dry_build_command = "(cd tools/relay && npm run build:dry)"

        self.assertIn("numbers-relay", self.jobs)
        numbers_job = self.jobs["numbers-relay"]
        self.assertEqual(numbers_job["runs-on"], "ubuntu-latest")
        self.assertNotIn("strategy", numbers_job)

        runs = self.step_runs(numbers_job)
        self.assertEqual(runs.count(merge_command), 1)
        self.assertEqual(runs.count(runtime_command), 1)
        self.assertEqual(runs.count(dry_build_command), 1)

        merge_owners = [
            name
            for name, job in self.jobs.items()
            if merge_command in self.step_runs(job)
        ]
        runtime_owners = [
            name
            for name, job in self.jobs.items()
            if runtime_command in self.step_runs(job)
        ]
        self.assertEqual(merge_owners, ["numbers-relay"])
        self.assertEqual(runtime_owners, ["numbers-relay"])

        setup_node = [
            step
            for step in numbers_job["steps"]
            if str(step.get("uses", "")).startswith("actions/setup-node@")
        ]
        self.assertEqual(len(setup_node), 1)
        self.assertEqual(setup_node[0]["with"]["node-version"], 22)
        self.assertEqual(setup_node[0]["with"]["cache"], "npm")
        self.assertEqual(
            setup_node[0]["with"]["cache-dependency-path"],
            "tools/relay/package-lock.json",
        )

    def test_host_gate_skips_javascript_suites(self) -> None:
        host_gate_runs = self.step_runs(self.jobs["host-gate"])
        run_sh_commands = [
            command.strip()
            for command in host_gate_runs
            if "./test/run.sh" in command
        ]

        self.assertEqual(
            run_sh_commands,
            ["xvfb-run -a ./test/run.sh --skip-js"],
        )
        self.assertFalse(
            any(
                "node --test tools/relay/test.mjs" in command
                or "npm test" in command
                for command in host_gate_runs
            )
        )

    def test_tokenserver_matrix_does_not_run_numbers_relay_suites(self) -> None:
        tokenserver_job = self.jobs["tokenserver"]

        self.assertIn("matrix", tokenserver_job["strategy"])
        self.assertFalse(
            any(
                str(step.get("uses", "")).startswith("actions/setup-node@")
                for step in tokenserver_job["steps"]
            )
        )
        self.assertFalse(
            any(
                "tools/relay" in run or "npm test" in run
                for run in self.step_runs(tokenserver_job)
            )
        )

    def test_npm_runtime_suite_does_not_repeat_merge_suite(self) -> None:
        package = json.loads(read("tools/relay/package.json"))

        self.assertEqual(
            package["scripts"]["test"],
            "vitest run --config vitest.config.mjs && "
            "node --test deploy.test.mjs",
        )
        self.assertEqual(
            package["scripts"]["test:merge"],
            "node --test test.mjs",
        )
        self.assertEqual(package["scripts"]["build:dry"],
                         "node deploy.mjs ci-dry")
        self.assertEqual(package["scripts"]["deploy:dry"],
                         "node deploy.mjs dry-run")
        self.assertEqual(package["scripts"]["deploy"],
                         "node deploy.mjs deploy")


if __name__ == "__main__":
    unittest.main()
