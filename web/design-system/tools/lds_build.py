#!/usr/bin/env python3
"""
Lune Design System — build af projekt-CSS.

    python tools/lds_build.py config/v6.json       → dist/v6/lune-ui.css (+ .gz)
    python tools/lds_build.py config/touch.json    → dist/touch/lune-ui.css (+ .gz)
    python tools/lds_build.py --check              → kontrast-tjek af tokens (exit 1 ved fejl)

Scriptet fylder to genererede områder i css/lune-ui.src.css:
  @generated:tokens  custom properties fra tokens/tokens.json (farver som light-dark())
  @generated:state   synlighedsregler for (tilstand × omfang), aktiv tilstand og
                     valgt zonefelt — ud fra projektets liste af omfang
og udfolder kompakt-blokken (@compact:begin … @compact:end) to gange: under
density.media (mus på bred skærm, medmindre data-density="comfortable") og
for .app[data-density="compact"]. Blokken skrives med CSS-nesting relativt
til .app; token-værdierne kommer fra density.compact i tokens.json.
Alt andet i kildefilen er håndskrevet og fælles for begge projekter.
"""
import json, gzip, sys, pathlib, argparse, re, math

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from oklab import to_oklab, from_oklab, cr as contrast_ratio  # noqa: E402

TOK = json.loads((ROOT/"tokens/tokens.json").read_text(encoding="utf-8"))

# Known heat-source type ids for config validation.
# "http" is the UI/CSS id; firmware API stores it as generic_http.
KNOWN_HEAT_SOURCE_TYPES = frozenset({"http", "generic_http", "asgard"})

# ------------------------------------------------------------------ tokens --
def flat_colors():
    """Raw colour token dicts (light may be a tint recipe)."""
    out = {}
    for group in TOK["color"].values():
        for k, v in group.items():
            out[k] = v
    return out

def is_tint(val):
    return isinstance(val, dict) and "tint" in val

def derive_tint(recipe, theme, C):
    """Card lightness/warmth, slightly darker, with status hue at low chroma."""
    card = resolve("card", theme, C)
    status = resolve(recipe["tint"], theme, C)
    L, a, b = to_oklab(card)
    _, sa, sb = to_oklab(status)
    h = math.atan2(sb, sa)
    chroma = float(recipe["chroma"])
    dL = float(recipe["dL"])
    return from_oklab(L + dL, a + chroma * math.cos(h), b + chroma * math.sin(h))

def resolve(name, theme, C):
    if name.startswith("#"):
        return name
    v = C[name]
    if "ref" in v:
        return resolve(v["ref"], theme, C)
    val = v[theme]
    if is_tint(val):
        return derive_tint(val, theme, C)
    return val

def tokens_css():
    C = flat_colors()
    L = []
    L.append("  .app {")
    L.append("    color-scheme: light dark;")
    for gname, group in TOK["color"].items():
        L.append(f"    /* color.{gname} */")
        for k, v in group.items():
            if "ref" in v:
                val = f"var(--{v['ref']})"
            else:
                light = resolve(k, "light", C)
                dark = resolve(k, "dark", C)
                val = f"light-dark({light}, {dark})"
            L.append(f"    --{k}: {val};")
    for section in ("font", "type", "space", "radius", "size", "shadow", "motion"):
        L.append(f"    /* {section} */")
        for k, v in TOK[section].items():
            L.append(f"    --{k}: {v['value']};")
    L.append("  }")
    return "\n".join(L)

