#!/usr/bin/env python3
"""
Bygger docs/design-system.html — en levende reference for Lune Design System 2.
Siden bruger selve systemets CSS (dist/v6/lune-ui.css), så eksemplerne altid
viser den rigtige kode. Kør efter lds_build.py:

    python tools/lds_build.py config/v6.json && python tools/build_docs.py
"""
import html, json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import lds_build as L

ROOT = L.ROOT
T = L.TOK
esc = html.escape
C = L.flat_colors()

css_path = ROOT/"dist/v6/lune-ui.css"
if not css_path.exists(): sys.exit("Kør først: python tools/lds_build.py config/v6.json")
CSS = css_path.read_text(encoding="utf-8")

DOC_CSS = """
/* Kun til dokumentationssiden */
.doc-nav { display:flex; flex-wrap:wrap; gap:6px; padding-block: var(--space-2) var(--space-4); }
.doc-nav a { padding: 6px 14px; border-radius: var(--r-pill); background: var(--card); box-shadow: var(--card-edge); font-size: var(--fs-sm); font-weight:500; text-decoration:none; color: var(--muted); }
.doc-nav a:hover { color: var(--fg); }
.doc-h { grid-column: 1 / -1; padding-top: var(--space-6); }
.doc-h h2 { font-size: var(--fs-2xl); letter-spacing: -.03em; }
.doc-h p { color: var(--muted); max-width: 68ch; margin-top: var(--space-2); }
.doc .view { display: grid; }
.lead { font-size: var(--fs-lg); color: var(--muted); max-width: 60ch; }
.stage { padding: var(--space-5); border-radius: var(--r-field); background: var(--bg); display:grid; gap: var(--space-4); container-type: inline-size; }
.stage.row { display:flex; flex-wrap:wrap; align-items:center; gap: var(--space-3); }
.code { margin:0; padding: var(--space-4); border-radius: var(--r-field); background: var(--raised); overflow:auto; font: var(--fs-xs)/1.6 var(--font-mono); color: var(--fg); white-space: pre; max-height: 260px; }
.rules { display:grid; grid-template-columns: 1fr 1fr; gap: var(--space-4); font-size: var(--fs-sm); }
.rules ul { margin: 6px 0 0 18px; display:grid; gap:4px; color: var(--fg); }
.rules h4 { font-size: var(--fs-sm); font-weight:600; }
.rules .do h4 { color: var(--ok); } .rules .dont h4 { color: var(--danger); }
@media (max-width: 599.98px) { .rules { grid-template-columns: 1fr; } }
.sw-grid { display:grid; grid-template-columns: repeat(auto-fill, minmax(220px,1fr)); gap: var(--space-3); }
.sw { display:grid; grid-template-columns: 56px 1fr; gap: var(--space-3); align-items:center; }
.sw .chips { display:grid; grid-template-columns: 1fr 1fr; height: 44px; border-radius: 10px; overflow:hidden; box-shadow: 0 0 0 1px var(--border); }
.sw b { font-size: var(--fs-sm); font-weight:600; }
.sw small { display:block; font-size: var(--fs-xs); color: var(--muted); line-height:1.35; }
.sw code { display:block; font: var(--fs-xs) var(--font-mono); color: var(--muted); word-break: break-all; }
.tok { font: var(--fs-xs) var(--font-mono); color: var(--muted); }
.type-row { display:grid; grid-template-columns: 120px 1fr; gap: var(--space-4); align-items: baseline; padding: 10px 0; border-bottom: 1px solid var(--border); }
.type-row:last-child { border-bottom: 0; }
.space-row { display:grid; grid-template-columns: 90px 60px 1fr; gap: var(--space-3); align-items:center; font-size: var(--fs-sm); padding: 4px 0; }
.space-row i { display:block; height: 12px; background: var(--accent); border-radius: 3px; }
.radius-demo { display:flex; flex-wrap:wrap; gap: var(--space-4); }
.radius-demo div { width: 96px; height: 64px; background: var(--raised); display:grid; place-items:center; font-size: var(--fs-xs); color: var(--muted); }
.principles { counter-reset: p; }
.principle h3 { font-size: var(--fs-md); }
.principle p { font-size: var(--fs-sm); color: var(--muted); }
.shell { font: var(--fs-xs)/1.5 var(--font-mono); white-space: pre; overflow:auto; padding: var(--space-4); border-radius: var(--r-field); background: var(--bg); }
"""

# ---------------------------------------------------------------- helpers --
def section(id_, title, intro, body):
    return f'''
      <header class="doc-h" id="{id_}"><h2>{title}</h2><p>{intro}</p></header>
      {body}'''

def comp(title, desc, demo, code, do, dont, cls="", stage_cls=""):
    do_li = "".join(f"<li>{x}</li>" for x in do); dont_li = "".join(f"<li>{x}</li>" for x in dont)
    return f'''
      <section class="panel {cls}">
        <header class="panel-head"><h3>{title}</h3></header>
        <p class="note">{desc}</p>
        <div class="stage {stage_cls}">{demo}</div>
        <div class="rules"><div class="do"><h4>Brug</h4><ul>{do_li}</ul></div><div class="dont"><h4>Undgå</h4><ul>{dont_li}</ul></div></div>
        <details class="more"><summary>Markup</summary><pre class="code">{esc(code.strip())}</pre></details>
      </section>'''

def swatch(name, v):
    if "ref" in v:
        l = L.resolve(v["ref"], "light", C); d = L.resolve(v["ref"], "dark", C); val = f"= --{v['ref']}"
        note = esc(v.get("use",""))
    else:
        tinted = L.is_tint(v.get("light"))
        l = L.resolve(name, "light", C)
        d = L.resolve(name, "dark", C)
        val = f"{l} / {d}"
        note = esc(v.get("use",""))
        if tinted:
            note = (note + " · " if note else "") + "afledt af --card"
    return f'''<div class="sw"><div class="chips"><i style="background:{l}"></i><i style="background:{d}"></i></div>
      <div><b>--{name}</b><code>{esc(val)}</code><small>{note}</small></div></div>'''

# ---------------------------------------------------------------- indhold --
principles = [
  ("Et blik er nok", "Store tal, små labels under. Én hero-temperatur pr. visning."),
  ("Farve betyder noget", "Neutralt, indtil der er noget at sige. Én betydning pr. farve."),
  ("Samme sted, samme ting", "Zonestrimlen vælger hvad, tilstandspillen vælger hvordan."),
  ("Få store paneler", "2–4 paneler pr. visning, altid åbne. Kun ekspert-tuning foldes."),
  ("Tilstand uden JavaScript", "Radio, checkbox og details. JS kun til live-data."),
  ("Tilgængelig fra start", "≥ 4,5:1 kontrast, ≥ 44 px trykflader, tastatur og fokus."),
]
intro = f'''
      <div class="view-head" style="grid-column:1/-1;padding-top:var(--space-4)"><h2 style="font-size:var(--fs-hero);line-height:1;letter-spacing:-.05em">Lune Design System</h2><p>v{T["$meta"]["version"]} · Lune V6 og Lune Touch</p></div>
      <p class="lead" style="grid-column:1/-1">Tokens, komponenter og regler til at bygge UI på ESP32: ren CSS og HTML, tilstand uden JavaScript, ingen eksterne ressourcer. Denne side bruger selve systemets CSS. Skift tema øverst til højre for at se begge temaer.</p>
      ''' + "".join(f'<section class="panel c4 principle"><h3>{a}</h3><p>{b}</p></section>' for a, b in principles)

