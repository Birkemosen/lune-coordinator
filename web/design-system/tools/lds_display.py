#!/usr/bin/env python3
"""
Lune Design System — tema til vægskærmen (LVGL, 1024×600, RGB565).

    python tools/lds_display.py            → dist/display/lune_theme.h + lune_theme.yaml
    python tools/lds_display.py --check    → palet-tjek (hex ↔ RGB565) og kontrast
    python tools/lds_display.py --install ../lune-coordinator          # firmware-artefakter
    python tools/lds_display.py --install ../lune-coordinator --check  # diff mod committet

Læser tokens.json → "display". Laver:
  lune_theme.h     C-konstanter (lv_color_hex) for mørkt og lyst tema + størrelser
  lune_theme.yaml  ESPHome-pakke: farve-substitutions, fonte (Geist) og
                   lvgl style_definitions for kort, felter, knapper og badges

--install læser produktets `lds.yaml` (schema 2) med `- kind:` / `dest:`. Kendte kinds:
  display-header  LVGL C-header (LDS_DARK_* / LDS_LIGHT_* / LDS_FS_* / LDS_NIGHT_* / mål / tider)
  display-yaml    ESPHome substitutions (lds_*, lds_light_*, lds_night_*)
  display-cpp     C++-konstanter i lune::tokens (mørkt tema, uden lvgl.h)
  brand-png       lune_v6_mark.png, lune_halo.png, lune_touch_lockup.png i dest/
  brand-svg       de fem SVG-mærker i dest/
  logo            lune-mark-card.svg som dest
Andre kinds (css, html, binder …) ignoreres her; dem bygger lds_build.py.
Firmware-artefakterne bruger den udfoldede 565-farve (det skærmen viser).
Brand-mærkerne (seksrørs-halo) kommer fra tokens/brand.json via tools/lds_brand.py.
"""
from __future__ import annotations

import argparse, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
TOK = json.loads((ROOT/"tokens/tokens.json").read_text(encoding="utf-8"))
D = TOK["display"]
BRAND_TOK = ROOT/"tokens/brand.json"
GEN = "Genereret af lune-design-system tools/lds_display.py fra tokens/tokens.json — redigér ikke."

def lum(h):
    h = h.lstrip("#"); r, g, b = [int(h[i:i+2], 16)/255 for i in (0, 2, 4)]
    f = lambda c: c/12.92 if c <= 0.03928 else ((c+0.055)/1.055)**2.4
    return 0.2126*f(r)+0.7152*f(g)+0.0722*f(b)
def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True); return (la+.05)/(lb+.05)

PAIRS = [("fg","card",7),("fg","raised",7),("muted","card",4.5),("muted","raised",4.5),("faint","card",4.5),
         ("accent-ink","card",4.5),("on-accent","accent-ink",4.5),("info","card",4.5),("ok","card",4.5),
         ("warn","card",4.5),("danger","card",4.5),("violet","card",4.5),("danger","danger-bg",4.5),
         ("info","info-bg",4.5),("ok","ok-bg",4.5),("warn","warn-bg",4.5),("violet","violet-bg",4.5),
         ("bg","danger",4.5),("inv-fg","inv-bg",7)]
# Kort mod baggrund (dybde uden skygge) — kun lyst tema, som på web (3.3).
LIGHT_ONLY = [("card","bg",1.24)]

def check():
    bad = 0
    for th, P in D["palette"].items():
        for a, b, need in PAIRS + (LIGHT_ONLY if th == "light" else []):
            r = ratio(P[a]["hex"], P[b]["hex"]); ok = r >= need; bad += not ok
            print(f"{'OK ' if ok else 'FEJL'} {th:<5} {a:>10} på {b:<9} {r:5.2f}:1 (krav {need})")
    return bad

def cname(k): return k.upper().replace("-", "_")

