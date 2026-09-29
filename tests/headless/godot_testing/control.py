"""Client for Godot's control channel (`--control-port`).

Values use Godot's `JSON.from_native()` encoding. `encode()` and `decode()`
convert the common cases (bool, int, float, str, lists, dicts, vectors) to and
from Python; anything else passes through in its encoded form.
"""

from __future__ import annotations

import base64
import json
import re
import socket
import time
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Any

from godot_testing import png
from godot_testing.runner import Launched

_LISTENING = re.compile(r"Control channel listening on 127\.0\.0\.1:(\d+)")
_VECTOR_TYPES = {"Vector2", "Vector2i", "Vector3", "Vector3i", "Vector4", "Vector4i", "Color"}


@dataclass(frozen=True)
class Native:
    """A Godot value with no Python counterpart, e.g. `Native("Vector3", 1.0, 2.0, 3.0)`."""

    type: str
    args: tuple[Any, ...]

    def __init__(self, type: str, *args: Any) -> None:  # noqa: A002 - mirrors the wire format.
        object.__setattr__(self, "type", type)
        object.__setattr__(self, "args", args)


class ControlError(RuntimeError):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(f"{message} (code {code})")
        self.code = code
        self.message = message


def encode(value: Any) -> Any:
    """Python value -> `JSON.from_native()` form."""
    if value is None or isinstance(value, bool):
        return value
    if isinstance(value, Native):
        return {"type": value.type, "args": [a if isinstance(a, (int, float)) else encode(a) for a in value.args]}
    if isinstance(value, int):
        return f"i:{value}"
    if isinstance(value, float):
        return f"f:{value!r}"
    if isinstance(value, str):
        return f"s:{value}"
    if isinstance(value, (list, tuple)):
        return {"type": "Array", "args": [encode(v) for v in value]}
    if isinstance(value, dict):
        args = []
        for key, item in value.items():
            args += [encode(key), encode(item)]
        return {"type": "Dictionary", "args": args}
    raise TypeError(f"can't encode {type(value).__name__}; pass the JSON.from_native() form directly")


def decode(value: Any) -> Any:
    """`JSON.from_native()` form -> Python value (vectors become tuples)."""
    if isinstance(value, str):
        prefix, _, rest = value.partition(":")
        if prefix == "i":
            return int(rest)
        if prefix == "f":
            return float(rest)
        if prefix in ("s", "sn", "np"):
            return rest
        return value
    if isinstance(value, dict) and "type" in value:
        kind, args = value["type"], value.get("args", [])
        if kind == "Array":
            return [decode(v) for v in args]
        if kind == "Dictionary":
            return {decode(args[i]): decode(args[i + 1]) for i in range(0, len(args), 2)}
        if kind in _VECTOR_TYPES:
            return tuple(args)
    return value


@dataclass
class Frame:
    width: int
    height: int
    png_bytes: bytes

    def image(self) -> png.Image:
        return png.decode(self.png_bytes)


class ControlClient:
    """Blocking JSON-RPC client. One request in flight at a time."""

    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 60.0) -> None:
        self._socket = socket.create_connection((host, port), timeout=timeout)
        self._reader = self._socket.makefile("r", encoding="utf-8", newline="\n")
        self._next_id = 1

    @classmethod
    def connect(cls, port: int, deadline: float = 60.0) -> ControlClient:
        """Connects, retrying until the game has opened the port or `deadline` seconds pass."""
        end = time.monotonic() + deadline
        while True:
            try:
                return cls(port)
            except ConnectionRefusedError:
                if time.monotonic() > end:
                    raise
                time.sleep(0.05)

    def close(self) -> None:
        self._reader.close()
        self._socket.close()

    def __enter__(self) -> ControlClient:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def request(self, name: str, /, **params: Any) -> Any:
        """Sends the JSON-RPC request `name` and returns its result; raises ControlError on an error reply."""
        request_id = self._next_id
        self._next_id += 1
        line = json.dumps({"jsonrpc": "2.0", "id": request_id, "method": name, "params": params})
        self._socket.sendall(line.encode("utf-8") + b"\n")
        response_line = self._reader.readline()
        if not response_line:
            raise ConnectionError("the game closed the control channel")
        response = json.loads(response_line)
        if response.get("id") != request_id:
            raise ControlError(-1, f"response id {response.get('id')} does not match request {request_id}")
        if "error" in response:
            raise ControlError(response["error"]["code"], response["error"]["message"])
        return response.get("result")

    def _request_object(self, name: str, /, **params: Any) -> dict[str, Any]:
        result = self.request(name, **params)
        if not isinstance(result, dict):
            raise ControlError(-1, f"{name} returned {result!r}, expected an object")
        return result

    # Run control. `pause` and `step` return once the game is held, with its frame counters.
    def state(self) -> dict[str, Any]:
        return self._request_object("state")

    def pause(self) -> dict[str, Any]:
        return self._request_object("pause")

    def resume(self) -> dict[str, Any]:
        return self._request_object("resume")

    def step(self, frames: int = 1) -> dict[str, Any]:
        return self._request_object("step", frames=frames)

    def quit(self, code: int = 0) -> None:
        self.request("quit", code=code)

    # Scene access.
    def tree(self, path: str = "/root", depth: int = 1) -> dict[str, Any]:
        return self._request_object("tree", path=path, depth=depth)

    def get(self, path: str, prop: str) -> Any:
        return decode(self.request("get", path=path, property=prop))

    def set(self, path: str, prop: str, value: Any) -> None:
        self.request("set", path=path, property=prop, value=encode(value))

    def call(self, path: str, method: str, *args: Any) -> Any:
        return decode(self.request("call", path=path, method=method, args=[encode(a) for a in args]))

    # Input, delivered on the next frame.
    def action(self, name: str, pressed: bool = True, strength: float = 1.0) -> None:
        self.request("action", action=name, pressed=pressed, strength=strength)

    def input(self, event_type: str, **properties: Any) -> None:
        encoded = {
            k: v if k in ("keycode", "physical_keycode", "key_label") and isinstance(v, str) else encode(v)
            for k, v in properties.items()
        }
        self.request("input", type=event_type, properties=encoded)

    def key(self, keycode: str, pressed: bool = True) -> None:
        self.input("InputEventKey", keycode=keycode, pressed=pressed)

    def capture(self) -> Frame:
        result = self._request_object("capture")
        return Frame(result["width"], result["height"], base64.b64decode(result["png"]))


def wait_for_port(launched: Launched, deadline: float = 120.0) -> int:
    """Waits until the game announces its control port in its output."""
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        match = _LISTENING.search(launched.output())
        if match:
            return int(match.group(1))
        if launched.process.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError("the game never opened its control channel:\n" + launched.output())


def control_args(port: int = 0, wait: bool = True) -> Sequence[str]:
    args = ["--control-port", str(port)]
    return [*args, "--control-wait"] if wait else args