shell = section("arkitektur", "Arkitektur", "Ingen sidebar. To akser: tilstand (Dashboard/Konfiguration) og omfang (system + zoner). Én visning pr. kombination.", f'''
      <section class="panel c7">
        <header class="panel-head"><h3>Skallen</h3></header>
        <div class="shell">┌─────────────────────────────────────────────────────┐
│ [logo Lune V6 ▾]   ( Dashboard | Konfiguration )  ☾ │
│ [System][ Z1 ][ Z2 ][ Z3 ][Z4–5][ Z5 ][ Z6 ]        │
├─────────────────────────────────────────────────────┤
│ Titel på omfang  undertitel                         │
│ ┌──── c5 ─────┐ ┌────────── c7 ──────────┐         │
│ │ panel       │ │ panel                  │         │
│ └─────────────┘ └────────────────────────┘         │
└─────────────────────────────────────────────────────┘</div>
        <p class="note">Mobil: tilstandspillen svæver i bunden (tommelfingerhøjde); header og strimmel klæber ikke.</p>
      </section>
      <section class="panel c5">
        <header class="panel-head"><h3>Hvad hører hvor</h3></header>
        <dl class="kv">
          <div><dt>Dashboard</dt><dd>Live-værdier, grafer, måltemperatur, nulstil fejl</dd></div>
          <div><dt>Konfiguration</dt><dd>Hardware, følere, grupper, grænser, service</dd></div>
          <div><dt>Testen</dt><dd>Gør en beboer det i en almindelig uge? → Dashboard</dd></div>
          <div><dt>Id'er</dt><dd><span class="tok">#m-dash · #s-z1 · #v-conf-z1</span></dd></div>
        </dl>
        <p class="msg violet"><span><b>Genereret CSS.</b> Reglerne for tilstand × omfang skrives af <span class="tok">lds_build.py</span> ud fra <span class="tok">config/v6.json</span> og <span class="tok">config/touch.json</span>.</span></p>
      </section>''')

# farver
color_panels = ""
names = {"surface":"Flader og linjer","text":"Tekst","inverse":"Inverteret","heat":"Varme","status":"Status"}
for g, grp in T["color"].items():
    sws = "".join(swatch(k, v) for k, v in grp.items())
    color_panels += f'<section class="panel"><header class="panel-head"><h3>{names.get(g,g)}</h3><p>venstre: lyst tema · højre: mørkt tema</p></header><div class="sw-grid">{sws}</div></section>'
meaning = '''
      <section class="panel">
        <header class="panel-head"><h3>Farvernes betydning</h3><p>Én betydning pr. farve. Farve er aldrig eneste signal.</p></header>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Farve</th><th>Betyder</th><th>Eksempel</th><th>Aldrig</th></tr></thead>
          <tbody>
            <tr><td><span class="badge hot">Varme</span></td><td>Varme og afvigelse fra mål</td><td>Niveausegmenter, bjælker, "Kalder", preload-bånd</td><td>Fejl, links, pynt</td></tr>
            <tr><td><span class="badge info">Info</span></td><td>Vejr, prognoser, sensorer, forbindelser</td><td>Vejrbesked, vindgraf, returkurve</td><td>Handlinger</td></tr>
            <tr><td><span class="badge ok">OK</span></td><td>I orden</td><td>Online, motor lært</td><td>Tændt switch</td></tr>
            <tr><td><span class="badge warn">Advarsel</span></td><td>Kræver opmærksomhed snart</td><td>Mangler læring, manuel tilstand, zone under mål</td><td>Blokerende fejl</td></tr>
            <tr><td><span class="badge bad">Fejl</span></td><td>Handling nu, destruktivt</td><td>Motorfejl, nulstil og genlær</td><td>Varme</td></tr>
            <tr><td><span class="badge violet">Læring</span></td><td>Læring, kalibrering, gruppering</td><td>Adaptiv balancering, grupperet zone</td><td>—</td></tr>
          </tbody>
        </table></div>
      </section>'''
rows = L.contrast_report()
pairs = {}
for fg, bg, th, r, need, ok in rows:
    pairs.setdefault((fg, bg, need), {})[th] = (r, ok)
ctab = "".join(
    f'<tr><td class="tok">{fg}</td><td class="tok">{bg}</td><td class="num">{need}:1</td>'
    + "".join(
        (
            f'<td class="num">{v[th][0]:.2f}:1 <span class="badge {"ok" if v[th][1] else "bad"}">{"OK" if v[th][1] else "Fejl"}</span></td>'
            if th in v
            else '<td class="num"><span class="note">—</span></td>'
        )
        for th in ("light", "dark")
    )
    + "</tr>"
    for (fg, bg, need), v in pairs.items()
)
contrast = f'''
      <section class="panel">
        <header class="panel-head"><h3>Kontrast</h3><p>Beregnet fra tokens ved build · <span class="tok">python tools/lds_build.py --check</span></p></header>
        <div class="table-wrap"><table class="table"><thead><tr><th>Tekst</th><th>På</th><th class="num">Krav</th><th class="num">Lyst</th><th class="num">Mørkt</th></tr></thead><tbody>{ctab}</tbody></table></div>
      </section>'''
colors = section("farver", "Farver", "Alle farver har en lys og en mørk værdi og bliver til light-dark() i CSS. Temaet skiftes alene med color-scheme. Skriv aldrig hex-værdier i komponenter.", meaning + color_panels + contrast)

# typografi, afstand, radier
trow = "".join(f'<div class="type-row"><div><b class="tok">--{k}</b><small class="note" style="display:block">{v["px"]} px</small></div><div style="font-size:var(--{k});font-weight:{600 if v["px"]>=18 else 400};letter-spacing:{"-.03em" if v["px"]>=24 else "0"};line-height:1.15">{"21,4 °C" if v["px"]>=32 else "Fremløb og retur"} <small class="note" style="font-size:var(--fs-xs);letter-spacing:0">{esc(v["use"])}</small></div></div>' for k, v in T["type"].items())
srow = "".join(f'<div class="space-row"><span class="tok">--{k}</span><span>{v["value"]}</span><i style="width:{v["value"]}"></i></div>' for k, v in T["space"].items())
rdemo = "".join(f'<div style="border-radius:{v["value"] if v["value"]!="999px" else "32px"}">--{k}<br>{v["value"]}</div>' for k, v in T["radius"].items())
found = section("grundelementer", "Typografi, afstand og radier", "Én skrifttype (Geist med systemfont som fallback). Vægte 400/500/600. Sentence case, ingen versaler. Tal er altid tabular.", f'''
      <section class="panel c7"><header class="panel-head"><h3>Typeskala</h3></header><div>{trow}</div></section>
      <section class="panel c5"><header class="panel-head"><h3>Afstand</h3><p>8-punkts skala</p></header><div>{srow}</div>
        <div class="sub"><h4>Radier — jo større flade, jo større radius</h4><div class="radius-demo">{rdemo}</div></div>
        <p class="msg info"><span><b>Dybde uden skygger.</b> Kort er <span class="tok">--card</span> på <span class="tok">--bg</span> + <span class="tok">--card-edge</span>. Skygge kun på svævende elementer.</span></p>
      </section>''')

