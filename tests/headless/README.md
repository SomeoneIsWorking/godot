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
