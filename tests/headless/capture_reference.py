"""Captures windowed reference frames for test_offscreen.py on a machine with a display.

Usage: uv run python capture_reference.py [--out DIR]
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from godot_testing import png, runner
from test_offscreen import DRIVERS, REFERENCE_DIR


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=REFERENCE_DIR)
    args = parser.parse_args()
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        print("capture_reference.py needs a display (DISPLAY or WAYLAND_DISPLAY) for the windowed run", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    for driver in DRIVERS:
        out = args.out / f"{driver}.png"
        result = runner.run(
            ["--rendering-driver", driver, "--audio-driver", "Dummy", "--windowed", "--resolution", "320x240"],
            user_args=[f"--capture={out}", "--frames=10"],
            keep_display=True,
        )
        if result.returncode != 0 or not out.is_file():
            print(result.output, file=sys.stderr)
            return 1
        image = png.read(out)
        print(f"{driver}: {image.width}x{image.height}, {image.distinct_colors()} colors -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
