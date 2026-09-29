"""End-to-end tests for the offscreen display server (`--offscreen`)."""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from godot_testing import png, runner

# Renders the test scene's clear color, lit cube and 2D rect; a blank or
# near-blank frame has far fewer colors than that.
MIN_DISTINCT_COLORS = 16
REFERENCE_DIR = Path(
    os.environ.get("GODOT_HEADLESS_REFERENCE_DIR", runner.REPO_ROOT / "scratch" / "headless-reference")
)

DRIVERS = ["vulkan", "opengl3"]


def capture(tmp_path: Path, driver: str, name: str = "frame.png", env: dict[str, str] | None = None) -> png.Image:
    out = tmp_path / name
    result = runner.run(
        ["--offscreen", "--rendering-driver", driver, "--audio-driver", "Dummy"],
        user_args=[f"--capture={out}", "--frames=10"],
        env=env,
    )
    assert result.returncode == 0, result.output
    assert out.is_file(), result.output
    return png.read(out)


@pytest.mark.parametrize("driver", DRIVERS)
def test_offscreen_renders_real_frame(tmp_path: Path, driver: str) -> None:
    image = capture(tmp_path, driver)
    assert (image.width, image.height) == (320, 240)
    assert image.distinct_colors() >= MIN_DISTINCT_COLORS
    # The 2D ColorRect at (8, 8)-(72, 40) is opaque green in every renderer.
    red, green, blue = image.pixel(40, 24)[:3]
    assert green > 150 and red < 100 and blue < 100


@pytest.mark.parametrize("driver", DRIVERS)
def test_offscreen_frames_repeat_across_runs(tmp_path: Path, driver: str) -> None:
    first = capture(tmp_path, driver, "first.png")
    second = capture(tmp_path, driver, "second.png")
    assert first.max_channel_difference(second) == 0


@pytest.mark.parametrize("driver", DRIVERS)
def test_offscreen_matches_windowed_reference(tmp_path: Path, driver: str) -> None:
    reference = REFERENCE_DIR / f"{driver}.png"
    if not reference.is_file():
        pytest.skip(f"no windowed reference at {reference}; run `uv run python capture_reference.py` on a desktop")
    assert capture(tmp_path, driver).max_channel_difference(png.read(reference)) == 0


def test_offscreen_vulkan_without_headless_surface_fails_by_name(tmp_path: Path) -> None:
    # No Vulkan driver at all: the instance lacks VK_EXT_headless_surface.
    result = runner.run(
        ["--offscreen", "--rendering-driver", "vulkan", "--audio-driver", "Dummy"],
        user_args=[f"--capture={tmp_path / 'never.png'}"],
        env={
            "VK_DRIVER_FILES": str(tmp_path / "no-such-icd.json"),
            "VK_ICD_FILENAMES": str(tmp_path / "no-such-icd.json"),
        },
    )
    assert result.returncode != 0, result.output
    assert "VK_EXT_headless_surface" in result.output
    # Explicitly requested offscreen rendering must never degrade to another display server.
    assert "falling back" not in result.output
    assert not (tmp_path / "never.png").exists()


def test_offscreen_opengl_without_surfaceless_platform_fails_by_name(tmp_path: Path) -> None:
    result = runner.run(
        ["--offscreen", "--rendering-driver", "opengl3", "--audio-driver", "Dummy"],
        user_args=[f"--capture={tmp_path / 'never.png'}"],
        env={"__EGL_VENDOR_LIBRARY_FILENAMES": str(tmp_path / "no-such-vendor.json")},
    )
    assert result.returncode != 0, result.output
    assert "EGL_MESA_platform_surfaceless" in result.output
    assert "falling back" not in result.output
    assert not (tmp_path / "never.png").exists()


def test_offscreen_rejects_unsupported_driver() -> None:
    result = runner.run(["--offscreen", "--rendering-driver", "dummy", "--quit"])
    assert result.returncode != 0, result.output
    assert 'can\'t render with the "dummy" driver' in result.output
