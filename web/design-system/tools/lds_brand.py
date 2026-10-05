"""Emit Lune brand marks from tokens/brand.json (moved from Birkemosen/lds).

The six-pipe thermal halo is the product mark. Geometry and colours come from
tokens; this module only renders SVG, PNG, and a small JS helper.
"""

from __future__ import annotations

import math
import struct
import zlib

HEADER = "Generated from lune-design-system tokens/brand.json by tools/lds_display.py. Do not edit."


def _hex_rgb(value: str) -> tuple[int, int, int]:
    raw = value.lstrip("#")
    return int(raw[0:2], 16), int(raw[2:4], 16), int(raw[4:6], 16)


def _parse_rgba(value: str) -> tuple[float, float, float, float] | None:
    if not value.startswith("rgba("):
        return None
    body = value[value.index("(") + 1 : value.rindex(")")]
    parts = [p.strip() for p in body.split(",")]
    return float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])


def _rgba(value: str) -> tuple[float, float, float, float]:
    parsed = _parse_rgba(value)
    if parsed:
        return parsed
    r, g, b = _hex_rgb(value)
    return r, g, b, 1.0


def _thermal_at(tokens: dict, t: float) -> tuple[int, int, int]:
    thermal = tokens["color"]["thermal"]
    supply = _hex_rgb(thermal["supply"])
    heat = _hex_rgb(thermal["heat"])
    ret = _hex_rgb(thermal["return"])
    stop = thermal["heat-stop"] / 100.0
    t = max(0.0, min(1.0, t))
    if t <= stop:
        u = t / stop if stop else 0.0
        a, b = supply, heat
    else:
        u = (t - stop) / (1.0 - stop) if stop < 1 else 1.0
        a, b = heat, ret
    return (
        int(round(a[0] + (b[0] - a[0]) * u)),
        int(round(a[1] + (b[1] - a[1]) * u)),
        int(round(a[2] + (b[2] - a[2]) * u)),
    )


def _pipe_xs(tokens: dict) -> list[float]:
    return [float(x) for x in tokens["brand"]["pipe-xs"]]


def _defs(tokens: dict, prefix: str, blur: float = 6.0) -> str:
    thermal = tokens["color"]["thermal"]
    metal = tokens["color"]["metallic"]
    return f'''    <defs>
      <filter id="{prefix}-glow" x="-50%" y="-50%" width="200%" height="200%">
        <feGaussianBlur stdDeviation="{blur}" result="blur"/>
        <feComposite in="SourceGraphic" in2="blur" operator="over"/>
      </filter>
      <linearGradient id="{prefix}-thermal" gradientUnits="userSpaceOnUse" x1="120" y1="175" x2="280" y2="175">
        <stop offset="0%" stop-color="{thermal["supply"]}"/>
        <stop offset="{thermal["heat-stop"]}%" stop-color="{thermal["heat"]}"/>
        <stop offset="100%" stop-color="{thermal["return"]}"/>
      </linearGradient>
      <linearGradient id="{prefix}-metallic" x1="0%" y1="0%" x2="0%" y2="100%">
        <stop offset="0%" stop-color="{metal["top"]}"/>
        <stop offset="100%" stop-color="{metal["bottom"]}"/>
      </linearGradient>
    </defs>'''


def _rotate_scale(brand: dict, scale: float | None) -> str:
    cx, cy = brand["cx"], brand["cy"]
    parts = [f"translate({cx} {cy})", "rotate(-90)"]
    if scale and scale != 1:
        parts.append(f"scale({scale})")
    parts.append(f"translate({-cx} {-cy})")
    return " ".join(parts)