# ------------------------------------------------------------------ validate
def validate_cfg(cfg, path=""):
    """Validate project config. Exit with clear message on failure."""
    errs = []
    modes = cfg.get("modes") or []
    if not modes:
        errs.append("modes must be a non-empty list")

    tiers = bool(cfg.get("tiers"))
    manifolds = cfg.get("manifolds")
    scopes = cfg.get("scopes")

    if tiers:
        if not isinstance(manifolds, list) or not manifolds:
            errs.append("tiers requires a non-empty manifolds list")
        else:
            if len(manifolds) > 4:
                errs.append(f"manifolds: max 4, got {len(manifolds)}")
            ids = []
            for i, m in enumerate(manifolds):
                mid = m.get("id")
                if not mid or not re.fullmatch(r"m[1-4]", str(mid)):
                    errs.append(f"manifolds[{i}].id must be m1–m4, got {mid!r}")
                else:
                    ids.append(mid)
                z = m.get("zones")
                if not isinstance(z, int) or z < 1 or z > 6:
                    errs.append(f"manifolds[{i}].zones must be 1–6, got {z!r}")
            if len(ids) != len(set(ids)):
                errs.append(f"manifolds ids must be unique, got {ids}")
        if scopes:
            errs.append("tiers config must not also list flat scopes (use manifolds)")
    else:
        if not scopes:
            errs.append("scopes must be a non-empty list (or set tiers+manifolds)")
        else:
            for s in scopes:
                if "id" not in s or "kind" not in s:
                    errs.append(f"scope missing id/kind: {s!r}")

    hs = cfg.get("heat_source_types")
    if hs is not None:
        if not isinstance(hs, list) or not hs:
            errs.append("heat_source_types must be a non-empty list when set")
        else:
            for t in hs:
                if t not in KNOWN_HEAT_SOURCE_TYPES:
                    errs.append(
                        f"unknown heat_source_type {t!r}; "
                        f"known: {', '.join(sorted(KNOWN_HEAT_SOURCE_TYPES))}"
                    )

    groups = cfg.get("typed_groups")
    if groups is not None:
        if not isinstance(groups, dict):
            errs.append("typed_groups must be an object {prefix: [type, ...]}")
        else:
            for prefix, types in groups.items():
                if prefix == "hs" or not re.fullmatch(r"[a-z][a-z0-9]*", str(prefix)):
                    errs.append(f"typed_groups prefix {prefix!r} must be lowercase [a-z0-9] and not 'hs'")
                if not isinstance(types, list) or len(types) < 2:
                    errs.append(f"typed_groups[{prefix!r}] needs 2+ types")
                elif not all(re.fullmatch(r"[a-z][a-z0-9_]*", str(t)) for t in types):
                    errs.append(f"typed_groups[{prefix!r}] types must be lowercase ids")

    if errs:
        where = f" in {path}" if path else ""
        print(f"Config validation failed{where}:", file=sys.stderr)
        for e in errs:
            print(f"  • {e}", file=sys.stderr)
        sys.exit(1)

def expand_scope_ids(cfg):
    """Return list of scope radio ids for invert/focus rules."""
    if cfg.get("tiers"):
        ids = ["house"]
        for m in cfg["manifolds"]:
            n = int(m["id"][1:])
            ids.append(f"m{n}")
            for z in range(1, int(m["zones"]) + 1):
                ids.append(f"m{n}z{z}")
        return ids
    return [s["id"] for s in cfg["scopes"]]

def invert_tile_rule(scope_ids):
    body = """ {
    --seg-off: color-mix(in srgb, var(--inv-fg) 18%, transparent);
    --muted:   color-mix(in srgb, var(--inv-fg) 60%, transparent);
    background: var(--inv-bg);
    color: var(--inv-fg);
  }"""
    return ",\n".join(f'  #s-{s}:checked ~ .app .tile[for="s-{s}"]' for s in scope_ids) + body

def focus_tile_rule(scope_ids):
    return (",\n".join(f'  #s-{s}:focus-visible ~ .app .tile[for="s-{s}"]' for s in scope_ids) +
            " { outline: 2px solid var(--focus); outline-offset: 2px; }")

def parent_selected_rules(manifolds):
    """When a zone of manifold N is selected, mark the manifold tile as parent."""
    if not manifolds:
        return ""
    sels = [
        f'  input[data-kind="zone"][data-m="{int(m["id"][1:])}"]:checked ~ .app .tile[for="s-m{int(m["id"][1:])}"]'
        for m in manifolds
    ]
    return (
        "  /* Forælder-valgt: manifold når en af dens zoner er valgt */\n"
        + ",\n".join(sels)
        + """ {
    box-shadow: inset 0 0 0 2px var(--fg);
    background: var(--card);
    color: var(--fg);
  }"""
    )

def substrip_rules(manifolds):
    L = ["  /* Understrimmel synlig når manifold eller dens zone er valgt */"]
    for m in manifolds:
        n = int(m["id"][1:])
        L.append(
            f'  #s-m{n}:checked ~ .app .substrip[data-m="{n}"],\n'
            f'  input[data-kind="zone"][data-m="{n}"]:checked ~ .app .substrip[data-m="{n}"]'
            f' {{ display: grid; }}'
        )
    return "\n".join(L)

def shared_view_rules(modes, manifolds):
    """house / manifold / zone shared views."""
    L = ["  /* Delte visninger: house | manifold | zone × mode */"]
    house = [f"  #m-{mode}:checked ~ #s-house:checked ~ .app #v-{mode}-house" for mode in modes]
    man = [
        f'  #m-{mode}:checked ~ input[data-kind="manifold"]:checked ~ .app #v-{mode}-manifold'
        for mode in modes
    ]
    zone = [
        f'  #m-{mode}:checked ~ input[data-kind="zone"]:checked ~ .app #v-{mode}-zone'
        for mode in modes
    ]
    L.append(",\n".join(house) + " { display: grid; }")
    L.append(",\n".join(man) + " { display: grid; }")
    L.append(",\n".join(zone) + " { display: grid; }")
    return "\n".join(L)

