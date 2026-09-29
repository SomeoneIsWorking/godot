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


def _command(args: Sequence[str], project: Path, user_args: Sequence[str]) -> list[str]:
    command = [str(find_godot()), "--path", str(project), *args]
    return [*command, "--", *user_args] if user_args else command


def _environment(env: Mapping[str, str] | None, keep_display: bool) -> dict[str, str]:
    run_env = dict(os.environ)
    if not keep_display:
        for name in _DISPLAY_VARIABLES:
            run_env.pop(name, None)
    run_env.update(env or {})
    return run_env


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
    completed = subprocess.run(
        _command(args, project, user_args),
        env=_environment(env, keep_display),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )
    return Result(completed.returncode, completed.stdout)


@dataclass
class Launched:
    """A running Godot process whose combined output goes to `log`."""

    process: subprocess.Popen[bytes]
    log: Path

    def output(self) -> str:
        return self.log.read_text(errors="replace")


def launch(
    args: Sequence[str],
    log: Path,
    *,
    project: Path = TEST_PROJECT,
    user_args: Sequence[str] = (),
    env: Mapping[str, str] | None = None,
) -> Launched:
    """Starts Godot on `project` without a display; the caller must `stop()` it.

    Output goes to a file rather than a pipe, so a chatty game can never block on a full pipe.
    """
    with log.open("wb") as sink:
        process = subprocess.Popen(
            _command(args, project, user_args),
            env=_environment(env, keep_display=False),
            stdout=sink,
            stderr=subprocess.STDOUT,
        )
    return Launched(process, log)


def stop(launched: Launched, timeout: float = 30.0) -> tuple[int, str]:
    """Waits for the process to exit, killing it (by PID) if it doesn't; returns its exit code and output."""
    try:
        launched.process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        launched.process.kill()
        launched.process.wait()
    return launched.process.returncode, launched.output()
