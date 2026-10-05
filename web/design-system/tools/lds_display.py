#!/usr/bin/env python3
"""
Lune Design System — vægskærm (LVGL / RGB565): palet-tjek og firmware-artefakter.

    python tools/lds_display.py --check                      # kun palet og kontrast
    python tools/lds_display.py --install ../lune-coordinator  # skriv artefakter
    python tools/lds_display.py --install ../lune-coordinator --check  # diff mod committet

Læser `display` i tokens/tokens.json og brand-mærker fra tokens/brand.json. Hex ↔
RGB565 med afrunding (r·31/255, g·63/255, b·31/255). `raised` i lyst tema er
håndplukket så kvantiseringen ikke giver grønt stik — den lagrede 565-værdi skal
matches. Firmware-artefakterne bruger den udfoldede 565-farve (det skærmen viser).

Produktrepoet har en `lds.yaml` (schema 2) med `- kind:` / `dest:`. Kendte kinds:
  display-header  LVGL C-header (LDS_DARK_* / LDS_LIGHT_* / LDS_FS_* / mål / tider)
  display-yaml    ESPHome substitutions (lds_*, lds_light_*, lds_night_*)
  display-cpp     C++-konstanter i lune::tokens (mørkt tema, uden lvgl.h)
  brand-png       lune_v6_mark.png, lune_halo.png, lune_touch_lockup.png i dest/
  brand-svg       de fem SVG-mærker i dest/
  logo            lune-mark-card.svg som dest
Andre kinds (css, html, binder …) ignoreres her; dem bygger lds_build.py.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from oklab import cr  # noqa: E402

TOK = json.loads((ROOT / "tokens/tokens.json").read_text(encoding="utf-8"))
BRAND_TOK = ROOT / "tokens/brand.json"
GEN = "Genereret af lune-design-system tools/lds_display.py fra tokens/tokens.json — redigér ikke."

# Rækkefølge og navne i de genererede filer (palet-nøgle → navn).
PALETTE_ORDER = (
    "bg", "card", "raised", "field", "line", "seg-off", "fg", "muted", "faint",
    "accent", "accent-ink", "on-accent", "info", "ok", "warn", "danger", "violet",
    "info-bg", "ok-bg", "warn-bg", "danger-bg", "violet-bg", "inv-bg", "inv-fg",
)
PNG_NAMES = ("lune_v6_mark.png", "lune_halo.png", "lune_touch_lockup.png")
SVG_NAMES = ("lune-mark.svg", "lune-mark-card.svg", "lune-touch-lockup.svg",
             "lune-v6-mark.svg", "lune-halo.svg")


def hex_to_rgb565(hex_color: str) -> int:
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return (round(r * 31 / 255) << 11) | (round(g * 63 / 255) << 5) | round(b * 31 / 255)


def parse_rgb565(s: str) -> int:
    return int(str(s), 0)


def check_palette(theme: str) -> list[str]:
    errs = []
    pal = TOK.get("display", {}).get("palette", {}).get(theme)
    if not pal:
        return [f"display.palette.{theme} mangler"]
    for name, entry in pal.items():
        hx = entry.get("hex")
        rs = entry.get("rgb565")
        if not hx or not rs:
            errs.append(f"{theme}.{name}: mangler hex eller rgb565")
            continue
        got = hex_to_rgb565(hx)
        want = parse_rgb565(rs)
        if got != want:
            # raised light is hand-picked: stored value is authoritative if noted
            if name == "raised" and theme == "light" and entry.get("note"):
                if want != 0xD699:
                    errs.append(
                        f"{theme}.raised: håndplukket rgb565 skal være 0xD699, fik {rs}"
                    )
                elif got != want:
                    # hex must still round to the hand-picked value
                    errs.append(
                        f"{theme}.raised: hex {hx} afrunder til 0x{got:04X}, "
                        f"forventet håndplukket 0x{want:04X}"
                    )
            else:
                errs.append(
                    f"{theme}.{name}: {hx} → 0x{got:04X}, tokens siger {rs}"
                )
    return errs


def check_contrast_light() -> list[str]:
    """Same spirit as web light checks, on the wall palette hexes."""
    pal = TOK["display"]["palette"]["light"]
    H = {k: v["hex"] for k, v in pal.items()}
    errs = []
    reqs = [
        ("fg", "card", 7),
        ("muted", "card", 4.5),
        ("faint", "card", 4.5),
        ("card", "bg", 1.24),
        ("info", "card", 4.5),
        ("ok", "card", 4.5),
        ("warn", "card", 4.5),
        ("danger", "card", 4.5),
        ("violet", "card", 4.5),
        ("info", "info-bg", 4.5),
        ("ok", "ok-bg", 4.5),
        ("warn", "warn-bg", 4.5),
        ("danger", "danger-bg", 4.5),
        ("violet", "violet-bg", 4.5),
    ]
    for a, b, need in reqs:
        if a not in H or b not in H:
            continue
        r = cr(H[a], H[b])
        ok = r >= need
        print(f"{'OK ' if ok else 'FEJL'}  display {a:>10} på {b:<10} light {r:5.2f}:1  (krav {need})")
        if not ok:
            errs.append(f"display {a}/{b} {r:.2f} < {need}")
    return errs


def rgb565_to_rgb(v: int) -> int:
    """Udfold 565 til 24-bit: den farve skærmen faktisk viser."""
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return (round(r * 255 / 31) << 16) | (round(g * 255 / 63) << 8) | round(b * 255 / 31)


def screen_colors(theme: str) -> list[tuple[str, int, int]]:
    """(navn, 24-bit, 565) i PALETTE_ORDER for et tema."""
    pal = TOK["display"]["palette"][theme]
    missing = [k for k in PALETTE_ORDER if k not in pal]
    if missing:
        raise SystemExit(f"display.palette.{theme} mangler {missing}")
    out = []
    for key in PALETTE_ORDER:
        v565 = parse_rgb565(pal[key]["rgb565"])
        out.append((key.replace("-", "_"), rgb565_to_rgb(v565), v565))
    return out


def night_colors() -> list[tuple[str, int]]:
    night = TOK["display"]["night"]
    return [(k, int(v.lstrip("#"), 16)) for k, v in night.items() if not k.startswith("$")]


def emit_lvgl_header() -> str:
    d = TOK["display"]
    w, h = d["resolution"]
    lines = [f"// {GEN}", f"// Lune Design System · vægskærm {w}×{h} · RGB565", "#pragma once",
             '#include "lvgl.h"', ""]
    for theme in ("dark", "light"):
        lines.append(f"// ---- {theme} ----")
        for name, rgb, v565 in screen_colors(theme):
            macro = f"LDS_{theme.upper()}_{name.upper()}"
            lines.append(f"#define {macro:<22} lv_color_hex(0x{rgb:06x})   /* 565: 0x{v565:04X} */")
        lines.append("")
    digits = ", ".join(x.upper() for x in d["type_digits_only"] if not x.startswith("d"))
    lines.append(f"// ---- typografi (px) — {digits}: kun cifre , ° − ----")
    for k, v in d["type"].items():
        lines.append(f"#define {'LDS_FS_' + k.upper():<13} {v}")
    lines += ["", "// ---- night (dæmpet, uden for paletten) ----"]
    for name, rgb in night_colors():
        lines.append(f"#define {'LDS_NIGHT_' + name.upper():<17} lv_color_hex(0x{rgb:06x})")
    lines += ["", "// ---- mål (px) ----"]
    for k, v in d["size"].items():
        lines.append(f"#define {'LDS_' + k.upper():<16} {v}")
    lines += ["", "// ---- tider ----"]
    for k, v in d["timing"].items():
        if isinstance(v, (int, float)):
            lines.append(f"#define {'LDS_' + k.upper():<18} {v}")
    return "\n".join(lines) + "\n"


def emit_lvgl_yaml() -> str:
    lines = [f"# {GEN}", "# Mørkt tema er lds_*, lyst lds_light_*, nat lds_night_*.", "substitutions:"]
    for theme, prefix in (("dark", "lds_"), ("light", "lds_light_")):
        for name, rgb, _ in screen_colors(theme):
            lines.append(f'  {prefix}{name}: "0x{rgb:06X}"')
    for name, rgb in night_colors():
        lines.append(f'  lds_night_{name}: "0x{rgb:06X}"')
    return "\n".join(lines) + "\n"


def emit_cpp_header() -> str:
    lines = [f"// {GEN}", "// Mørkt tema som 24-bit RGB til kode uden lvgl.h (fx lv_color_hex(lune::tokens::kOk)).",
             "#pragma once", "", "#include <cstdint>", "", "namespace lune::tokens {", ""]
    for name, rgb, _ in screen_colors("dark"):
        ident = "k" + "".join(p.capitalize() for p in name.split("_"))
        lines.append(f"constexpr uint32_t {ident} = 0x{rgb:06X};")
    lines += ["", "}  // namespace lune::tokens"]
    return "\n".join(lines) + "\n"


def brand_artifacts() -> dict[str, str | bytes]:
    from lds_brand import brand_artifacts as render  # noqa: E402

    return render(json.loads(BRAND_TOK.read_text(encoding="utf-8")))


def load_manifest(path: pathlib.Path) -> list[dict[str, str]]:
    """Det lille lds.yaml-udsnit: en liste af {kind, dest[, source, note]}."""
    items: list[dict[str, str]] = []
    cur: dict[str, str] | None = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line.startswith("- "):
            if cur:
                items.append(cur)
            cur = {}
            line = line[2:].strip()
        if cur is None or ":" not in line:
            continue
        key, val = (x.strip() for x in line.split(":", 1))
        cur[key] = val
    if cur:
        items.append(cur)
    return [i for i in items if "kind" in i]


def write_or_check(path: pathlib.Path, content: str | bytes, check: bool, diffs: list[str]) -> None:
    data = content.encode("utf-8") if isinstance(content, str) else content
    if check:
        if not path.is_file():
            diffs.append(f"mangler: {path}")
        elif path.read_bytes() != data:
            diffs.append(str(path))
        return
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"skrev {path}")


def install(consumer: pathlib.Path, check: bool) -> list[str]:
    manifest = consumer / "lds.yaml"
    if not manifest.is_file():
        raise SystemExit(f"mangler {manifest}")
    diffs: list[str] = []
    brand = None
    for item in load_manifest(manifest):
        kind, dest = item["kind"], item.get("dest")
        renderers = {"display-header": emit_lvgl_header, "display-yaml": emit_lvgl_yaml,
                     "display-cpp": emit_cpp_header}
        if not dest or kind not in (*renderers, "brand-png", "brand-svg", "logo"):
            continue
        target = consumer / dest
        if kind in renderers:
            write_or_check(target, renderers[kind](), check, diffs)
            continue
        brand = brand or brand_artifacts()
        names = {"brand-png": PNG_NAMES, "brand-svg": SVG_NAMES}.get(kind)
        if names:
            for name in names:
                write_or_check(target / name, brand[name], check, diffs)
        else:
            write_or_check(target, brand["lune-mark-card.svg"], check, diffs)
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="tjek palet/kontrast; med --install: diff mod committede filer")
    ap.add_argument("--install", action="append", type=pathlib.Path, default=[],
                    help="produktrepo med lds.yaml (kan gentages)")
    args = ap.parse_args()
    if not args.check and not args.install:
        ap.error("angiv --check og/eller --install")

    errs = []
    for th in ("light", "dark"):
        e = check_palette(th)
        for x in e:
            print(f"FEJL  {x}")
        errs.extend(e)
    errs.extend(check_contrast_light())
    if errs:
        print(f"{len(errs)} fejl i display-paletten", file=sys.stderr)
        sys.exit(1)
    print("display-palette OK")

    diffs: list[str] = []
    for consumer in args.install:
        diffs.extend(install(consumer.resolve(), args.check))
    if diffs:
        print("design-verify FEJL — kør lds_display.py --install uden --check:", file=sys.stderr)
        for d in diffs:
            print(f"  {d}", file=sys.stderr)
        sys.exit(1)
    if args.install and args.check:
        print("design-verify OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