# ---------------------------------------------------------------- komponenter
LVL = '<span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span>'
strip_demo = f'''<nav class="strip" aria-label="Eksempel">
  <label class="tile tile-sys is-selected"><b>Manifold</b><span class="big">34,2° / 29,8°</span><small>ΔT 4,4° · kalder</small></label>
  <label class="tile" data-state="calling" data-level="4">{LVL}<span class="tile-id">Z1</span><span class="tile-name">Josephine</span><span class="tile-val">21,4°</span></label>
  <label class="tile" data-state="idle" data-level="1">{LVL}<span class="tile-id">Z2</span><span class="tile-name">Laura</span><span class="tile-val">20,8°</span></label>
  <label class="tile" data-state="idle" data-level="1">{LVL}<span class="tile-id">Z3</span><span class="tile-name">Toilet</span><span class="tile-val">22,6°</span></label>
  <label class="tile" data-state="calling" data-level="3" data-group="primary">{LVL}<span class="tile-id">Z4–5</span><span class="tile-name">Stue rum 1</span><span class="tile-val">21,1°</span></label>
  <label class="tile" data-state="calling" data-level="3" data-group="member">{LVL}<span class="tile-id">Z5</span><span class="tile-name">Stue rum 2</span><span class="tile-val">21,0°</span></label>
  <label class="tile" data-state="fault" data-level="0">{LVL}<span class="tile-id">Z6</span><span class="tile-name">Soveværelse</span><span class="tile-val">Fejl</span></label>
</nav>'''
strip_code = '''<nav class="strip" aria-label="Vælg zone eller manifold">
  <label class="tile tile-sys" for="s-sys">
    <b>Manifold</b><span class="big">34,2° / 29,8°</span><small>ΔT 4,4° · kalder</small>
  </label>
  <label class="tile" for="s-z1" data-state="calling" data-level="4">
    <span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span>
    <span class="tile-id">Z1</span><span class="tile-name">Josephine</span><span class="tile-val">21,4°</span>
  </label>
  <!-- data-state: calling|idle|fault|off · data-level: 0–5 · data-group: primary|member -->
</nav>'''
mode_demo = '''<nav class="mode" aria-label="Tilstand" style="position:static;transform:none;box-shadow:none"><label class="is-active">Dashboard</label><label>Konfiguration</label></nav>
<nav class="lang" aria-label="Sprog"><a aria-current="true">EN</a><a>DA</a></nav>'''
mode_code = '''<nav class="mode" aria-label="Tilstand">
  <label for="m-dash"><svg …/>Dashboard</label>
  <label for="m-conf"><svg …/>Konfiguration</label>
</nav>
<nav class="lang" aria-label="Sprog"><a href="/en/" hreflang="en" lang="en" aria-current="true">EN</a><a href="/da/" hreflang="da" lang="da">DA</a></nav>'''

panel_demo = '''<div class="panel alert"><div class="panel-head"><h3>Z6 Soveværelse har motorfejl</h3></div><p class="note">Ventilen holdes lukket, indtil fejlen er nulstillet. Tjek aktuatoren først.</p><div class="panel-foot" style="justify-content:flex-start"><button class="btn" type="button">Åbn Z6</button></div></div>
<section class="panel"><header class="panel-head"><h3>Varme nu</h3><span class="badge hot">Kalder</span></header>
<dl class="metrics"><div class="metric"><dt>Fremløb</dt><dd>34,2 <small>°C</small></dd></div><div class="metric"><dt>Retur</dt><dd>29,8 <small>°C</small></dd></div><div class="metric"><dt>ΔT</dt><dd>4,4 <small>K</small></dd></div><div class="metric"><dt>Samlet åbning</dt><dd>32 <small>%</small></dd></div></dl>
<div class="bar" style="--v:32%"><i></i></div></section>'''
panel_code = '''<section class="panel c5">
  <header class="panel-head"><h3>Varme nu</h3><p>undertitel</p><span class="badge hot">Kalder</span></header>
  <dl class="metrics">
    <div class="metric"><dt>Fremløb</dt><dd data-bind="manifold.flow">34,2 <small>°C</small></dd></div>
  </dl>
  <div class="bar" style="--v:32%" role="meter" aria-valuenow="32" aria-valuemin="0" aria-valuemax="100" aria-label="Samlet ventilåbning"><i></i></div>
  <footer class="panel-foot"><button class="btn primary" type="submit">Gem</button></footer>
</section>

<div class="panel alert">…</div>   <!-- maks. én pr. visning, øverst -->'''

badge_demo = '<span class="badge">Adaptiv</span> <span class="badge hot">Kalder</span> <span class="badge info">+0,4 °C</span> <span class="badge ok">Online</span> <span class="badge warn">Kræver læring</span> <span class="badge bad">Motorfejl</span> <span class="badge violet">Lærer løbende</span>'
msg_demo = '''<p class="msg info"><span><b>Vinden øges til 11 m/s i nat.</b> Z1 og Z5 forvarmes fra kl. 20 til 07.</span></p>
<p class="msg warn"><span>Manuel tilstand suspenderer automatisk styring af alle zoner, indtil den slås fra.</span></p>
<p class="msg violet"><span><b>Grupperet med Z4.</b> Mål og tidsplan styres fra Z4.</span></p>
<p class="msg ok"><span><b>Gemt.</b> Ændringen er sendt til enheden.</span></p>
<p class="msg bad"><span><b>Kunne ikke gemme.</b> Tjek forbindelsen til enheden.</span></p>'''

kv_demo = '''<dl class="kv"><div><dt>Motor</dt><dd class="c-ok">Lært</dd></div><div><dt>Preheat adv.</dt><dd>0,35 °C</dd></div><div><dt>Vejr-offset nu</dt><dd class="c-info">+0,4 °C</dd></div><div><dt>Seneste fejl</dt><dd class="c-bad">Endestop timeout</dd></div></dl>'''

CL='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M15 5l-7 7 7 7"/></svg>'; CR='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 5l7 7-7 7"/></svg>'
climate_demo = f'''<div class="climate"><div class="now">21,4<small>°C</small></div><div class="target"><button type="button" data-step="-1" aria-label="Sænk måltemperatur">{CL}</button><label class="value"><small>Mål °C</small><input type="number" value="22.0" min="16" max="28" step="0.5"></label><button type="button" data-step="1" aria-label="Hæv måltemperatur">{CR}</button></div></div>'''

