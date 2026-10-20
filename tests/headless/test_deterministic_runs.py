"""End-to-end tests for deterministic runs (`--fixed-fps`, `--random-seed`, `--frame-time`)."""

from __future__ import annotations

import re
from pathlib import Path

from godot_testing import runner

TRACE_FRAMES = "--trace=120"
DETERMINISTIC = ["--headless", "--audio-driver", "Dummy", "--fixed-fps", "60", "--random-seed", "42", "--frame-time"]


def trace(args: list[str]) -> tuple[str, str]:
    result = runner.run(args, user_args=[TRACE_FRAMES])
    assert result.returncode == 0, result.output
    digest = re.search(r"^trace ([0-9a-f]{64})$", result.output, re.MULTILINE)
    last = re.search(r"^trace-last (.*)$", result.output, re.MULTILINE)
    assert digest and last, result.output
    return digest.group(1), last.group(1)


def test_runs_repeat_frame_for_frame() -> None:
    first = trace(DETERMINISTIC)
    assert trace(DETERMINISTIC) == first


def test_frame_time_starts_at_the_epoch_and_advances_per_frame() -> None:
    _, last = trace(DETERMINISTIC)
    ticks, unix_time, _, datetime = last.split(" ")[:4]
    # The first frame reads 0, so the 120th reads 119 frames.
    assert int(ticks) == 119 * 1_000_000 // 60
    assert int(unix_time) == 946684800 + 1
    assert datetime == "2000-01-01T00:00:01"


def test_a_different_seed_changes_the_run() -> None:
    other = [*DETERMINISTIC]
    other[other.index("42")] = "43"
    assert trace(other)[0] != trace(DETERMINISTIC)[0]


def test_without_random_seed_runs_differ() -> None:
    # Discriminator: the digest must be able to tell runs apart.
    unseeded = [arg for arg in DETERMINISTIC if arg not in ("--random-seed", "42")]
    assert trace(unseeded)[0] != trace(unseeded)[0]


def test_without_frame_time_ticks_follow_the_wall_clock() -> None:
    wall = [arg for arg in DETERMINISTIC if arg != "--frame-time"]
    _, last = trace(wall)
    assert int(last.split(" ")[1]) > 1_600_000_000


def test_frame_time_requires_a_fixed_frame_rate() -> None:
    result = runner.run(["--headless", "--frame-time", "--quit"])
    assert result.returncode != 0
    assert "--frame-time requires --fixed-fps" in result.output


def test_invalid_seed_is_rejected() -> None:
    result = runner.run(["--headless", "--random-seed", "nope", "--quit"])
    assert result.returncode != 0
    assert "--random-seed <seed>" in result.output


def test_offscreen_movie_is_identical_across_runs(tmp_path: Path) -> None:
    frames = []
    for name in ("a", "b"):
        movie = tmp_path / name / "frame.png"
        movie.parent.mkdir()
        args = [
            "--offscreen",
            "--rendering-driver",
            "vulkan",
            *DETERMINISTIC[1:],
            "--write-movie",
            str(movie),
            "--quit-after",
            "30",
        ]
        result = runner.run(args)
        assert result.returncode == 0, result.output
        frames.append(sorted(movie.parent.glob("*.png")))
    first, second = frames
    assert len(first) >= 30
    assert [p.name for p in first] == [p.name for p in second]
    for a, b in zip(first, second):
        assert a.read_bytes() == b.read_bytes(), a.name
