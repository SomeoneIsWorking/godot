"""Launches a Godot binary for tests, with a scrubbed environment and bounded lifetime."""

from __future__ import annotations

import os
import subprocess
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

# Variables that would let a run reach a real display server.
_DISPLAY_VARIABLES = ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET")

REPO_ROOT = Path(__file__).resolve().parents[3]
TEST_PROJECT = Path(__file__).resolve().parents[1] / "project"


def find_godot() -> Path:
    """Returns the binary named by GODOT_BIN, else the newest editor build in bin/."""
    configured = os.environ.get("GODOT_BIN")
    if configured:
        path = Path(configured)
        if not path.is_file():
            raise FileNotFoundError(f"GODOT_BIN={configured} is not a file")
        return path
    candidates = sorted(
        (p for p in (REPO_ROOT / "bin").glob("godot.linuxbsd.editor*") if p.is_file() and os.access(p, os.X_OK)),
        key=lambda p: p.stat().st_mtime,
    )
    if not candidates:
        raise FileNotFoundError(f"no Godot editor binary in {REPO_ROOT / 'bin'}; build one or set GODOT_BIN")
    return candidates[-1]


@dataclass(frozen=True)
class Result:
    returncode: int
    output: str


def run(
    args: Sequence[str],
    *,
    project: Path = TEST_PROJECT,
    user_args: Sequence[str] = (),
    env: Mapping[str, str] | None = None,
    keep_display: bool = False,
    timeout: float = 120.0,
) -> Result:
    """Runs Godot on `project` and returns its exit code and combined output."""
    run_env = dict(os.environ)
    if not keep_display:
        for name in _DISPLAY_VARIABLES:
            run_env.pop(name, None)
    run_env.update(env or {})
    command = [str(find_godot()), "--path", str(project), *args]
    if user_args:
        command += ["--", *user_args]
    completed = subprocess.run(
        command,
        env=run_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )
    return Result(completed.returncode, completed.stdout)
