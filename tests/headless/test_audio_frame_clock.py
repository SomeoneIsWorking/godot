"""End-to-end tests for the frame-clocked silent audio driver (`--fixed-fps` with `--audio-driver Dummy`)."""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import pytest
from godot_testing import runner
from godot_testing.control import ControlClient, control_args, wait_for_port

HEADLESS = ["--headless", "--audio-driver", "Dummy"]


def _game(tmp_path: Path, args: list[str]) -> Iterator[ControlClient]:
    launched = runner.launch([*HEADLESS, *args, *control_args()], tmp_path / "game.log")
    try:
        with ControlClient.connect(wait_for_port(launched)) as client:
            client.step(2)  # Let the player start.
            yield client
            client.quit()
    finally:
        code, output = runner.stop(launched)
        assert code == 0, output


@pytest.fixture
def frame_clocked(tmp_path: Path) -> Iterator[ControlClient]:
    yield from _game(tmp_path, ["--fixed-fps", "60"])


@pytest.fixture
def wall_clocked(tmp_path: Path) -> Iterator[ControlClient]:
    yield from _game(tmp_path, [])


def test_audio_advances_exactly_with_frames(frame_clocked: ControlClient) -> None:
    start = frame_clocked.call("/root/Main", "audio_position")
    frame_clocked.step(60)
    assert frame_clocked.call("/root/Main", "audio_position") - start == pytest.approx(1.0, abs=1e-9)
    frame_clocked.step(15)
    assert frame_clocked.call("/root/Main", "audio_position") - start == pytest.approx(1.25, abs=1e-9)


def test_audio_is_still_while_the_game_is_held(frame_clocked: ControlClient) -> None:
    before = frame_clocked.call("/root/Main", "audio_position")
    time.sleep(0.5)
    assert frame_clocked.call("/root/Main", "audio_position") == before


def test_audio_sync_formula_tracks_frames(frame_clocked: ControlClient) -> None:
    # position + time_since_last_mix - output_latency is the documented way to sync to audio.
    start = frame_clocked.call("/root/Main", "audio_sync_time")
    frame_clocked.step(30)
    assert frame_clocked.call("/root/Main", "audio_sync_time") - start == pytest.approx(0.5, abs=1e-9)


def test_without_fixed_fps_audio_follows_the_wall_clock(wall_clocked: ControlClient) -> None:
    # The discriminator for the tests above: the threaded dummy driver keeps mixing while the game is held.
    before = wall_clocked.call("/root/Main", "audio_position")
    time.sleep(0.5)
    assert wall_clocked.call("/root/Main", "audio_position") > before