def header():
    L = ["// Genereret af tools/lds_display.py fra tokens.json — redigér ikke.",
         "// Lune Design System · vægskærm 1024×600 · RGB565", "#pragma once", "#include \"lvgl.h\"", ""]
    for th, P in D["palette"].items():
        L.append(f"// ---- {th} ----")
        for k, v in P.items():
            L.append(f"#define LDS_{th.upper()}_{cname(k):<12} lv_color_hex(0x{v['hex'][1:]})   /* 565: {v['rgb565']} */")
        L.append("")
    L.append("// ---- typografi (px) — 2XL og HERO: kun cifre , ° − ----")
    for k, v in D["type"].items(): L.append(f"#define LDS_FS_{cname(k):<6} {v}")
    L.append("\n// ---- nat (dæmpet, uden for paletten) ----")
    for k, v in D["night"].items():
        if not k.startswith("$"): L.append(f"#define LDS_NIGHT_{cname(k):<8} lv_color_hex(0x{v[1:]})")
    L.append("\n// ---- mål (px) ----")
    for k, v in D["size"].items(): L.append(f"#define LDS_{cname(k):<12} {v}")
    L.append("\n// ---- tider ----")
    for k, v in D["timing"].items():
        if isinstance(v, int): L.append(f"#define LDS_{cname(k):<14} {v}")
    return "\n".join(L) + "\n"

def yaml():
    P = D["palette"]["dark"]; S = D["size"]; F = D["type"]
    col = lambda k: "0x" + P[k]["hex"][1:].upper()
    Y = ["# Genereret af tools/lds_display.py fra tokens.json — redigér ikke.",
         "# Lune Design System · vægskærm (mørkt tema). Brug som ESPHome-pakke:",
         "#   packages: { lds: !include lune_theme.yaml }",
         "", "substitutions:"]
    for k in P: Y.append(f"  lds_{k.replace('-','_')}: \"{col(k)}\"")
    Y += ["", "font:"]
    digits = "0123456789,.°−-: "
    for k, px in F.items():
        w = 600 if px >= 32 else 500
        Y.append(f"  - file: {{ type: gfonts, family: Geist, weight: {w} }}")
        Y.append(f"    id: lds_font_{k}")
        Y.append(f"    size: {px}")
        Y.append(f"    bpp: 4")
        if k in D["type_digits_only"]:
            Y.append(f"    glyphs: \"{digits}\"")
        else:
            Y.append("    glyphsets: [GF_Latin_Kernel]")
            Y.append("    glyphs: \"æøåÆØÅéü°−·→×²³–…\"")
    Y += ["", "lvgl:", "  color_depth: 16", "  bg_color: ${lds_bg}", "  default_font: lds_font_sm",
          "  style_definitions:",
          f"    - id: lds_card           # panel: huset, manifold-række, zone-kort",
          f"      bg_color: ${{lds_card}}", "      bg_opa: COVER", f"      radius: {S['r_card']}", "      border_width: 0", "      shadow_width: 0", f"      pad_all: {S['pad']}",
          f"    - id: lds_tile           # zonefelt (på kort)",
          f"      bg_color: ${{lds_raised}}", "      bg_opa: COVER", f"      radius: {S['r_tile']}", "      border_width: 0", "      pad_all: 10", "      text_color: ${lds_fg}",
          f"    - id: lds_tile_primary   # gruppens primære zone",
          f"      border_width: 2", "      border_color: ${lds_violet}",
          f"    - id: lds_tile_selected  # valgt/presset: inverteret",
          f"      bg_color: ${{lds_inv_bg}}", "      text_color: ${lds_inv_fg}",
          f"    - id: lds_seg_on       # ventilåbning: neutral", "      bg_color: ${lds_fg}", "      bg_opa: COVER", "      radius: 2",
          f"    - id: lds_seg_off", "      bg_color: ${lds_seg_off}", "      bg_opa: COVER", "      radius: 2",
          f"    - id: lds_seg_fault", "      bg_color: ${lds_danger}", "      bg_opa: COVER", "      radius: 2",
          f"    - id: lds_btn_round      # ‹ › − +  ({S['hit']} / {S['hit_lg']} px)",
          f"      bg_color: ${{lds_raised}}", "      bg_opa: COVER", "      radius: 999", "      border_width: 0", "      shadow_width: 0", "      text_color: ${lds_fg}",
          f"    - id: lds_btn_pill       # forvalg og handlinger ({S['pill_h']} px høj)",
          f"      bg_color: ${{lds_raised}}", "      bg_opa: COVER", "      radius: 999", "      border_width: 0", "      shadow_width: 0", "      pad_hor: 28", "      text_color: ${lds_fg}", "      text_font: lds_font_sm",
          f"    - id: lds_btn_active     # aktivt forvalg / primær",
          f"      bg_color: ${{lds_inv_bg}}", "      text_color: ${lds_inv_fg}",
          f"    - id: lds_btn_danger     # Nulstil fejl",
          f"      bg_color: ${{lds_danger}}", "      text_color: ${lds_bg}",
          f"    - id: lds_badge_hot", "      bg_color: ${lds_accent_ink}", "      bg_opa: COVER", "      radius: 999", "      pad_hor: 14", "      pad_ver: 4", "      text_color: ${lds_on_accent}", "      text_font: lds_font_xs",
          f"    - id: lds_badge_bad", "      bg_color: ${lds_danger_bg}", "      bg_opa: COVER", "      radius: 999", "      pad_hor: 14", "      pad_ver: 4", "      text_color: ${lds_danger}", "      text_font: lds_font_xs",
          f"    - id: lds_msg_info", "      bg_color: ${lds_info_bg}", "      bg_opa: COVER", f"      radius: {S['r_msg']}", "      pad_all: 12", "      text_color: ${lds_info}", "      text_font: lds_font_xs",
          f"    - id: lds_text_muted", "      text_color: ${lds_muted}",
          f"    - id: lds_text_hero", "      text_font: lds_font_hero", "      text_color: ${lds_fg}",
         ]
    return "\n".join(Y) + "\n"

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
    pal = D.get("palette", {}).get(theme)
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