sp_t = "0,30 20,29 40,28 60,27 80,27 100,26 120,27 140,26 160,31 180,34 200,35 220,34 240,33"
sp_g = "0,26 150,26 150,33 205,33 205,26 240,26"
comfort_demo = f'''<div class="comfort">
<label data-state="calling"><span class="id">Z1</span><span class="name">Josephine</span><span class="val"><b class="c-warn">21,4°</b> / 22,0°</span><svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-hidden="true"><polygon class="a" points="{sp_t} 240,26 205,26 205,33 150,33 150,26 0,26"/><polyline class="g" points="{sp_g}"/><polyline class="t" points="{sp_t}"/></svg></label>
<label data-state="fault"><span class="id">Z6</span><span class="name">Soveværelse</span><span class="val"><b class="bad">Motorfejl</b></span><svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-hidden="true"><polygon class="a" points="0,20 150,20 170,30 240,34 240,20 0,20"/><polyline class="g" points="0,20 240,20"/><polyline class="t" points="0,20 150,20 170,30 240,34"/></svg></label>
</div>'''

def stepper(id_, val, unit, label):
    return f'<div class="stepper"><button type="button" data-step="-1" aria-label="Sænk {label}">−</button><span class="value"><input type="number" id="{id_}" value="{val}" step="0.5"><span class="unit">{unit}</span></span><button type="button" data-step="1" aria-label="Hæv {label}">+</button></div>'
fields_demo = f'''<div class="field row"><label for="d1">Fremløbsprobe</label><select class="select" id="d1"><option>Probe 7</option></select></div>
<div class="field row"><label for="d2">Areal</label>{stepper("d2","12.0","m²","areal")}</div>
<div class="field"><label for="d3">BLE-sensor (MAC)<span class="hint">BTHome, fx Shelly BLU H&amp;T</span></label><div class="pair wide-first"><input class="input" id="d3" placeholder="AA:BB:CC:DD:EE:FF"><button type="button" class="btn">Scan</button></div></div>
<div class="field"><span class="label">Ventiltype</span><div class="seg" role="radiogroup" aria-label="Ventiltype"><label><input type="radio" name="dvt"><span>Normally open</span></label><label><input type="radio" name="dvt" checked><span>Normally closed</span></label></div></div>
<div class="gated"><label class="switch"><span class="switch-text"><b>Opsug overskudsvarme</b><small>Slå til for at låse op for felterne</small></span><input type="checkbox" role="switch"></label><div class="gated-body sub"><div class="field row"><label for="d4">Absorb-bånd</label>{stepper("d4","0.5","°C","absorb-bånd")}</div></div></div>'''
fields_code = '''<div class="field row"><label for="probe_flow">Fremløbsprobe</label><select class="select" id="probe_flow">…</select></div>

<div class="field row"><label for="z1_area">Areal</label>
  <div class="stepper">
    <button type="button" data-step="-1" aria-label="Sænk areal">−</button>
    <span class="value"><input type="number" id="z1_area" value="12.0" min="0" max="200" step="0.5"><span class="unit">m²</span></span>
    <button type="button" data-step="1" aria-label="Hæv areal">+</button>
  </div>
</div>

<div class="seg" role="radiogroup" aria-label="Ventiltype">
  <label><input type="radio" name="manifold_type" value="no"><span>Normally open</span></label>
  <label><input type="radio" name="manifold_type" value="nc" checked><span>Normally closed</span></label>
</div>

<div class="gated">
  <label class="switch"><span class="switch-text"><b>Opsug overskudsvarme</b></span><input type="checkbox" role="switch"></label>
  <div class="gated-body sub">… felter der kun gælder når switchen er tændt …</div>
</div>'''

compass_demo = '''<div class="compass" role="group" aria-label="Ydervægge"><i class="c"></i><label data-wall="n"><input type="checkbox" aria-label="Nord"><span>N</span></label><label data-wall="e"><input type="checkbox" aria-label="Øst"><span>Ø</span></label><label data-wall="s"><input type="checkbox" checked aria-label="Syd"><span>S</span></label><label data-wall="w"><input type="checkbox" checked aria-label="Vest"><span>V</span></label></div>'''

btn_demo = '''<form class="panel" data-save="doc-confirm" style="display:contents">
<button class="btn primary" type="button">Gem regulering</button>
<button class="btn" type="button">Hent nu</button>
<button class="btn danger" type="button" popovertarget="doc-confirm-bal">Nulstil balancering…</button>
<div id="doc-confirm-bal" popover class="confirm-pop" role="alertdialog" aria-labelledby="doc-confirm-bal-t" aria-describedby="doc-confirm-bal-d">
  <p class="confirm-title" id="doc-confirm-bal-t">Nulstil balancering?</p>
  <p id="doc-confirm-bal-d">Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p>
  <div class="confirm-actions">
    <button class="btn" type="button" popovertarget="doc-confirm-bal" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="submit" name="action" value="reset_balancing" popovertarget="doc-confirm-bal" popovertargetaction="hide">Nulstil</button>
  </div>
</div>
</form>'''

table_demo = '''<div class="table-wrap"><table class="table"><thead><tr><th>Zone</th><th class="num">Prior</th><th class="num">Lært</th><th class="num">Effektiv</th></tr></thead><tbody><tr><td>Z1</td><td class="num">1,00</td><td class="num c-violet">1,08</td><td class="num">1,08</td></tr><tr><td>Z2</td><td class="num">0,85</td><td class="num c-violet">0,81</td><td class="num">0,81</td></tr></tbody></table></div>
<pre class="log" lang="en">14:05  <span class="info">forecast</span> fetched, max wind 11 m/s
14:05  <span class="violet">balancing</span> Z3 0.42 → 0.44
14:06  <span class="bad">motor Z6</span> end-stop timeout (38 s)</pre>'''

trend_demo = '''<div class="sub trend-wrap" style="margin:0"><h4>Sidste 24 timer <span class="legend"><i class="lf"></i>Fremløb<i class="lr"></i>Retur</span></h4>
<svg class="trend" viewBox="0 0 240 80" preserveAspectRatio="none" role="img" aria-label="Fremløb og retur, sidste 24 timer"><polygon class="dt" points="0,40 40,38 80,37 120,36 150,24 180,18 210,20 240,30 240,62 210,56 180,54 150,56 120,64 80,65 40,64 0,65"/><polyline class="f" points="0,40 40,38 80,37 120,36 150,24 180,18 210,20 240,30"/><polyline class="r" points="0,65 40,64 80,65 120,64 150,56 180,54 210,56 240,62"/></svg>
<div class="axis" aria-hidden="true"><span>−24 t</span><span>−12 t</span><span>nu</span></div></div>'''