def typed_fields_rules(types, prefix="hs"):
    """Reusable typed-fields: hide all, show sibling fieldset for checked type radio."""
    if not types:
        return ""
    L = []
    L.append("  /* Typeafhængige felter (.typed-fields / .hs-fields) — uden :has() */")
    L.append("  .typed-fields, .hs-fields { display: none; gap: var(--space-4); }")
    sels = []
    for t in types:
        # Normalize generic_http → still use its own id if present
        sels.append(
            f'  #{prefix}-{t}:checked ~ .typed-fields[data-type="{t}"],\n'
            f'  #{prefix}-{t}:checked ~ .hs-fields[data-type="{t}"]'
        )
    L.append(",\n".join(sels) + " { display: grid; }")
    # External state radios + .seg label[for] (Touch heat-source type pill)
    L.append("  /* .seg med eksterne .state-radioer — aktiv pille */")
    pill = [
        f'  #{prefix}-{t}:checked ~ .seg label[for="{prefix}-{t}"] > span'
        for t in types
    ]
    L.append(",\n".join(pill) + " { background: var(--inv-bg); color: var(--inv-fg); }")
    focus = [
        f'  #{prefix}-{t}:focus-visible ~ .seg label[for="{prefix}-{t}"] > span'
        for t in types
    ]
    L.append(",\n".join(focus) + " { outline: 2px solid var(--focus); }")
    # Dashboard panel type filters
    L.append("  /* Dashboard: skjul type-fremmed indhold */")
    for t in types:
        others = [o for o in types if o != t]
        for o in others:
            L.append(
                f'  [data-hs-type="{t}"] .hs-type-{o} {{ display: none; }}'
            )
    return "\n".join(L)

def typed_group_rules(prefix, types, base=True):
    """Extra typed-field groups (config `typed_groups`): same mechanics as the
    heat-source type, own radio ids `#{prefix}-{type}`. Each group's radios,
    `.seg` and fieldsets share one parent (sibling selectors)."""
    L = []
    if base:
        L.append("  .typed-fields { display: none; gap: var(--space-4); }")
    L.append(f"  /* Typeafhængige felter: gruppe '{prefix}' */")
    L.append(",\n".join(
        f'  #{prefix}-{t}:checked ~ .typed-fields[data-type="{t}"]' for t in types
    ) + " { display: grid; }")
    L.append(",\n".join(
        f'  #{prefix}-{t}:checked ~ .seg label[for="{prefix}-{t}"] > span' for t in types
    ) + " { background: var(--inv-bg); color: var(--inv-fg); }")
    L.append(",\n".join(
        f'  #{prefix}-{t}:focus-visible ~ .seg label[for="{prefix}-{t}"] > span' for t in types
    ) + " { outline: 2px solid var(--focus); }")
    return "\n".join(L)

# ------------------------------------------------------------------ state ---
def state_css(cfg):
    modes = cfg["modes"]
    L = []

    # Mode pill
    L.append("  /* Aktiv tilstand: inverteret pille */")
    L.append(",\n".join(f'  #m-{m}:checked ~ .app .mode label[for="m-{m}"]' for m in modes) +
             " { background: var(--inv-bg); color: var(--inv-fg); }")
    L.append(",\n".join(f'  #m-{m}:focus-visible ~ .app .mode label[for="m-{m}"]' for m in modes) +
             " { outline: 2px solid var(--focus); outline-offset: 2px; }")
    L.append("")

    if cfg.get("tiers"):
        manifolds = cfg["manifolds"]
        scope_ids = expand_scope_ids(cfg)
        if cfg.get("shared_views", True):
            L.append(shared_view_rules(modes, manifolds))
        else:
            # Fallback: one view per radio id (unlikely for tiers)
            L.append("  /* Synlig visning: præcis én pr. (tilstand × omfang) */")
            sel = [f"  #m-{m}:checked ~ #s-{s}:checked ~ .app #v-{m}-{s}"
                   for m in modes for s in scope_ids]
            L.append(",\n".join(sel) + " { display: grid; }")
        L.append("")
        L.append("  /* Valgt omfang: inverteret felt */")
        L.append(invert_tile_rule(scope_ids))
        L.append(focus_tile_rule(scope_ids))
        L.append("")
        L.append(parent_selected_rules(manifolds))
        L.append("")
        L.append(substrip_rules(manifolds))
        L.append("")
        L.append(f"  .strip.strip--tiers {{ --strip-n: {len(manifolds)}; }}")
    else:
        scopes = [s["id"] for s in cfg["scopes"]]
        L.append("  /* Synlig visning: præcis én pr. (tilstand × omfang) */")
        sel = [f"  #m-{m}:checked ~ #s-{s}:checked ~ .app #v-{m}-{s}"
               for m in modes for s in scopes]
        L.append(",\n".join(sel) + " { display: grid; }")
        L.append("")
        L.append("  /* Valgt omfang: inverteret zonefelt */")
        L.append(invert_tile_rule(scopes))
        L.append(focus_tile_rule(scopes))
        zones = [s for s in cfg["scopes"] if s["kind"] == "zone"]
        if cfg.get("strip", {}).get("variant") != "many":
            L.append("")
            L.append(f"  .strip {{ --strip-n: {len(zones)}; }}")

    hs = cfg.get("heat_source_types")
    if hs:
        L.append("")
        L.append(typed_fields_rules(hs))

    for prefix, types in (cfg.get("typed_groups") or {}).items():
        L.append("")
        L.append(typed_group_rules(prefix, types, base=not hs))

    return "\n".join(L)

