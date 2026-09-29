"""Minimal PNG decoder for 8-bit RGB/RGBA images, as written by Godot's Image.save_png()."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHANNELS_BY_COLOR_TYPE = {2: 3, 6: 4}


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    channels: int
    pixels: bytes  # Row-major, `channels` bytes per pixel, no padding.

    def pixel(self, x: int, y: int) -> tuple[int, ...]:
        offset = (y * self.width + x) * self.channels
        return tuple(self.pixels[offset : offset + self.channels])

    def distinct_colors(self) -> int:
        step = self.channels
        return len({self.pixels[i : i + step] for i in range(0, len(self.pixels), step)})

    def max_channel_difference(self, other: Image) -> int:
        if (self.width, self.height, self.channels) != (other.width, other.height, other.channels):
            raise ValueError(
                f"image shapes differ: {self.width}x{self.height}x{self.channels} "
                f"vs {other.width}x{other.height}x{other.channels}"
            )
        return max((abs(a - b) for a, b in zip(self.pixels, other.pixels)), default=0)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode(data: bytes) -> Image:
    if not data.startswith(_SIGNATURE):
        raise ValueError("not a PNG file")
    offset = len(_SIGNATURE)
    header: tuple[int, ...] | None = None
    compressed = bytearray()
    while offset < len(data):
        (length,) = struct.unpack(">I", data[offset : offset + 4])
        kind = data[offset + 4 : offset + 8]
        body = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError("PNG has no IHDR chunk")
    width, height, bit_depth, color_type, _, _, interlace = header
    if bit_depth != 8 or color_type not in _CHANNELS_BY_COLOR_TYPE or interlace != 0:
        raise ValueError(f"unsupported PNG: depth={bit_depth} color_type={color_type} interlace={interlace}")
    channels = _CHANNELS_BY_COLOR_TYPE[color_type]
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    pixels = bytearray(stride * height)
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1 : start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + up) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                row[i] = (row[i] + _paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"invalid PNG filter type {filter_type}")
        pixels[y * stride : (y + 1) * stride] = row
        previous = row
    return Image(width, height, channels, bytes(pixels))


def read(path: Path) -> Image:
    return decode(path.read_bytes())