components = section("komponenter", "Komponenter", "Levende eksempler med den rigtige CSS. Fold 'Markup' ud for koden. Fuld beskrivelse i DESIGN.md afsnit 5.", "".join([
  comp("Zonestrimmel og zonefelt", "Overblik og omfangs-navigation i ét. System-feltet først, zoner i fysisk rækkefølge. Valgt felt inverteres.", strip_demo, strip_code,
       ["Niveau 0–5 fra ventilåbning; tændt zone mindst 1", "Fejl som ord i værdifeltet (\"Fejl\")", "<span class=tok>strip--many</span> på Touch med mange rum"],
       ["Ekstra farver eller rammer på det valgte felt", "Flere end 8 felter i standard-strimlen"]),
  comp("Tilstand og sprog", "Tilstandspillen har altid præcis to valg. Sprogvælgeren vises kun, når buildet har flere sprog.", mode_demo, mode_code,
       ["Midt i headeren på web; svævende i bunden på mobil", "Almindelige links til /en/ og /da/"],
       ["Flere tilstande eller faner", "JavaScript til sprogskift"], stage_cls="row"),
  comp("Panel", "Standard-containeren. Titel, valgfri undertitel og badge, indhold, footer med handlinger. Alert-varianten til fejl der kræver handling.", panel_demo, panel_code,
       ["2–4 paneler pr. visning, bredde c4–c8", "Én titel og ét emne pr. panel", "Footeren flugter i bunden"],
       ["Paneler i paneler", "Mere end én alert pr. visning", "Skygger på paneler"]),
  comp("Badge", "Kort status i en panelheader.", badge_demo, '<span class="badge hot">Kalder</span>  <!-- hot | info | ok | warn | bad | violet -->',
       ["Ét ord eller en værdi", "Højst ét pr. panelheader"], ["Som knap eller link"], cls="c6", stage_cls="row"),
  comp("Tal: kv", "Sekundære detaljer som nøgle/værdi. Værdien må have en statusfarve.", kv_demo, '<dl class="kv"><div><dt>Motor</dt><dd class="c-ok">Lært</dd></div></dl>',
       ["Detaljer under nøgletallene", "c-ok, c-warn, c-info, c-bad på værdien"], ["Til hovedtallet (brug .metric)"], cls="c6"),
  comp("Besked", "Forklarer hvad der sker og hvorfor. Første sætning fed og konkret.", msg_demo, '<p class="msg info"><span><b>Vinden øges til 11 m/s i nat.</b> Z1 og Z5 forvarmes …</span></p>',
       ["Én besked pr. panel", "Konsekvens eller handling i anden sætning"], ["Fejl der kræver handling nu (brug .panel.alert)"], cls="c6"),
  comp("Test af forbindelser", "Testknappen viser altid et .test-result. To linjer. Bliver stående til næste test. Gemmes ikke.",
       '''<div class="test-result" aria-live="polite"><div class="msg ok"><span><b>Læsning OK · 14:32:05 · 200 · 84 ms</b>20,8 °C fra temperature_feedback_z1</span></div></div>
<div class="test-result" aria-live="polite"><div class="msg bad"><span><b>Fejl · 14:32:18 · Timeout</b>Tjek host og port, og at varmekilden kan nås.</span></div></div>
<div class="test-result" data-state="running" aria-live="polite">Tester…</div>''',
       '<div class="test-result" aria-live="polite"><div class="msg ok"><span><b>Læsning OK · 14:32:05 · 200 · 84 ms</b>…</span></div></div>',
       ["aria-live=polite", "Første linje: handling, tid, kode, varighed", "Anden linje: hvad der blev læst eller sendt", "Knap har aria-busy mens den kører"],
       ["Resultat kun i konsollen", "Gemme testresultatet", "Rå fejlkode som eneste tekst"], cls="c8"),
  comp("Sendes til", "Live tabel over det, der faktisk afsendes. Samme tal på dashboardet. Synkronisering slået fra: dæmpet forklaring.",
       '''<div class="sub"><h4>Sendes til Asgard</h4>
<div class="table-wrap"><table class="table"><thead><tr><th></th><th>Værdi</th><th>Mål</th><th>Seneste afsendelse</th></tr></thead>
<tbody>
<tr><th>Vægtet hustemperatur</th><td class="num">20,3 °C</td><td class="mono">temperature_feedback_z1</td><td>Bekræftet · 42 s siden</td></tr>
<tr><th>Komfort-setpunkt</th><td class="num">20,5 °C</td><td class="mono">virtual_thermostat</td><td class="muted">Sendes ikke – Synkronisér komfortmål er slået fra</td></tr>
</tbody></table></div>
<details class="more"><summary>Hvordan beregnes den?</summary>
<div class="table-wrap"><table class="table"><thead><tr><th>Zone</th><th>Temperatur</th><th>Vægt</th><th>Bidrag</th></tr></thead>
<tbody><tr><td>Stue</td><td class="num">21,3 °C</td><td class="num">48 m² · 34 %</td><td class="num">7,3 °C</td></tr></tbody></table></div>
</details></div>''',
       '<table class="table">…vægtet temperatur, komfort-setpunkt…</table>\n<details class="more"><summary>Hvordan beregnes den?</summary>…</details>',
       ["Værdier fra samme kilde som dashboardet", "Vægt efter firmwarens areal- eller UA-vægt", "Bidragene summerer til den viste temperatur"],
       ["Opfundne felter", "Rå status som trusted"], cls="c8"),
  comp("Klima-kontrol", "Zone-dashboardets hero: aktuel temperatur og målet mellem to store knapper.", climate_demo, '<div class="climate"><div class="now">21,4<small>°C</small></div><div class="target">…</div></div>',
       ["Én pr. visning", "Deaktiveret med note på grupperede medlemmer"], ["Til konfigurationsværdier (brug .stepper)"], cls="c6"),
  comp("Komfortliste med sparkline", "Aktuel/mål og 24-timers graf pr. zone. Rækken åbner zonen.", comfort_demo, '<label for="s-z1" data-state="calling"><span class="id">Z1</span><span class="name">…</span><span class="val">…</span><svg class="spark" …/></label>',
       ["Mål som trappekurve", "Akse mindst 3 °C", "Gul værdi når > 0,5 °C under mål"], ["Tekst inde i SVG'en"], cls="c6"),
  comp("Trendgraf", "SVG kun til figurer; akser og forklaring i HTML. Farver efter betydning.", trend_demo, '<svg class="trend" viewBox="0 0 240 80" preserveAspectRatio="none" role="img" aria-label="…"><polyline class="f" points="…"/><polyline class="r" points="…"/></svg>',
       ["role=img og en aria-label med pointen", "non-scaling strokes"], ["Chart-biblioteker", "Tekst i SVG"], cls="c6"),
  comp("Felter, stepper, segment og switch", "Label over kontrol, eller ved siden af når panelet er bredt nok. Switchen kan gate resten af afsnittet.", fields_demo, fields_code,
       ["Tal med naturligt trin: stepper", "2–3 valg: segment; flere: select", "Hint inde i label"], ["Grøn switch", "Felter uden label"]),
  comp("Kompas", "Ydervægge som fire checkboxe. Bogstaverne kommer fra sprogkataloget.", compass_demo, '<div class="compass" role="group" aria-label="Ydervægge"><i class="c"></i><label data-wall="n"><input type="checkbox" aria-label="Nord"><span>N</span></label>…</div>',
       ["aria-label med fuldt navn"], ["Tekst uden sprogkatalog"], cls="c4"),
  comp("Knapper og bekræftelse", "Én primær pr. panel. Destruktive handlinger åbner .confirm-pop. Annullér har fokus.", btn_demo, '''<button class="btn danger" type="button" popovertarget="confirm-bal">Nulstil balancering…</button>
<div id="confirm-bal" popover class="confirm-pop" role="alertdialog" aria-labelledby="confirm-bal-t" aria-describedby="confirm-bal-d">
  <p class="confirm-title" id="confirm-bal-t">Nulstil balancering?</p>
  <p id="confirm-bal-d">Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p>
  <div class="confirm-actions">
    <button class="btn" type="button" popovertarget="confirm-bal" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="submit" name="action" value="reset_balancing" popovertarget="confirm-bal" popovertargetaction="hide">Nulstil</button>
  </div>
</div>''', ["Titel er et spørgsmål med handling og objekt", "Farlig knap gentager verbet, aldrig OK", "Esc og klik udenfor lukker uden at gemme"], ["Inline-udfoldning", "\"OK\"", "Modaler"], cls="c8", stage_cls="row"),
  comp("Tabel og log", "Tal højrestillet. Loggen er engelsk (fra firmwaren) og farver kilder efter betydning.", table_demo, '<div class="table-wrap"><table class="table">…<td class="num">1,08</td>…</table></div>\n<pre class="log" lang="en">…</pre>',
       ["table-wrap for vandret scroll"], ["Tabeller til layout"]),
  comp("Hierarkisk zonestrimmel (Touch)", "Hus + manifolds på niveau 1; zoner i .substrip når en manifold eller zone er valgt.",
       '''<nav class="strip strip--tiers" style="--strip-n:2">
  <label class="tile tile-sys" for="doc-house"><span class="tile-id">Hus</span><span class="temps"><span class="temp flow"><span class="temp-lab">F</span><span class="tile-val">20,8°</span></span></span></label>
  <label class="tile tile-manifold is-parent" for="doc-m1"><span class="tile-id">M1</span><span class="tile-name">Stueetage</span><span class="tile-val">33,1° / 29,9°</span><span class="mini" aria-hidden="true"><i data-level="3"></i><i data-level="1"></i><i data-level="0" data-state="off"></i></span></label>
  <label class="tile tile-manifold" for="doc-m2"><span class="tile-id">M2</span><span class="tile-name">1. sal</span><span class="tile-val">31,2° / 28,4°</span><span class="mini" aria-hidden="true"><i data-level="2"></i><i data-level="5" data-state="fault"></i></span></label>
</nav>
<nav class="substrip" style="display:grid;--sub-n:3" aria-label="Zoner">
  <label class="tile" data-state="calling" data-level="3"><span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span><span class="tile-id">Z1</span><span class="tile-name">Stue</span><span class="tile-val">21,4°</span></label>
  <label class="tile" data-state="idle" data-level="1"><span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span><span class="tile-id">Z2</span><span class="tile-name">Køkken</span><span class="tile-val">21,0°</span></label>
  <label class="tile" data-state="fault" data-level="0"><span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span><span class="tile-id">Z3</span><span class="tile-name">Gang</span><span class="tile-val">Fejl</span></label>
</nav>''',
       '<nav class="strip strip--tiers">…tile-sys + tile-manifold…</nav>\n<nav class="substrip" data-m="1" style="--sub-n:6">…zone tiles…</nav>',
       ["Max 5 felter på niveau 1 (hus+4)", "Understrimmel = V6-zonefelter", "Forælder-valgt: inset ring, ikke inverteret"],
       ["Flad liste med 8+ zoner", "Vandret scroll / wrap / overlay-menu"]),
  comp("Typeafhængige felter", "Lokale radioer + .typed-fields / .hs-fields. Synlighed uden JS (genereret af lds_build).",
       '''<input class="state" type="radio" name="doc_hs" id="doc-hs-http" value="http">
<input class="state" type="radio" name="doc_hs" id="doc-hs-asgard" value="asgard" checked>
<div class="seg" role="radiogroup" aria-label="Type"><label for="doc-hs-http"><span>Generisk HTTP</span></label><label for="doc-hs-asgard"><span>Asgard (Odin)</span></label></div>
<fieldset class="hs-fields typed-fields" data-type="http" style="display:grid"><p class="hint">write_url_template + read_url_template</p></fieldset>
<fieldset class="hs-fields typed-fields" data-type="asgard" style="display:grid"><p class="hint">host, port, entity, odin_plan_enabled</p></fieldset>''',
       '<input class="state" type="radio" name="hs_type" id="hs-http" value="http">\n…\n<fieldset class="hs-fields typed-fields" data-type="http">…</fieldset>',
       ["Kun firmware-felter", "Første type i heat_source_types er standard", "Ved 4+ typer: .select"],
       ["JS til at skifte typefelter", "Opfundne felter"]),
  comp("Plan vs. virkelighed", "Søjlegraf .bars: planlagt (--muted) og faktisk (--accent). Akser i HTML.",
       '''<div class="bars" style="--bars-n:6"><div class="bars-plot" role="img" aria-label="Planlagt og faktisk varme, 6 timer">
<div class="col"><i class="plan" style="--plan:40"></i><i class="act" style="--act:32"></i></div>
<div class="col"><i class="plan" style="--plan:55"></i><i class="act" style="--act:48"></i></div>
<div class="col"><i class="plan" style="--plan:70"></i><i class="act" style="--act:61"></i></div>
<div class="col"><i class="plan" style="--plan:50"></i><i class="act" style="--act:44"></i></div>
<div class="col"><i class="plan" style="--plan:35"></i><i class="act" style="--act:30"></i></div>
<div class="col"><i class="plan" style="--plan:45"></i><i class="act" style="--act:52"></i></div>
</div><div class="bars-legend"><span><i class="lp"></i>Planlagt</span><span><i class="la"></i>Faktisk</span></div></div>''',
       '<div class="bars" style="--bars-n:12"><div class="bars-plot">…<div class="col"><i class="plan" style="--plan:40"></i><i class="act" style="--act:32"></i></div>…</div></div>',
       ["Planlagt muted, faktisk accent (varme)", "Tekst uden for SVG/HTML-søjler"],
       ["Chart-bibliotek", "Samme farve til plan og faktisk"]),
  comp("Sektioner og sektionslinks", "Kun Konfiguration. 3+ sektioner får .section-nav. Kun Service/Udvikler foldes. Alene-panel fylder hele bredden med .subs.cols-2.",
       '''<nav class="section-nav" aria-label="Sektioner"><a href="#doc-sec-a">Manifold</a><a href="#doc-sec-b">Forbindelser</a><a href="#doc-sec-c">Service</a></nav>
<section class="section" id="doc-sec-a" aria-labelledby="doc-sec-a-h"><h2 class="section-h" id="doc-sec-a-h">Manifold og motorer<i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></h2><div class="section-grid"><div class="panel c6"><header class="panel-head"><h3>Eksempel</h3></header><p class="note">Åben sektion.</p></div></div></section>
<section class="section" id="doc-sec-b" aria-labelledby="doc-sec-b-h"><h2 class="section-h" id="doc-sec-b-h">Forbindelser<i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></h2><div class="section-grid"><form class="panel" data-state="approved"><header class="panel-head"><h3>Forbindelser</h3></header>
<div class="subs cols-2"><div class="sub"><h4>Lune Touch <span class="badge ok">Godkendt</span></h4><p class="note">Styrer denne V6</p><dl class="kv"><div><dt>Navn</dt><dd>Lune Touch</dd></div><div><dt>Leverer</dt><dd>Vejr-preload · Mål · Absorbering · Varmetilstand</dd></div></dl><div class="actions"><button class="btn danger" type="button" popovertarget="doc-confirm-touch">Frakobl Touch…</button>
<div id="doc-confirm-touch" popover class="confirm-pop" role="alertdialog" aria-labelledby="doc-confirm-touch-t" aria-describedby="doc-confirm-touch-d">
  <p class="confirm-title" id="doc-confirm-touch-t">Frakobl Touch?</p>
  <p id="doc-confirm-touch-d">Fjerner godkendt styring. Opdagelse alene giver aldrig styring.</p>
  <div class="confirm-actions">
    <button class="btn" type="button" popovertarget="doc-confirm-touch" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="button" popovertarget="doc-confirm-touch" popovertargetaction="hide">Frakobl</button>
  </div>
</div></div></div>
<div class="sub"><h4>BLE-ur</h4><p class="note">Udsend tid til Shelly BLU.</p><div class="actions"><button class="btn" type="button">Synkronisér nu</button></div></div></div>
<footer class="panel-foot"><button class="btn primary" type="button">Gem forbindelser</button></footer></form></div></section>
<details class="section" id="doc-sec-c"><summary class="section-h">Service <span class="badge">1 panel</span><i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></summary><div class="section-grid"><div class="panel c6"><header class="panel-head"><h3>Manual</h3></header></div></div></details>''',
       '<form class="panel">…<div class="subs cols-2"><div class="sub">…</div><div class="sub">…</div></div><footer class="panel-foot">Gem…</footer></form>',
       ["Alene-panel uden cN + .subs.cols-2", "Underafsnit-handlinger; footer kun Gem", "Maks. 5 sektioner"],
       ["Tom halv kolonne", "Folde Connect", "Gem-knap i underafsnit"]),
  comp("Feltbredder (kompakt)", "Med mus på skærm ≥ 1024 px står label og kontrol på samme række med naturlig bredde. Stepper 136 px. Lange felter (.w-lg) står under labelen. Touch er uændret (fuld/halv bredde, 48 px).",
       '''<div class="section-grid"><div class="panel c6"><header class="panel-head"><h3>Varmekilde</h3></header>
<div class="field row"><label for="doc-w-port">Port</label><input class="input w-xs" id="doc-w-port" type="number" value="8080"></div>
<div class="field row"><label for="doc-w-host">Host</label><input class="input w-md" id="doc-w-host" value="heat-bridge.local"></div>
<div class="field row"><label for="doc-w-url">Status-URL</label><input class="input w-lg" id="doc-w-url" value="http://heat-bridge.local/api/status"></div>
<div class="field row"><label for="doc-w-int">Interval</label><div class="stepper"><button type="button" aria-label="Sænk">−</button><span class="value"><input id="doc-w-int" type="number" value="60"><span class="unit">s</span></span><button type="button" aria-label="Hæv">+</button></div></div></div>
<div class="panel c6"><header class="panel-head"><h3>Varme nu</h3></header><div class="sub trend-wrap" data-empty><h4>Fremløb og retur</h4><p class="empty">Ingen målinger endnu</p><svg class="trend" viewBox="0 0 100 40" aria-hidden="true"></svg></div>
<dl class="kv"><div><dt>Fremløb</dt><dd>—</dd></div><div><dt>Retur</dt><dd>—</dd></div></dl></div></div>''',
       '<div class="field row"><label for="port">Port</label><input class="input w-xs" id="port" type="number"></div>\n<div class="sub trend-wrap" data-empty><h4>…</h4><p class="empty">…</p><svg class="trend">…</svg></div>',
       [".w-xs 8ch · .w-sm 14ch · .w-md 22ch · .w-lg 32ch", "Tom graf → én .empty-linje (data-empty på beholderen)", "Manglende værdi = «—»", "Konfiguration: 1/2/3 spalter, naturlig højde"],
       ["0 eller 0,0 for en manglende måling", "Flad graf uden data", ".stack eller ens højde i Konfiguration"]),
  comp("Tilstandsafhængige handlinger", "data-state på panelet; data-show-when på knapper. Næste skridt er primær. Underafsnit bærer tilstandshandlinger.",
       '''<div class="section-grid">
<form class="panel" data-state="unpaired"><header class="panel-head"><h3>Forbindelser</h3></header>
<div class="subs cols-2"><div class="sub"><h4>Lune Touch <span class="badge">Afventer</span></h4>
<p class="note" data-show-when="unpaired">Tilføj manifolden i Touch.</p>
<div class="actions"><button class="btn primary" type="button" data-show-when="unpaired">Godkend Lune Touch</button></div></div>
<div class="sub"><h4>BLE-ur</h4><p class="note">Sekundær handling her.</p></div></div>
<footer class="panel-foot"><button class="btn primary" type="button">Gem forbindelser</button></footer></form>
</div>''',
       '<div class="sub">…<div class="actions"><button data-show-when="unpaired" class="btn primary">…</button></div></div>',
       ["Kun relevante handlinger", "Godkend er primær når unpaired", "Footer kun Gem"],
       ["Vise Godkend og Frakobl samtidig", "Gem-knap i underafsnit"]),
]))