def emit_mark_svg(
    tokens: dict,
    *,
    sku: str = "",
    landscape: bool = False,
    lockup: bool = False,
    card: bool = False,
    halo_only: bool = False,
    prefix: str = "lm",
) -> str:
    brand = tokens["brand"]
    metal = tokens["color"]["metallic"]
    dark = tokens["color"]["neutral"]["dark"]
    cx, cy = brand["cx"], brand["cy"]
    scale = brand["lockup-scale"] if lockup else None
    cut_r = brand["lockup-cut-r"] if lockup else brand["cut-r"]
    disc_r = brand["lockup-disc-r"] if lockup else brand["disc-r"]
    turn = _rotate_scale(brand, scale) if landscape or lockup else ""
    stroke = brand["stroke"]
    halo_r = brand["halo-r"]
    word = ""
    if sku and not halo_only:
        if sku.upper() == "V6":
            size, tracking, y = brand["v6-size"], brand["v6-tracking"], brand["v6-y"]
        else:
            size, tracking, y = brand["lune-size"], brand["lune-tracking"], brand["lune-y"]
        word = (
            f'    <text x="{cx}" y="{y}" fill="{metal["word"]}" '
            f'font-family="system-ui, -apple-system, sans-serif" font-size="{size}" '
            f'font-weight="700" letter-spacing="{tracking}" text-anchor="middle">{sku}</text>'
        )
    pipes = "\n".join(
        f'      <line x1="{x}" y1="{brand["pipe-from"]}" x2="{x}" y2="{brand["pipe-to"]}"/>'
        for x in _pipe_xs(tokens)
    )
    transform_attr = f' transform="{turn}"' if turn else ""
    blur = 10 if card else (5 if lockup else 6)
    if card:
        view_box = "0 0 400 400"
        card_rect = f'    <rect width="400" height="400" fill="{dark["surface"]}" rx="24"/>\n'
    elif lockup:
        view_box = brand["viewBox-lockup"]
        card_rect = ""
    elif landscape:
        view_box = brand["viewBox-landscape"]
        card_rect = ""
    else:
        view_box = brand["viewBox-portrait"]
        card_rect = ""
    label = "Lune Touch" if lockup else ("Lune V6" if sku == "V6" else "Lune")
    body = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view_box}" fill="none" role="img" aria-label="{label}">
  <!-- {HEADER} -->
{_defs(tokens, prefix, blur=blur)}
{card_rect}    <g{transform_attr} stroke="url(#{prefix}-thermal)" stroke-width="{stroke}" stroke-linecap="round" fill="none" opacity="0.95" filter="url(#{prefix}-glow)">
{pipes}
    </g>
    <circle cx="{cx}" cy="{cy}" r="{cut_r}" fill="{dark["bg"] if not card else dark["surface"]}"/>
    <path{transform_attr} d="M{cx - halo_r} {cy}A{halo_r} {halo_r} 0 0 1 {cx + halo_r} {cy}" fill="none" stroke="url(#{prefix}-thermal)" stroke-width="{stroke}" stroke-linecap="round" filter="url(#{prefix}-glow)"/>
    <circle cx="{cx}" cy="{cy}" r="{disc_r}" fill="url(#{prefix}-metallic)" stroke="{metal["rim"]}" stroke-width="{brand["disc-stroke"]}"/>
{word}
</svg>
'''
    return body


def emit_js(tokens: dict) -> str:
    brand = tokens["brand"]
    thermal = tokens["color"]["thermal"]
    pipe = tokens["color"]["pipe"]
    metal = tokens["color"]["metallic"]
    xs = ", ".join(str(x) for x in brand["pipe-xs"])
    return f"""// {HEADER}
export const brand = {{
  cx: {brand["cx"]},
  cy: {brand["cy"]},
  haloR: {brand["halo-r"]},
  cutR: {brand["cut-r"]},
  discR: {brand["disc-r"]},
  stroke: {brand["stroke"]},
  pipeXs: [{xs}],
  pipeFrom: {brand["pipe-from"]},
  pipeTo: {brand["pipe-to"]},
  lockupScale: {brand["lockup-scale"]},
  lockupCutR: {brand["lockup-cut-r"]},
  lockupDiscR: {brand["lockup-disc-r"]},
  luneSize: {brand["lune-size"]},
  luneTracking: {brand["lune-tracking"]},
  luneY: {brand["lune-y"]},
  v6Size: {brand["v6-size"]},
  v6Tracking: {brand["v6-tracking"]},
  v6Y: {brand["v6-y"]},
  viewBoxPortrait: {brand["viewBox-portrait"]!r},
  viewBoxLandscape: {brand["viewBox-landscape"]!r},
  viewBoxLockup: {brand["viewBox-lockup"]!r},
  lockupWidth: {brand["lockup-width"]},
  lockupHeight: {brand["lockup-height"]},
  manifoldWidth: {brand["manifold-width"]},
  manifoldHeight: {brand["manifold-height"]},
  thermal: {{ supply: {thermal["supply"]!r}, heat: {thermal["heat"]!r}, ret: {thermal["return"]!r}, heatStop: {thermal["heat-stop"]} }},
  pipe: {{ calling: {pipe["calling"]!r}, idle: {pipe["idle"]!r}, unused: {pipe["unused"]!r} }},
  metallic: {{ top: {metal["top"]!r}, bottom: {metal["bottom"]!r}, rim: {metal["rim"]!r}, word: {metal["word"]!r} }},
}};

let _markSeq = 0;

function _turn(scale) {{
  const {{ cx, cy }} = brand;
  const bits = [`translate(${{cx}} ${{cy}})`, "rotate(-90)"];
  if (scale && scale !== 1) bits.push(`scale(${{scale}})`);
  bits.push(`translate(${{-cx}} ${{-cy}})`);
  return bits.join(" ");
}}

function _defs(prefix) {{
  const t = brand.thermal;
  const m = brand.metallic;
  return `<defs>
    <filter id="${{prefix}}-glow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur stdDeviation="5" result="blur"/>
      <feComposite in="SourceGraphic" in2="blur" operator="over"/>
    </filter>
    <linearGradient id="${{prefix}}-thermal" gradientUnits="userSpaceOnUse" x1="120" y1="175" x2="280" y2="175">
      <stop offset="0%" stop-color="${{t.supply}}"/>
      <stop offset="${{t.heatStop}}%" stop-color="${{t.heat}}"/>
      <stop offset="100%" stop-color="${{t.ret}}"/>
    </linearGradient>
    <linearGradient id="${{prefix}}-metallic" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="${{m.top}}"/>
      <stop offset="100%" stop-color="${{m.bottom}}"/>
    </linearGradient>
  </defs>`;
}}

/** Live 6-pipe mark. `states[i]` is "calling" | "idle" | "unused". */
export function luneMark({{
  states = [],
  selected = -1,
  sku = "",
  landscape = false,
  lockup = false,
  prefix = "",
}} = {{}}) {{
  const id = prefix || `lm${{++_markSeq}}`;
  const scale = lockup ? brand.lockupScale : null;
  const cutR = lockup ? brand.lockupCutR : brand.cutR;
  const discR = lockup ? brand.lockupDiscR : brand.discR;
  const turn = landscape || lockup ? ` transform="${{_turn(scale)}}"` : "";
  const viewBox = lockup
    ? brand.viewBoxLockup
    : landscape
      ? brand.viewBoxLandscape
      : brand.viewBoxPortrait;
  const pipes = brand.pipeXs.map((x, i) => {{
    const kind = states[i] || "idle";
    const focus = i === selected ? " is-focus" : "";
    return `<line class="pipe is-${{kind}}${{focus}}" x1="${{x}}" y1="${{brand.pipeFrom}}" x2="${{x}}" y2="${{brand.pipeTo}}"/>`;
  }}).join("");
  let word = "";
  if (sku) {{
    const isV6 = sku.toUpperCase() === "V6";
    const size = isV6 ? brand.v6Size : brand.luneSize;
    const tracking = isV6 ? brand.v6Tracking : brand.luneTracking;
    const y = isV6 ? brand.v6Y : brand.luneY;
    word = `<text class="sku" x="${{brand.cx}}" y="${{y}}" text-anchor="middle" fill="${{brand.metallic.word}}" font-size="${{size}}" font-weight="700" letter-spacing="${{tracking}}" font-family="system-ui,-apple-system,sans-serif">${{sku}}</text>`;
  }}
  return `<svg class="lune-mark${{landscape || lockup ? " is-landscape" : ""}}" viewBox="${{viewBox}}" fill="none" aria-hidden="true">
    ${{_defs(id)}}
    <g class="pipes"${{turn}} stroke="url(#${{id}}-thermal)" stroke-width="${{brand.stroke}}" stroke-linecap="round" fill="none">${{pipes}}</g>
    <circle class="disc-cut" cx="${{brand.cx}}" cy="${{brand.cy}}" r="${{cutR}}"></circle>
    <path class="halo-arc"${{turn}} d="M${{brand.cx - brand.haloR}} ${{brand.cy}}A${{brand.haloR}} ${{brand.haloR}} 0 0 1 ${{brand.cx + brand.haloR}} ${{brand.cy}}" fill="none" stroke="url(#${{id}}-thermal)" stroke-width="${{brand.stroke}}" stroke-linecap="round" filter="url(#${{id}}-glow)"></path>
    <circle class="disc" cx="${{brand.cx}}" cy="${{brand.cy}}" r="${{discR}}"></circle>
    ${{word}}
  </svg>`;
}}

export function luneTouchLockup() {{
  return luneMark({{ sku: "LUNE", lockup: true, prefix: "lt-lockup" }});
}}

export function luneV6Mark() {{
  return luneMark({{ sku: "V6", landscape: true, prefix: "v6-mark" }});
}}
"""


def emit_mark_css(tokens: dict) -> str:
    brand = tokens["brand"]
    pipe = tokens["color"]["pipe"]
    metal = tokens["color"]["metallic"]
    thermal = tokens["color"]["thermal"]
    return "\n".join(
        [
            f"  --thermal-supply: {thermal['supply']};",
            f"  --thermal-heat: {thermal['heat']};",
            f"  --thermal-return: {thermal['return']};",
            f"  --pipe-calling: {pipe['calling']};",
            f"  --pipe-idle: {pipe['idle']};",
            f"  --pipe-unused: {pipe['unused']};",
            f"  --brand-word: {metal['word']};",
            f"  --brand-rim: {metal['rim']};",
            f"  --brand-lockup-w: {brand['lockup-width']}px;",
            f"  --brand-lockup-h: {brand['lockup-height']}px;",
            f"  --brand-manifold-w: {brand['manifold-width']}px;",
            f"  --brand-manifold-h: {brand['manifold-height']}px;",
        ]
    )


def emit_lockup_css() -> str:
    return """
.lune-lockup { display: flex; align-items: center; gap: 8px; }
.lune-lockup svg { display: block; width: var(--brand-lockup-w); height: var(--brand-lockup-h); flex: 0 0 auto; overflow: visible; }
.lune-lockup .product { color: var(--text-strong); font-size: .72rem; font-weight: 700; letter-spacing: .18em; text-transform: uppercase; }
.lune-mark.is-landscape { width: var(--brand-manifold-w); height: var(--brand-manifold-h); }
.lune-mark .halo-arc { fill: none; stroke-linecap: round; }
.lune-mark .disc-cut { fill: var(--bg); }
.lune-mark .disc { fill: #0c0b09; stroke: var(--brand-rim, #25221E); stroke-width: 1.5; }
.lune-mark .pipe { fill: none; stroke-linecap: round; }
.lune-mark .pipe.is-calling { stroke: var(--pipe-calling); filter: drop-shadow(0 0 4px var(--pipe-calling)); stroke-dasharray: 7 9; animation: lune-pipe-run 1.05s linear infinite; }
.lune-mark .pipe.is-idle { stroke: var(--pipe-idle); opacity: .42; }
.lune-mark .pipe.is-unused { stroke: var(--pipe-unused); }
.lune-mark .pipe.is-focus { stroke-width: 8.5; }
.lune-mark .sku, .lune-mark .word { fill: var(--brand-word, #FAF6EF); font-family: var(--font-ui); font-weight: 700; }
@keyframes lune-pipe-run { to { stroke-dashoffset: -16; } }
@media (prefers-reduced-motion: reduce) {
  .lune-mark .pipe.is-calling { animation: none; stroke-dasharray: none; }
}
"""


def _png(width: int, height: int, pixels: bytearray) -> bytes:
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


def _blend(dst: bytearray, i: int, r: int, g: int, b: int, a: float) -> None:
    if a <= 0:
        return
    a = min(1.0, a)
    dr, dg, db, da = dst[i], dst[i + 1], dst[i + 2], dst[i + 3] / 255.0
    out_a = a + da * (1 - a)
    if out_a <= 0:
        return
    dst[i] = int(round((r * a + dr * da * (1 - a)) / out_a))
    dst[i + 1] = int(round((g * a + dg * da * (1 - a)) / out_a))
    dst[i + 2] = int(round((b * a + db * da * (1 - a)) / out_a))
    dst[i + 3] = int(round(out_a * 255))


def _capsule_d(px: float, py: float, x1: float, y1: float, x2: float, y2: float) -> float:
    vx, vy = x2 - x1, y2 - y1
    length = math.hypot(vx, vy) or 1.0
    t = max(0.0, min(1.0, ((px - x1) * vx + (py - y1) * vy) / (length * length)))
    return math.hypot(px - (x1 + vx * t), py - (y1 + vy * t))


def _rotate_pt(x: float, y: float, cx: float, cy: float, scale: float, landscape: bool) -> tuple[float, float]:
    rx, ry = x - cx, y - cy
    if scale != 1:
        rx *= scale
        ry *= scale
    if landscape:
        # SVG rotate(-90) with y-down: (x, y) -> (y, -x)
        rx, ry = ry, -rx
    return cx + rx, cy + ry


def emit_png(
    tokens: dict,
    *,
    landscape: bool = False,
    lockup: bool = False,
    halo_only: bool = False,
    width: int = 220,
    height: int = 160,
) -> bytes:
    brand = tokens["brand"]
    metal = tokens["color"]["metallic"]
    dark = tokens["color"]["neutral"]["dark"]
    bg = _hex_rgb(dark["bg"])
    disc = _hex_rgb(metal["bottom"])
    rim = _hex_rgb(metal["rim"])
    cx, cy = float(brand["cx"]), float(brand["cy"])
    scale = float(brand["lockup-scale"]) if lockup else 1.0
    cut_r = float(brand["lockup-cut-r"] if lockup else brand["cut-r"])
    disc_r = float(brand["lockup-disc-r"] if lockup else brand["disc-r"])
    halo_r = float(brand["halo-r"]) * scale
    stroke = float(brand["stroke"]) * scale
    view = [float(v) for v in (brand["viewBox-lockup"] if lockup else brand["viewBox-landscape"] if landscape else brand["viewBox-portrait"]).split()]
    vx, vy, vw, vh = view
    xs = _pipe_xs(tokens)
    y1, y2 = float(brand["pipe-from"]), float(brand["pipe-to"])
    pixels = bytearray(width * height * 4)
    turn = landscape or lockup

    def map_pt(x: float, y: float) -> tuple[float, float]:
        tx, ty = _rotate_pt(x, y, cx, cy, scale, turn)
        return (tx - vx) * width / vw, (ty - vy) * height / vh

    mcx, mcy = map_pt(cx, cy)
    scale_x = width / vw
    scale_y = height / vh
    px_r = disc_r * scale_x
    cut_px = cut_r * scale_x
    halo_px = halo_r * scale_x
    stroke_px = stroke * ((scale_x + scale_y) / 2)
    glow_px = stroke_px * 0.9

    pipes: list[tuple[tuple[float, float], tuple[float, float], tuple[int, int, int]]] = []
    xmin, xmax = min(xs), max(xs)
    span = xmax - xmin or 1.0
    for x in xs:
        color = _thermal_at(tokens, (x - xmin) / span)
        p1 = map_pt(x, y1)
        p2 = map_pt(x, y2)
        pipes.append((p1, p2, color))

    # Arc endpoints after the same transform: original top semicircle.
    arc_samples = []
    for i in range(49):
        ang = math.pi + (math.pi * i / 48.0)  # pi .. 2pi (top half in y-down? original path is top: y <= cy)
        # Original: M left,cy A r r 0 0 1 right,cy — sweep 1 with y-down is the UPPER half.
        ax = cx + halo_r / scale * math.cos(math.pi - ang) if scale else cx
        # Simpler: sample original arc then transform.
        theta = math.pi * i / 48.0  # 0 left to pi right along top
        ox = cx + math.cos(math.pi - theta) * brand["halo-r"]
        oy = cy - math.sin(theta) * brand["halo-r"]
        arc_samples.append(map_pt(ox, oy))

    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4
            d_disc = math.hypot(x - mcx, y - mcy)
            best_pipe = 1e9
            pipe_color = (0, 0, 0)
            for p1, p2, color in pipes:
                d = _capsule_d(x + 0.5, y + 0.5, p1[0], p1[1], p2[0], p2[1])
                if d < best_pipe:
                    best_pipe = d
                    pipe_color = color
            # Hide pipe under the cut disc.
            if d_disc < cut_px:
                best_pipe = 1e9
            pipe_a = 0.0
            half = max(1.15, stroke_px * 0.42)
            if best_pipe < half + glow_px:
                core = 1.0 if best_pipe <= half else max(0.0, 1.0 - (best_pipe - half) / max(half * 0.35, 0.4))
                glow = math.exp(-(max(0.0, best_pipe - half) / max(glow_px, 0.8)) ** 2) * 0.28
                pipe_a = min(1.0, core * 0.96 + glow)
            best_arc = 1e9
            for a, b in zip(arc_samples, arc_samples[1:]):
                best_arc = min(best_arc, _capsule_d(x + 0.5, y + 0.5, a[0], a[1], b[0], b[1]))
            arc_a = 0.0
            if best_arc < half + glow_px:
                core = 1.0 if best_arc <= half else max(0.0, 1.0 - (best_arc - half) / max(half * 0.35, 0.4))
                glow = math.exp(-(max(0.0, best_arc - half) / max(glow_px, 0.8)) ** 2) * 0.28
                arc_a = min(1.0, core * 0.96 + glow)
            if pipe_a > 0:
                _blend(pixels, i, pipe_color[0], pipe_color[1], pipe_color[2], pipe_a)
            if arc_a > 0:
                t = max(0.0, min(1.0, (x + 0.5 - (mcx - halo_px)) / max(halo_px * 2, 1)))
                col = _thermal_at(tokens, t if not turn else (y + 0.5 - (mcy - halo_px)) / max(halo_px * 2, 1))
                _blend(pixels, i, col[0], col[1], col[2], arc_a)
            if d_disc <= px_r + 1.2:
                rim_w = max(1.1, brand["disc-stroke"] * scale_x)
                if d_disc >= px_r - rim_w:
                    pixels[i] = rim[0]
                    pixels[i + 1] = rim[1]
                    pixels[i + 2] = rim[2]
                    pixels[i + 3] = 255
                elif not halo_only:
                    shade = (y - (mcy - px_r)) / max(px_r * 2, 1)
                    top = _hex_rgb(metal["top"])
                    bot = disc
                    shade = max(0.0, min(1.0, shade))
                    pixels[i] = int(top[0] + (bot[0] - top[0]) * shade)
                    pixels[i + 1] = int(top[1] + (bot[1] - top[1]) * shade)
                    pixels[i + 2] = int(top[2] + (bot[2] - top[2]) * shade)
                    pixels[i + 3] = 255
                else:
                    pixels[i] = bg[0]
                    pixels[i + 1] = bg[1]
                    pixels[i + 2] = bg[2]
                    pixels[i + 3] = 255

    return _png(width, height, pixels)


def brand_artifacts(tokens: dict) -> dict[str, str | bytes]:
    """Logical artifact name -> file contents."""
    return {
        "lune-mark.svg": emit_mark_svg(tokens, sku="LUNE", prefix="lm"),
        "lune-mark-card.svg": emit_mark_svg(tokens, sku="LUNE", card=True, prefix="lm-card"),
        "lune-touch-lockup.svg": emit_mark_svg(tokens, sku="LUNE", lockup=True, prefix="lt"),
        "lune-v6-mark.svg": emit_mark_svg(tokens, sku="V6", landscape=True, prefix="v6"),
        "lune-halo.svg": emit_mark_svg(tokens, halo_only=True, prefix="lh"),
        "lune-mark.generated.js": emit_js(tokens),
        "lune_v6_mark.png": emit_png(tokens, landscape=True, width=108, height=80),
        "lune_halo.png": emit_png(tokens, landscape=False, halo_only=True, width=180, height=210),
        "lune_touch_lockup.png": emit_png(tokens, lockup=True, width=80, height=48),
    }
