#!/usr/bin/env python3
"""
Lune Touch web-UI — Lune Design System 2 (config/touch.json). Product UI for coordinator.

    python build_ui.py
    python build_ui.py --langs en,da --preview --hs-type asgard

Output (in --out, default ./dist):
    lune-ui.css(.gz)
    ui.js(.gz)
    <lang>/index.html(.gz)
    preview-<lang>.html   (with --preview)
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parent
DS_ROOT = ROOT.parent / "design-system"
CFG = json.loads((DS_ROOT / "config" / "touch.json").read_text(encoding="utf-8"))

# Demo zones: (m, z, name, temp, target, state, opening%, group)
# group: None | "primary" | "member"
ZONES = [
    (1, 1, "Entré", 20.9, 21.0, "idle", 12, None),
    (1, 2, "Køkken", 21.2, 21.0, "idle", 8, None),
    (1, 3, "Spise", 21.4, 21.5, "calling", 52, None),
    (1, 4, "Stue", 21.1, 21.5, "idle", 40, "primary"),
    (1, 5, "Spisekrog", 21.0, 21.5, "calling", 48, "member"),
    (1, 6, "Kontor", 20.6, 21.0, "idle", 10, None),
    (2, 1, "Soveværelse", 19.8, 20.0, "idle", 15, None),
    (2, 2, "Bad", 22.1, 22.0, "idle", 6, None),
    (2, 3, "Gang", 20.2, 20.0, "fault", 0, None),
    (2, 4, "Værelse", 20.5, 21.0, "idle", 11, None),
    (2, 5, "Kontor", 20.7, 21.0, "idle", 9, None),
    (3, 1, "Stue", 20.4, 20.5, "idle", 14, None),
    (3, 2, "Soveværelse", 19.6, 19.5, "idle", 7, None),
    (3, 3, "Bad", 22.0, 22.0, "idle", 5, None),
    (4, 1, "Værksted", 16.8, 18.0, "idle", 20, None),
    (4, 2, "Bil", 8.0, 12.0, "off", 0, None),
]

MANIFOLDS = [
    {
        "n": int(m["id"][1:]),
        "label": m["label"],
        "zones": int(m["zones"]),
        "flow": 33.1 - i * 1.5,
        "return": 29.9 - i * 1.2,
    }
    for i, m in enumerate(CFG["manifolds"])
]


# Price zones and currencies come from the firmware's table (single source).
_PRICE_H = (ROOT.parent.parent / "components" / "lune_touch_coordinator" / "energy_price.h").read_text(encoding="utf-8")
PRICE_ZONES = [
    (m.group(1), m.group(2).lower())
    for m in re.finditer(r'\{"([A-Za-z0-9-]+)", "[0-9A-Z-]{16}", ZoneGroup::([A-Z_]+),', _PRICE_H)
]
PRICE_CURRENCIES = re.findall(r'\{"([A-Z]{3})", ([0-9.]+)f\}', _PRICE_H)
assert len(PRICE_ZONES) >= 30 and PRICE_CURRENCIES, "price tables not found in energy_price.h"


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


def zone_level(z):
    st, op = z[5], z[6]
    if st in ("fault", "off"):
        return 0
    return max(1, min(5, math.ceil(op / 20)))


def zone_by_mz():
    return {(z[0], z[1]): z for z in ZONES}


def manifold_stats(mn, zb):
    n = next(x["zones"] for x in MANIFOLDS if x["n"] == mn)
    zones = [zb[(mn, z)] for z in range(1, n + 1) if (mn, z) in zb]
    calling = sum(1 for z in zones if z[5] == "calling")
    faults = sum(1 for z in zones if z[5] == "fault")
    state = "fault" if faults else ("calling" if calling else ("idle" if zones else "off"))
    return zones, calling, faults, state


def ensure_css(path: pathlib.Path) -> str:
    if path.is_file():
        return path.read_text(encoding="utf-8")
    cfg = DS_ROOT / "config" / "touch.json"
    build = DS_ROOT / "tools" / "lds_build.py"
    print(f"Mangler {path}, kører lds_build …")
    subprocess.run([sys.executable, str(build), str(cfg)], cwd=DS_ROOT, check=True)
    if not path.is_file():
        sys.exit(f"Kunne ikke bygge {path}")
    return path.read_text(encoding="utf-8")


def render(T, langs, lang_urls, css_href, js_href, hs_type, inline_css=None):
    ST = {k: T(f"state.{k}") for k in ("calling", "idle", "fault", "off")}
    zb = zone_by_mz()
    hs_type = hs_type if hs_type in ("http", "asgard") else "asgard"

    def switch(name, t, sub, on):
        sub_html = f"<small>{sub}</small>" if sub else ""
        chk = " checked" if on else ""
        return (
            f'<label class="switch"><span class="switch-text"><b>{t}</b>{sub_html}</span>'
            f'<input type="checkbox" role="switch" name="{name}"{chk}></label>'
        )

    def row(id_, label, control, hint=""):
        h = f'<span class="hint">{hint}</span>' if hint else ""
        return f'<div class="field row"><label for="{id_}">{label}{h}</label>{control}</div>'

    def stepper(name, val, mn, mx, step, unit, label, dec=1):
        return (
            f'<div class="stepper"><button type="button" data-step="-1" aria-label="{T("common.decrease", x=label.lower())}">−</button>'
            f'<span class="value"><input type="number" inputmode="decimal" id="{name}" name="{name}" '
            f'value="{val:.{dec}f}" min="{mn}" max="{mx}" step="{step}"><span class="unit">{unit}</span></span>'
            f'<button type="button" data-step="1" aria-label="{T("common.increase", x=label.lower())}">+</button></div>'
        )

    def unit_cur(html):
        # Units that follow the calculation currency (binder updates them).
        return html.replace('<span class="unit">DKK/kWh</span>', '<span class="unit" data-price-cur>DKK</span>')

    zone_groups = {}
    for zid, group in PRICE_ZONES:
        zone_groups.setdefault(group, []).append(zid)
    price_zone_select = '<select class="select" id="price_zone" name="zone">' + "".join(
        f'<optgroup label="{T("price.group." + g)}">' + "".join(
            f'<option value="{z}"{" selected" if z == "DK1" else ""}>{z}</option>' for z in zs
        ) + "</optgroup>" for g, zs in zone_groups.items()
    ) + "</select>"
    price_currency_select = '<select class="select" id="price_currency" name="currency">' + "".join(
        f'<option value="{c}" data-fx="{fx}"{" selected" if c == "DKK" else ""}>{c}</option>' for c, fx in PRICE_CURRENCIES
    ) + "</select>"

    def help_btn(hid, topic):
        return (
            f'<button class="help-btn" type="button" popovertarget="{hid}" style="anchor-name:--a-{hid}" '
            f'aria-label="{T("help.aria", topic=topic)}">?</button>'
        )

    def help_pop(hid, body_key, more=""):
        link = (
            f'<a href="https://github.com/Birkemosen/lune-coordinator/blob/main/{more}">{T("help.readMore")}</a>'
            if more else ""
        )
        return (
            f'<div id="{hid}" popover class="help-pop" style="position-anchor:--a-{hid}">'
            f"<p>{T(body_key)}</p>{link}</div>"
        )

    def metric(label, val, unit, bind=""):
        b = f' data-bind="{bind}"' if bind else ""
        return f'<div class="metric"><dt>{label}</dt><dd{b}>{val} <small>{unit}</small></dd></div>'

    def sect(key):
        return f'<header class="section-head"><span>{T(key)}</span></header>'

    def ss_id(key):
        return "ss-" + key.replace("/", "-").replace("_", "-")

    def foot_save(key, primary, left=""):
        status = f'<span class="save-status" id="{ss_id(key)}" aria-live="polite"></span>'
        undo = f'<button type="reset" class="btn">{T("common.undo")}</button>'
        btn = f'<button class="btn primary" type="submit">{primary}</button>'
        if left:
            return f'<footer class="panel-foot"><div class="foot-start">{left}{status}</div>{undo}{btn}</footer>'
        return f'<footer class="panel-foot">{status}{undo}{btn}</footer>'

    def port_input(name, val=80):
        return (
            f'<input class="input w-xs" type="number" inputmode="numeric" id="{name}" name="{name}" '
            f'value="{val}" min="1" max="65535" step="1">'
        )

    def section_h(sid, key, summary=False):
        tag = "summary" if summary else "h2"
        hid = "" if summary else f' id="{sid}-h"'
        return (
            f'<{tag} class="section-h"{hid}>{T(key)}'
            f'<i class="section-h-dot" aria-hidden="true"></i>'
            f'<i class="section-h-line" aria-hidden="true"></i></{tag}>'
        )

    def file_input(name, accept):
        return (
            f'<label class="input file" for="{name}">'
            f'<input class="sr-only" id="{name}" name="{name}" type="file" accept="{accept}">'
            f'<span class="file-pick">{T("common.chooseFile")}</span>'
            f'<span class="file-name" data-empty="{T("common.noFile")}">{T("common.noFile")}</span></label>'
        )

    def scope_title_manifold(m):
        mid = f"M{m['n']}"
        return T("scope.title.manifold", id=mid, name=m["label"])

    def scope_title_zone(z):
        mid = f"M{z[0]}"
        zid = f"Z{z[1]}"
        return T("scope.title.zone", id=f"{mid} {zid}", name=z[2])

    house_sub = T(
        "dash.house.sub",
        manifolds=len(MANIFOLDS),
        calling=sum(1 for z in ZONES if z[5] == "calling"),
        faults=sum(1 for z in ZONES if z[5] == "fault"),
    )

    # ---- scope radios (before .app)
    scope_inputs = [
        f'<input class="state" type="radio" name="scope" id="s-house" checked '
        f'data-kind="house" aria-label="{T("scope.house")}" '
        f'data-title="{T("scope.title.house")}" data-sub="{house_sub}">'
    ]
    # Touch shows the house only. The V6 boards appear as rows of System +
    # zone tiles on the dashboard (filled by binder.js); zones are configured
    # on each V6, so there is no manifold/zone scope strip here.
    strip = ""

    def heat_bars():
        cols = []
        for i in range(12):
            plan = 35 + (i * 9) % 55
            act = max(10, plan - 8 + (i % 5))
            cols.append(
                f'<div class="col"><i class="plan" style="--plan:{plan}"></i><i class="act" style="--act:{act}"></i></div>'
            )
        return (
            f'<div class="bars" style="--bars-n:12"><div class="bars-plot">{"".join(cols)}</div>'
            f'<div class="bars-legend"><span><i class="lp"></i>{T("heat.barsLegendPlan")}</span>'
            f'<span><i class="la"></i>{T("heat.barsLegendAct")}</span></div></div>'
        )

    dash_house = f'''
      <section class="view" id="v-dash-house" aria-labelledby="h-dash-house">
        <header class="view-head"><h2 id="h-dash-house" data-bind="scope.title">{T("scope.title.house")}</h2><p data-bind="scope.sub">{house_sub}</p></header>

        <div class="panel alert" hidden>
          <div class="panel-head"><h3>{T("alert.heatConn")}</h3></div>
          <p class="note">{T("alert.heatConnBody")}</p>
          <div class="panel-foot" style="justify-content:flex-start"><label class="btn" for="m-conf">{T("alert.openHeat")}</label></div>
        </div>

        <div class="panel alert" data-bind-alert="board" hidden>
          <div class="panel-head"><h3 data-bind="alert.boardTitle">{T("alert.boardFault", board="V6")}</h3></div>
          <p class="note">{T("alert.boardFaultBody")}</p>
          <div class="panel-foot" style="justify-content:flex-start"><label class="btn" for="m-conf">{T("alert.openControllers")}</label></div>
        </div>

        <div class="boards" data-bind-boards aria-label="{T("boards.label")}"></div>

        <form class="panel c4" data-save="house-target">
          <header class="panel-head"><h3>{T("climate.title")}</h3><span class="badge" data-bind="house.badge">{ST["idle"]}</span></header>
          <div class="climate">
            <div class="now" data-bind="house.temp">{T.num(21.2)}<small>°C</small></div>
            <div class="target">
              <button type="button" data-step="-1" aria-label="{T("common.decrease", x=T("climate.targetAria"))}">−</button>
              <label class="value"><small>{T("climate.target")}</small><input type="number" inputmode="decimal" id="house_target" name="house_target" value="21.0" min="5" max="30" step="0.5"></label>
              <button type="button" data-step="1" aria-label="{T("common.increase", x=T("climate.targetAria"))}">+</button>
            </div>
            <p class="autosave" aria-live="polite"></p>
          </div>
          <dl class="kv">
            <div><dt>{T("climate.coverage")}</dt><dd data-bind="house.coverage">0 / 0</dd></div>
            <div><dt>{T("climate.authority")}</dt><dd data-bind="house.authority">Touch</dd></div>
            <div><dt>{T("climate.outdoor")}</dt><dd data-bind="house.outdoor">{T.num(8.4)} °C</dd></div>
          </dl>
        </form>

        <section class="panel c8" data-hs-type="{hs_type}">
          <header class="panel-head"><h3>{T("heat.title")}</h3><span class="badge" data-bind="heat.badge">{T("status.waiting")}</span></header>
          <div class="subs cols-2">
            <div class="sub">
              <h4 data-bind="dash.hsName">Asgard</h4>
              <dl class="metrics">
                {metric(T("heat.weighted"), "—", "°C", "heat.weighted")}
                {metric(T("heat.setpoint"), "—", "°C", "heat.setpoint")}
              </dl>
              <dl class="kv">
                <div><dt>{T("heat.lastPush")}</dt><dd data-bind="heat.lastPush">—</dd></div>
                <div><dt>{T("dash.targetRole")}</dt><dd data-bind="dash.targetRole">—</dd></div>
                <div><dt>{T("dash.hpTemps")}</dt><dd data-bind="dash.hpTemps">—</dd></div>
              </dl>
              <p class="note muted" data-bind="heat.sentNote" hidden></p>
            </div>
            <div class="sub" data-bind-show="dash.odin" hidden>
              <h4>Odin</h4>
              <dl class="kv">
                <div><dt>{T("dash.odinLink")}</dt><dd data-bind="dash.odinLink">—</dd></div>
                <div><dt>{T("dash.odinDriver")}</dt><dd data-bind="dash.odinDriver">—</dd></div>
                <div><dt>{T("dash.odinNow")}</dt><dd data-bind="dash.odinNow">—</dd></div>
                <div><dt>{T("hs.odinState")}</dt><dd data-bind="dash.odinPlan">—</dd></div>
              </dl>
            </div>
          </div>
        </section>

        <section class="panel" data-panel="flow">
          <header class="panel-head"><h3>{T("flow.title")}</h3><p>{T("flow.sub")}</p></header>
          <div class="subs cols-2">
            <div class="sub">
              <h4>{T("pump.title")}</h4>
              <dl class="metrics">
                {metric(T("pump.flow"), "—", "l/min", "pump.flow")}
                {metric(T("pump.head"), "—", "m", "pump.head")}
                {metric(T("pump.power"), "—", "W", "pump.power")}
              </dl>
              <dl class="kv">
                <div><dt>{T("pump.flowM3h")}</dt><dd data-bind="pump.flowM3h">—</dd></div>
                <div><dt>{T("pump.host")}</dt><dd data-bind="pump.host">—</dd></div>
              </dl>
            </div>
            <div class="sub">
              <h4>{T("flow.dist")}</h4>
              <div class="dist" data-bind-dist><p class="dist-note">—</p></div>
            </div>
          </div>
        </section>

        <section class="panel" data-panel="plan">
          <header class="panel-head"><h3>{T("planG.title")}</h3><p data-bind="planG.sub">{T("planG.sub")}</p></header>
          <p class="empty" data-bind-show="planG.empty">{T("planG.empty")}</p>
          <div class="plan" data-bind-plan aria-label="{T("planG.aria")}"></div>
          <p class="fc-legend" aria-hidden="true">
            <span><i class="lbar"></i>{T("planG.lHeat")}</span>
            <span><i class="ldhw"></i>{T("planG.lDhw")}</span>
            <span><i class="lleg"></i>{T("planG.lLeg")}</span>
            <span><i class="llift"></i>{T("planG.lLift")}</span>
            <span><i class="lpre"></i>{T("planG.lPre")}</span>
            <span><i class="lch"></i>{T("planG.lCharge")}</span>
            <span><i class="lins"></i>{T("planG.lInsufficient")}</span>
          </p>
        </section>

        <section class="panel" data-panel="forecast">
          <header class="panel-head"><h3>{T("fc.title")}</h3><p data-bind="fc.sub">{T("fc.sub", model="—", time="—")}</p><span class="badge info" hidden>{T("fc.badge", v="—")}</span></header>
          <dl class="metrics">
            {metric(T("fc.now"), "—", "°C", "forecast.temp")}
            {metric(T("fc.windMax"), "—", "m/s", "forecast.windmax")}
            {metric(T("fc.tempMin"), "—", "°C", "forecast.tmin")}
          </dl>
          <p class="empty">{T("fc.empty")}</p>
          <!-- Weather icons and wind arrow, referenced by binder.js (<use href="#i-…">). -->
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
            <span><i class="lt"></i>{T("fc.lTemp")}</span>
            <span><i class="lsun"></i>{T("fc.lSun")}</span>
            <span><i class="lw"></i>{T("fc.lWind")}</span>
            <span><svg class="dir" viewBox="0 0 24 24"><use href="#i-arrow"/></svg>{T("fc.lDir")}</span>
            <span><i class="lpre"></i>{T("fc.lPre")}</span>
          </p>
        </section>

      </section>'''

    http_checked = " checked" if hs_type == "http" else ""
    asgard_checked = " checked" if hs_type == "asgard" else ""

    conf_house = f'''
      <section class="view" id="v-conf-house" aria-labelledby="h-conf-house">
        <header class="view-head"><h2 id="h-conf-house" data-bind="scope.title">{T("scope.title.house")}</h2><p data-bind="scope.sub">{house_sub}</p></header>
        <nav class="section-nav" aria-label="{T("sect.nav")}">
          <a href="#sec-setup">{T("sect.setup")}</a>
          <a href="#sec-heat">{T("sect.heat")}</a>
          <a href="#sec-device">{T("sect.device")}</a>
          <a href="#sec-service">{T("sect.service")}</a>
        </nav>
        <section class="section" id="sec-setup" aria-labelledby="sec-setup-h">
          {section_h("sec-setup", "sect.setup")}
          <div class="section-grid">
        <form class="panel" data-save="add-node">
          <header class="panel-head"><h3>{T("ctrl.title")}</h3><p>{T("ctrl.sub")}</p>{help_btn("help-ctrl", T("ctrl.title"))}</header>
          {help_pop("help-ctrl", "help.controllers", "docs/Manual.md#controllers")}
          <div class="table-wrap">
            <table class="table" data-bind-nodes>
              <thead><tr><th>{T("ctrl.name")}</th><th>{T("ctrl.host")}</th><th>{T("heat.status")}</th><th>{T("ctrl.zones")}</th><th></th></tr></thead>
              <tbody></tbody>
            </table>
          </div>
          <div data-bind-scan></div>
          <details class="more">
            <summary>{T("ctrl.addManual")}</summary>
            <div class="field"><label for="node_name">{T("ctrl.name")}</label><input class="input" id="node_name" name="name" placeholder="Ground floor"></div>
            <div class="field"><label for="node_host">{T("ctrl.host")}</label><input class="input" id="node_host" name="host" placeholder="lune-v6.local" inputmode="url"></div>
          </details>
          {foot_save("add-node", T("common.add"), left='<button class="btn" type="button" data-action="scan-nodes">' + T("ctrl.scan") + '</button>')}
        </form>
          </div>
        </section>

        <section class="section" id="sec-heat" aria-labelledby="sec-heat-h">
          {section_h("sec-heat", "sect.heat")}
          <div class="section-grid">
        <form class="panel wide gated" data-save="heat-source">
          <header class="panel-head"><h3>{T("hs.title")}</h3>{help_btn("help-hs", T("hs.title"))}</header>
          {help_pop("help-hs", "help.heatSource", "docs/Manual.md#heat-source")}
          {switch("enabled", T("hs.enabled"), "", True)}
          <div class="gated-body">
            <input class="state" type="radio" name="hs_type" id="hs-http" value="http"{http_checked}>
            <input class="state" type="radio" name="hs_type" id="hs-asgard" value="asgard"{asgard_checked}>
            <div class="seg" role="radiogroup" aria-label="{T("hs.type")}">
              <label for="hs-http"><span>{T("hs.typeHttp")}</span></label>
              <label for="hs-asgard"><span>{T("hs.typeAsgard")}</span></label>
            </div>
            <fieldset class="hs-fields typed-fields subs cols-2" data-type="http">
              <div class="sub">
                <h4>{T("hs.subConnection")}</h4>
                {row("http_host", T("hs.host"), '<input class="input w-md" id="http_host" name="http_host" value="heat-bridge.local">')}
                {row("http_port", T("hs.port"), port_input("http_port"))}
                {row("http_push_interval_s", T("hs.pushInterval"), stepper("http_push_interval_s", 60, 5, 3600, 5, "s", T("hs.pushInterval"), dec=0))}
              </div>
              <div class="sub">
                <h4>{T("hs.subMapping")}</h4>
                {row("http_weighted_temperature_variable", T("hs.weightedVar"), '<input class="input w-md" id="http_weighted_temperature_variable" name="http_weighted_temperature_variable" value="temperature_feedback_z1">')}
                {row("write_url_template", T("hs.writeUrl"), '<input class="input w-lg" id="write_url_template" name="write_url_template" placeholder="http://{host}:{port}/{entity}/set?value={value}">')}
                {row("read_url_template", T("hs.readUrl"), '<input class="input w-lg" id="read_url_template" name="read_url_template" placeholder="http://{host}:{port}/{entity}">')}
              </div>
              <div class="sub">
                <h4>{T("hs.subLevers")}</h4>
                <p class="hint">{T("hs.leversHint")}</p>
                {row("target_url_template", T("hs.leverTarget"), '<input class="input w-lg" id="target_url_template" name="target_url_template" placeholder="http://{host}:{port}/target?value={value}">')}
                {row("heat_request_url_template", T("hs.leverRequest"), '<input class="input w-lg" id="heat_request_url_template" name="heat_request_url_template" placeholder="http://{host}:{port}/heat?value={value}">')}
                {row("curve_offset_url_template", T("hs.leverCurve"), '<input class="input w-lg" id="curve_offset_url_template" name="curve_offset_url_template" placeholder="http://{host}:{port}/curve_offset?value={value}">')}
                {row("curve_gain", T("hs.curveGain"), stepper("curve_gain", 2.0, 0.0, 10.0, 0.5, "°C/°C", T("hs.curveGain")))}
                {row("curve_max_offset_c", T("hs.curveMax"), stepper("curve_max_offset_c", 5.0, 0.0, 15.0, 0.5, "°C", T("hs.curveMax")))}
                <dl class="kv"><div><dt>{T("hs.route")}</dt><dd data-bind="levers.state">—</dd></div></dl>
              </div>
              <div class="sub" data-bind-hs-delivery="http">
                <h4>{T("hs.sentHttp")}</h4>
                <p class="mono" data-bind="hs.sent.url">—</p>
                <div class="table-wrap"><table class="table">
                  <thead><tr><th></th><th>{T("hs.sentValue")}</th><th>{T("hs.sentTarget")}</th><th>{T("hs.sentWhen")}</th></tr></thead>
                  <tbody>
                    <tr><th>{T("hs.rowTemp")}</th><td class="num" data-bind="hs.http.temp">—</td><td class="mono" data-bind="hs.http.entity">—</td><td data-bind="hs.http.when">—</td></tr>
                  </tbody>
                </table></div>
                <details class="more">
                  <summary>{T("hs.howCalc")}</summary>
                  <div class="table-wrap"><table class="table">
                    <thead><tr><th>{T("hs.calcName")}</th><th>{T("hs.calcTemp")}</th><th>{T("hs.calcWeight")}</th><th>{T("hs.calcContrib")}</th></tr></thead>
                    <tbody data-bind-weight-rows></tbody>
                  </table></div>
                </details>
              </div>
              <div class="sub">
                <h4>{T("hs.subTest")}</h4>
                <div class="actions">
                  <button class="btn" type="button" data-action="hs-test-read">{T("hs.testRead")}</button>
                  <button class="btn" type="button" data-action="hs-test-push">{T("hs.testPush")}</button>
                </div>
                <div class="test-result" aria-live="polite"></div>
              </div>
            </fieldset>
            <fieldset class="hs-fields typed-fields subs cols-2" data-type="asgard">
              <div class="sub">
                <h4>{T("hs.subConnection")}</h4>
                {row("asgard_host", T("hs.host"), '<input class="input w-md" id="asgard_host" name="asgard_host" value="asgard.local">')}
                {row("asgard_port", T("hs.port"), port_input("asgard_port"))}
                {row("asgard_push_interval_s", T("hs.pushInterval"), stepper("asgard_push_interval_s", 60, 5, 3600, 5, "s", T("hs.pushInterval"), dec=0))}
              </div>
              <div class="sub">
                <h4>{T("hs.subHouseTemp")}</h4>
                <p class="hint">{T("hs.houseTempHint")}</p>
                {row("asgard_weighted_temperature_variable", T("hs.asgardTempEntity"), '<input class="input w-md" id="asgard_weighted_temperature_variable" name="asgard_weighted_temperature_variable" value="Virtual Thermostat Input z1">')}
                <dl class="kv">
                  <div><dt>{T("hs.sentValue")}</dt><dd data-bind="hs.asgard.temp">—</dd></div>
                  <div><dt>{T("hs.sentWhen")}</dt><dd data-bind="hs.asgard.when">—</dd></div>
                </dl>
                <details class="more">
                  <summary>{T("hs.howCalc")}</summary>
                  <div class="table-wrap"><table class="table">
                    <thead><tr><th>{T("hs.calcName")}</th><th>{T("hs.calcTemp")}</th><th>{T("hs.calcWeight")}</th><th>{T("hs.calcContrib")}</th></tr></thead>
                    <tbody data-bind-weight-rows></tbody>
                  </table></div>
                </details>
              </div>
              <div class="sub">
                <h4>{T("hs.subTarget")}</h4>
                {switch("target_sync_enabled", T("hs.targetSync"), T("hs.targetSyncHint"), True)}
                {row("climate_entity", T("hs.asgardClimate"), '<input class="input w-md" id="climate_entity" name="climate_entity" value="Virtual Thermostat z1">')}
                <dl class="kv">
                  <div><dt>{T("hs.sentValue")}</dt><dd data-bind="hs.asgard.setpoint">—</dd></div>
                  <div><dt>{T("hs.sentWhen")}</dt><dd data-bind="hs.asgard.setpointWhen">—</dd></div>
                  <div><dt>{T("dash.targetRole")}</dt><dd data-bind="dash.targetRole">—</dd></div>
                </dl>
              </div>
              <div class="sub">
                <h4>{T("hs.subTest")}</h4>
                <div class="actions">
                  <button class="btn" type="button" data-action="hs-test-read">{T("hs.testRead")}</button>
                  <button class="btn" type="button" data-action="hs-test-push">{T("hs.testPush")}</button>
                </div>
                <div class="test-result" aria-live="polite"></div>
              </div>
              <div class="sub">
                <h4>{T("hs.subOdin")}</h4>
                {row("odin_host", T("hs.odinHost"), '<input class="input w-md" id="odin_host" name="odin_host" placeholder="192.168.1.20">')}
                {switch("odin_plan_enabled", T("hs.odinPlan"), T("hs.odinPlanHint"), True)}
                {switch("odin_control_enabled", T("hs.odinControl"), T("hs.odinControlHint"), False)}
                {row("odin_max_lift_c", T("hs.odinMaxLift"), stepper("odin_max_lift_c", 1.5, 0.3, 3.0, 0.1, "°C", T("hs.odinMaxLift")))}
              </div>
              <div class="sub">
                <h4>{T("hs.odinStatus")}</h4>
                <dl class="kv">
                  <div><dt>{T("hs.odinLink")}</dt><dd data-bind="odin.link">—</dd></div>
                  <div><dt>{T("hs.odinState")}</dt><dd data-bind="odin.state">—</dd></div>
                  <div><dt>{T("hs.route")}</dt><dd data-bind="odin.route">—</dd></div>
                </dl>
                <details class="more">
                  <summary>{T("hs.mqtt")}</summary>
                  <p class="hint">{T("hs.mqttHint")}</p>
                  {switch("mqtt_enabled", T("hs.mqttEnabled"), "", False)}
                  {row("mqtt_host", T("hs.host"), '<input class="input w-md" id="mqtt_host" name="mqtt_host" placeholder="192.168.1.10">')}
                  {row("mqtt_port", T("hs.port"), '<input class="input w-xs" type="number" inputmode="numeric" id="mqtt_port" name="mqtt_port" value="1883" min="1" max="65535">')}
                  {row("mqtt_username", T("hs.mqttUser"), '<input class="input w-md" id="mqtt_username" name="mqtt_username" autocomplete="off">')}
                  {row("mqtt_password", T("hs.mqttPassword"), '<input class="input w-md" type="password" id="mqtt_password" name="mqtt_password" autocomplete="new-password">')}
                  {row("mqtt_topic_prefix", T("hs.mqttPrefix"), '<input class="input w-md" id="mqtt_topic_prefix" name="mqtt_topic_prefix" placeholder="hp/hp1">')}
                  {row("mqtt_hp_id", T("hs.mqttHpId"), '<input class="input w-xs" id="mqtt_hp_id" name="mqtt_hp_id" placeholder="hp1">')}
                  <p class="hint" data-bind="hs.mqttStatus" aria-live="polite"></p>
                </details>
              </div>
            </fieldset>
          </div>
          {foot_save("heat-source", T("hs.save"))}
        </form>

        <form class="panel wide gated" data-save="prices">
          <header class="panel-head"><h3>{T("price.title")}</h3><span class="badge" data-bind="price.badge">{T("price.state.disabled")}</span>{help_btn("help-price", T("price.title"))}</header>
          {help_pop("help-price", "help.price", "docs/Manual.md#electricity-price-to-odin")}
          {switch("enabled", T("price.enabled"), T("price.enabledHint"), False)}
          <div class="gated-body"><div class="subs">
            <input class="state" type="radio" name="model" id="pm-odin" value="odin">
            <input class="state" type="radio" name="model" id="pm-touch" value="touch" checked>
            <div class="seg" role="radiogroup" aria-label="{T("price.model")}" style="justify-self:start">
              <label for="pm-odin"><span>{T("price.modelOdin")}</span></label>
              <label for="pm-touch"><span>{T("price.modelTouch")}</span></label>
            </div>
            <div class="subs cols-2">
              <div class="sub">
                <h4>{T("price.subZone")}</h4>
                {row("price_zone", T("price.zone"), price_zone_select)}
                {row("price_token", T("price.token"), '<input class="input w-md" type="password" id="price_token" name="entsoe_token" autocomplete="new-password" spellcheck="false" maxlength="63">')}
                <p class="hint">{T("price.tokenHint")}</p>
              </div>
            </div>
            <fieldset class="typed-fields subs cols-2" data-type="odin">
              <div class="sub">
                <h4>{T("price.subOdinOwn")}</h4>
                <input class="state" type="radio" name="odin_mode" id="om-dynamic" value="dynamic" checked>
                <input class="state" type="radio" name="odin_mode" id="om-fixed" value="fixed">
                <div class="seg" role="radiogroup" aria-label="{T("price.odinModeLabel")}">
                  <label for="om-dynamic"><span>{T("price.odinDynamic")}</span></label>
                  <label for="om-fixed"><span>{T("price.odinFixed")}</span></label>
                </div>
                <fieldset class="typed-fields" data-type="dynamic">
                  <div class="seg" role="radiogroup" aria-label="{T("price.odinSourceLabel")}">
                    <label><input type="radio" name="odin_source" value="energy_charts" checked><span>Energy-Charts</span></label>
                    <label><input type="radio" name="odin_source" value="entsoe"><span>ENTSO-E</span></label>
                  </div>
                  <p class="hint">{T("price.odinDynamicHint")}</p>
                </fieldset>
                <fieldset class="typed-fields" data-type="fixed">
                  {row("odin_fixed_price", T("price.odinFixedPrice"), stepper("odin_fixed_price", 0.25, 0, 5, 0.01, "€/kWh", T("price.odinFixedPrice"), dec=3))}
                </fieldset>
                <p class="hint" data-bind="price.dkNote">{T("price.odinDkNote")}</p>
              </div>
              <div class="sub">
                <h4>{T("price.subOdinNow")}</h4>
                <dl class="kv">
                  <div><dt>{T("price.odinNowMode")}</dt><dd data-bind="price.odin.mode">—</dd></div>
                  <div><dt>{T("price.odinNowZone")}</dt><dd data-bind="price.odin.zone">—</dd></div>
                  <div><dt>{T("price.odinNowFixed")}</dt><dd data-bind="price.odin.fixed">—</dd></div>
                  <div><dt>{T("price.token")}</dt><dd data-bind="price.odin.token">—</dd></div>
                </dl>
                <div class="actions"><button class="btn" type="button" data-action="price-odin-write">{T("price.odinWrite")}</button></div>
                <div class="test-result" data-bind-price-odin-result aria-live="polite"></div>
              </div>
            </fieldset>
            <fieldset class="typed-fields subs cols-2" data-type="touch">
              <div class="sub">
                <h4>{T("price.subSpot")}</h4>
                <input class="state" type="radio" name="spot_source" id="ss-eds" value="eds" checked>
                <input class="state" type="radio" name="spot_source" id="ss-energy_charts" value="energy_charts">
                <input class="state" type="radio" name="spot_source" id="ss-entsoe" value="entsoe">
                <input class="state" type="radio" name="spot_source" id="ss-fixed" value="fixed">
                <div class="seg" role="radiogroup" aria-label="{T("price.spotSource")}">
                  <label for="ss-eds" data-dk-only><span>{T("price.srcEds")}</span></label>
                  <label for="ss-energy_charts"><span>Energy-Charts</span></label>
                  <label for="ss-entsoe"><span>ENTSO-E</span></label>
                  <label for="ss-fixed"><span>{T("price.srcFixed")}</span></label>
                </div>
                <fieldset class="typed-fields" data-type="eds"><p class="hint">{T("price.spotEdsHint")}</p></fieldset>
                <fieldset class="typed-fields" data-type="energy_charts"><p class="hint">{T("price.spotEcHint")}</p></fieldset>
                <fieldset class="typed-fields" data-type="entsoe"><p class="hint">{T("price.spotEntsoeHint")}</p></fieldset>
                <fieldset class="typed-fields" data-type="fixed">
                  {row("spot_fixed_eur", T("price.spotFixed"), stepper("spot_fixed_eur", 0.10, -1, 5, 0.01, "€/kWh", T("price.spotFixed"), dec=3))}
                </fieldset>
              </div>
              <div class="sub">
                <h4>{T("price.subTaxes")}</h4>
                {row("price_currency", T("price.currency"), price_currency_select)}
                {row("fx", T("price.fx"), '<input class="input w-sm" type="number" inputmode="decimal" id="fx" name="fx" value="7.46" min="0.01" max="10000" step="any">', T("price.fxHint"))}
                {row("energy_tax", T("price.energyTax"), unit_cur(stepper("energy_tax", 0.008, 0, 500, 0.001, "DKK/kWh", T("price.energyTax"), dec=3)))}
                {row("markup", T("price.markup"), unit_cur(stepper("markup", 0.0, -100, 500, 0.01, "DKK/kWh", T("price.markup"), dec=3)))}
                {row("vat_pct", T("price.vat"), stepper("vat_pct", 25, 0, 50, 0.5, "%", T("price.vat"), dec=1))}
                <div class="actions"><button class="btn" type="button" data-action="price-zone-defaults">{T("price.applyDefaults")}</button></div>
                <p class="hint">{T("price.perKwhHint")}</p>
                <p class="hint" data-bind="price.defaultsNote" aria-live="polite">{T("price.applyDefaultsHint")}</p>
              </div>
              <div class="sub">
                <h4>{T("price.subGrid")}</h4>
                <input class="state" type="radio" name="grid_source" id="gt-datahub" value="datahub" checked>
                <input class="state" type="radio" name="grid_source" id="gt-schedule" value="schedule">
                <input class="state" type="radio" name="grid_source" id="gt-none" value="none">
                <div class="seg" role="radiogroup" aria-label="{T("price.gridSource")}">
                  <label for="gt-datahub" data-dk-only><span>{T("price.srcDatahub")}</span></label>
                  <label for="gt-schedule"><span>{T("price.srcSchedule")}</span></label>
                  <label for="gt-none"><span>{T("price.srcNone")}</span></label>
                </div>
                <fieldset class="typed-fields" data-type="datahub">
                  {row("price_gln", T("price.gln"), '<input class="input w-md" id="price_gln" name="grid_gln" value="5790000610976" inputmode="numeric" maxlength="13" spellcheck="false">')}
                  {row("price_code", T("price.code"), '<input class="input w-md" id="price_code" name="grid_code" value="TNT1009" maxlength="23" spellcheck="false">')}
                  <p class="hint">{T("price.gridDatahubHint")}</p>
                </fieldset>
                <fieldset class="typed-fields" data-type="schedule">
                  <div class="table-wrap"><table class="table">
                    <thead><tr><th>{T("price.schedFrom")}</th><th class="num" data-price-unit>DKK/kWh</th><th></th></tr></thead>
                    <tbody data-bind-price-sched></tbody>
                  </table></div>
                  <input type="hidden" name="grid_schedule" value='[{{"h":0,"v":0.077}},{{"h":6,"v":0.231}},{{"h":17,"v":0.692}},{{"h":21,"v":0.231}}]'>
                  <p class="hint">{T("price.schedHint")}</p>
                  <div class="actions"><button class="btn" type="button" data-action="price-sched-add">{T("price.schedAdd")}</button></div>
                </fieldset>
                <fieldset class="typed-fields" data-type="none">
                  <p class="hint">{T("price.gridNoneHint")}</p>
                </fieldset>
              </div>
              <div class="sub">
                <h4>{T("price.subSystem")}</h4>
                <input class="state" type="radio" name="system_source" id="en-datahub" value="datahub" checked>
                <input class="state" type="radio" name="system_source" id="en-fixed" value="fixed">
                <div class="seg" role="radiogroup" aria-label="{T("price.systemSource")}">
                  <label for="en-datahub" data-dk-only><span>{T("price.srcDatahub")}</span></label>
                  <label for="en-fixed"><span>{T("price.srcFixed")}</span></label>
                </div>
                <fieldset class="typed-fields" data-type="datahub">
                  <p class="hint">{T("price.systemDatahubHint")}</p>
                </fieldset>
                <fieldset class="typed-fields" data-type="fixed">
                  {row("system_fixed", T("price.systemFixed"), unit_cur(stepper("system_fixed", 0.115, -100, 500, 0.001, "DKK/kWh", T("price.systemFixed"), dec=3)))}
                  <p class="hint">{T("price.systemFixedHint")}</p>
                </fieldset>
              </div>
              <div class="sub">
                <h4>{T("price.subStatus")}</h4>
                <dl class="kv">
                  <div><dt>{T("price.lastPush")}</dt><dd data-bind="price.lastPush">—</dd></div>
                  <div><dt>{T("price.hours")}</dt><dd data-bind="price.hours">—</dd></div>
                  <div><dt>{T("price.spotUsed")}</dt><dd data-bind="price.spotUsed">—</dd></div>
                  <div><dt>{T("price.odinMode")}</dt><dd data-bind="price.odinMode">—</dd></div>
                  <div><dt>{T("price.problem")}</dt><dd data-bind="price.problem">—</dd></div>
                </dl>
                <p class="hint" data-bind="price.note"></p>
                <div class="actions"><button class="btn" type="button" data-action="price-push">{T("price.pushNow")}</button></div>
                <div class="test-result" data-bind-price-result aria-live="polite"></div>
              </div>
              <div class="sub" data-bind-price-today>
                <h4>{T("price.subToday")}</h4>
                <div class="bars" style="--bars-n:24"><div class="bars-plot" data-bind-price-bars></div>
                  <div class="axis" aria-hidden="true"><span>00</span><span>06</span><span>12</span><span>18</span><span>24</span></div></div>
                <p class="hint" data-bind="price.inclAll">{T("price.inclAll", cur="DKK")}</p>
                <dl class="kv">
                  <div><dt>{T("price.now")}</dt><dd data-bind="price.now">—</dd></div>
                  <div><dt>{T("price.cheapest")}</dt><dd data-bind="price.min">—</dd></div>
                  <div><dt>{T("price.dearest")}</dt><dd data-bind="price.max">—</dd></div>
                  <div><dt>{T("price.peakAvg")}</dt><dd data-bind="price.peak">—</dd></div>
                </dl>
              </div>
            </fieldset>
          </div></div>
          {foot_save("prices", T("price.save"))}
        </form>

        <form class="panel wide" data-save="rooms">
          <header class="panel-head"><h3>{T("rooms.title")}</h3><p>{T("rooms.sub")}</p>{help_btn("help-rooms", T("rooms.title"))}</header>
          {help_pop("help-rooms", "help.rooms", "docs/Manual.md#rooms")}
          <div class="table-wrap"><table class="table">
            <thead><tr><th>{T("rooms.name")}</th><th>{T("rooms.include")}</th><th class="num">{T("rooms.weight")}</th><th class="num">{T("rooms.wind")}</th><th class="num">{T("rooms.solar")}</th></tr></thead>
            <tbody data-bind-room-rows><tr><td colspan="5" class="muted">—</td></tr></tbody>
          </table></div>
          {foot_save("rooms", T("rooms.save"))}
        </form>

        <form class="panel c5" data-save="circulation">
          <header class="panel-head"><h3>{T("pumpCfg.title")}</h3>{help_btn("help-pump", T("pumpCfg.title"))}</header>
          {help_pop("help-pump", "help.pump", "docs/Manual.md#pump")}
          {row("pump_host", T("pumpCfg.host"), '<input class="input w-md" id="pump_host" name="host" value="" placeholder="alpha2go.local">')}
          {row("pump_port", T("pumpCfg.port"), port_input("pump_port"))}
          {row("pump_flow_entity", T("pumpCfg.flowEntity"), '<input class="input w-md" id="pump_flow_entity" name="flow_entity" value="pump_flow">')}
          {row("pump_head_entity", T("pumpCfg.headEntity"), '<input class="input w-md" id="pump_head_entity" name="head_entity" value="pump_head_pressure">')}
          {row("pump_power_entity", T("pumpCfg.powerEntity"), '<input class="input w-md" id="pump_power_entity" name="power_entity" value="pump_power">')}
          {foot_save("circulation", T("pumpCfg.save"))}
        </form>

        <form class="panel c5" data-save="weather">
          <header class="panel-head"><h3>{T("weatherCfg.title")}</h3>{help_btn("help-weather", T("weatherCfg.title"))}</header>
          {help_pop("help-weather", "help.weather", "docs/Manual.md#weather")}
          <div class="pair">
            {row("wx_lat", T("weatherCfg.lat"), '<input class="input w-sm" id="wx_lat" name="latitude" value="55.6761" inputmode="decimal">')}
            {row("wx_lon", T("weatherCfg.lon"), '<input class="input w-sm" id="wx_lon" name="longitude" value="12.5683" inputmode="decimal">')}
          </div>
          {row("wx_boost", T("weatherCfg.boost"), stepper("wx_boost", 1.5, 0, 3, 0.1, "°C", T("weatherCfg.boost")))}
          {foot_save("weather", T("weatherCfg.save"), left='<button class="btn" type="button" data-action="wx-geo">' + T("weatherCfg.geo") + '</button>')}
        </form>
          </div>
        </section>

        <section class="section" id="sec-device" aria-labelledby="sec-device-h">
          {section_h("sec-device", "sect.device")}
          <div class="section-grid">
        <form class="panel c4" data-save="settings">
          <header class="panel-head"><h3>{T("id.title")}</h3>{help_btn("help-identity", T("id.title"))}</header>
          {help_pop("help-identity", "help.identity", "docs/Manual.md#identity")}
          {row("dev_name", T("id.name"), f'<input class="input w-md" id="dev_name" name="name" value="{T("device.sample")}">')}
          {row("dev_idle", T("id.idle"), stepper("dev_idle", 5, 0, 120, 1, "min", T("id.idle"), dec=0))}
          {foot_save("settings", T("id.save"))}
        </form>

        <form class="panel c4" data-save="wifi">
          <header class="panel-head"><h3>{T("wifi.title")}</h3>{help_btn("help-wifi", T("wifi.title"))}</header>
          {help_pop("help-wifi", "help.wifi", "docs/Manual.md#wifi")}
          <dl class="kv">
            <div><dt>{T("wifi.current")}</dt><dd data-bind="wifi.current">—</dd></div>
            <div><dt>{T("wifi.status")}</dt><dd data-bind="wifi.status">—</dd></div>
          </dl>
          {row("wifi_ssid", T("wifi.ssid"), '<input class="input w-md" id="wifi_ssid" name="ssid" maxlength="32" autocomplete="off" spellcheck="false">')}
          {row("wifi_password", T("wifi.password"), '<input class="input w-md" type="password" id="wifi_password" name="password" maxlength="64" autocomplete="new-password">')}
          <p class="hint">{T("wifi.hint")}</p>
          {foot_save("wifi", T("wifi.save"))}
        </form>

        <form class="panel c4" data-save="firmware">
          <header class="panel-head"><h3>{T("csys.firmware")}</h3>{help_btn("help-firmware", T("csys.firmware"))}</header>
          {help_pop("help-firmware", "help.firmware", "docs/Manual.md#firmware")}
          <dl class="kv">
            <div><dt>{T("csys.fwInstalled")}</dt><dd data-bind="fw.installed">—</dd></div>
            <div><dt>{T("csys.fwLatest")}</dt><dd data-bind="fw.latest">—</dd></div>
          </dl>
          <div class="actions">
            <button class="btn" type="submit" name="action" value="check">{T("csys.fwCheck")}</button>
            <button class="btn" type="submit" name="action" value="install" disabled>{T("csys.fwInstall")}</button>
          </div>
          <div class="field"><label for="ota_file">{T("csys.fwUpload")}</label>{file_input("ota_file", ".bin,.ota.bin")}</div>
          <footer class="panel-foot"><button class="btn primary" type="submit" name="action" value="upload" disabled>{T("csys.fwUploadBtn")}</button></footer>
        </form>

        <form class="panel c4" data-save="backup">
          <header class="panel-head"><h3>{T("csys.backup")}</h3>{help_btn("help-backup", T("csys.backup"))}</header>
          {help_pop("help-backup", "help.backup", "docs/Manual.md#backup")}
          <p class="note">{T("csys.backupNote")}</p>
          <div class="field"><label for="backup_file">{T("csys.backupImport")}</label>{file_input("backup_file", "application/json,.json")}</div>
          <footer class="panel-foot">
            <button class="btn" type="submit" name="action" value="export">{T("csys.backupExport")}</button>
            <button class="btn primary" type="submit" name="action" value="import" disabled>{T("csys.backupImportBtn")}</button>
          </footer>
        </form>
          </div>
        </section>

        <details class="section" id="sec-service">
          {section_h("sec-service", "sect.service", summary=True)}
          <div class="section-grid">
        <section class="panel">
          <header class="panel-head"><h3>{T("svc.title")}</h3>{help_btn("help-service", T("svc.title"))}</header>
          {help_pop("help-service", "help.service", "docs/Manual.md#service")}
          <div class="sub">
            <h4>{T("svc.commands")}</h4>
            <pre class="log" data-bind="log" aria-live="polite" lang="en">—</pre>
          </div>
          <div class="sub">
            <h4>{T("svc.diag")}</h4>
            <dl class="kv">
              <div><dt>{T("diag.nodes")}</dt><dd data-bind="diag.nodes">—</dd></div>
              <div><dt>{T("diag.poll")}</dt><dd data-bind="diag.poll">—</dd></div>
              <div><dt>{T("diag.ota")}</dt><dd data-bind="diag.ota">—</dd></div>
            </dl>
          </div>
          <div class="actions">
            <button class="btn danger" type="button" popovertarget="confirm-reset" style="anchor-name:--a-confirm-reset">{T("common.reset")}…</button>
          </div>
          <div id="confirm-reset" popover class="confirm-pop" role="alertdialog" aria-labelledby="confirm-reset-t" aria-describedby="confirm-reset-d" style="position-anchor:--a-confirm-reset">
            <p class="confirm-title" id="confirm-reset-t">{T("id.resetConfirm")}</p>
            <p id="confirm-reset-d">{T("id.resetNote")}</p>
            <div class="confirm-actions">
              <button class="btn" type="button" popovertarget="confirm-reset" popovertargetaction="hide" autofocus>{T("common.cancel")}</button>
              <button class="btn danger-solid" type="button" data-action="reset-registry" popovertarget="confirm-reset" popovertargetaction="hide">{T("common.reset")}</button>
            </div>
          </div>
        </section>
          </div>
        </details>
      </section>'''

    views = dash_house + conf_house

    cur = T.meta("_lang")
    if len(langs) > 1:
        links = "".join(
            f'<a href="{lang_urls[c.meta("_lang")]}" hreflang="{c.meta("_lang")}" lang="{c.meta("_lang")}" '
            f'title="{c.meta("_name")}"{" aria-current=\"true\"" if c.meta("_lang") == cur else ""}>'
            f'{c.meta("_short")}</a>'
            for c in langs
        )
        langnav = f'<nav class="lang" aria-label="{T("lang.label")}">{links}</nav>'
    else:
        langnav = ""

    rt = {k: T(k) for k in (
        "rt.savedOk", "rt.saveFailed", "rt.saving", "rt.unsaved.one", "rt.unsaved.other",
        "rt.nothingToSave", "rt.leaveUnsaved", "rt.autoSaving", "rt.autoSaved", "rt.autoFailed", "rt.retry",
        "rt.secondsAgo", "rt.minutesAgo", "rt.offline", "common.undo",
        "state.calling", "state.idle", "state.fault", "state.off",
        "tile.fault", "tile.off", "badge.ok", "badge.warn", "badge.bad", "badge.calling",
        "common.on", "common.off", "common.approve", "common.remove", "common.days",
        "common.faults", "common.noFaults",
        "device.this", "device.copied", "device.copyDiag",
        "dash.house.sub", "dash.manifold.sub", "dash.zone.sub", "fc.sub",
        "alert.boardFault", "ctrl.removeTitle", "ctrl.removeAsk", "ctrl.removeConfirm",
        "ctrl.removeBtn", "ctrl.removeDo", "ctrl.editName", "ctrl.found", "ctrl.addRow",
        "ctrl.empty", "ctrl.host", "common.unnamed", "common.cancel",
        "tile.manifold.live",
        "heat.badge.ok", "heat.badge.bad", "heat.badge.off",
        "hs.typeHttp", "hs.typeAsgard", "hs.notSent",
        "hs.calcWeightArea", "hs.calcWeightUa", "hs.testing",
        "hs.testReadOk", "hs.testPushOk", "hs.testReadBody", "hs.testPushBody",
        "hs.testFailLine", "hs.testCheckHost", "hs.testCheckDns", "hs.testCheckHttp",
        "hs.testStatus.failed",
        "hs.testReason.timeout", "hs.testReason.http", "hs.testReason.dns",
        "hs.testReason.missing", "hs.testReason.network", "hs.testReason.endpoint",
        "hs.testReason.notReady", "hs.testReason.busy", "hs.testReason.rejected",
        "status.trusted", "status.paired", "status.unpaired", "status.waiting",
        "status.unreachable", "status.confirmed", "status.sent", "status.mismatch",
        "status.blocked", "status.unknown", "status.disabled",
        "scope.title.house", "scope.title.manifold", "scope.title.zone",
        "house.below", "house.at", "house.above", "dash.house.sub.one",
        "fc.badge", "fc.windFrom", "fc.dirTitle", "fc.sky.sun", "fc.sky.partly", "fc.sky.cloud",
        "fc.sky.rain", "fc.sky.snow", "fc.sky.moon", "diag.pollOk", "diag.pollFail",
        "rooms.includeAria", "rooms.weightAria", "rooms.windAria", "rooms.solarAria",
        "m.supplyShort", "m.returnShort", "tile.fault", "tile.off", "strip.sub",
        "diag.ota.valid", "diag.ota.pending_verify", "diag.ota.new", "diag.ota.invalid",
        "diag.ota.aborted", "diag.ota.undefined",
        "hs.notSentLease", "hs.route.odin_schedule",
        "hp.compOn", "hp.compOff", "flow.none", "flow.note", "flow.noteLpm", "flow.zoneTip", "flow.closed", "fc.now", "role.reserve", "role.reserveNow", "role.asgardNow", "role.offOdin", "role.drives", "role.off",
        "driver.odin", "driver.asgard", "now.heat", "now.off", "now.dhw", "planG.odin", "planG.noOdin",
        "planG.tip.heat", "planG.tip.dhw", "planG.tip.legionella", "planG.tipPre", "planG.tipCharge", "planG.tipInsufficient", "planG.tipHeat", "planG.tipLift",
        "hs.mqttPasswordSet", "hs.mqttState", "hs.mqttConnected", "hs.mqttDisconnected", "hs.mqttOff",
        "ctrl.editHost", "ctrl.hostInvalid",
        "wifi.connectedTo", "wifi.notConnected", "wifi.apActive", "wifi.sent", "wifi.needSsid",
        "wifi.switch.pending", "wifi.switch.connected", "wifi.switch.reverted", "wifi.switch.failed",
        "strip.lease.refused", "strip.lease.none", "strip.charge.now", "strip.charge.insufficient",
        "hs.link.ok", "hs.link.forwarder_off", "hs.link.telemetry_stale", "hs.link.no_room_temperature",
        "hs.link.odin_unreachable", "hs.link.unknown", "hs.odinStatus.disabled", "hs.odinStatus.idle",
        "hs.odinStatus.watching", "hs.odinStatus.lifted", "hs.odinStatus.yielded", "hs.odinStatus.rate_limited",
        "hs.odinStatus.write_failed", "hs.odinStatus.odin_unreachable", "hs.odinStatus.schedule_unreadable", "hs.odinStatus.clock_invalid",
        "hs.odinWanted", "hs.route.virtual_thermostat", "hs.route.odin_schedule", "hs.route.generic",
        "hs.route.none", "hs.leverState",
        "price.state.ok", "price.state.waiting", "price.state.running", "price.state.error", "price.state.disabled",
        "price.odinMode.api", "price.odinMode.energy_charts", "price.odinMode.unknown", "price.odinMode.pending",
        "price.err.spot", "price.err.spot_incomplete", "price.err.grid", "price.err.energinet", "price.err.odin",
        "price.err.no_odin_host", "price.err.clock", "price.err.other", "price.cached", "price.noOdinHost",
        "price.pushOk", "price.pushOkBody", "price.pushFail", "price.pushPending", "price.pushDisabled",
        "price.schedRemove", "price.schedHourAria", "price.schedValueAria", "price.atHour", "price.hoursValue",
        "price.noData", "price.lastPushValue",
        "price.state.odin", "price.odinMode.fixed", "price.odinMode.entsoe", "price.tokenSet", "price.tokenUnset",
        "price.tokenSaved", "price.inclAll", "price.defaultsApplied", "price.defaultsUnknown",
        "price.odinWriteOk", "price.odinWriteFail", "price.spotUsed.eds",
        "price.spotUsed.energy_charts", "price.spotUsed.entsoe", "price.spotUsed.fixed", "price.spotFallback",
        "price.err.datahub_dk_only", "price.err.no_token", "price.odinMode.dynamic",
    )}
    rt["_dec"] = T.meta("_dec")
    rt["_walls"] = T.meta("_walls")
    rt["_lang"] = cur
    rt_json = json.dumps(rt, ensure_ascii=False, separators=(",", ":"))

    LOGO = (
        '<svg class="logo" viewBox="0 0 32 32" aria-hidden="true">'
        '<circle cx="16" cy="16" r="15" fill="var(--fg)"/>'
        '<path d="M10 22V12M14 22V10M18 22V13M22 22V11" stroke="var(--accent)" stroke-width="2.4" stroke-linecap="round"/></svg>'
    )
    I_DASH = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 14a8 8 0 0 1 16 0"/><path d="M12 14l4-4"/><circle cx="12" cy="14" r="1.2"/></svg>'
    I_CONF = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="17" r="2"/></svg>'
    css_tag = f"<style>\n{inline_css}\n</style>" if inline_css else f'<link rel="stylesheet" href="{css_href}">'
    js_tag = f'<script src="{js_href}" defer></script>' if js_href else ""
    others = (
        "".join(
            f'<link rel="alternate" hreflang="{c.meta("_lang")}" href="{lang_urls[c.meta("_lang")]}">'
            for c in langs
            if c.meta("_lang") != cur
        )
        if len(langs) > 1
        else ""
    )

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

<!-- TILSTAND — før .app -->
<input class="state" type="radio" name="mode" id="m-dash" checked aria-label="{T("mode.dash")}">
<input class="state" type="radio" name="mode" id="m-conf" aria-label="{T("mode.conf")}">
{"".join(scope_inputs)}
<input class="state" type="checkbox" id="theme" aria-label="{T("theme.toggle")}">

<div class="app">
  <div class="top">
    <div class="wrap">
      <header class="header">
        <details class="device">
          <summary>{LOGO}<span class="name"><b>Lune Touch</b><small data-bind="device.about.place">{T("device.sample")}</small></span><span class="caret" aria-hidden="true"></span></summary>
          <div class="device-menu">
            <section class="device-about" aria-labelledby="device-about-h">
              <h3 id="device-about-h">{T("device.about")}</h3>
              <dl class="kv">
                <div><dt>{T("device.name")}</dt><dd data-bind="device.about.name">Lune Touch</dd></div>
                <div><dt>{T("device.place")}</dt><dd data-bind="device.about.place">{T("device.sample")}</dd></div>
                <div><dt>{T("device.ip")}</dt><dd data-bind="device.about.ip">—</dd></div>
                <div><dt>{T("device.mac")}</dt><dd data-bind="device.about.mac">—</dd></div>
                <div><dt>{T("device.firmware")}</dt><dd data-bind="device.about.firmware">—</dd></div>
                <div><dt>{T("device.esphome")}</dt><dd data-bind="device.about.esphome">—</dd></div>
                <div><dt>{T("device.uptime")}</dt><dd data-bind="device.about.uptime">—</dd></div>
              </dl>
              <button type="button" class="btn" data-action="copy-diag">{T("device.copyDiag")}</button>
            </section>
            <nav aria-label="{T("nav.devices")}" data-bind-devices>
              <a href="/" aria-current="page"><i></i>Lune Touch<small>{T("device.this")}</small></a>
            </nav>
          </div>
        </details>
        <nav class="mode" aria-label="{T("mode.label")}">
          <label for="m-dash">{I_DASH}{T("mode.dash")}</label>
          <label for="m-conf">{I_CONF}{T("mode.conf")}</label>
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
      {strip}
    </div>
  </div>
  <main class="content wrap">{views}
  </main>
</div>

<script type="application/json" id="i18n">{rt_json}</script>
{js_tag}
<script>
(function(){{
/* +/−, submit and dropdown close live in binder.js (one handler each). */
window.addEventListener('hashchange',function(){{var el=document.getElementById((location.hash||'').slice(1));if(el&&el.tagName==='DETAILS')el.open=true;}});
}})();
</script>
</body>
</html>
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--langs", default=os.environ.get("LUNE_UI_LANGS", "en,da"))
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--css", default=str(DS_ROOT / "dist" / "touch" / "lune-ui.css"))
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--hs-type", default="asgard", choices=("http", "asgard"), help="default heat-source type in preview HTML")
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

    binder = (ROOT / "binder.js").read_text(encoding="utf-8")
    (out / "ui.js").write_text(binder, encoding="utf-8")
    js_gz = gzip.compress(binder.encode(), 9, mtime=0)
    (out / "ui.js.gz").write_bytes(js_gz)

    # Firmware serves /lune-ui.css and /ui.js as immutable (1 year). Version the
    # URLs by content so a firmware update never runs a cached old binder/CSS.
    css_href = "/lune-ui.css?v=" + hashlib.sha1(css.encode()).hexdigest()[:10]
    js_href = "/ui.js?v=" + hashlib.sha1(binder.encode()).hexdigest()[:10]

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
            prev = prev.replace(
                '<script src="ui.js" defer></script>',
                '<script>window.LUNE_TOUCH_MOCK=true;</script>\n<script src="ui.js" defer></script>',
            )
            (out / f"preview-{c.meta('_lang')}.html").write_text(prev, encoding="utf-8")

    total = len(css_gz) + len(js_gz) + sum(len(g) for _, g in pages)
    print(
        f"Byggede {', '.join(codes)} → {out}/  (gzip: {total / 1024:.1f} kB: "
        f"css {len(css_gz) / 1024:.1f} + js {len(js_gz) / 1024:.1f} + "
        + " + ".join(f"{l} {len(g) / 1024:.1f}" for l, g in pages)
        + ")"
    )
    print(f"  lune-ui.css.gz: {len(css_gz)} B")
    print(f"  ui.js.gz: {len(js_gz)} B")
    for l, g in pages:
        print(f"  {l}/index.html.gz: {len(g)} B")


if __name__ == "__main__":
    main()
