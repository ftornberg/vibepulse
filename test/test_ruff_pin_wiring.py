#!/usr/bin/env python3
"""Regression guard: ruff's two pins must name the same release.

requirements-dev.txt installs ruff; pyproject.toml's `required-version`
makes ruff refuse any other release, so the venv can never lint with a rule
set CI does not run. ruff cannot read that pin from the requirements file,
so it is a second copy by necessity — and a Dependabot bump moves only the
first one (0.16.10 in 81718d1, 0.17.2 in #20), after which the host gate
dies with "Required version `==X` does not match the running version `Y`".
This guard fails first, and says which line to move."""

import re
import tomllib
from pathlib import Path


root = Path(__file__).resolve().parents[1]

requirements = (root / "requirements-dev.txt").read_text(encoding="utf-8")
installed = re.search(r"^ruff==([0-9][0-9.]*)\s*$", requirements, re.MULTILINE)
assert installed, "requirements-dev.txt ska pinna ruff==X"

with (root / "pyproject.toml").open("rb") as fh:
    required = tomllib.load(fh)["tool"]["ruff"]["required-version"]
assert required == f"=={installed.group(1)}", (
    f"pyproject.toml required-version = \"{required}\" men requirements-dev.txt "
    f"installerar ruff=={installed.group(1)} — flytta required-version med pinnen"
)

print(f"OK: ruff {installed.group(1)} pinnas lika i requirements-dev.txt och pyproject.toml")
