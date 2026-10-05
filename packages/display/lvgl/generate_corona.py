#!/usr/bin/env python3
"""Bake packages/display/lvgl/corona.png from design tokens.

The bitmap is a solar-corona sheath: bright at the dial rim, falling off
radially. Interior and far field stay transparent so LVGL can blend it
behind the temperature face.
"""

from __future__ import annotations

import math
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TOKENS = ROOT / "tokens.generated.yaml"
OUT = ROOT / "corona.png"
SIZE = 400
FACE_DIAMETER = 260


def token_rgb(name: str) -> tuple[int, int, int]:
    text = TOKENS.read_text()
    match = re.search(rf"lt_color_{name}: \"0x([0-9A-Fa-f]{{6}})\"", text)
    if not match:
        raise SystemExit(f"missing token {name}")
    value = int(match.group(1), 16)
    return (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF


def png_rgba(width: int, height: int, pixels: bytearray) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(
        b"IDAT", zlib.compress(bytes(raw), 9)
    ) + chunk(b"IEND", b"")


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[float, float, float]:
    return (
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    )


def main() -> None:
    warm = token_rgb("accent")
    cool = token_rgb("accent_forest")
    cx = cy = (SIZE - 1) / 2.0
    inner = FACE_DIAMETER / 2.0
    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - cx
            dy = y - cy
            radius = math.hypot(dx, dy)
            if radius >= cx:
                continue
            limb = radius - inner
            if limb < -1.2:
                continue
            hole = 1.0 if limb >= 0.0 else max(0.0, 1.0 + limb)
            distance = max(limb, 0.0)
            angle = math.atan2(dy, dx)
            # Peak on the rim, then a soft radial bloom. No filled discs and no
            # polygonal streamers — light falls off like a solar sheath.
            limb_glow = math.exp(-(distance / 5.0) ** 2)
            halo = math.exp(-(distance / 38.0) ** 2)
            alpha = hole * min(1.0, limb_glow * 0.58 + halo * 0.36)
            if alpha < 0.015:
                continue
            warm_amt = 0.5 + 0.5 * math.cos(angle - math.radians(205))
            red, green, blue = mix(cool, warm, warm_amt)
            index = (y * SIZE + x) * 4
            pixels[index] = int(red)
            pixels[index + 1] = int(green)
            pixels[index + 2] = int(blue)
            pixels[index + 3] = int(alpha * 255.0)
    OUT.write_bytes(png_rgba(SIZE, SIZE, pixels))
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
