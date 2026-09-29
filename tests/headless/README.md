# Headless testing

Engine features that let automated tests run a game with no display, plus their
end-to-end tests and a Python client. Unit tests live with the engine's doctest
suite (`godot --test`).

Run the end-to-end tests against a built editor binary (the newest
`bin/godot.linuxbsd.editor*`, or `GODOT_BIN`):

```sh
cd tests/headless
uv run --locked pytest
```

## Offscreen rendering (`--offscreen`)

`--offscreen` (same as `--display-driver offscreen`, Linux only) creates a display
server with one virtual main window and no display connection. It renders real
frames through the same path a windowed run uses:

| Driver | Mechanism | Requires |
|---|---|---|
| `vulkan` (Forward+, Mobile) | swap chain on a `VK_EXT_headless_surface` surface; presenting is a no-op | a Vulkan driver with `VK_EXT_headless_surface` (Mesa: RADV, ANV, lavapipe, ...) |
| `opengl3` (Compatibility) | pbuffer on Mesa's surfaceless EGL platform | EGL with `EGL_MESA_platform_surfaceless` and OpenGL 3.3 |

- No window, no X11/Wayland connection and no Xvfb are involved; `DISPLAY` and
  `WAYLAND_DISPLAY` are ignored.
- The window size comes from `--resolution` or the project's window settings.
  Subwindows must be embedded (`display/window/subwindows/embed_subwindows`).
- Capture frames the usual way, e.g. `get_viewport().get_texture().get_image()`.
  The render target is the same one a windowed run blits to the screen.
- Offscreen is explicit-only: it is never picked by default or as a fallback, and
  if its driver can't start, the engine exits with an error that names the
  missing extension instead of falling back to another display server or
  renderer. `--headless` keeps its meaning (dummy renderer, nothing is drawn).
- CI without a GPU can use lavapipe:
  `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`.

`test_offscreen.py` checks that both drivers produce a real frame, that two runs
produce identical frames, and the named failures when the extension or EGL
platform is missing. To also compare against a windowed run, capture references
on a desktop session once:

```sh
uv run --locked python capture_reference.py   # writes scratch/headless-reference/<driver>.png
```

## Control channel (`--control-port`)

Debug builds (editor and debug export templates) can open a loopback endpoint
that a test driver uses to control the game frame by frame:

```sh
godot --path game --offscreen --control-port 0 --control-wait
# prints: Control channel listening on 127.0.0.1:<port>
```

- `--control-port <port>` opens it on `127.0.0.1` (`0` picks a free port). The
  project settings `debug/control_channel/enabled`, `port` and
  `wait_for_client` do the same from the project. Release builds never open it.
- `--control-wait` holds the game on its first frame until a client steps or
  resumes it, so a driver sees every frame from the start.
- Protocol: JSON-RPC 2.0, one request or response per line. Values use
  `JSON.from_native()` encoding (`"i:3"`, `{"type": "Vector3", "args": [...]}`).

| Method | Params | Result |
|---|---|---|
| `state` | | `{frame, physics_frame, paused}` |
| `pause` | | state, once the game is held |
| `step` | `frames` (default 1) | state, once `frames` more frames ran |
| `resume` | | state |
| `quit` | `code` | |
| `tree` | `path` (`/root`), `depth` (1, max 64) | `{name, class, path, child_count, children}` |
| `get` / `set` | `path`, `property` (`position:x` works), `value` | value / null |
| `call` | `path`, `method`, `args` | return value |
| `action` | `action`, `pressed`, `strength` | queues an `InputEventAction` |
| `input` | `type` (an `InputEvent` class), `properties` | queues the event; key properties accept names like `"Space"` |
| `capture` | | `{width, height, png}` (base64) of the main viewport |