patterns = section("moenstre", "Mønstre og indhold", "Sådan sættes komponenterne sammen. Fuld beskrivelse i DESIGN.md afsnit 6–8.", '''
      <section class="panel c6" id="demo-dirty-panel"><header class="panel-head"><h3>Ugemte ændringer og gem</h3></header>
        <p class="note">Hverdagshandlinger autogemmes; opsætning gemmes eksplicit pr. panel. Skjul eller deaktivér aldrig gem-knappen.</p>
        <form class="panel" data-save="demo-dirty" style="box-shadow:none;background:transparent;padding:0">
          <div class="field"><label for="demo-name">Zonenavn</label><input class="input" id="demo-name" name="demo_name" value="Stue" maxlength="24"></div>
          <div class="field"><span class="label">Tilstand</span><div class="seg" role="radiogroup" aria-label="Tilstand"><label><input type="radio" name="demo_mode" value="static"><span>Statisk</span></label><label><input type="radio" name="demo_mode" value="adaptive" checked><span>Adaptiv</span></label></div></div>
          <footer class="panel-foot">
            <div class="foot-start"><span class="save-status" id="ss-demo-dirty" aria-live="polite"></span></div>
            <button type="reset" class="btn">Fortryd</button>
            <button class="btn primary" type="submit">Gem regulering</button>
          </footer>
        </form>
        <p class="note">Prøv: ændr felterne (prik + «2 ændringer ikke gemt»), Fortryd, Gem (success), eller tilføj <code>?fail</code> i data-save via knappen nedenfor for fejltilstand.</p>
        <div class="actions"><button class="btn" type="button" id="demo-fail-toggle">Næste gem fejler</button></div>
      </section>
      <section class="panel c6"><header class="panel-head"><h3>Gem og fejl</h3></header>
        <dl class="kv">
          <div><dt>Clean/dirty</dt><dd>form[data-js], prikker, Fortryd, aria-disabled — DESIGN.md 6.1</dd></div>
          <div><dt>Autogem</dt><dd>Dashboard .climate, 1,5 s debounce; ingen gem-knap</dd></div>
          <div><dt>Fejl nu</dt><dd>.panel.alert øverst + rød i strimmel og liste</dd></div>
          <div><dt>Snart</dt><dd>.msg.warn eller c-warn på værdien</dd></div>
          <div><dt>Destruktivt</dt><dd>.confirm-pop; titlen er spørgsmålet, knappen er verbet — aldrig OK</dd></div>
          <div><dt>Offline</dt><dd>badge i enhedspanelet; frys værdier, tøm dem ikke</dd></div>
        </dl></section>
      <section class="panel c6"><header class="panel-head"><h3>Sprog og tal</h3></header>
        <dl class="kv">
          <div><dt>Tone</dt><dd>Saglig, kort, aktiv form, sentence case</dd></div>
          <div><dt>Knapper</dt><dd>Verbum + objekt: "Gem regulering" → "Gemt ✓"</dd></div>
          <div><dt>Decimaltegn</dt><dd>Efter sprog: 21,4 / 21.4</dd></div>
          <div><dt>Enheder</dt><dd>Mellemrum: 21,4 °C, 6 m/s — kort grad i felter: 21,4°</dd></div>
          <div><dt>i18n</dt><dd>Build-time, --langs en,da, alle aria-labels oversat</dd></div>
        </dl></section>
      <section class="panel"><header class="panel-head"><h3>ESP32-budget</h3><span class="badge ok">39 kB gzip</span></header>
        <dl class="metrics">
          <div class="metric"><dt>CSS</dt><dd>10,8 <small>kB gzip</small></dd></div>
          <div class="metric"><dt>Side pr. sprog</dt><dd>14 <small>kB gzip</small></dd></div>
          <div class="metric"><dt>Eksterne requests</dt><dd>0</dd></div>
          <div class="metric"><dt>JS til tilstand</dt><dd>0 <small>linjer</small></dd></div>
        </dl></section>''')