def rgb565_to_rgb(v: int) -> int:
    """Udfold 565 til 24-bit: den farve skærmen faktisk viser."""
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return (round(r * 255 / 31) << 16) | (round(g * 255 / 63) << 8) | round(b * 255 / 31)


def screen_colors(theme: str) -> list[tuple[str, int, int]]:
    """(navn, 24-bit, 565) i PALETTE_ORDER for et tema."""
    pal = D["palette"][theme]
    missing = [k for k in PALETTE_ORDER if k not in pal]
    if missing:
        raise SystemExit(f"display.palette.{theme} mangler {missing}")
    out = []
    for key in PALETTE_ORDER:
        v565 = parse_rgb565(pal[key]["rgb565"])
        out.append((key.replace("-", "_"), rgb565_to_rgb(v565), v565))
    return out


def night_colors() -> list[tuple[str, int]]:
    night = D["night"]
    return [(k, int(v.lstrip("#"), 16)) for k, v in night.items() if not k.startswith("$")]


def emit_lvgl_header() -> str:
    d = D
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

    errs = []
    for th in ("light", "dark"):
        for x in check_palette(th):
            print(f"FEJL  {x}")
            errs.append(x)
    bad = check()
    if args.check or args.install:
        if errs or bad:
            print(f"{len(errs) + bad} fejl i display-paletten", file=sys.stderr)
            sys.exit(1)
        print("display-palette OK")
    elif errs or bad:
        print("ADVARSEL: skærmpaletten består ikke kontrastkravene")

    if args.install:
        diffs: list[str] = []
        for consumer in args.install:
            diffs.extend(install(consumer.resolve(), args.check))
        if diffs:
            print("design-verify FEJL — kør lds_display.py --install uden --check:", file=sys.stderr)
            for d in diffs:
                print(f"  {d}", file=sys.stderr)
            sys.exit(1)
        if args.check:
            print("design-verify OK")
        sys.exit(0)
    if args.check:
        sys.exit(0)

    out = ROOT/"dist/display"; out.mkdir(parents=True, exist_ok=True)
    (out/"lune_theme.h").write_text(header(), encoding="utf-8")
    (out/"lune_theme.yaml").write_text(yaml(), encoding="utf-8")
    print(f"→ {out/'lune_theme.h'}\n→ {out/'lune_theme.yaml'}")

if __name__ == "__main__":
    main()
