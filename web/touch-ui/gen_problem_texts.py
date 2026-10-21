#!/usr/bin/env python3
"""Problemtekster til firmwaren fra i18n-kataloget.

Touch sender problemer til Home Assistant (esphome.lune_problem) med titel og
besked på enhedens sprog. Teksterne ejes af kataloget i i18n/<sprog>.json
(nøgler `problem.*`), ligesom webbens tekster; dette script skriver dem som
C++-tabeller i components/lune_touch_coordinator/problem_texts.h.

    python web/touch-ui/gen_problem_texts.py          # skriv
    python web/touch-ui/gen_problem_texts.py --check  # fejl, hvis filen er forældet
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent.parent / "components" / "lune_touch_coordinator" / "problem_texts.h"
LANGS = ("da", "en")
PREFIX = "problem."


def ident(key):
    # problem.hpOver.title -> HP_OVER_TITLE
    rest = key[len(PREFIX):]
    rest = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", rest)
    return re.sub(r"[^A-Za-z0-9]+", "_", rest).upper()


def c_string(s):
    out = []
    for ch in s:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def render():
    cats = {lang: json.load(open(ROOT / "i18n" / f"{lang}.json", encoding="utf-8")) for lang in LANGS}
    keys = [k for k in cats["da"] if k.startswith(PREFIX)]
    for lang in LANGS:
        missing = [k for k in keys if k not in cats[lang]]
        if missing:
            raise SystemExit(f"{lang}.json mangler: {', '.join(missing)}")
    lines = [
        "// GENERERET af web/touch-ui/gen_problem_texts.py fra web/touch-ui/i18n/*.json — ret ikke i hånden.",
        "// Problemtitler og -beskeder til esphome.lune_problem; {navn} udfyldes af lune_touch_problems::fill().",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace lune_touch_problem_texts {",
        "",
        "enum Key : uint8_t {",
    ]
    lines += [f"  {ident(k)}," for k in keys]
    lines += ["  KEY_COUNT,", "};", ""]
    for lang in LANGS:
        lines.append(f"static const char *const {lang.upper()}[KEY_COUNT] = {{")
        lines += [f"    {c_string(cats[lang][k])}," for k in keys]
        lines.append("};")
        lines.append(f"static constexpr char {lang.upper()}_DEC = '{cats[lang]['_dec']}';")
        lines.append("")
    lines += [
        "/// Catalogue for a language code (\"da\" | \"en\"); Danish is the default.",
        "inline const char *const *catalogue(const char *lang) {",
        "  return (lang != nullptr && lang[0] == 'e' && lang[1] == 'n') ? EN : DA;",
        "}",
        "",
        "inline char decimal_sign(const char *lang) {",
        "  return (lang != nullptr && lang[0] == 'e' && lang[1] == 'n') ? EN_DEC : DA_DEC;",
        "}",
        "",
        "}  // namespace lune_touch_problem_texts",
        "",
    ]
    return "\n".join(lines)


def main():
    text = render()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT} er forældet: kør python web/touch-ui/gen_problem_texts.py")
            return 1
        print("problem_texts.h: OK")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"skrev {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
