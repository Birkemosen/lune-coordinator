#!/usr/bin/env python3
"""
Lune Design System — vægskærm (LVGL / RGB565) palet-tjek.

    python tools/lds_display.py --check

Læser `display.palette` i tokens/tokens.json. Hex ↔ RGB565 med afrunding
(r·31/255, g·63/255, b·31/255). `raised` i lyst tema er håndplukket så
kvantiseringen ikke giver grønt stik — den lagrede 565-værdi skal matches.
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", required=True)
    ap.parse_args()

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
    sys.exit(0)


if __name__ == "__main__":
    main()