The channel is serviced when the SceneTree emits `process_frame`. A held game
stops there: physics for that frame has run, `_process` has not. Input sent
while held is delivered at the start of the next frame, like device input, so
`step` after `input` is frame-exact. If the last client disconnects while the
game is held, it resumes. Errors use JSON-RPC codes: `-32700` parse, `-32600`
request, `-32601` method, `-32602` params, `-32001` node not found, `-32002`
call failed, `-32003` nothing to capture (the dummy renderer never draws; use
`--offscreen`).

`godot_testing/control.py` is a small Python client:

```python
from godot_testing.control import ControlClient
with ControlClient.connect(port) as game:
    game.step(10)
    game.key("Space")
    game.step()
    score = game.get("/root/Main/Hud", "score")
    game.capture().image()
```

## Frame-clocked silent audio (`--fixed-fps` with the `Dummy` audio driver)

With `--fixed-fps <fps>`, frames are unpaced and frame time is the game's only
clock. If the `Dummy` audio driver is selected (`--audio-driver Dummy`, or
`--headless`), it then runs without its thread: each main loop iteration mixes
exactly the audio that plays during that frame (`process_step × mix_rate`,
carrying fractions so nothing drifts). As a result:

- `AudioStreamPlayer.get_playback_position()` advances by exactly 1/fps per
  frame and stands still while the game is held by the control channel.
- `AudioServer.get_time_since_last_mix()` is `0`, and `get_output_latency()`
  and `get_time_to_next_mix()` are one frame's audio, so the usual sync formula
  (`position + time_since_last_mix - output_latency`) gives the frame's start
  time.
- `finished` signals, `AudioEffectCapture`, spectrum analyzers and anything
  else fed by the mixer see audio at the frame rate, however fast the frames
  run.

Without `--fixed-fps`, the dummy driver keeps its wall-clock thread. With
`--write-movie`, the movie writer drives the same per-frame mixing and records
the samples.

## Deterministic runs (`--fixed-fps`, `--random-seed`, `--frame-time`)

```sh
godot --path game --headless --fixed-fps 60 --random-seed 42 --frame-time
```

- `--fixed-fps <fps>` (existing): every frame advances `_process` and physics by
  exactly 1/fps and frames run unpaced, as fast as they can be produced.
  `--write-movie` implies it.
- `--random-seed <seed>`: every `randomize()` call (the one `SceneTree` makes at
  startup for `randf()`/`randi()`, `RandomNumberGenerator.new()`, and explicit
  calls) seeds from a sequence derived from `<seed>` instead of the clock.
- `--frame-time` (requires `--fixed-fps`): `Time.get_ticks_msec/usec()` report
  `frame × 1/fps`, and `Time.get_unix_time_from_system()` and the
  `*_from_system()` date functions report 2000-01-01T00:00:00 UTC plus that time;
  the time zone is UTC. Time stands still within a frame.
- With the `Dummy` audio driver, audio is mixed per frame (see above).

These compose with `--offscreen`, `--control-port` and `--write-movie`; the
tests check that two runs produce the same per-frame trace of physics state
and time readings, and byte-identical movie frames.

What is deterministic under all three flags: `_process`/`_physics_process`
deltas and counts, timers, tweens, animations, input injected through the
control channel (delivered on a known frame), random numbers, physics
(Godot Physics and Jolt step deterministically on one thread), audio
playback positions with the `Dummy` driver, and rendered frames on the same
GPU, driver and binary.

What is not:

- Work on other threads: `WorkerThreadPool` task order, threaded resource
  loading (`ResourceLoader.load_threaded_*` finishes on a varying frame),
  `physics/*/run_on_separate_thread`, navigation map synchronization and
  `Thread`s in game code. `randomize()` calls from several threads take their
  seeds in a varying order.
- `OS` and engine clocks: `OS.delay_*`, `Engine.get_frames_per_second()`,
  `Performance` monitors, profilers and anything else that reads the real
  clock rather than `Time`. Only `Time`'s methods follow `--frame-time`.
- The real audio drivers (they mix on the device's clock), networking, file
  timestamps, and hardware input.
- Rendering across different GPUs, drivers or engine builds, and GPU-driven
  effects that read timers.
