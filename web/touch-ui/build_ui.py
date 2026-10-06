#!/usr/bin/env python3
"""
Lune Touch web-UI — Lune Design System 2.3 (config/touch.json): Hjem / ark / System
(DESIGN.md 15). Skabelon: lune-design-system/examples/touch/build_ui.py. Produktets
binder (binder.js) fylder live-data i siden og gemmer via firmwarens API.

    python build_ui.py
    python build_ui.py --langs en,da --preview --hs-type asgard

Output (i --out, standard ./dist):
    lune-ui.css(.gz)       projekt-CSS (web/design-system/dist/touch/lune-ui.css)
    ui.js(.gz)             lune-forms.js (LDS: gem/ugemt, autogem, ark, deep links)
                           + binder.js — én fil, fordi firmwaren kun serverer /ui.js
    <lang>/index.html(.gz) én side pr. sprog
    preview-<lang>.html    selvstændige filer med inline CSS og mock-data (--preview)

Rum og styringer kendes først ved runtime. Siden har derfor én skabelon pr. arktype
(<template id="tpl-room"> og <template id="tpl-mani">); binderen opretter et ark pr. rum
og styring ud fra data og binder formularerne med window.luneForms (LDS 2.3.1). Uden
JavaScript findes rum- og styringsark derfor ikke (der er heller ingen data).
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parent
DS_ROOT = ROOT.parent / "design-system"
LDS_DEFS = (DS_ROOT / "css" / "lds-svg-defs.html").read_text(encoding="utf-8")
FORMS_JS = (DS_ROOT / "js" / "lune-forms.js").read_text(encoding="utf-8")

# Rum- og styringsark kendes først ved runtime: siden har én skabelon af hver
# (<template id="tpl-room"> / "tpl-mani"), som binderen kloner pr. rum og styring.
# SLOT erstattes af pladsens nummer (r1, r2 … / m1, m2 …).
SLOT = "__N__"

# Prisområder og valutaer kommer fra firmwarens tabel (én kilde).
_PRICE_H = (ROOT.parent.parent / "components" / "lune_touch_coordinator" / "energy_price.h").read_text(encoding="utf-8")
PRICE_ZONES = [
    (m.group(1), m.group(2).lower())
    for m in re.finditer(r'\{"([A-Za-z0-9-]+)", "[0-9A-Z-]{16}", ZoneGroup::([A-Z_]+),', _PRICE_H)
]
PRICE_CURRENCIES = re.findall(r'\{"([A-Z]{3})", ([0-9.]+)f\}', _PRICE_H)
assert len(PRICE_ZONES) >= 30 and PRICE_CURRENCIES, "price tables not found in energy_price.h"

DEFAULT_SCHEDULE = [{"h": 0, "v": 0.077}, {"h": 6, "v": 0.231}, {"h": 17, "v": 0.692}, {"h": 21, "v": 0.231}]
DASH = "—"


class Cat:
    def __init__(self, lang, fallback):
        self.d = json.load(open(ROOT / "i18n" / f"{lang}.json", encoding="utf-8"))
        self.fb = fallback
        self.missing = set()

    def __call__(self, k, **kw):
        if k in self.d:
            s = self.d[k]
        elif self.fb and k in self.fb.d:
            self.missing.add(k)
            s = self.fb.d[k]
        else:
            raise KeyError(f"Mangler i18n-nøgle: {k}")
        return s.format(**kw) if kw else s

    def num(self, x, dec=1):
        return f"{x:.{dec}f}".replace(".", self.d["_dec"])

    def meta(self, k):
        return self.d[k]


def ensure_css(path: pathlib.Path) -> str:
    if path.is_file():
        return path.read_text(encoding="utf-8")
    print(f"Mangler {path}, kører lds_build …")
    subprocess.run([sys.executable, str(DS_ROOT / "tools" / "lds_build.py"), str(DS_ROOT / "config" / "touch.json")],
                   cwd=DS_ROOT, check=True)
    if not path.is_file():
        sys.exit(f"Kunne ikke bygge {path}")
    return path.read_text(encoding="utf-8")


I = {  # ikoner (stroke, currentColor) — samme som LDS-eksemplet
    "heat": '<svg viewBox="0 0 24 24"><path d="M12 3c3 4 5 6.5 5 10a5 5 0 0 1-10 0c0-2 1-3.5 2-5 .5 2 1.5 3 3 3-1-3 0-6 0-8z"/></svg>',
    "plan": '<svg viewBox="0 0 24 24"><rect x="4" y="5" width="16" height="15" rx="2"/><path d="M4 10h16M9 3v4M15 3v4"/></svg>',
    "wx": '<svg viewBox="0 0 24 24"><path d="M3 9h11a3 3 0 1 0-3-3M3 14h15a3 3 0 1 1-3 3M3 19h7"/></svg>',
    "pump": '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><path d="M12 4v8l6 4"/></svg>',
    "room": '<svg viewBox="0 0 24 24"><rect x="4" y="4" width="16" height="16" rx="3"/><path d="M4 12h8V4"/></svg>',
    "mani": '<svg viewBox="0 0 24 24"><path d="M4 7h16M4 12h16M4 17h16M8 7v10M16 7v10"/></svg>',
}
CAT_ICONS = {
    "device": '<path d="M4 11l8-7 8 7v9H4z"/>',
    "controllers": '<rect x="4" y="4" width="16" height="16" rx="3"/><path d="M4 12h16M12 4v16"/>',
    "heatsource": '<path d="M12 3c3 4 5 6.5 5 10a5 5 0 0 1-10 0c0-2 1-3.5 2-5 .5 2 1.5 3 3 3-1-3 0-6 0-8z"/>',
    "prices": '<path d="M12 3v18M16 7H10a3 3 0 0 0 0 6h4a3 3 0 0 1 0 6H8"/>',
    "pump": '<circle cx="12" cy="12" r="8"/><path d="M12 4v8l6 4"/>',
    "weather": '<path d="M3 9h11a3 3 0 1 0-3-3M3 14h15a3 3 0 1 1-3 3M3 19h7"/>',
    "network": '<path d="M2 9a15 15 0 0 1 20 0M5 13a10 10 0 0 1 14 0M8.5 16.5a5 5 0 0 1 7 0"/><circle cx="12" cy="20" r="1"/>',
    "firmware": '<path d="M12 4v10M8 10l4 4 4-4M5 19h14"/>',
    "service": '<path d="M14 6a4 4 0 0 0-5 5l-5 5 3 3 5-5a4 4 0 0 0 5-5l-2 2-3-3z"/>',
}


# ---------------------------------------------------------------- render ---
def render(T, langs, lang_urls, css_href, js_href, hs_type, inline_css=None):
    hs_type = hs_type if hs_type in ("http", "asgard") else "asgard"
    H = T.meta("_h")

    # ---- byggeklodser: grupperede lister, ark, gem-bjælke (som LDS-eksemplet)
    def stepper(name, val, mn, mx, step, unit, label, dec=1, id_=None, unit_attr=""):
        i = id_ or name
        return (f'<div class="stepper"><button type="button" data-step="-1" aria-label="{T("common.decrease", x=label.lower())}">−</button>'
                f'<span class="value"><input type="number" inputmode="decimal" id="{i}" name="{name}" value="{val:.{dec}f}" min="{mn}" max="{mx}" step="{step}">'
                f'<span class="unit"{unit_attr}>{unit}</span></span>'
                f'<button type="button" data-step="1" aria-label="{T("common.increase", x=label.lower())}">+</button></div>')

    def lab(id_, text):
        return f'<label for="{id_}">{text}</label>'

    def srow(label, control, hint="", cls="", attrs=""):
        h = f"<small>{hint}</small>" if hint else ""
        return (f'<div class="setting{(" " + cls) if cls else ""}"{attrs}><div class="setting-label">{label}{h}</div>'
                f'<div class="setting-control">{control}</div></div>')

    def rrow(label, key, hint="", val=DASH):
        """Læseværdi (15.5): værdien til højre i 600-vægt, bundet til binderen."""
        return srow(f"<span>{label}</span>", f'<span class="setting-value" data-bind="{key}">{val}</span>', hint)

    def sstep(name, label, *a, hint="", id_=None, **k):
        i = id_ or name
        return srow(lab(i, label), stepper(name, *a, label=label, id_=i, **k), hint)

    def sinput(name, label, value="", cls="w-md", hint="", id_=None, typ="text", extra="", stack=False):
        i = id_ or name
        ctl = f'<input class="input {cls}" type="{typ}" id="{i}" name="{name}" value="{value}"{extra}>'
        return srow(lab(i, label), ctl, hint, cls="stack" if stack else "")

    def sport(name, label, val=80, id_=None):
        i = id_ or name
        return srow(lab(i, label), f'<input class="input w-xs" type="number" inputmode="numeric" id="{i}" name="{name}" value="{val}" min="1" max="65535" step="1">')

    def sswitch(name, t, sub, on, id_=None, extra=""):
        idattr = (f' id="{id_}"' if id_ else "") + extra
        return (f'<label class="setting switch"><span class="setting-label"><b>{t}</b>{f"<small>{sub}</small>" if sub else ""}</span>'
                f'<input type="checkbox" role="switch" name="{name}"{idattr}{" checked" if on else ""}></label>')

    def sbtn(label, btn, hint=""):
        return srow(f"<span>{label}</span>", btn, hint)

    def group(title, rows, extra="", pre="", attrs="", cls=""):
        c = f"setting-group {cls}".strip()
        return f'<section class="{c}"{attrs}><h4>{title}</h4>{pre}<div class="setting-list">{rows}</div>{extra}</section>'

    def ggroup(title, sw, rows, extra="", pre="", cls=""):
        c = f"setting-group {cls}".strip()
        return f'<section class="{c}"><h4>{title}</h4>{pre}<div class="setting-list gated">{sw}<div class="gated-body">{rows}</div></div>{extra}</section>'

    def note(text, attrs=""):
        return f'<p class="note"{attrs}>{text}</p>'

    def subpage(label, body, value="", vbind=""):
        vb = f' data-bind="{vbind}"' if vbind else ""
        return (f'<details class="subpage"><summary class="setting"><span class="sub-back">{T("common.back")}</span>'
                f'<span class="setting-label"><span>{label}</span></span><span class="setting-control"><span class="muted"{vb}>{value}</span></span></summary>'
                f'<div class="subpage-body">{body}</div></details>')

    def savebar(fid, label=None):
        return (f'<footer class="savebar"><span class="save-status" id="ss-{fid}" aria-live="polite"></span>'
                f'<button type="reset" class="btn">{T("common.undo")}</button><button class="btn primary" type="submit">{label or T("common.save")}</button></footer>')

    def seg(name, prefix, opts, sel, label, dk_only=()):
        """Typevalg (5.18): radioerne står før (søskende), segmentets labels peger på dem."""
        radios = "".join(f'<input class="state" type="radio" name="{name}" id="{prefix}-{v}" value="{v}"{" checked" if v == sel else ""} aria-label="{t}">'
                         for v, t in opts)
        labels = "".join(f'<label for="{prefix}-{v}"{" data-dk-only" if v in dk_only else ""}><span>{t}</span></label>' for v, t in opts)
        return radios, f'<div class="seg" role="radiogroup" aria-label="{label}">{labels}</div>'

    def typed(t, body, hs=False):
        cls = "hs-fields typed-fields" if hs else "typed-fields"
        return f'<fieldset class="{cls}" data-type="{t}">{body}</fieldset>'

    def confirm(pid, open_label, title, body, act_label, value=None, action=None, btn_type="submit", opener_attrs=""):
        act = f' name="action" value="{value}"' if value else ""
        da = f' data-action="{action}"' if action else ""
        return (f'<button class="btn danger" type="button" popovertarget="{pid}"{opener_attrs}>{open_label}</button>'
                f'<div id="{pid}" popover class="confirm-pop" role="alertdialog" aria-labelledby="{pid}-t" aria-describedby="{pid}-d">'
                f'<h4 id="{pid}-t">{title}</h4><p id="{pid}-d">{body}</p>'
                f'<div class="actions"><button class="btn" type="button" popovertarget="{pid}" popovertargetaction="hide" autofocus>{T("common.cancel")}</button>'
                f'<button class="btn danger-solid" type="{btn_type}"{act}{da} popovertarget="{pid}" popovertargetaction="hide">{act_label}</button></div></div>')

    def metric(label, unit, bind="", cls="", val=DASH, f=""):
        b = f' data-bind="{bind}"' if bind else ""
        fa = f' data-f="{f}"' if f else ""
        c = f' class="{cls}"' if cls else ""
        return f'<div class="metric"><dt>{label}</dt><dd{b}{fa}{c}>{val} <small>{unit}</small></dd></div>'

    def kv(rows):
        """rows: (label, bind-nøgle eller None, startværdi[, data-f])."""
        out = []
        for r in rows:
            label, key, val = r[0], r[1], r[2] if len(r) > 2 else DASH
            f = r[3] if len(r) > 3 else ""
            attrs = (f' data-bind="{key}"' if key else "") + (f' data-f="{f}"' if f else "")
            out.append(f"<div><dt>{label}</dt><dd{attrs}>{val}</dd></div>")
        return '<dl class="kv">' + "".join(out) + "</dl>"

    def help_btn(hid, topic):
        return (f'<button class="help-btn" type="button" popovertarget="{hid}" style="anchor-name:--a-{hid}" '
                f'aria-label="{T("help.aria", topic=topic)}">?</button>')

    def help_pop(hid, body_keys, more=""):
        link = f'<a href="https://github.com/Birkemosen/lune-coordinator/blob/main/{more}">{T("help.readMore")}</a>' if more else ""
        paras = "".join(f"<p>{T(k)}</p>" for k in body_keys)
        return f'<div id="{hid}" popover class="help-pop" style="position-anchor:--a-{hid}">{paras}{link}</div>'

    def sheet(sid, hashv, icon, tone, head, status, tabs, head_attrs="", status_attrs=""):
        """tabs: liste af (value, indhold). Første fane er standard. Én fane = ingen fane-bjælke."""
        radios = "".join(f'<input class="state tab" type="radio" name="tab-{sid}" id="t-{sid}-{v[0]}" value="{v}" data-hash="{T("hash." + v)}" '
                         f'aria-label="{T("tab." + v)}"{" checked" if k == 0 else ""}>' for k, (v, _) in enumerate(tabs))
        nav = ""
        if len(tabs) > 1:
            labels = "".join(f'<label for="t-{sid}-{v[0]}" data-tab="{v}">{T("tab." + v)}</label>' for v, _ in tabs)
            nav = f'<nav class="tabs" aria-label="{T("sheet.tabs")}">{labels}</nav>'
        panels = "".join(f'<section class="tab-panel" data-tab="{v}">{body}</section>' for v, body in tabs)
        return f'''
  <div id="sheet-{sid}" popover class="sheet" role="dialog" aria-labelledby="sheet-{sid}-t" data-hash="{hashv}">
    {radios}
    <header class="sheet-head">
      <span class="chip-icon"{f' data-tone="{tone}"' if tone else ""} aria-hidden="true">{icon}</span>
      <div><h2 id="sheet-{sid}-t"{head_attrs}>{head}</h2><p{status_attrs}>{DASH if status is None else status}</p></div>
      <button class="sheet-close" type="button" popovertarget="sheet-{sid}" popovertargetaction="hide" aria-label="{T("sheet.close")}">×</button>
      {nav}
    </header>
    <div class="sheet-body">{panels}</div>
  </div>'''

    def trend(title_key, bind, aria, legend="", cls="spark", h=100, f=""):
        """Graf, binderen fylder; uden data klapper den sammen til én .empty-linje (5.9)."""
        b = f' data-bind-trend="{bind}"' if bind else ""
        fa = f' data-f="{f}"' if f else ""
        return (f'<div class="sub trend-wrap" data-empty{fa}><h4><span data-f="ttl">{T(title_key)}</span>{legend}</h4>'
                f'<p class="empty">{T("room.noHistory")}</p>'
                f'<svg class="{cls}" style="height:{h}px" viewBox="0 0 240 80" preserveAspectRatio="none" role="img" aria-label="{aria}"{b}>'
                f'<polyline class="g" points=""/><polyline class="t" points=""/></svg>'
                f'<div class="axis" aria-hidden="true" data-f="axis"></div></div>')

    unit_cur = ' data-price-cur'

    # ================================================================ HJEM
    ta = T("climate.targetAria")
    alerts = f'''
        <div class="panel alert" data-bind-alert="heat" hidden>
          <div class="panel-head"><h3>{T("alert.heatConn")}</h3></div>
          <p class="note">{T("alert.heatConnBody")}</p>
          <div class="panel-foot" style="justify-content:flex-start"><a class="btn" href="#{T("hash.system")}/{T("hash.heatsource")}">{T("alert.openHeat")}</a></div>
        </div>
        <div class="panel alert" data-bind-alert="board" hidden>
          <div class="panel-head"><h3 data-bind="alert.boardTitle">{T("alert.boardFault", board="V6")}</h3></div>
          <p class="note">{T("alert.boardFaultBody")}</p>
          <div class="panel-foot" style="justify-content:flex-start"><a class="btn" href="#{T("hash.system")}/{T("hash.controllers")}">{T("alert.openControllers")}</a></div>
        </div>
        <div data-bind-alerts></div>'''

    tile_heat = f'''
          <button class="home-tile" type="button" popovertarget="sheet-heat" data-empty data-tile="heat">
            <span class="chip-icon" aria-hidden="true">{I["heat"]}</span><b>{T("tile.heat")}</b>
            <span class="ht-status" data-bind="heat.badgeText">{T("status.waiting")}</span>
            <span class="ht-val"><span data-bind="tile.heatVal">{DASH}</span> <small>{T("tile.heatSub")}</small></span>
            <svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true"></svg>
          </button>'''
    tile_plan = f'''
          <button class="home-tile" type="button" popovertarget="sheet-plan" data-empty data-tile="plan">
            <span class="chip-icon" data-tone="violet" aria-hidden="true">{I["plan"]}</span><b>{T("tile.plan")}</b>
            <span class="ht-status" data-bind="tile.planStatus">{T("tile.planNone")}</span>
            <span class="ht-val"><span data-bind="tile.planVal">{DASH}</span> <small data-bind="tile.planKwh"></small></span>
            <span class="ht-price" data-bind-show="tile.price" hidden><span data-bind="tile.priceText"></span> <span class="scale-chip" data-bind-scale></span></span>
            <svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true" data-bind-viz="plan"></svg>
          </button>'''
    tile_wx = f'''
          <button class="home-tile" type="button" popovertarget="sheet-weather" data-empty data-tile="weather">
            <span class="chip-icon" data-tone="info" aria-hidden="true">{I["wx"]}</span><b>{T("tile.weather")}</b>
            <span class="ht-status" data-bind="tile.weatherStatus">{T("tile.weatherStatusNone")}</span>
            <span class="ht-val"><span data-bind="tile.weatherVal">{DASH}</span> <small data-bind="tile.weatherSub"></small></span>
            <svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true" data-bind-viz="weather"><rect class="pre" x="0" y="0" width="0" height="48"/><polyline class="t" points=""/></svg>
          </button>'''
    tile_pump = f'''
          <button class="home-tile" type="button" popovertarget="sheet-pump" data-empty data-tile="pump">
            <span class="chip-icon" data-tone="neutral" aria-hidden="true">{I["pump"]}</span><b>{T("tile.pump")}</b>
            <span class="ht-status" data-bind="tile.pumpStatus">{T("tile.pumpNone")}</span>
            <span class="ht-val"><span data-bind="tile.pumpVal">{DASH}</span> <small data-bind="tile.pumpSub"></small></span>
            <svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true"></svg>
          </button>'''

    heatmap = f'''
        <section class="panel" aria-labelledby="h-heatmap">
          <header class="panel-head"><h3 id="h-heatmap">{T("heatmap.title")}</h3><p>{T("heatmap.sub")}</p></header>
          <div class="heatmap" data-bind-heatmap><p class="note">{T("common.emptyRooms")}</p></div>
          <p class="heatmap-note">{T("heatmap.note")}</p>
        </section>'''

    home = f'''
      <section class="view" id="v-home-house" aria-labelledby="h-home">
        {alerts}
        <section class="home-hero">
          <div>
            <small data-bind="home.greeting">{T("home.greeting.day")}</small>
            <h2 id="h-home" data-bind="home.headline">{T("home.headline.none")}</h2>
            <p data-bind="home.sentence">{T("home.sentence.none")}</p>
          </div>
          <form class="thermo" data-save="house-target">
            <div class="thermo-ring" style="--v:60;--now:0" role="img" aria-label="{T("climate.now")}" data-bind-thermo>
              <svg viewBox="0 0 100 100" aria-hidden="true"><circle class="trk" cx="50" cy="50" r="44" pathLength="100"/><circle class="arc" cx="50" cy="50" r="44" pathLength="100"/><circle class="now" cx="50" cy="50" r="44" pathLength="100"/></svg>
              <div class="thermo-val"><b data-bind="house.temp">{DASH}</b><span>{T("thermo.house")}</span></div>
            </div>
            <div class="climate">
              <div class="target">
                <button type="button" data-step="-1" aria-label="{T("common.decrease", x=ta)}">−</button>
                <label class="value"><small>{T("climate.target")}</small><input type="number" inputmode="decimal" id="house_target" name="house_target" value="21.0" min="5" max="30" step="0.5"></label>
                <button type="button" data-step="1" aria-label="{T("common.increase", x=ta)}">+</button>
              </div>
              <p class="autosave" aria-live="polite"></p>
            </div>
          </form>
        </section>
        <div class="home-tiles">{tile_heat}{tile_plan}{tile_wx}{tile_pump}</div>
        {heatmap}
      </section>'''

    # ================================================================ ARK
    # ---- Varme: varmekildens status, det der sendes, Odin; adfærd (heat_source.behavior)
    calc_table = (f'<details class="more"><summary>{T("hs.howCalc")}</summary>'
                  f'<div class="table-wrap"><table class="table"><thead><tr><th>{T("hs.calcName")}</th><th class="num">{T("hs.calcTemp")}</th>'
                  f'<th>{T("hs.calcWeight")}</th><th class="num">{T("hs.calcContrib")}</th></tr></thead>'
                  f'<tbody data-bind-weight-rows><tr><td colspan="4" class="muted">{DASH}</td></tr></tbody></table></div>'
                  f'<p class="note">{T("heat.howCalcNote")}</p></details>')
    sent = (group(T("hs.subHouseTemp"),
                  rrow(T("hs.sentValue"), "hs.asgard.temp", T("hs.houseTempHint")) +
                  rrow(T("hs.sentWhen"), "hs.asgard.when") +
                  rrow(T("hs.asgardTempEntity"), "hs.asgard.entity") +
                  rrow(T("hs.rowSetpoint"), "hs.asgard.setpoint") +
                  rrow(T("hs.sentWhen"), "hs.asgard.setpointWhen"), extra=calc_table, cls="hs-type-asgard") +
            group(T("hs.sentHttp"),
                  rrow(T("hs.rowTemp"), "hs.http.temp") +
                  rrow(T("hs.sentTarget"), "hs.http.entity") +
                  rrow(T("hs.sentWhen"), "hs.http.when"),
                  extra=f'<p class="note mono" data-bind="hs.sent.url">{DASH}</p>' + calc_table, cls="hs-type-http"))
    heat_over = f'''
        <div data-hs-type="{hs_type}" style="display:grid;gap:var(--space-5)">
          <dl class="metrics">
            {metric(T("heat.weighted"), "°C", "heat.weighted")}
            {metric(T("heat.setpoint"), "°C", "heat.setpoint")}
          </dl>
          {kv([(T("heat.status"), "heat.badgeText"), (T("heat.lastPush"), "heat.lastPush"), (T("dash.targetRole"), "dash.targetRole"),
               (T("dash.hpTemps"), "dash.hpTemps")])}
          <p class="note muted" data-bind="heat.sentNote" hidden></p>
          <div class="sub hs-type-asgard" data-bind-show="dash.odin" hidden><h4>Odin</h4>
            {kv([(T("dash.odinLink"), "dash.odinLink"), (T("dash.odinDriver"), "dash.odinDriver"), (T("dash.odinNow"), "dash.odinNow"),
                 (T("hs.odinState"), "dash.odinPlan"), (T("hs.route"), "odin.route")])}
          </div>
          {sent}
        </div>'''
    heat_set = f'''
        <form data-save="heat_source.behavior" data-patch>
          <div data-hs-type="{hs_type}" style="display:grid;gap:var(--space-5)">
          {ggroup(T("heat.comfortSync"), sswitch("target_sync_enabled", T("heat.targetSync"), T("heat.targetSyncSub"), True),
                  sinput("climate_entity", T("hs.asgardClimate"), "Virtual Thermostat z1"), extra=note(T("hs.targetSyncHint")), cls="hs-type-asgard")}
          {group(T("heat.odinPlan"), sswitch("odin_plan_enabled", T("heat.odinPlanSw"), T("hs.odinPlanHint"), True), cls="hs-type-asgard")}
          {ggroup(T("heat.odinControl"), sswitch("odin_control_enabled", T("heat.odinControlSw"), T("heat.odinControlSub"), False),
                  sstep("odin_max_lift_c", T("heat.odinMaxLift"), 1.5, 0.3, 3.0, 0.1, "°C"), extra=note(T("hs.odinControlHint")), cls="hs-type-asgard")}
          <p class="note hs-type-http">{T("heat.httpBehavior")}</p>
          <p class="note">{T("heat.connOnSystem")}</p>
          </div>
          {savebar("heat-behavior", T("hs.save"))}
        </form>'''
    sheet_heat = sheet("heat", T("hash.heat"), I["heat"], "", T("tile.heat"), None,
                       [("overview", heat_over), ("settings", heat_set)], status_attrs=' data-bind="heat.badgeText"')

    # ---- Næste varme: Odins plan + Touch' forvarmning; elpris i dag; plan mod virkelighed
    plan_over = f'''
        <dl class="metrics">
          {metric(T("plan.next"), "", "tile.planVal")}
          {metric(T("plan.energy"), "kWh", "plan.energyKwh")}
          {metric(T("plan.priceNow"), "", "price.nowShort")}
        </dl>
        <div class="sub" data-panel="plan"><h4 data-bind="planG.sub">{T("planG.sub")}</h4>
          <p class="note" data-bind-show="planG.empty">{T("planG.empty")}</p>
          <div class="plan" data-bind-plan role="img" aria-label="{T("planG.aria")}"></div>
          <p class="fc-legend" aria-hidden="true">
            <span><i class="lbar"></i>{T("planG.lHeat")}</span><span><i class="ldhw"></i>{T("planG.lDhw")}</span>
            <span><i class="lleg"></i>{T("planG.lLeg")}</span><span><i class="llift"></i>{T("planG.lLift")}</span>
            <span><i class="lpre"></i>{T("planG.lPre")}</span><span><i class="lch"></i>{T("planG.lCharge")}</span>
            <span><i class="lins"></i>{T("planG.lInsufficient")}</span>
          </p>
        </div>
        <div class="sub" data-bind-price-today><h4>{T("price.subToday")}</h4>
          <div class="bars" style="--bars-n:24"><div class="bars-plot" data-bind-price-bars></div>
            <div class="axis" aria-hidden="true"><span>00</span><span>06</span><span>12</span><span>18</span><span>24</span></div></div>
          <p class="note" data-bind="price.inclAll">{T("price.inclAll", cur="DKK")}</p>
          {kv([(T("price.now"), "price.now"), (T("price.cheapest"), "price.min"), (T("price.dearest"), "price.max"), (T("price.peakAvg"), "price.peak")])}
        </div>'''
    plan_hist = f'''
        <div class="sub" data-empty data-f="pvr"><h4>{T("plan.vsActual")}</h4>
          <p class="empty">{T("room.noHistory")}</p>
          <div class="bars" style="--bars-n:12"><div class="bars-plot" role="img" aria-label="{T("plan.vsActualAria")}" data-bind-pvr></div>
            <div class="bars-legend"><span><i class="lp"></i>{T("plan.planned")}</span><span><i class="la"></i>{T("plan.actual")}</span></div></div>
          {kv([(T("plan.planned"), "plan.planned"), (T("plan.actual"), "plan.actual")])}
        </div>'''
    sheet_plan = sheet("plan", T("hash.plan"), I["plan"], "violet", T("tile.plan"), None,
                       [("overview", plan_over), ("history", plan_hist)], status_attrs=' data-bind="tile.planStatus"')

    # ---- Vejr: prognose (produktets dobbelte graf), fortid, boost (weather.boost)
    wx_over = f'''
        <dl class="metrics">
          {metric(T("fc.now"), "°C", "forecast.temp")}
          {metric(T("fc.windMax"), "m/s", "forecast.windmax", "c-info")}
          {metric(T("fc.tempMin"), "°C", "forecast.tmin")}
        </dl>
        <p class="msg info" data-bind-show="fc.preload" hidden><span data-bind="fc.preloadMsg"></span></p>
        <div class="sub" data-panel="forecast" data-empty><h4 data-bind="fc.sub">{T("fc.sub", model=DASH, time=DASH)}</h4>
          <p class="empty">{T("fc.empty")}</p>
          <!-- Vejrikoner og vindpil, som binderen refererer (<use href="#i-…">). -->
          <svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
            <symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M4.9 19.1l1.8-1.8M17.3 6.7l1.8-1.8"/></symbol>
            <symbol id="i-moon" viewBox="0 0 24 24"><path d="M19 14.5A7.5 7.5 0 1 1 9.5 5a6 6 0 0 0 9.5 9.5z"/></symbol>
            <symbol id="i-cloud" viewBox="0 0 24 24"><path d="M7 18h10a4 4 0 0 0 0-8 5.5 5.5 0 0 0-10.6 1.5A3.3 3.3 0 0 0 7 18z"/></symbol>
            <symbol id="i-partly" viewBox="0 0 24 24"><path d="M8 5V3.5M3.5 8H2M4.6 4.6l-1-1M11.4 4.6l1-1"/><path d="M5.4 10.4A3.5 3.5 0 0 1 11 6.6"/><path d="M9 19h8a3.5 3.5 0 0 0 0-7 4.8 4.8 0 0 0-9.2 1.3A2.9 2.9 0 0 0 9 19z"/></symbol>
            <symbol id="i-rain" viewBox="0 0 24 24"><path d="M7 14h10a4 4 0 0 0 0-8 5.5 5.5 0 0 0-10.6 1.5A3.3 3.3 0 0 0 7 14z"/><path d="M8 17l-1 3M12 17l-1 3M16 17l-1 3"/></symbol>
            <symbol id="i-snow" viewBox="0 0 24 24"><path d="M7 14h10a4 4 0 0 0 0-8 5.5 5.5 0 0 0-10.6 1.5A3.3 3.3 0 0 0 7 14z"/><path d="M8 18h.01M12 18h.01M16 18h.01M10 21h.01M14 21h.01"/></symbol>
            <symbol id="i-arrow" viewBox="0 0 24 24"><path d="M12 20V4M6 10l6-6 6 6"/></symbol>
          </defs></svg>
          <div class="fc fc--dual" style="--now:0%">
            <div class="fc-icons" data-hourly aria-hidden="true"></div>
            <div class="fc-y" aria-hidden="true"></div>
            <div class="fc-plot fc-temp" role="img" aria-label="{T("fc.tempAria")}">
              <svg viewBox="0 0 720 100" preserveAspectRatio="none" data-bind-fc="temp"><rect class="pre" x="0" y="0" width="0" height="100"/><polygon class="sa" points=""/><polyline class="sl" points=""/><rect class="past" x="0" y="0" width="0" height="100"/><polyline class="tl" points=""/><line class="now" x1="0" x2="0" y1="0" y2="100"/></svg>
              <span class="fc-now">{T("fc.now")}</span>
            </div>
            <div class="fc-y2" aria-hidden="true"></div>
            <div class="fc-y" aria-hidden="true"></div>
            <div class="fc-plot fc-wind" role="img" aria-label="{T("fc.windAria")}">
              <svg viewBox="0 0 720 60" preserveAspectRatio="none" data-bind-fc="wind"><polygon class="wa" points=""/><polyline class="wl" points=""/><rect class="past" x="0" y="0" width="0" height="60"/><line class="now" x1="0" x2="0" y1="0" y2="60"/></svg>
            </div>
            <div class="fc-y2" aria-hidden="true"><span>m/s</span></div>
            <div class="fc-dirs" aria-hidden="true"></div>
            <div class="fc-x" aria-hidden="true"></div>
            <span class="fc-scrub" hidden aria-hidden="true"></span>
            <div class="fc-readout" hidden aria-hidden="true"></div>
          </div>
          <p class="fc-legend" aria-hidden="true">
            <span><i class="lt"></i>{T("fc.lTemp")}</span><span><i class="lsun"></i>{T("fc.lSun")}</span><span><i class="lw"></i>{T("fc.lWind")}</span>
            <span><svg class="dir" viewBox="0 0 24 24"><use href="#i-arrow"/></svg>{T("fc.lDir")}</span><span><i class="lpre"></i>{T("fc.lPre")}</span>
          </p>
        </div>'''
    wx_hist = trend("wx.past", "wx-past", T("wx.pastAria"), f="wxpast")
    wx_set = f'''
        <form data-save="weather.boost" data-patch>
          {group(T("wx.preload"), sstep("wx_boost", T("wx.boost"), 1.5, 0, 3, 0.1, "°C", hint=T("wx.boostHint")))}
          <p class="note">{T("wx.locationOnSystem")}</p>
          {savebar("weather-boost", T("weatherCfg.save"))}
        </form>'''
    sheet_wx = sheet("weather", T("hash.weather"), I["wx"], "info", T("tile.weather"), None,
                     [("overview", wx_over), ("history", wx_hist), ("settings", wx_set)], status_attrs=' data-bind="tile.weatherStatus"')

    # ---- Cirkulation: pumpen og fordeling pr. styring
    pump_over = f'''
        <dl class="metrics">
          {metric(T("pump.flow"), "l/min", "pump.flow")}
          {metric(T("pump.head"), "m", "pump.head")}
          {metric(T("pump.power"), "W", "pump.power")}
        </dl>
        {kv([(T("pump.flowM3h"), "pump.flowM3h"), (T("pump.host"), "pump.host")])}
        <div class="sub"><h4>{T("pump.dist")}</h4>
          <div class="dist" data-bind-dist><p class="dist-note">{DASH}</p></div></div>
        <p class="note">{T("flow.sub")}</p>'''
    sheet_pump = sheet("pump", T("hash.pump"), I["pump"], "neutral", T("tile.pump"), None,
                       [("overview", pump_over)], status_attrs=' data-bind="tile.pumpStatus"')

    # ---- Styring (skabelon; binderen kloner én pr. styring): status, fremløb/retur, rum, link til V6
    def manifold_sheet(n):
        over = f'''
        <p class="msg warn" data-f="offline" hidden><span><b>{T("v6.offlineStrong")}</b> <span data-f="offlineBody"></span></span></p>
        <dl class="metrics">{metric(T("m.supply"), "°C", f="flow")}{metric(T("m.return"), "°C", cls="c-info", f="ret")}</dl>
        {kv([(T("ctrl.status"), None, DASH, "state"), (T("ctrl.host"), None, DASH, "host"), ("IP", None, DASH, "ip"), (T("fw.installed"), None, DASH, "fw")])}
        <div class="sub"><h4>{T("mani.zones")}</h4><div class="comfort" data-f="zones"></div></div>
        <div class="actions"><a class="btn" data-f="link" href="#" target="_blank" rel="noopener">{T("v6.open")}</a></div>'''
        return sheet(f"m{n}", f"m{n}", I["mani"], "neutral", f"M{n}", None, [("overview", over)],
                     head_attrs=' data-f="name"', status_attrs=' data-f="status"')

    # ---- Rum (skabelon; binderen kloner én pr. rum): kun det Touch ejer er redigerbart (rooms, PATCH)
    WF = T.meta("_walls_full")

    def room_sheet(k):
        rid = f"r{k}"
        over = f'''
        <div class="panel alert" data-f="fault" hidden><div class="panel-head"><h3>{T("alert.motorFault")}</h3></div>
          <p class="note">{T("alert.roomFaultBody")}</p>
          <div class="panel-foot"><a class="btn" data-f="resetLink" href="#" target="_blank" rel="noopener">{T("v6.resetOnV6")}</a></div></div>
        <p class="msg warn" data-f="nodata" hidden><span><b>{T("room.noDataStrong")}</b> {T("room.noData")}</span></p>
        <dl class="metrics">
          {metric(T("room.temp"), "°C", f="temp")}
          {metric(T("room.target"), "°C", f="target")}
          {metric(T("room.valve"), "%", f="valve")}
        </dl>
        <div class="bar" style="--v:0%" data-f="bar" aria-hidden="true"><i></i></div>
        <div class="sub"><h4 data-f="loopsTitle">{T("room.loops", m="V6")}</h4><dl class="kv" data-f="loops"></dl></div>
        {trend("room.next24", "", T("room.next24Aria", name=rid), f'<span class="legend"><i class="lt"></i>{T("legend.temp")}<i class="lg"></i>{T("legend.target")}</span>', f="next24")}'''
        hist = f'''
        <div class="sub" data-empty data-f="hist"><h4>{T("room.histTitle")}</h4>
          <p class="empty">{T("room.noHistory")}</p>
          {kv([(T("room.histAvg"), None, DASH, "hAvg"), (T("room.histMin"), None, DASH, "hMin"), (T("room.histMax"), None, DASH, "hMax"),
               (T("room.histCalling"), None, DASH, "hCall"), (T("room.histSamples"), None, DASH, "hN")])}
        </div>'''
        v6 = group(f'<span data-f="fromV6">{T("room.fromV6", m="V6", z="—")}</span>',
                   srow(f"<span>{T('room.area')}</span>", f'<span class="setting-value" data-f="area">{DASH}</span>') +
                   srow(f"<span>{T('room.walls')}</span>", f'<span class="setting-value" data-f="walls">{DASH}</span>') +
                   srow(f"<span>{T('room.editOnV6Label')}</span>",
                        f'<a class="btn" data-f="editLink" href="#" target="_blank" rel="noopener">{T("room.editOnV6")}</a>'),
                   extra=f'<p class="offline-note" data-f="offlineNote" hidden></p>', attrs=' data-f="v6"')
        settings = f'''
        <form data-save="rooms" data-patch data-room-slot="{k}">
          {group(T("room.house"), sswitch(f"room:{rid}:include", T("room.include"), T("room.includeSub"), True) +
                 sstep(f"room:{rid}:weight", T("room.weight"), 1.0, 0, 5, 0.05, "×", dec=2, id_=f"{rid}-weight", hint=T("room.weightHint")))}
          {group(T("room.weather"), sstep(f"room:{rid}:wind", T("room.wind"), 0.5, 0, 1, 0.05, "", dec=2, id_=f"{rid}-wind") +
                 sstep(f"room:{rid}:solar", T("room.solar"), 0.3, 0, 1, 0.05, "", dec=2, id_=f"{rid}-solar"))}
          {v6}
          {savebar(f"room-{rid}", T("rconf.save"))}
        </form>'''
        return sheet(rid, rid, I["room"], "", DASH, None,
                     [("overview", over), ("history", hist), ("settings", settings)],
                     head_attrs=' data-f="name"', status_attrs=' data-f="status"')

    sheets = (sheet_heat + sheet_plan + sheet_wx + sheet_pump
              + f'\n  <template id="tpl-mani">{manifold_sheet(SLOT)}</template>'
              + f'\n  <template id="tpl-room">{room_sheet(SLOT)}</template>')

    # ================================================================ SYSTEM
    def syscat(cat, title, body, badge="", help_=""):
        tools = f'<span class="actions">{badge}{help_}</span>' if (badge or help_) else ""
        return (f'<section class="sys-cat" data-cat="{cat}" aria-labelledby="h-c-{cat}"><label class="sys-back" for="c-none">{T("sys.title")}</label>'
                f'<header><div><small>{T("sys.title")}</small><h2 id="h-c-{cat}">{title}</h2></div>{tools}</header>{body}</section>')

    def hhelp(cat, topic_key, keys, more):
        hid = f"help-{cat}"
        return help_btn(hid, T(topic_key)) + help_pop(hid, keys, more)

    def file_input(name, accept):
        return (f'<label class="input file w-md" for="{name}"><input class="sr-only" type="file" id="{name}" name="{name}" accept="{accept}">'
                f'<span class="file-pick">{T("common.chooseFile")}</span><span class="file-name" data-empty="{T("common.noFile")}">{T("common.noFile")}</span></label>')

    def form_status():
        return '<p class="note" data-form-status aria-live="polite"></p>'

    cats = []
    # Enhed
    cats.append(("device", f'''
      <form data-save="settings">
        {group(T("dev.identity"), sinput("name", T("id.name"), T("device.sample"), "w-sm", id_="dev_name") +
               sstep("dev_idle", T("id.idle"), 5, 0, 120, 1, "min", dec=0, hint=T("dev.idleHint")))}
        {savebar("settings", T("id.save"))}
      </form>''', "", hhelp("device", "id.title", ["help.identity"], "docs/Manual.md#identity")))

    # Styringer: tabellen og fundne styringer tegnes af binderen
    cats.append(("controllers", f'''
      <form data-save="add-node">
        <section class="setting-group"><h4>{T("ctrl.list")}</h4>
          <div class="table-wrap"><table class="table" data-bind-nodes><thead><tr><th>{T("ctrl.name")}</th><th>{T("ctrl.host")}</th><th>{T("ctrl.status")}</th><th class="num">{T("ctrl.zones")}</th><th></th></tr></thead>
            <tbody><tr><td colspan="5" class="muted">{T("ctrl.empty")}</td></tr></tbody></table></div>
        </section>
        {group(T("ctrl.find"), sbtn(T("ctrl.scanLabel"), f'<button class="btn" type="button" data-action="scan-nodes">{T("ctrl.scan")}</button>', T("ctrl.scanNote")),
               extra='<div class="setting-list" data-bind-scan hidden></div>')}
        {group(T("zs.advanced"), subpage(T("ctrl.addManual"), group(T("ctrl.addTitle"),
               sinput("name", T("ctrl.name"), "", "w-sm", id_="node_name", extra=f' placeholder="{T("ctrl.namePh")}"') +
               sinput("host", T("ctrl.host"), "", "w-md", id_="node_host", extra=' placeholder="lune-v6.local" inputmode="url" autocomplete="off" spellcheck="false"'),
               extra=f'<div class="actions"><button class="btn primary" type="submit">{T("ctrl.add")}</button></div>{form_status()}')))}
      </form>''', "", hhelp("controllers", "ctrl.title", ["help.controllers", "help.addController"], "docs/Manual.md#controllers")))

    # Varmekilde: forbindelsen (heat_source.connection, PATCH)
    r_hs, seg_hs = seg("hs_type", "hs", [("http", T("hs.typeHttp")), ("asgard", T("hs.typeAsgard"))], hs_type, T("hs.type"))
    http_f = typed("http",
        group(T("hs.connection"), sinput("http_host", T("hs.host"), "heat-bridge.local") +
              sport("http_port", T("hs.port")) +
              sstep("http_push_interval_s", T("hs.pushInterval"), 60, 5, 3600, 5, "s", dec=0)) +
        group(T("hs.mapping"), sinput("http_weighted_temperature_variable", T("hs.weightedVar"), "temperature_feedback_z1") +
              sinput("write_url_template", T("hs.writeUrl"), "", "w-lg", stack=True, extra=' placeholder="http://{host}:{port}/{entity}/set?value={value}"') +
              sinput("read_url_template", T("hs.readUrl"), "", "w-lg", stack=True, extra=' placeholder="http://{host}:{port}/{entity}"') +
              subpage(T("hs.httpControl"), group(T("hs.subLevers"),
                  sinput("target_url_template", T("hs.leverTarget"), "", "w-lg", stack=True, extra=' placeholder="http://{host}:{port}/target?value={value}"') +
                  sinput("heat_request_url_template", T("hs.leverRequest"), "", "w-lg", stack=True, extra=' placeholder="http://{host}:{port}/heat?value={value}"') +
                  sinput("curve_offset_url_template", T("hs.leverCurve"), "", "w-lg", stack=True, extra=' placeholder="http://{host}:{port}/curve_offset?value={value}"') +
                  sstep("curve_gain", T("hs.curveGain"), 2.0, 0.0, 10.0, 0.5, "°C/°C") +
                  sstep("curve_max_offset_c", T("hs.curveMax"), 5.0, 0.0, 15.0, 0.5, "°C") +
                  rrow(T("hs.route"), "levers.state"), pre=note(T("hs.leversHint")))),
              extra=note(T("help.heatHttp"))), hs=True)
    mqtt = subpage(T("hs.mqtt"),
        ggroup(T("hs.mqttConn"), sswitch("mqtt_enabled", T("hs.mqttEnabled"), T("hs.mqttSub"), False, extra=' data-save-now="false"'),
               sinput("mqtt_host", T("hs.host"), "", "w-md", extra=' placeholder="192.168.1.10"') +
               sport("mqtt_port", T("hs.port"), 1883), pre=note(T("hs.mqttHint")),
               extra=note("", ' data-bind="hs.mqttStatus" aria-live="polite"')) +
        group(T("hs.mqttLogin"), sinput("mqtt_username", T("hs.mqttUser"), "", "w-sm", extra=' autocomplete="off"') +
              sinput("mqtt_password", T("hs.mqttPassword"), "", "w-sm", typ="password", extra=' autocomplete="new-password"') +
              sinput("mqtt_topic_prefix", T("hs.mqttPrefix"), "", "w-md", extra=' placeholder="hp/hp1"') +
              sinput("mqtt_hp_id", T("hs.mqttHpId"), "", "w-xs", extra=' placeholder="hp1"')),
        vbind="hs.mqttShort")
    asg_f = typed("asgard",
        group(T("hs.connection"), sinput("asgard_host", T("hs.host"), "asgard.local") +
              sport("asgard_port", T("hs.port")) +
              sstep("asgard_push_interval_s", T("hs.pushInterval"), 60, 5, 3600, 5, "s", dec=0)) +
        group(T("hs.houseTemp"), sinput("asgard_weighted_temperature_variable", T("hs.asgardTempEntity"), "Virtual Thermostat Input z1",
                                        hint=T("hs.weightedVarHint"))) +
        group("Odin", sinput("odin_host", T("hs.odinHost"), "", hint=T("hs.odinHostHint"), extra=' placeholder="192.168.1.20"')) +
        group(T("zs.advanced"), mqtt), hs=True)
    test = (f'<div class="actions"><button class="btn" type="button" data-action="hs-test-read">{T("hs.testRead")}</button>'
            f'<button class="btn" type="button" data-action="hs-test-push">{T("hs.testPush")}</button></div>'
            f'<div class="test-result" aria-live="polite"></div>')
    cats.append(("heatsource", f'''
      <form data-save="heat_source.connection" data-patch>
        {r_hs}
        {group(T("hs.title"), sswitch("enabled", T("hs.enabled"), "", True, id_="hs_enabled") + srow(f'<span>{T("hs.type")}</span>', seg_hs))}
        {http_f}{asg_f}
        {test}
        {savebar("heat-connection", T("hs.save"))}
      </form>''', f'<span class="badge" data-bind="heat.badge">{T("status.waiting")}</span>',
                 hhelp("heatsource", "hs.title", ["help.heatSource", "help.heatAsgard"], "docs/Manual.md#heat-source")))

    # Elpris (efter Varmekilde): model + zone som hovedgruppe, dele som undersider
    zone_groups = {}
    for zid, g in PRICE_ZONES:
        zone_groups.setdefault(g, []).append(zid)
    price_zone_select = '<select class="select" id="price_zone" name="zone">' + "".join(
        f'<optgroup label="{T("price.group." + g)}">' + "".join(
            f'<option value="{z}"{" selected" if z == "DK1" else ""}>{z}</option>' for z in zs) + "</optgroup>"
        for g, zs in zone_groups.items()) + "</select>"
    price_currency_select = '<select class="select" id="price_currency" name="currency">' + "".join(
        f'<option value="{c}" data-fx="{fx}"{" selected" if c == "DKK" else ""}>{c}</option>' for c, fx in PRICE_CURRENCIES) + "</select>"
    r_pm, seg_pm = seg("model", "pm", [("odin", T("price.modelOdin")), ("touch", T("price.modelTouch"))], "touch", T("price.model"))
    r_om, seg_om = seg("odin_mode", "om", [("dynamic", T("price.odinDynamic")), ("fixed", T("price.odinFixed"))], "dynamic", T("price.odinModeLabel"))
    odin_src = ('<select class="select" id="odin_source" name="odin_source"><option value="energy_charts" selected>Energy-Charts</option>'
                '<option value="entsoe">ENTSO-E</option></select>')
    odin_f = typed("odin", r_om +
        group(T("price.subOdinOwn"), srow(f'<span>{T("price.odinModeLabel")}</span>', seg_om)) +
        typed("dynamic", group(T("price.odinDynamic"), srow(lab("odin_source", T("price.odinSourceLabel")), odin_src),
                               extra=note(T("price.odinDynamicHint")))) +
        typed("fixed", group(T("price.odinFixed"), sstep("odin_fixed_price", T("price.odinFixedPrice"), 0.25, 0, 5, 0.01, "€/kWh", dec=3))) +
        group(T("price.subOdinNow"), rrow(T("price.odinNowMode"), "price.odin.mode") + rrow(T("price.odinNowZone"), "price.odin.zone") +
              rrow(T("price.odinNowFixed"), "price.odin.fixed") + rrow(T("price.token"), "price.odin.token") +
              sbtn(T("price.writeNow"), f'<button class="btn" type="button" data-action="price-odin-write">{T("price.odinWrite")}</button>'),
              pre=note(T("price.odinDkNote"), ' data-bind="price.dkNote"'),
              extra='<div class="test-result" data-bind-price-odin-result aria-live="polite"></div>'))
    r_ss, seg_ss = seg("spot_source", "ss", [("eds", T("price.srcEds")), ("energy_charts", "Energy-Charts"), ("entsoe", "ENTSO-E"), ("fixed", T("price.srcFixed"))],
                       "eds", T("price.spotSource"), dk_only=("eds",))
    spot = (r_ss + group(T("price.spot"), srow(f'<span>{T("price.spotSource")}</span>', seg_ss)) +
            typed("eds", note(T("price.spotEdsHint"))) + typed("energy_charts", note(T("price.spotEcHint"))) +
            typed("entsoe", note(T("price.spotEntsoeHint"))) +
            typed("fixed", group(T("price.srcFixed"), sstep("spot_fixed_eur", T("price.spotFixed"), 0.10, -1, 5, 0.01, "€/kWh", dec=3))))
    taxes = group(T("price.subTaxes"),
        srow(lab("price_currency", T("price.currency")), price_currency_select) +
        srow(lab("fx", T("price.fx")), '<input class="input w-sm" type="number" inputmode="decimal" id="fx" name="fx" value="7.46" min="0.01" max="10000" step="any">', T("price.fxHint")) +
        sstep("energy_tax", T("price.energyTax"), 0.008, 0, 500, 0.001, "DKK", dec=3, unit_attr=unit_cur) +
        sstep("markup", T("price.markup"), 0.0, -100, 500, 0.01, "DKK", dec=3, unit_attr=unit_cur) +
        sstep("vat_pct", T("price.vat"), 25, 0, 50, 0.5, "%", dec=1) +
        sbtn(T("price.zoneDefaults"), f'<button class="btn" type="button" data-action="price-zone-defaults">{T("price.applyDefaults")}</button>'),
        extra=note(T("price.perKwhHint")) + note(T("price.applyDefaultsHint"), ' data-bind="price.defaultsNote" aria-live="polite"'))
    sched_rows = "".join(
        f'<tr><td><input class="input w-xs" type="number" inputmode="numeric" min="0" max="23" step="1" data-sched="h" value="{b["h"]}" aria-label="{T("price.schedHourAria")}"></td>'
        f'<td class="num"><input class="input w-sm" type="number" inputmode="decimal" min="-5" max="20" step="0.001" data-sched="v" value="{b["v"]:.3f}" aria-label="{T("price.schedValueAria")}"></td>'
        f'<td><button class="btn" type="button" data-action="price-sched-remove">{T("price.schedRemove")}</button></td></tr>'
        for b in DEFAULT_SCHEDULE)
    sched_json = json.dumps(DEFAULT_SCHEDULE, separators=(",", ":"))
    r_gt, seg_gt = seg("grid_source", "gt", [("datahub", T("price.srcDatahub")), ("schedule", T("price.srcSchedule")), ("none", T("price.srcNone"))],
                       "datahub", T("price.gridSource"), dk_only=("datahub",))
    grid = (r_gt + group(T("price.grid"), srow(f'<span>{T("price.gridSource")}</span>', seg_gt)) +
            typed("datahub", group(T("price.srcDatahub"),
                  sinput("grid_gln", T("price.gln"), "5790000610976", "w-md", id_="price_gln", extra=' inputmode="numeric" maxlength="13" spellcheck="false"') +
                  sinput("grid_code", T("price.code"), "TNT1009", "w-sm", id_="price_code", extra=' maxlength="23" spellcheck="false"'),
                  extra=note(T("price.gridDatahubHint")))) +
            typed("schedule", f'''<section class="setting-group"><h4>{T("price.srcSchedule")}</h4>
              <div class="table-wrap"><table class="table"><thead><tr><th>{T("price.schedFrom")}</th><th class="num" data-price-unit>DKK/kWh</th><th></th></tr></thead>
              <tbody data-bind-price-sched>{sched_rows}</tbody></table></div>
              <input type="hidden" name="grid_schedule" value='{sched_json}'>
              <div class="actions"><button class="btn" type="button" data-action="price-sched-add">{T("price.schedAdd")}</button></div>
              {note(T("price.schedHint"))}</section>''') +
            typed("none", note(T("price.gridNoneHint"))))
    r_en, seg_en = seg("system_source", "en", [("datahub", T("price.srcDatahub")), ("fixed", T("price.srcFixed"))], "datahub",
                       T("price.systemSource"), dk_only=("datahub",))
    systar = (r_en + group(T("price.system"), srow(f'<span>{T("price.systemSource")}</span>', seg_en)) +
              typed("datahub", note(T("price.systemDatahubHint"))) +
              typed("fixed", group(T("price.srcFixed"), sstep("system_fixed", T("price.systemFixed"), 0.115, -100, 500, 0.001, "DKK", dec=3, unit_attr=unit_cur),
                                   extra=note(T("price.systemFixedHint")))))
    touch_f = typed("touch",
        group(T("price.touchCalc"), subpage(T("price.subSpot"), spot, vbind="price.sumSpot") + subpage(T("price.subTaxes"), taxes, vbind="price.sumTaxes") +
              subpage(T("price.subGrid"), grid, vbind="price.sumGrid") + subpage(T("price.subSystem"), systar, vbind="price.sumSystem")) +
        group(T("price.subStatus"), rrow(T("price.lastPush"), "price.lastPush") + rrow(T("price.hours"), "price.hours") +
              rrow(T("price.spotUsed"), "price.spotUsed") + rrow(T("price.odinMode"), "price.odinMode") + rrow(T("price.problem"), "price.problem") +
              sbtn(T("price.pushNow"), f'<button class="btn" type="button" data-action="price-push">{T("price.pushNow")}</button>'),
              extra=note("", ' data-bind="price.note"') + '<div class="test-result" data-bind-price-result aria-live="polite"></div>'))
    cats.append(("prices", f'''
      <form data-save="prices">
        {r_pm}
        {group(T("price.title"), sswitch("enabled", T("price.enabled"), T("price.enabledHint"), False, id_="price_enabled") +
               srow(f'<span>{T("price.model")}</span>', seg_pm) +
               srow(lab("price_zone", T("price.zone")), price_zone_select) +
               sinput("entsoe_token", T("price.token"), "", "w-md", id_="price_token", typ="password", hint=T("price.tokenHint"),
                      extra=' autocomplete="new-password" spellcheck="false" maxlength="63"'))}
        {odin_f}{touch_f}
        {savebar("prices", T("price.save"))}
      </form>''', f'<span class="badge" data-bind="price.badge">{T("price.state.disabled")}</span>',
                 hhelp("prices", "price.title", ["help.price"], "docs/Manual.md#electricity-price-to-odin")))

    # Cirkulationspumpe
    cats.append(("pump", f'''
      <form data-save="circulation">
        {group(T("pump.connection"), sinput("host", T("pumpCfg.host"), "", id_="pump_host", extra=' placeholder="alpha2go.local"') +
               sport("pump_port", T("pumpCfg.port")))}
        {group(T("pump.entities"), sinput("flow_entity", T("pumpCfg.flowEntity"), "pump_flow", id_="pump_flow_entity") +
               sinput("head_entity", T("pumpCfg.headEntity"), "pump_head_pressure", id_="pump_head_entity") +
               sinput("power_entity", T("pumpCfg.powerEntity"), "pump_power", id_="pump_power_entity"))}
        {savebar("circulation", T("pumpCfg.save"))}
      </form>''', "", hhelp("pump", "pumpCfg.title", ["help.pump"], "docs/Manual.md#pump")))

    # Vejr (placering, PATCH)
    cats.append(("weather", f'''
      <form data-save="weather.location" data-patch>
        {group(T("wx.location"), sinput("latitude", T("weatherCfg.lat"), "55.6761", "w-sm", id_="wx_lat", extra=' inputmode="decimal"') +
               sinput("longitude", T("weatherCfg.lon"), "12.5683", "w-sm", id_="wx_lon", extra=' inputmode="decimal"') +
               sbtn(T("wx.geoLabel"), f'<button class="btn" type="button" data-action="wx-geo">{T("weatherCfg.geo")}</button>'),
               extra=note(T("wx.boostOnSheet")))}
        {savebar("weather-location", T("weatherCfg.save"))}
      </form>''', "", hhelp("weather", "weatherCfg.title", ["help.weather"], "docs/Manual.md#weather")))

    # Netværk
    cats.append(("network", f'''
      <form data-save="wifi">
        {group(T("wifi.title"), rrow(T("wifi.current"), "wifi.current") + rrow(T("wifi.status"), "wifi.status") +
               sinput("ssid", T("wifi.ssid"), "", "w-md", id_="wifi_ssid", extra=' maxlength="32" autocomplete="off" spellcheck="false"') +
               sinput("password", T("wifi.password"), "", "w-md", id_="wifi_password", typ="password", extra=' maxlength="64" autocomplete="new-password"'),
               extra=note(T("wifi.hint")) + form_status())}
        {savebar("wifi", T("wifi.save"))}
      </form>''', "", hhelp("network", "wifi.title", ["help.wifi"], "docs/Manual.md#wifi")))

    # Firmware og backup
    cats.append(("firmware", f'''
      <form data-save="firmware">
        {group(T("fw.firmware"), rrow(T("csys.fwInstalled"), "fw.installed") + rrow(T("csys.fwLatest"), "fw.latest") +
               sbtn(T("fw.update"), f'<button class="btn" type="submit" name="action" value="check">{T("csys.fwCheck")}</button>'
                                    f'<button class="btn primary" type="submit" name="action" value="install" hidden>{T("csys.fwInstall")}</button>') +
               srow(lab("ota_file", T("csys.fwUpload")), file_input("ota_file", ".bin,.ota.bin")) +
               sbtn(T("fw.uploadLabel"), f'<button class="btn" type="submit" name="action" value="upload" disabled>{T("csys.fwUploadBtn")}</button>'),
               extra=form_status())}
      </form>
      <form data-save="backup">
        {group(T("csys.backup"), sbtn(T("fw.export"), f'<button class="btn" type="submit" name="action" value="export">{T("csys.backupExport")}</button>', T("csys.backupNote")) +
               srow(lab("backup_file", T("csys.backupImport")), file_input("backup_file", "application/json,.json")),
               extra=f'<div class="actions">{confirm("cf-import", T("fw.import"), T("fw.importAsk"), T("fw.importNote"), T("fw.importDo"), value="import", opener_attrs=" disabled")}</div>' + form_status())}
      </form>''', "", hhelp("firmware", "csys.firmware", ["help.firmware", "help.backup"], "docs/Manual.md#firmware")))

    # Service: diagnostik, log, nulstil
    cats.append(("service", f'''
      <section class="sys-diag" style="display:grid;gap:var(--space-5)">
        {group(T("svc.house"), rrow(T("svc.coverage"), "house.coverage", T("svc.coverageHint")) + rrow(T("svc.authority"), "house.authority") +
               rrow(T("climate.outdoor"), "house.outdoor") + rrow(T("svc.dist"), "svc.dist"))}
        {group(T("svc.diag"), rrow(T("diag.nodes"), "diag.nodes") + rrow(T("diag.poll"), "diag.poll") + rrow(T("diag.ota"), "diag.ota") +
               rrow(T("svc.hostIp"), "svc.hostIp"))}
        <section class="setting-group"><h4>{T("svc.commands")}</h4><pre class="log" data-bind="log" aria-live="polite" lang="en">{DASH}</pre></section>
        <p class="note">{T("svc.helpBody")}</p>
        <div class="actions">{confirm("confirm-reset", T("svc.reset"), T("id.resetConfirm"), T("id.resetNote"), T("common.reset"), action="reset-registry", btn_type="button")}</div>
      </section>''', "", hhelp("service", "svc.title", ["help.service"], "docs/Manual.md#service")))

    titles = {"device": "cat.device", "controllers": "cat.controllers", "heatsource": "cat.heatsource", "prices": "cat.prices",
              "pump": "cat.pump", "weather": "cat.weather", "network": "cat.network", "firmware": "cat.firmware", "service": "cat.service"}
    sys_radios = f'<input class="state" type="radio" name="syscat" id="c-none" checked aria-label="{T("sys.cats")}">' + "".join(
        f'<input class="state" type="radio" name="syscat" id="c-{c}" data-hash="{T("hash." + c)}" aria-label="{T(titles[c])}">' for c, *_ in cats)
    sys_nav = "".join(f'<label for="c-{c}"><svg viewBox="0 0 24 24" aria-hidden="true">{CAT_ICONS[c]}</svg>{T(titles[c])}</label>' for c, *_ in cats)
    sys_view = f'''
      <section class="view" id="v-sys" aria-labelledby="h-sys">
        <h2 class="sr-only" id="h-sys">{T("sys.title")}</h2>
        {sys_radios}
        <div class="sys">
          <nav class="sys-nav" aria-label="{T("sys.cats")}">{sys_nav}</nav>
          <div class="sys-main">{"".join(syscat(c, T(titles[c]), body, badge, hp) for c, body, badge, hp in cats)}</div>
        </div>
      </section>'''

    # ================================================================ side
    cur = T.meta("_lang")
    if len(langs) > 1:
        links = "".join(f'<a href="{lang_urls[c.meta("_lang")]}" hreflang="{c.meta("_lang")}" lang="{c.meta("_lang")}" title="{c.meta("_name")}"'
                        f'{" aria-current=" + chr(34) + "true" + chr(34) if c.meta("_lang") == cur else ""}>{c.meta("_short")}</a>' for c in langs)
        langnav = f'<nav class="lang" aria-label="{T("lang.label")}">{links}</nav>'
    else:
        langnav = ""

    rt_keys = (
        "rt.savedOk", "rt.saveFailed", "rt.saving", "rt.unsaved.one", "rt.unsaved.other",
        "rt.nothingToSave", "rt.leaveUnsaved", "rt.autoSaving", "rt.autoSaved", "rt.autoFailed", "rt.retry",
        "rt.secondsAgo", "rt.minutesAgo", "rt.offline", "common.undo",
        "state.calling", "state.idle", "state.fault", "state.off",
        "tile.fault", "tile.off", "common.unnamed", "common.cancel", "common.days", "common.none",
        "device.this", "device.copied", "device.copyDiag",
        "alert.boardFault", "alert.roomFault", "alert.roomFaultBody", "common.open",
        "ctrl.removeAsk", "ctrl.removeConfirm", "ctrl.removeBtn", "ctrl.removeDo", "ctrl.editName", "ctrl.editHost", "ctrl.hostInvalid",
        "ctrl.found", "ctrl.addRow", "ctrl.empty", "ctrl.host", "ctrl.scanNone",
        "heat.badge.off", "hs.typeHttp", "hs.typeAsgard", "hs.notSent", "hs.notSentLease",
        "hs.calcWeightArea", "hs.calcWeightUa", "hs.testing",
        "hs.testReadOk", "hs.testPushOk", "hs.testReadBody", "hs.testPushBody",
        "hs.testFailLine", "hs.testCheckHost", "hs.testCheckDns", "hs.testCheckHttp", "hs.testStatus.failed",
        "hs.testReason.timeout", "hs.testReason.http", "hs.testReason.dns", "hs.testReason.missing", "hs.testReason.network",
        "hs.testReason.endpoint", "hs.testReason.notReady", "hs.testReason.busy", "hs.testReason.rejected",
        "hs.host", "hs.writeUrl", "hs.readUrl",
        "status.trusted", "status.paired", "status.unpaired", "status.waiting", "status.unreachable", "status.confirmed",
        "status.sent", "status.mismatch", "status.blocked", "status.unknown", "status.disabled", "status.online", "status.offline",
        "fc.sub", "fc.badge", "fc.windFrom", "fc.dirTitle", "fc.now", "fc.preloadMsg",
        "fc.sky.sun", "fc.sky.partly", "fc.sky.cloud", "fc.sky.rain", "fc.sky.snow", "fc.sky.moon",
        "diag.pollOk", "diag.pollFail", "diag.ota.valid", "diag.ota.pending_verify", "diag.ota.new", "diag.ota.invalid",
        "diag.ota.aborted", "diag.ota.undefined",
        "m.supplyShort", "m.returnShort", "strip.lease.refused", "strip.lease.none", "strip.charge.now", "strip.charge.insufficient",
        "hp.compOn", "hp.compOff", "flow.none", "flow.note", "flow.noteLpm", "flow.zoneTip",
        "role.reserve", "role.reserveNow", "role.asgardNow", "role.offOdin", "role.drives", "role.off",
        "driver.odin", "driver.asgard", "now.heat", "now.off", "now.dhw", "planG.odin", "planG.noOdin",
        "planG.tip.heat", "planG.tip.dhw", "planG.tip.legionella", "planG.tipPre", "planG.tipCharge", "planG.tipInsufficient", "planG.tipLift",
        "hs.mqttPasswordSet", "hs.mqttState", "hs.mqttConnected", "hs.mqttDisconnected", "hs.mqttOff", "common.on", "common.off",
        "wifi.connectedTo", "wifi.notConnected", "wifi.apActive", "wifi.sent", "wifi.needSsid",
        "wifi.switch.pending", "wifi.switch.connected", "wifi.switch.reverted", "wifi.switch.failed",
        "hs.link.ok", "hs.link.forwarder_off", "hs.link.telemetry_stale", "hs.link.no_room_temperature", "hs.link.odin_unreachable", "hs.link.unknown",
        "hs.odinStatus.disabled", "hs.odinStatus.idle", "hs.odinStatus.watching", "hs.odinStatus.lifted", "hs.odinStatus.yielded",
        "hs.odinStatus.rate_limited", "hs.odinStatus.write_failed", "hs.odinStatus.odin_unreachable", "hs.odinStatus.schedule_unreadable",
        "hs.odinStatus.clock_invalid", "hs.odinWanted", "hs.route.virtual_thermostat", "hs.route.odin_schedule", "hs.route.generic",
        "hs.route.none", "hs.leverState",
        "price.state.ok", "price.state.waiting", "price.state.running", "price.state.error", "price.state.disabled", "price.state.odin",
        "price.odinMode.api", "price.odinMode.energy_charts", "price.odinMode.unknown", "price.odinMode.pending", "price.odinMode.fixed",
        "price.odinMode.entsoe", "price.odinMode.dynamic",
        "price.err.spot", "price.err.spot_incomplete", "price.err.grid", "price.err.energinet", "price.err.odin",
        "price.err.no_odin_host", "price.err.clock", "price.err.other", "price.err.datahub_dk_only", "price.err.no_token",
        "price.cached", "price.noOdinHost", "price.pushOk", "price.pushOkBody", "price.pushFail", "price.pushPending", "price.pushDisabled",
        "price.schedRemove", "price.schedHourAria", "price.schedValueAria", "price.atHour", "price.hoursValue",
        "price.noData", "price.lastPushValue", "price.tokenSet", "price.tokenUnset", "price.tokenSaved", "price.inclAll",
        "price.defaultsApplied", "price.defaultsUnknown", "price.odinWriteOk", "price.odinWriteFail",
        "price.spotUsed.eds", "price.spotUsed.energy_charts", "price.spotUsed.entsoe", "price.spotUsed.fixed", "price.spotFallback",
        "price.srcEds", "price.srcDatahub", "price.srcSchedule", "price.srcNone", "price.srcFixed", "price.vat",
        "price.scale.1", "price.scale.2", "price.scale.3", "price.scale.4", "price.scale.5",
        "csys.fwChecking", "csys.fwUpToDate", "csys.fwAvailable", "csys.fwNoReleases", "csys.fwCheckFailed", "csys.fwInstalling",
        "csys.fwUploading", "csys.fwUploadDone", "csys.fwUploadFailed", "csys.fwNoFile",
        "csys.backupExporting", "csys.backupExported", "csys.backupImporting", "csys.backupImported", "csys.backupInvalid", "csys.backupFailed",
        "home.greeting.morning", "home.greeting.day", "home.greeting.evening", "home.greeting.night",
        "home.headline.below", "home.headline.at", "home.headline.above", "home.headline.fault", "home.headline.none",
        "home.sentence.below", "home.sentence.above", "home.sentence.at", "home.sentence.none", "thermo.aria",
        "tile.planNone", "tile.planVal", "tile.planNow", "tile.planKwh", "tile.planStatusOdin", "tile.price",
        "tile.weatherStatus", "tile.weatherStatusNone", "tile.weatherSub", "tile.pumpStatus", "tile.pumpNone", "tile.pumpSub",
        "tile.room.aria", "v6.offline", "v6.offlineBody", "v6.offlineNoTime", "sheet.manifoldStatus", "sheet.roomStatus",
        "room.loops", "room.valveShort", "room.fromV6", "room.wallsNone", "room.floorUnset", "room.histCallingVal", "room.next24Aria",
        "wx.pastN", "wx.past", "hash.settings", "trend.now",
    )
    rt = {k: T(k) for k in rt_keys}
    rt["_dec"] = T.meta("_dec")
    rt["_walls"] = T.meta("_walls")
    rt["_walls_full"] = T.meta("_walls_full")
    rt["_h"] = H
    rt["_lang"] = cur
    rt_json = json.dumps(rt, ensure_ascii=False, separators=(",", ":"))

    LOGO = ('<svg class="logo" viewBox="0 0 32 32" aria-hidden="true"><circle cx="16" cy="16" r="15" fill="var(--fg)"/>'
            '<path d="M10 22V12M14 22V10M18 22V13M22 22V11" stroke="var(--accent)" stroke-width="2.4" stroke-linecap="round"/></svg>')
    I_HOME = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 11l8-7 8 7v9h-5v-6H9v6H4z"/></svg>'
    I_SYS = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/></svg>'
    css_tag = f"<style>\n{inline_css}\n</style>" if inline_css else f'<link rel="stylesheet" href="{css_href}">'
    js_tag = f'<script src="{js_href}" defer></script>' if js_href else ""
    others = "".join(f'<link rel="alternate" hreflang="{c.meta("_lang")}" href="{lang_urls[c.meta("_lang")]}">'
                     for c in langs if c.meta("_lang") != cur) if len(langs) > 1 else ""

    return f'''<!doctype html>
<html lang="{cur}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" media="(prefers-color-scheme: light)" content="#fbfaf8">
<meta name="theme-color" media="(prefers-color-scheme: dark)" content="#121210">
<title>{T("doc.title")}</title>
{others}
{css_tag}
</head>
<body>

<!-- TILSTAND — før .app: Hjem/System, omfang (kun huset), tema -->
<input class="state" type="radio" name="mode" id="m-home" checked aria-label="{T("nav.home")}">
<input class="state" type="radio" name="mode" id="m-sys" data-hash="{T("hash.system")}" aria-label="{T("nav.system")}">
<input class="state" type="radio" name="scope" id="s-house" checked aria-label="{T("scope.house")}">
<input class="state" type="checkbox" id="theme" aria-label="{T("theme.toggle")}">

<div class="app">
{LDS_DEFS}
  <div class="navbar-wrap wrap">
    <header class="header">
      <details class="device">
        <summary>{LOGO}<span class="name"><b>Lune Touch</b><small data-bind="device.about.place">{T("device.sample")}</small></span><span class="caret" aria-hidden="true"></span></summary>
        <div class="device-menu">
          <section class="device-about" aria-labelledby="device-about-h">
            <h3 id="device-about-h">{T("device.about")}</h3>
            {kv([(T("device.name"), "device.about.name", "Lune Touch"), (T("device.place"), "device.about.place", T("device.sample")),
                 (T("device.ip"), "device.about.ip"), (T("device.mac"), "device.about.mac"), (T("device.firmware"), "device.about.firmware"),
                 (T("device.esphome"), "device.about.esphome"), (T("device.uptime"), "device.about.uptime")])}
            <button type="button" class="btn" data-action="copy-diag">{T("device.copyDiag")}</button>
          </section>
          <nav aria-label="{T("nav.devices")}" data-bind-devices>
            <a href="/" aria-current="page"><i></i>Lune Touch<small>{T("device.this")}</small></a>
          </nav>
        </div>
      </details>
      <nav class="mode" aria-label="{T("nav.label")}">
        <label for="m-home">{I_HOME}{T("nav.home")}</label>
        <label for="m-sys">{I_SYS}{T("nav.system")}</label>
      </nav>
      <div class="tools">
        {langnav}
        <label class="icon-btn theme-btn" for="theme" title="{T("theme.toggle")}">
          <svg class="i-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5 19 19M5 19l1.5-1.5M17.5 6.5 19 5"/></svg>
          <svg class="i-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/></svg>
          <span class="sr-only">{T("theme.toggle")}</span>
        </label>
      </div>
    </header>
  </div>
  <main class="content wrap">{home}{sys_view}
  </main>
{sheets}
</div>

<script type="application/json" id="i18n">{rt_json}</script>
{js_tag}
</body>
</html>
'''


# ---------------------------------------------------------------- main -----
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--langs", default=os.environ.get("LUNE_UI_LANGS", "en,da"))
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--css", default=str(DS_ROOT / "dist" / "touch" / "lune-ui.css"))
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--hs-type", default="asgard", choices=("http", "asgard"), help="varmekildens type i siden før binderen har data")
    a = ap.parse_args()

    codes = [c.strip() for c in a.langs.split(",") if c.strip()]
    avail = sorted(p.stem for p in (ROOT / "i18n").glob("*.json"))
    for c in codes:
        if c not in avail:
            sys.exit(f"Ukendt sprog '{c}'. Tilgængelige: {', '.join(avail)}")
    base = Cat("en", None) if "en" in avail else None
    cats = [Cat(c, None if c == "en" else base) for c in codes]

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    css = ensure_css(pathlib.Path(a.css))
    (out / "lune-ui.css").write_text(css, encoding="utf-8")
    css_gz = gzip.compress(css.encode(), 9, mtime=0)
    (out / "lune-ui.css.gz").write_bytes(css_gz)

    # lune-forms.js (LDS) først: binder formularer ved indlæsning; binderen lytter på lune:save.
    js = FORMS_JS.rstrip() + "\n" + (ROOT / "binder.js").read_text(encoding="utf-8")
    (out / "ui.js").write_text(js, encoding="utf-8")
    js_gz = gzip.compress(js.encode(), 9, mtime=0)
    (out / "ui.js.gz").write_bytes(js_gz)

    # Firmwaren serverer /lune-ui.css og /ui.js som immutable (1 år). Versionér URL'erne
    # efter indhold, så en firmwareopdatering aldrig kører en cachet gammel binder/CSS.
    css_href = "/lune-ui.css?v=" + hashlib.sha1(css.encode()).hexdigest()[:10]
    js_href = "/ui.js?v=" + hashlib.sha1(js.encode()).hexdigest()[:10]

    urls = {c.meta("_lang"): f"/{c.meta('_lang')}/" for c in cats}
    pages = []
    for c in cats:
        html = render(c, cats, urls, css_href, js_href, a.hs_type)
        d = out / c.meta("_lang")
        d.mkdir(exist_ok=True)
        (d / "index.html").write_text(html, encoding="utf-8")
        gz = gzip.compress(html.encode(), 9, mtime=0)
        (d / "index.html.gz").write_bytes(gz)
        pages.append((c.meta("_lang"), gz))
        if c.missing:
            print(f"ADVARSEL: {c.meta('_lang')} mangler {len(c.missing)} nøgler: {', '.join(sorted(c.missing))}")

    if a.preview:
        for c in cats:
            prev = render(c, cats, urls, None, "ui.js", a.hs_type, inline_css=css)
            prev = prev.replace('<script src="ui.js" defer></script>',
                                '<script>window.LUNE_TOUCH_MOCK=true;</script>\n<script src="ui.js" defer></script>')
            (out / f"preview-{c.meta('_lang')}.html").write_text(prev, encoding="utf-8")

    total = len(css_gz) + len(js_gz) + sum(len(g) for _, g in pages)
    print(f"Byggede {', '.join(codes)} → {out}/  (gzip: {total / 1024:.1f} kB: "
          f"css {len(css_gz) / 1024:.1f} + js {len(js_gz) / 1024:.1f} + "
          + " + ".join(f"{l} {len(g) / 1024:.1f}" for l, g in pages) + ")")
    print(f"  lune-ui.css.gz: {len(css_gz)} B")
    print(f"  ui.js.gz: {len(js_gz)} B")
    for l, g in pages:
        print(f"  {l}/index.html.gz: {len(g)} B")


if __name__ == "__main__":
    main()