def compact_css(src):
    """Expand the hand-written compact block twice (media + forced attribute)."""
    a_tag, b_tag = "/* @compact:begin */", "/* @compact:end */"
    a = src.index(a_tag); b = src.index(b_tag)
    block = src[a + len(a_tag):b].strip("\n")
    dens = TOK["density"]
    vars_ = "\n".join(f"    --{k}: {v};" for k, v in dens["compact"].items())
    body = vars_ + "\n" + block
    out = (f"/* Kompakt: genereret fra density i tokens.json + @compact-blokken */\n"
           f"  @media {dens['media']} {{\n"
           f"  .app:not([data-density=\"comfortable\"]) {{\n{body}\n  }}\n  }}\n"
           f"  .app[data-density=\"compact\"] {{\n{body}\n  }}")
    return src[:a] + out + src[b + len(b_tag):]

def fill(src, name, body):
    a = src.index(f"/* @generated:{name}"); a = src.index("*/", a) + 2
    b = src.index(f"/* @end:{name} */")
    return src[:a] + "\n" + body + "\n  " + src[b:]

# ------------------------------------------------------------------ kontrast
def ratio(a, b):
    return contrast_ratio(a, b)

def contrast_report():
    C = flat_colors(); rows = []
    for fg, bg, need in TOK["contrast"]:
        for th in ("light", "dark"):
            # card↔bg depth is a light-theme surface rule; dark stays as-is.
            if (fg, bg) == ("card", "bg") and th == "dark":
                continue
            r = ratio(resolve(fg, th, C), resolve(bg, th, C))
            rows.append((fg, bg, th, r, need, r >= need))
    return rows

# ------------------------------------------------------------------ main ----
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("config", nargs="?")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", default=str(ROOT/"dist"))
    a = ap.parse_args()

    rows = contrast_report()
    bad = [r for r in rows if not r[5]]
    if a.check or not a.config:
        for fg, bg, th, r, need, ok in rows:
            print(f"{'OK ' if ok else 'FEJL'}  {fg:>11} på {bg:<10} {th:<5} {r:5.2f}:1  (krav {need})")
        sys.exit(1 if bad else 0)
    if bad:
        print("ADVARSEL: tokens består ikke kontrastkravene — kør --check")

    cfg_path = pathlib.Path(a.config)
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    validate_cfg(cfg, str(cfg_path))

    src = (ROOT/"css/lune-ui.src.css").read_text(encoding="utf-8")
    css = compact_css(fill(fill(src, "tokens", tokens_css()), "state", state_css(cfg)))
    css = css.replace("Kildefil. Den færdige CSS pr. projekt bygges med tools/lds_build.py.",
                      f"Bygget til {cfg['project']} af tools/lds_build.py — redigér css/lune-ui.src.css, ikke denne fil.")
    out = pathlib.Path(a.out)/cfg["id"]; out.mkdir(parents=True, exist_ok=True)
    (out/"lune-ui.css").write_text(css, encoding="utf-8")
    gz = gzip.compress(css.encode(), 9, mtime=0); (out/"lune-ui.css.gz").write_bytes(gz)
    n = len(expand_scope_ids(cfg)) if cfg.get("tiers") else len(cfg["scopes"])
    print(f"{cfg['project']}: {out/'lune-ui.css'}  ({len(css)/1024:.1f} kB, {len(gz)/1024:.1f} kB gzip, {n} omfang)")

if __name__ == "__main__":
    main()