nav = "".join(f'<a href="#{a}">{b}</a>' for a, b in [("arkitektur","Arkitektur"),("farver","Farver"),("grundelementer","Typografi og afstand"),("komponenter","Komponenter"),("moenstre","Mønstre")])

page = f'''<!doctype html>
<html lang="da">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>Lune Design System {T["$meta"]["version"]}</title>
<style>
{CSS}
{DOC_CSS}
</style>
</head>
<body>
<input class="state" type="checkbox" id="theme" aria-label="Skift tema">
<div class="app doc">
  <div class="top"><div class="wrap">
    <header class="header" style="grid-template-columns:1fr auto">
      <div class="device"><summary style="pointer-events:none"><svg class="logo" viewBox="0 0 32 32" aria-hidden="true"><circle cx="16" cy="16" r="15" fill="var(--fg)"/><path d="M10 22V12M14 22V10M18 22V13M22 22V11" stroke="var(--accent)" stroke-width="2.4" stroke-linecap="round"/></svg><span class="name"><b>Lune Design System</b><small>v{T["$meta"]["version"]}</small></span></summary></div>
      <label class="icon-btn theme-btn" for="theme" title="Skift lyst/mørkt tema">
        <svg class="i-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5 19 19M5 19l1.5-1.5M17.5 6.5 19 5"/></svg>
        <svg class="i-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/></svg>
      </label>
    </header>
    <nav class="doc-nav" aria-label="Indhold">{nav}</nav>
  </div></div>
  <main class="content wrap">
    <div class="view">{intro}{shell}{colors}{found}{components}{patterns}</div>
  </main>
</div>
<script type="application/json" id="i18n">{{"rt.unsaved.one":"{{n}} ændring ikke gemt","rt.unsaved.other":"{{n}} ændringer ikke gemt","rt.saving":"Gemmer…","rt.savedOk":"Gemt","rt.saveFailed":"Kunne ikke gemme.","rt.nothingToSave":"Ingen ændringer at gemme","rt.leaveUnsaved":"Du har ugemte ændringer.","rt.autoSaving":"Gemmer…","rt.autoSaved":"Gemt","rt.autoFailed":"Ikke gemt – prøv igen","rt.retry":"Prøv igen","common.undo":"Fortryd"}}</script>
<script>
document.addEventListener('click',function(e){{var b=e.target.closest('[data-step]');if(!b||b.disabled)return;var i=b.parentNode.querySelector('input');b.dataset.step>0?i.stepUp():i.stepDown();i.dispatchEvent(new Event('change',{{bubbles:true}}));}});
</script>
<script>
__LUNE_FORMS__
</script>
<script>
(function(){{
  var failNext=false;
  var tog=document.getElementById('demo-fail-toggle');
  if(tog) tog.addEventListener('click',function(){{ failNext=!failNext; tog.textContent=failNext?'Næste gem lykkes':'Næste gem fejler'; }});
  document.addEventListener('lune:save',function(e){{
    var d=e.detail||{{}}, f=d.form||document.querySelector('form.panel[data-save="'+d.key+'"]');
    if(!f||!f.luneSaved) return;
    var fail=failNext; failNext=false;
    if(tog) tog.textContent='Næste gem fejler';
    setTimeout(function(){{ f.luneSaved(!fail); }}, 600);
  }});
}})();
</script>
</body>
</html>'''
forms_js = (ROOT / "js" / "lune-forms.js").read_text(encoding="utf-8")
page = page.replace("__LUNE_FORMS__", forms_js)
out = ROOT/"docs/design-system.html"
out.write_text(page, encoding="utf-8")
print(f"{out}  ({len(page)/1024:.0f} kB)")
