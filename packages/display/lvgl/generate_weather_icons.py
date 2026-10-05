#!/usr/bin/env python3
"""Bake 32x32 sky icons for the overview forecast strip from design tokens."""

from __future__ import annotations

import math
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TOKENS = ROOT / "tokens.generated.yaml"
SIZE = 32


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


def blank() -> list[list[tuple[float, float, float, float]]]:
    return [[(0.0, 0.0, 0.0, 0.0) for _ in range(SIZE)] for _ in range(SIZE)]


def stamp_circle(
    canvas: list[list[tuple[float, float, float, float]]],
    cx: float,
    cy: float,
    radius: float,
    color: tuple[int, int, int],
    alpha: float = 1.0,
) -> None:
    r2 = radius * radius
    for y in range(SIZE):
        for x in range(SIZE):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 > (radius + 1.0) ** 2:
                continue
            edge = max(0.0, 1.0 - (math.sqrt(d2) - radius))
            cover = min(1.0, edge) * alpha
            if cover <= 0.0:
                continue
            sr, sg, sb, sa = canvas[y][x]
            out_a = cover + sa * (1.0 - cover)
            if out_a <= 0.0:
                continue
            canvas[y][x] = (
                (color[0] * cover + sr * sa * (1.0 - cover)) / out_a,
                (color[1] * cover + sg * sa * (1.0 - cover)) / out_a,
                (color[2] * cover + sb * sa * (1.0 - cover)) / out_a,
                out_a,
            )


def stamp_disc_cut(
    canvas: list[list[tuple[float, float, float, float]]],
    cx: float,
    cy: float,
    radius: float,
) -> None:
    for y in range(SIZE):
        for x in range(SIZE):
            if (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius:
                canvas[y][x] = (0.0, 0.0, 0.0, 0.0)


def stamp_ray(
    canvas: list[list[tuple[float, float, float, float]]],
    cx: float,
    cy: float,
    angle: float,
    inner: float,
    outer: float,
    color: tuple[int, int, int],
) -> None:
    dx = math.cos(angle)
    dy = math.sin(angle)
    steps = int(outer - inner + 2)
    for i in range(steps):
        t = inner + i
        stamp_circle(canvas, cx + dx * t, cy + dy * t, 1.15, color)


def write(name: str, canvas: list[list[tuple[float, float, float, float]]]) -> None:
    pixels = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = canvas[y][x]
            index = (y * SIZE + x) * 4
            pixels[index] = int(max(0.0, min(255.0, r)))
            pixels[index + 1] = int(max(0.0, min(255.0, g)))
            pixels[index + 2] = int(max(0.0, min(255.0, b)))
            pixels[index + 3] = int(max(0.0, min(255.0, a * 255.0)))
    path = ROOT / name
    path.write_bytes(png_rgba(SIZE, SIZE, pixels))
    print(f"wrote {path} ({path.stat().st_size} bytes)")


def paint_sun(canvas: list[list[tuple[float, float, float, float]]], cx: float, cy: float, scale: float) -> None:
    solar = token_rgb("series_solar")
    for i in range(8):
        stamp_ray(canvas, cx, cy, i * math.pi / 4, 7.2 * scale, 11.4 * scale, solar)
    stamp_circle(canvas, cx, cy, 6.2 * scale, solar)


def paint_cloud(canvas: list[list[tuple[float, float, float, float]]], ox: float, oy: float) -> None:
    cloud = token_rgb("text")
    stamp_circle(canvas, 10 + ox, 18 + oy, 6.2, cloud, 0.92)
    stamp_circle(canvas, 17 + ox, 16 + oy, 7.4, cloud, 0.95)
    stamp_circle(canvas, 23 + ox, 19 + oy, 5.6, cloud, 0.92)


def paint_moon(canvas: list[list[tuple[float, float, float, float]]], cx: float, cy: float, scale: float) -> None:
    text = token_rgb("text")
    stamp_circle(canvas, cx, cy, 9.2 * scale, text)
    stamp_disc_cut(canvas, cx + 5.5 * scale, cy - 3.5 * scale, 8.4 * scale)


def paint_drops(canvas: list[list[tuple[float, float, float, float]]]) -> None:
    drop = token_rgb("text")
    for x, y in ((9.5, 24.5), (16.0, 27.5), (23.0, 24.8)):
        stamp_circle(canvas, x, y, 2.3, drop)
        stamp_circle(canvas, x + 0.2, y + 2.6, 1.6, drop)


def paint_snow(canvas: list[list[tuple[float, float, float, float]]]) -> None:
    text = token_rgb("text")
    for x, y in ((10.0, 26.0), (16.0, 28.5), (22.5, 25.5)):
        stamp_circle(canvas, x, y, 1.6, text)
        stamp_ray(canvas, x, y, 0.0, 1.4, 4.2, text)
        stamp_ray(canvas, x, y, math.pi / 2, 1.4, 4.2, text)
        stamp_ray(canvas, x, y, math.pi / 4, 1.4, 3.6, text)
        stamp_ray(canvas, x, y, -math.pi / 4, 1.4, 3.6, text)


def main() -> None:
    sun = blank()
    paint_sun(sun, 15.5, 15.5, 1.0)
    write("weather_sun.png", sun)

    partly = blank()
    paint_sun(partly, 20.5, 11.0, 0.72)
    paint_cloud(partly, 0.0, 2.0)
    write("weather_partly.png", partly)

    cloudy = blank()
    paint_cloud(cloudy, 1.0, 0.0)
    write("weather_cloud.png", cloudy)

    moon = blank()
    paint_moon(moon, 16.0, 16.0, 1.0)
    write("weather_moon.png", moon)

    partly_night = blank()
    paint_moon(partly_night, 21.5, 10.0, 0.88)
    paint_cloud(partly_night, -3.0, 5.0)
    write("weather_partly_night.png", partly_night)

    rain = blank()
    paint_cloud(rain, 1.0, -5.0)
    paint_drops(rain)
    write("weather_rain.png", rain)

    snow = blank()
    paint_cloud(snow, 1.0, -5.0)
    paint_snow(snow)
    write("weather_snow.png", snow)


if __name__ == "__main__":
    main()
