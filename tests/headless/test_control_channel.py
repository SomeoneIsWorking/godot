"""End-to-end tests for the control channel (`--control-port`)."""

from __future__ import annotations

import json
import socket
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

import pytest
from godot_testing import runner
from godot_testing.control import ControlClient, ControlError, Native, control_args, wait_for_port

OFFSCREEN = ["--offscreen", "--rendering-driver", "vulkan", "--audio-driver", "Dummy"]


@dataclass
class Game:
    client: ControlClient
    port: int


@pytest.fixture
def game(tmp_path: Path) -> Iterator[Game]:
    launched = runner.launch([*OFFSCREEN, *control_args()], tmp_path / "game.log")
    try:
        port = wait_for_port(launched)
        with ControlClient.connect(port) as client:
            yield Game(client, port)
            client.quit()
    finally:
        code, output = runner.stop(launched)
        assert code == 0, output


def test_wait_holds_the_game_until_stepped(game: Game) -> None:
    first = game.client.state()
    assert first["paused"]
    again = game.client.state()
    assert again["frame"] == first["frame"]
    stepped = game.client.step(5)
    assert stepped["frame"] == first["frame"] + 5
    assert stepped["paused"]


def test_scene_access(game: Game) -> None:
    root = game.client.tree("/root", depth=2)
    main = next(child for child in root["children"] if child["name"] == "Main")
    assert main["class"] == "Node3D"
    game.client.set("/root/Main", "position", Native("Vector3", 1.0, 2.0, 3.0))
    assert game.client.get("/root/Main", "position") == (1.0, 2.0, 3.0)
    assert game.client.call("/root/Main", "get_child_count") >= 4


def test_capture_returns_the_rendered_frame(game: Game) -> None:
    game.client.step(10)
    frame = game.client.capture()
    image = frame.image()
    assert (image.width, image.height) == (320, 240)
    red, green, blue = image.pixel(40, 24)[:3]
    assert green > 150 and red < 100 and blue < 100


def test_injected_input_arrives_on_the_next_frame(game: Game) -> None:
    game.client.step(2)
    game.client.key("Space", pressed=True)
    assert not game.client.call("/root/Main", "is_key_seen")
    game.client.step(1)
    assert game.client.call("/root/Main", "is_key_seen")


def test_errors_are_reported_and_the_game_keeps_running(game: Game) -> None:
    with pytest.raises(ControlError) as missing:
        game.client.get("/root/Missing", "name")
    assert missing.value.code == -32001
    with pytest.raises(ControlError) as unknown:
        game.client.request("no_such_method")
    assert unknown.value.code == -32601
    with pytest.raises(ControlError) as bad_type:
        game.client.input("Node")
    assert bad_type.value.code == -32602
    # A malformed line gets a parse error, and the connection stays usable.
    with socket.create_connection(("127.0.0.1", game.port), timeout=30) as raw:
        raw.sendall(b"{not json\n")
        response = json.loads(raw.makefile("r").readline())
        assert response["error"]["code"] == -32700
    assert game.client.state()["paused"]


def test_channel_is_closed_unless_requested(tmp_path: Path) -> None:
    code, output = runner.stop(runner.launch([*OFFSCREEN, "--quit-after", "30"], tmp_path / "game.log"))
    assert code == 0, output
    assert "Control channel listening" not in output


def test_invalid_port_is_rejected() -> None:
    result = runner.run([*OFFSCREEN, "--control-port", "70000", "--quit"])
    assert result.returncode != 0
    assert "--control-port <port> must be between 0 and 65535" in result.output
