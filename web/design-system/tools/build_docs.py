#!/usr/bin/env python3
"""
Bygger docs/design-system.html — en levende reference for Lune Design System 2.
Siden bruger selve systemets CSS (bygget som V6, men med alle @only-blokke),
så eksemplerne altid viser den rigtige kode:

    python tools/build_docs.py
"""
import html, json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import lds_build as L

ROOT = L.ROOT
T = L.TOK
esc = html.escape
C = L.flat_colors()

# Referencesiden viser alle komponenter, også de projektspecifikke (@only-blokke).
DOC_CFG = json.loads((ROOT/"config/v6.json").read_text(encoding="utf-8"))
DOC_CFG.update(modes=["home", "sys"], systemCategories=["device", "network", "service"])  # demo af System-siden
CSS = L.build_css(DOC_CFG, project_id=None)

DOC_CSS = """
/* Kun til dokumentationssiden */
.doc-nav { display:flex; flex-wrap:wrap; gap:6px; padding-block: var(--space-2) var(--space-4); }
.doc-nav a { padding: 6px 14px; border-radius: var(--r-pill); background: var(--card); box-shadow: var(--card-edge); font-size: var(--fs-sm); font-weight:500; text-decoration:none; color: var(--muted); }
.doc-nav a:hover { color: var(--fg); }
.doc-h { grid-column: 1 / -1; padding-top: var(--space-6); }
.doc-h h2 { font-size: var(--fs-2xl); letter-spacing: -.03em; }
.doc-h p { color: var(--muted); max-width: 68ch; margin-top: var(--space-2); }
.doc .view { display: grid; }
/* Referencesiden: indhold står på siden, ikke i kort. Kort vises kun i eksemplerne (.stage). */
.doc .view > .panel { background: transparent; box-shadow: none; padding: 0; }
.doc-layout { display: grid; grid-template-columns: 210px minmax(0, 1fr); gap: 48px; align-items: start; }
.doc-toc { position: sticky; top: 92px; display: flex; flex-direction: column; gap: 2px; font-size: var(--fs-sm); }
.doc-toc b { margin: 14px 0 4px 10px; font-size: var(--fs-xs); font-weight: 600; color: var(--faint); }
.doc-toc b:first-child { margin-top: 0; }
.doc-toc a { padding: 6px 10px; border-radius: 10px; color: var(--muted); text-decoration: none; }
.doc-toc a:hover { background: var(--card); color: var(--fg); }
@media (max-width: 899.98px) { .doc-layout { grid-template-columns: 1fr; } .doc-toc { display: none; } }
.doc-h, .doc-h2, .doc-comp, #principper { scroll-margin-top: 96px; }
.doc-h2 { grid-column: 1 / -1; padding-top: var(--space-5); border-top: 1px solid var(--border); margin-top: var(--space-4); }
.doc-h2 h3 { font-size: var(--fs-xl); font-weight: 600; letter-spacing: -.02em; }
.doc-h2 p { color: var(--muted); margin-top: 4px; max-width: 68ch; }
.doc-comp { grid-column: 1 / -1; display: flex; flex-direction: column; gap: var(--space-3); padding-block: var(--space-4) var(--space-2); min-width: 0; }
.doc-comp h3 { font-size: var(--fs-lg); font-weight: 600; }
.doc-desc { color: var(--muted); max-width: 72ch; font-size: var(--fs-md); }
@media (min-width: 900px) { .view > .doc-comp.c4 { grid-column: span 4; } .view > .doc-comp.c6 { grid-column: span 6; } .view > .doc-comp.c8 { grid-column: span 8; } }
.lead { font-size: var(--fs-lg); color: var(--muted); max-width: 60ch; }
.stage { padding: var(--space-5); border-radius: var(--r-card); background: var(--bg); box-shadow: inset 0 0 0 1px var(--border); display:grid; gap: var(--space-4); container-type: inline-size; }
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

def slug(t):
    import re
    t=t.lower().replace("æ","ae").replace("ø","oe").replace("å","aa")
    return re.sub(r"[^a-z0-9]+","-",t).strip("-")
def comp(title, desc, demo, code, do, dont, cls="", stage_cls=""):
    do_li = "".join(f"<li>{x}</li>" for x in do); dont_li = "".join(f"<li>{x}</li>" for x in dont)
    return f'''
      <article class="doc-comp {cls}" id="k-{slug(title)}">
        <h3>{title}</h3>
        <p class="doc-desc">{desc}</p>
        <div class="stage {stage_cls}">{demo}</div>
        <div class="rules"><div class="do"><h4>Brug</h4><ul>{do_li}</ul></div><div class="dont"><h4>Undgå</h4><ul>{dont_li}</ul></div></div>
        <details class="more"><summary>Markup</summary><pre class="code">{esc(code.strip())}</pre></details>
      </article>'''

def swatch(name, v):
    if "ref" in v:
        l = L.resolve(v["ref"], "light", C); d = L.resolve(v["ref"], "dark", C); val = f"= --{v['ref']}"
    else:
        l = L.resolve(name, "light", C); d = L.resolve(name, "dark", C); val = f"{l} / {d}"
    note = esc(v.get("use",""))
    if any(L.is_tint(v.get(th)) for th in ("light", "dark")):
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

shell = section("arkitektur", "Arkitektur", "Tre niveauer: Hjem (overblik og hverdagshandlinger), ark pr. ting (overblik, historik og tingens indstillinger) og System (det der gælder hele enheden). Erstatter Dashboard/Konfiguration. Fuld beskrivelse i DESIGN.md afsnit 15.", f'''
      <section class="panel c7">
        <header class="panel-head"><h3>Hjem → ark → indstillinger</h3></header>
        <div class="shell">┌──────────────────────────────────────────────────────┐
│ [Lune ▾]        ( Hjem | System )          EN·DA  ☾  │  navbar
├──────────────────────────────────────────────────────┤
│ Huset er 23,3 °C – varmepumpen hviler      ◯ 23,3°   │  hovedsektion
│ [Varme] [Næste varme] [Vejr] [Cirkulation]           │  maks. 4 felter
│ Rum: [Kontor][Bad][Køkken] …  ← tryk på en ting      │
└──────────────────────────────────────┬───────────────┘
                     ┌─────────────────┴──────────────┐
                     │ Josephine              ×       │  ark (520 px)
                     │ (Overblik|Historik|Indstil.)   │
                     │ Komfort ▢▢▢  Rum ▢▢▢  Gulv ▢▢  │  grupperede lister
                     │ 2 ændringer ikke gemt   [Gem]  │
                     └────────────────────────────────┘</div>
        <p class="note">System er en egen side: kategoriliste til venstre (240 px), indhold til højre (maks. 640 px).</p>
      </section>
      <section class="panel c5">
        <header class="panel-head"><h3>Hvor hører en indstilling hjemme?</h3></header>
        <dl class="kv">
          <div><dt>Hjem</dt><dd>Mål, til/fra — ændres i en almindelig uge (autogem)</dd></div>
          <div><dt>Ark</dt><dd>Hører til én ting i huset: rum, manifold, varmepumpe, vejr</dd></div>
          <div><dt>System</dt><dd>Gælder enheden: styringer, forbindelser, netværk, firmware</dd></div>
          <div><dt>Avanceret</dt><dd>Sjældent: underside i arket eller System › Service</dd></div>
        </dl>
        <p class="msg info"><span><b>Indstillinger er lister, ikke kort.</b> Maks. 6 rækker pr. gruppe og 5 grupper pr. side. Kontroller har naturlig bredde (stepper 140 px, port 9ch, host 22–26ch).</span></p>
      </section>''')

# farver
color_panels = ""
names = {"surface":"Flader og linjer","text":"Tekst","inverse":"Inverteret","heat":"Varme","status":"Lag 1: status","domain":"Lag 2: domæner","scale":"Lag 3: skalaer (5 trin)","data":"Data-aliaser"}
for g, grp in T["color"].items():
    sws = "".join(swatch(k, v) for k, v in grp.items())
    color_panels += f'<section class="panel"><header class="panel-head"><h3>{names.get(g,g)}</h3><p>venstre: lyst tema · højre: mørkt tema</p></header><div class="sw-grid">{sws}</div></section>'
meaning = '''
      <section class="panel">
        <header class="panel-head"><h3>Farvernes betydning</h3><p>Én betydning pr. farve. Farve er aldrig eneste signal.</p></header>
        <div class="table-wrap"><table class="table">
          <thead><tr><th>Farve</th><th>Betyder</th><th>Eksempel</th><th>Aldrig</th></tr></thead>
          <tbody>
            <tr><td><span class="badge hot">Varme</span></td><td>Varme og afvigelse fra mål</td><td>"Kalder", afvigelse fra mål, preload-bånd, fremløb</td><td>Fejl, links, pynt, ventilåbning</td></tr>
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
ctab = "".join(f'<tr><td class="tok">{fg}</td><td class="tok">{bg}</td><td class="num">{need}:1</td>' +
               "".join(f'<td class="num">{v[th][0]:.2f}:1 <span class="badge {"ok" if v[th][1] else "bad"}">{"OK" if v[th][1] else "Fejl"}</span></td>' if th in v else '<td class="num"><span class="note">—</span></td>' for th in ("light","dark")) + '</tr>'
               for (fg, bg, need), v in pairs.items())
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
LVL = '<span class="lvl" aria-hidden="true">'+'<i></i>'*10+'</span>'
def _dev(d):
    k=1 if d<=-1 else 2 if d<=-0.3 else 3 if d<0.3 else 4 if d<1 else 5
    sign="+" if d>0.05 else "−" if d<-0.05 else "±"
    return f'<span class="tile-dev" data-dev="{k}">{sign}{abs(d):.1f}°</span>'.replace(".", ",",1) if False else f'<span class="tile-dev" data-dev="{k}">{sign}{str(round(abs(d),1)).replace(".", ",")}°</span>'
strip_demo = f'''<nav class="strip" aria-label="Eksempel">
  <label class="tile tile-sys is-selected"><b>Manifold</b><span class="big">34,2° / 29,8°</span><small>ΔT 4,4° · kalder</small></label>
  <label class="tile" data-state="calling" data-level="7">{LVL}<span class="tile-pct">62 %</span><span class="tile-id">Z1</span><span class="tile-name">Josephine</span>{_dev(-0.6)}<span class="tile-val">21,4°</span></label>
  <label class="tile" data-state="idle" data-level="2">{LVL}<span class="tile-pct">18 %</span><span class="tile-id">Z2</span><span class="tile-name">Laura</span>{_dev(-0.2)}<span class="tile-val">20,8°</span></label>
  <label class="tile" data-state="idle" data-level="1">{LVL}<span class="tile-pct">6 %</span><span class="tile-id">Z3</span><span class="tile-name">Toilet</span>{_dev(0.6)}<span class="tile-val">22,6°</span></label>
  <label class="tile" data-state="calling" data-level="6" data-group="primary">{LVL}<span class="tile-pct">54 %</span><span class="tile-id">Z4–5</span><span class="tile-name">Stue rum 1</span>{_dev(-0.4)}<span class="tile-val">21,1°</span></label>
  <label class="tile" data-state="calling" data-level="5" data-group="member">{LVL}<span class="tile-pct">48 %</span><span class="tile-id">Z5</span><span class="tile-name">Stue rum 2</span>{_dev(-0.5)}<span class="tile-val">21,0°</span></label>
  <label class="tile" data-state="fault" data-level="0">{LVL}<span class="tile-pct">0 %</span><span class="tile-id">Z6</span><span class="tile-name">Soveværelse</span><span class="tile-val">Fejl</span></label>
</nav>'''
strip_code = '''<nav class="strip" aria-label="Vælg zone eller manifold">
  <label class="tile tile-sys" for="s-sys">
    <b>Manifold</b><span class="big">34,2° / 29,8°</span><small>ΔT 4,4° · kalder</small>
  </label>
  <label class="tile" for="s-z1" data-state="calling" data-level="7">
    <span class="lvl" aria-hidden="true"><i></i>…×10</span>
    <span class="tile-pct">62 %</span>
    <span class="tile-id">Z1</span><span class="tile-name">Josephine</span>
    <span class="tile-dev" data-dev="2">−0,6°</span>
    <span class="tile-val">21,4°</span>
  </label>
  <!-- data-state: calling|idle|fault|off · data-level: 0–10 (ventilåbning i 10 %-trin, neutral) · tile-pct: præcis procent · data-group: primary|member (violet) · tile-dev data-dev: 1–5 -->
</nav>'''
mode_demo = '''<nav class="mode" aria-label="Hovedmenu" style="position:static;transform:none;box-shadow:none"><label class="is-active">Hjem</label><label>System</label></nav>
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

CL='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12h14"/></svg>'; CR='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg>'
climate_demo = f'''<div class="climate"><div class="now">21,4<small>°C</small></div><div class="target"><button type="button" data-step="-1" aria-label="Sænk måltemperatur">{CL}</button><label class="value"><small>Mål °C</small><input type="number" value="22.0" min="16" max="28" step="0.5"></label><button type="button" data-step="1" aria-label="Hæv måltemperatur">{CR}</button></div></div>'''

sp_t = "0,30 20,29 40,28 60,27 80,27 100,26 120,27 140,26 160,31 180,34 200,35 220,34 240,33"
sp_g = "0,26 150,26 150,33 205,33 205,26 240,26"
comfort_demo = f'''<div class="comfort">
<label data-state="calling"><span class="id">Z1</span><span class="name">Josephine</span><span class="val"><b class="c-warn">21,4°</b> / 22,0°</span><svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-hidden="true"><polygon class="a" points="{sp_t} 240,26 205,26 205,33 150,33 150,26 0,26"/><polyline class="g" points="{sp_g}"/><polyline class="t" points="{sp_t}"/></svg></label>
<label data-state="fault"><span class="id">Z6</span><span class="name">Soveværelse</span><span class="val"><b class="bad">Motorfejl</b></span><svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-hidden="true"><polygon class="a" points="0,20 150,20 170,30 240,34 240,20 0,20"/><polyline class="g" points="0,20 240,20"/><polyline class="t" points="0,20 150,20 170,30 240,34"/></svg></label>
</div>'''

def stepper(id_, val, unit, label):
    return f'<div class="stepper"><button type="button" data-step="-1" aria-label="Sænk {label}">−</button><span class="value"><input type="number" id="{id_}" value="{val}" step="0.5"><span class="unit">{unit}</span></span><button type="button" data-step="1" aria-label="Hæv {label}">+</button></div>'
fields_demo = f'''<div style="display:grid;gap:var(--space-5);max-width:640px">
<section class="setting-group"><h4>Måling</h4><div class="setting-list">
  <div class="setting"><div class="setting-label"><label for="d1">Fremløbsprobe</label></div><div class="setting-control"><select class="select" id="d1" style="width:180px"><option>Probe 7</option></select></div></div>
  <div class="setting"><div class="setting-label"><label for="d3">BLE-sensor</label><small>BTHome, fx Shelly BLU H&amp;T</small></div><div class="setting-control"><input class="input w-md" id="d3" placeholder="AA:BB:CC:DD:EE:FF"><button type="button" class="btn">Scan</button></div></div>
  <div class="setting"><div class="setting-label"><label for="d2">Areal</label></div><div class="setting-control">{stepper("d2","12.0","m²","areal")}</div></div>
</div></section>
<section class="setting-group"><h4>Ventiler</h4><div class="setting-list">
  <div class="setting"><div class="setting-label"><span>Ventiltype</span><small>Som angivet på aktuatoren</small></div><div class="setting-control"><div class="seg" role="radiogroup" aria-label="Ventiltype"><label><input type="radio" name="dvt"><span>NO</span></label><label><input type="radio" name="dvt" checked><span>NC</span></label></div></div></div>
</div></section>
<section class="setting-group"><h4>Opsugning</h4><div class="setting-list gated">
  <label class="setting switch"><span class="setting-label"><b style="font-weight:500">Opsug overskudsvarme</b><small>Gulvet bruges som buffer</small></span><input type="checkbox" role="switch"></label>
  <div class="gated-body"><div class="setting"><div class="setting-label"><label for="d4">Absorb-bånd</label></div><div class="setting-control">{stepper("d4","0.5","°C","absorb-bånd")}</div></div></div>
</div><p class="note">Felterne under switchen låses op, når den er slået til.</p></section>
</div>'''
fields_code = '''<section class="setting-group">
  <h4>Måling</h4>
  <div class="setting-list">
    <div class="setting">
      <div class="setting-label"><label for="probe_flow">Fremløbsprobe</label></div>
      <div class="setting-control"><select class="select" id="probe_flow">…</select></div>
    </div>
    <div class="setting">
      <div class="setting-label"><label for="ble">BLE-sensor</label><small>BTHome, fx Shelly BLU H&T</small></div>
      <div class="setting-control"><input class="input w-md" id="ble"><button type="button" class="btn">Scan</button></div>
    </div>
  </div>
</section>

<!-- Gating: switch som første række, afhængige rækker i .gated-body -->
<div class="setting-list gated">
  <label class="setting switch"><span class="setting-label"><b>Opsug overskudsvarme</b></span><input type="checkbox" role="switch"></label>
  <div class="gated-body"><div class="setting">…</div></div>
</div>'''

compass_demo = '''<div class="compass" role="group" aria-label="Ydervægge"><i class="c"></i><label data-wall="n"><input type="checkbox" aria-label="Nord"><span>N</span></label><label data-wall="e"><input type="checkbox" aria-label="Øst"><span>Ø</span></label><label data-wall="s"><input type="checkbox" checked aria-label="Syd"><span>S</span></label><label data-wall="w"><input type="checkbox" checked aria-label="Vest"><span>V</span></label></div>'''

btn_demo = '''<button class="btn primary" type="button">Gem regulering</button><button class="btn" type="button">Hent nu</button><button class="btn" type="button" disabled>Nulstil fejl</button>
<button class="btn danger" type="button" popovertarget="doc-cf">Nulstil balancering…</button>
<div id="doc-cf" popover class="confirm-pop" role="alertdialog" aria-labelledby="doc-cf-t"><h4 id="doc-cf-t">Nulstil balancering?</h4><p>Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p><div class="actions"><button class="btn" type="button" popovertarget="doc-cf" popovertargetaction="hide" autofocus>Annullér</button><button class="btn danger-solid" type="button" popovertarget="doc-cf" popovertargetaction="hide">Nulstil</button></div></div>'''

table_demo = '''<div class="table-wrap"><table class="table"><thead><tr><th>Zone</th><th class="num">Prior</th><th class="num">Lært</th><th class="num">Effektiv</th></tr></thead><tbody><tr><td>Z1</td><td class="num">1,00</td><td class="num c-violet">1,08</td><td class="num">1,08</td></tr><tr><td>Z2</td><td class="num">0,85</td><td class="num c-violet">0,81</td><td class="num">0,81</td></tr></tbody></table></div>
<pre class="log" lang="en">14:05  <span class="info">forecast</span> fetched, max wind 11 m/s
14:05  <span class="violet">balancing</span> Z3 0.42 → 0.44
14:06  <span class="bad">motor Z6</span> end-stop timeout (45 s)</pre>'''

trend_demo = '''<div class="sub trend-wrap" style="margin:0"><h4>Sidste 24 timer <span class="legend"><i class="lf"></i>Fremløb<i class="lr"></i>Retur</span></h4>
<svg class="trend" viewBox="0 0 240 80" preserveAspectRatio="none" role="img" aria-label="Fremløb og retur, sidste 24 timer"><polygon class="dt" points="0,40 40,38 80,37 120,36 150,24 180,18 210,20 240,30 240,62 210,56 180,54 150,56 120,64 80,65 40,64 0,65"/><polyline class="f" points="0,40 40,38 80,37 120,36 150,24 180,18 210,20 240,30"/><polyline class="r" points="0,65 40,64 80,65 120,64 150,56 180,54 210,56 240,62"/></svg>
<div class="axis" aria-hidden="true"><span>−24 t</span><span>−12 t</span><span>nu</span></div></div>'''

def tile(zid, name, val, lvl, pct, state="idle", extra="", dev=None):
    d = _dev(dev) if dev is not None else ""
    return f'<label class="tile" data-state="{state}" data-level="{lvl}"{extra}>{LVL}<span class="tile-pct">{pct} %</span><span class="tile-id">{zid}</span><span class="tile-name">{name}</span>{d}<span class="tile-val">{val}</span></label>'
states_demo = '<nav class="strip" aria-label="Eksempel" style="--strip-n:5;grid-template-columns:repeat(5,minmax(0,1fr))">' + "".join([
  tile("Z1", "Josephine", "21,4°", 3, 28, "blocked", "", -0.6).replace('<span class="tile-val">21,4°</span>', '<span class="tile-val">Blokeret</span>'),
  tile("Z2", "Laura", "Lærer 60 %", 6, 60, "learning"),
  tile("Z3", "Toilet", "22,6°", 1, 6, "idle", ' data-learn="needed"', 0.6),
  tile("Z4", "Stue", "21,1°", 6, 54, "calling", ' data-charge'),
  tile("Z5", "Kontor", "20,4°", 4, 38, "calling", ' data-charge="insufficient"'),
]) + '</nav>'
states_code = '''<label class="tile" for="s-z1" data-state="blocked" data-level="3">…<span class="tile-val">Blokeret</span></label>
<label class="tile" data-state="learning" data-level="6">…<span class="tile-val">Lærer 60 %</span></label>  <!-- bjælke = læringsprocent, violet -->
<label class="tile" data-learn="needed">…</label>          <!-- gult ! øverst til højre: ikke lært, læring kører ikke -->
<label class="tile" data-charge>…</label>                   <!-- ↑ Touch lader gulvet op før vind/kulde -->
<label class="tile" data-charge="insufficient">…</label>    <!-- gul ↑: gulvet kan ikke dække det hele -->
<label class="tile tile-sys" data-lease="none">…</label>    <!-- gult ! : V6 kører lokalt uden Touch -->'''

tiers_demo = f'''<nav class="strip strip--tiers" style="--strip-n:2" aria-label="Eksempel">
  <label class="tile tile-sys"><span class="tile-id">Hus</span><span class="temps"><span class="temp flow"><span class="temp-lab">F</span><span class="tile-val">33,1°</span></span><span class="temp ret"><span class="temp-lab">R</span><span class="tile-val">29,9°</span></span></span></label>
  <label class="tile tile-manifold is-parent"><span class="tile-id">M1</span><span class="tile-name">Stueetage</span><span class="tile-val">33,1° / 29,9°</span><span class="mini" aria-hidden="true"><i data-level="3"></i><i data-level="1"></i><i data-level="0" data-state="off"></i></span></label>
  <label class="tile tile-manifold"><span class="tile-id">M2</span><span class="tile-name">1. sal</span><span class="tile-val">31,2° / 28,4°</span><span class="mini" aria-hidden="true"><i data-level="2"></i><i data-level="5" data-state="fault"></i></span></label>
</nav>
<nav class="substrip" style="display:grid;--sub-n:3" aria-label="Zoner">{tile("Z1","Stue","21,4°",7,62,"calling")}{tile("Z2","Køkken","21,0°",2,14)}{tile("Z3","Gang","Fejl",0,0,"fault")}</nav>'''

hs_demo = '''<div style="display:grid;gap:var(--space-3)">
<input class="state" type="radio" name="doc_hs" id="doc-hs-http" value="http">
<input class="state" type="radio" name="doc_hs" id="doc-hs-asgard" value="asgard" checked>
<div class="seg" role="radiogroup" aria-label="Type"><label for="doc-hs-http"><span>Generisk HTTP</span></label><label for="doc-hs-asgard"><span>Asgard (Odin)</span></label></div>
<fieldset class="hs-fields typed-fields" data-type="http"><p class="hint">write_url_template + read_url_template</p></fieldset>
<fieldset class="hs-fields typed-fields" data-type="asgard"><p class="hint">host, port, entity, odin_plan_enabled</p></fieldset>
</div>'''
# Referencesiden er bygget med config/v6.json (uden heat_source_types); demoen får sine egne regler.
hs_css = '''#doc-hs-http:checked ~ .typed-fields[data-type="http"], #doc-hs-asgard:checked ~ .typed-fields[data-type="asgard"] { display: grid; }
.typed-fields { display: none; }
#doc-hs-http:checked ~ .seg label[for="doc-hs-http"] > span, #doc-hs-asgard:checked ~ .seg label[for="doc-hs-asgard"] > span { background: var(--inv-bg); color: var(--inv-fg); }'''

sections_demo = '''<nav class="section-nav" aria-label="Sektioner"><a href="#doc-sec-a">Manifold</a><a href="#doc-sec-b" data-dirty>Forbindelser</a><a href="#doc-sec-c">Service</a></nav>
<section class="section" id="doc-sec-a" aria-labelledby="doc-sec-a-h"><h2 class="section-h" id="doc-sec-a-h">Manifold og motorer<i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></h2><div class="section-grid"><div class="panel c6"><header class="panel-head"><h3>Manifold</h3><button class="help-btn" type="button" popovertarget="doc-help-manifold" aria-label="Hjælp: Manifold">?</button></header><div id="doc-help-manifold" popover class="help-pop"><p>Ventiltype og prober for hele manifolden. Ændringer gælder alle zoner.</p><a href="#">Læs mere</a></div><p class="note">Åben sektion med hjælp (?).</p></div></div></section>
<section class="section" id="doc-sec-b" data-dirty aria-labelledby="doc-sec-b-h"><h2 class="section-h" id="doc-sec-b-h">Forbindelser<i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></h2><div class="section-grid"><form class="panel" data-state="approved"><header class="panel-head"><h3>Forbindelser</h3></header>
<div class="subs cols-2"><div class="sub"><h4>Lune Touch <span class="badge ok">Godkendt</span></h4><p class="note">Styrer denne V6</p><dl class="kv"><div><dt>Navn</dt><dd>Lune Touch</dd></div><div><dt>Leverer</dt><dd>Vejr-preload · Mål · Absorbering</dd></div></dl><div class="actions"><button class="btn danger" type="button" popovertarget="doc-confirm-touch" data-show-when="approved error">Frakobl Touch…</button>
<div id="doc-confirm-touch" popover class="confirm-pop" role="alertdialog" aria-labelledby="doc-confirm-touch-t" aria-describedby="doc-confirm-touch-d"><h4 id="doc-confirm-touch-t">Frakobl Touch?</h4><p id="doc-confirm-touch-d">Fjerner godkendt styring. Opdagelse alene giver aldrig styring.</p><div class="actions"><button class="btn" type="button" popovertarget="doc-confirm-touch" popovertargetaction="hide" autofocus>Annullér</button><button class="btn danger-solid" type="button" popovertarget="doc-confirm-touch" popovertargetaction="hide">Frakobl</button></div></div></div></div>
<div class="sub"><h4>BLE-ur</h4><p class="note">Udsend tid til Shelly BLU.</p><div class="actions"><button class="btn" type="button">Synkronisér nu</button></div></div></div>
<footer class="panel-foot"><button class="btn primary" type="button">Gem forbindelser</button></footer></form></div></section>
<details class="section" id="doc-sec-c"><summary class="section-h">Service <span class="badge">1 panel</span><i class="section-h-dot" aria-hidden="true"></i><i class="section-h-line" aria-hidden="true"></i></summary><div class="section-grid"><div class="panel c6"><header class="panel-head"><h3>Manual</h3></header></div></div></details>'''

test_demo = '''<div class="test-result" aria-live="polite"><div class="msg ok"><span><b>Læsning OK · 14:32:05 · 200 · 84 ms</b>20,8 °C fra temperature_feedback_z1</span></div></div>
<div class="test-result" aria-live="polite"><div class="msg bad"><span><b>Fejl · 14:32:18 · Timeout</b>Tjek host og port, og at varmekilden kan nås.</span></div></div>
<div class="test-result" data-state="running" aria-live="polite">Tester…</div>'''

sends_demo = '''<div class="sub"><h4>Sendes til Asgard</h4>
<div class="table-wrap"><table class="table"><thead><tr><th></th><th>Værdi</th><th>Mål</th><th>Seneste afsendelse</th></tr></thead>
<tbody>
<tr><th>Vægtet hustemperatur</th><td class="num">20,3 °C</td><td class="mono">temperature_feedback_z1</td><td>Bekræftet · 42 s siden</td></tr>
<tr><th>Komfort-setpunkt</th><td class="num">20,5 °C</td><td class="mono">virtual_thermostat</td><td class="muted">Sendes ikke – Synkronisér komfortmål er slået fra</td></tr>
</tbody></table></div>
<details class="more"><summary>Hvordan beregnes den?</summary>
<div class="table-wrap"><table class="table"><thead><tr><th>Zone</th><th>Temperatur</th><th>Vægt</th><th>Bidrag</th></tr></thead>
<tbody><tr><td>Stue</td><td class="num">21,3 °C</td><td class="num">48 m² · 34 %</td><td class="num">7,3 °C</td></tr></tbody></table></div>
</details></div>'''

bars_demo = '<div class="bars" style="--bars-n:6"><div class="bars-plot" role="img" aria-label="Planlagt og faktisk varme, 6 timer">' + "".join(
  f'<div class="col"><i class="plan" style="--plan:{p}"></i><i class="act" style="--act:{a}"></i></div>' for p, a in ((40,32),(55,48),(70,61),(50,44),(35,30),(45,52))
) + '</div><div class="bars-legend"><span><i class="lp"></i>Planlagt</span><span><i class="la"></i>Faktisk</span></div></div>'

empty_demo = '''<div class="sub trend-wrap" data-empty style="margin:0"><h4>Fremløb og retur</h4><p class="empty">Ingen målinger endnu</p><svg class="trend" viewBox="0 0 100 40" aria-hidden="true"></svg></div>
<dl class="kv"><div><dt>Fremløb</dt><dd>—</dd></div><div><dt>Retur</dt><dd>—</dd></div></dl>'''

state_demo = '''<form class="panel" data-state="unpaired" style="box-shadow:var(--card-edge);background:var(--card)"><header class="panel-head"><h3>Forbindelser</h3></header>
<div class="subs cols-2"><div class="sub"><h4>Lune Touch <span class="badge">Afventer</span></h4>
<p class="note" data-show-when="unpaired">Tilføj manifolden i Touch.</p>
<div class="actions"><button class="btn primary" type="button" data-show-when="unpaired">Godkend Lune Touch</button><button class="btn danger" type="button" data-show-when="approved error">Frakobl Touch…</button></div></div>
<div class="sub"><h4>BLE-ur</h4><p class="note">Sekundær handling her.</p></div></div>
<footer class="panel-foot"><button class="btn primary" type="button">Gem forbindelser</button></footer></form>'''

_bars = [(30,""),(45,""),(60,""),(0,""),(55,"dhw"),(70,"dhw"),(0,""),(35,""),(40,""),(50,""),(0,""),(0,""),(80,"legionella"),(0,""),(25,""),(30,""),(45,""),(55,""),(0,""),(20,""),(35,""),(40,""),(0,""),(30,"")]
plan_demo = ('<div class="plan" role="img" aria-label="Opvarmningsplan, næste 24 timer">'
  '<div class="plan-lab plan-lab--y"><b>Odin</b><span class="plan-y"><i>4</i><i>2</i><i>0</i></span></div>'
  '<div class="plan-lane plan-lane--odin">' + "".join(f'<i class="plan-bar" style="--v:{v}"{f" data-mode={chr(34)}{m}{chr(34)}" if m else ""}></i>' for v, m in _bars) +
  '<span class="plan-lift" style="--a:2;--b:9"></span></div>'
  '<span class="plan-lab">Stue</span><div class="plan-lane"><span class="plan-seg" data-kind="preload" style="--a:3;--b:7" title="Forvarme"></span><span class="plan-seg" data-kind="charge" style="--a:14;--b:18" title="Oplader før vind"></span></div>'
  '<span class="plan-lab">Kontor</span><div class="plan-lane"><span class="plan-seg" data-kind="charge" data-insufficient style="--a:13;--b:19" title="Oplader – dækker ikke hele underskuddet"></span></div>'
  '<div class="plan-x"><span>Nu</span><span style="left:25%">18</span><span class="m" style="left:37.5%">21</span><span class="d" style="left:50%">tir</span><span class="m" style="left:62.5%">03</span><span style="left:75%">06</span><span class="m" style="left:87.5%">09</span></div>'
  '</div><div class="fc-legend"><span><i class="lbar"></i>Rumvarme</span><span><i class="ldhw"></i>Varmt vand</span><span><i class="lleg"></i>Legionella</span><span><i class="llift"></i>Løft af komfortbånd</span><span><i class="lpre"></i>Forvarme</span><span><i class="lch"></i>Opladning</span><span><i class="lins"></i>Dækker ikke</span></div>')
_items_main = [
  comp("Zonefelt: tilstande", "Blokeret (gul), motorlæring (violet, bjælken viser læringsprocent), mangler læring (gult !), opladning planlagt af Touch (↑) og opladning der ikke slår til (gul ↑). Skiltet tager afvigelses-chippens plads.", states_demo, states_code,
       ["Statusord i værdifeltet («Blokeret», «Lærer 60 %»)", "Skilte med solid fyld-tone og on-…-tekst"],
       ["Gul til «ikke godkendt endnu» uden advarselstekst", "Firmwarens CALIBRATING på en stille zone som «lærer»"]),
  comp("Hierarkisk zonestrimmel (Touch)", "Hus + manifolds på niveau 1; zoner i .substrip, når en manifold eller zone er valgt. Mini-søjlerne er ventilåbning (neutral); fejl rød, blokeret gul, læring violet.", tiers_demo,
       '<nav class="strip strip--tiers">…tile-sys + tile-manifold…</nav>\n<nav class="substrip" data-m="1" style="--sub-n:6">…zonefelter…</nav>',
       ["Max 5 felter på niveau 1 (hus + 4)", "Understrimmel = V6-zonefelter", "Forælder-valgt: inset ring, ikke inverteret"],
       ["Flad liste med 8+ zoner", "Vandret scroll, wrap eller overlay-menu"]),
  comp("Plan vs. virkelighed", "Søjlegraf .bars: planlagt (neutral) og faktisk (varme). Akser og forklaring i HTML.", bars_demo,
       '<div class="bars" style="--bars-n:12"><div class="bars-plot">…<div class="col"><i class="plan" style="--plan:40"></i><i class="act" style="--act:32"></i></div>…</div></div>',
       ["Planlagt neutral, faktisk accent (varme)", "Tekst uden for søjlerne"], ["Chart-bibliotek", "Samme farve til plan og faktisk"], cls="c6"),
  comp("Tom graf og manglende værdier", "Uden data klapper grafen sammen til én .empty-linje (data-empty på beholderen). Manglende værdier er «—», aldrig 0.", empty_demo,
       '<div class="sub trend-wrap" data-empty><h4>…</h4><p class="empty">…</p><svg class="trend">…</svg></div>\n<dd>—</dd>',
       ["«—» for null fra firmwaren", "Farve kun på en rigtig måling"], ["0 eller 0,0 for en manglende måling", "Flad graf uden data"], cls="c6"),
  comp("Typeafhængige felter", "Lokale radioer + .typed-fields / .hs-fields. Synlighed uden JS; reglerne genereres af lds_build.py fra heat_source_types og typed_groups.", hs_demo,
       '<input class="state" type="radio" name="hs_type" id="hs-http" value="http">\n…\n<div class="seg" role="radiogroup"><label for="hs-http"><span>…</span></label>…</div>\n<fieldset class="hs-fields typed-fields" data-type="http">…</fieldset>',
       ["Kun firmware-felter", "Første type i heat_source_types er standard", "Radioer, .seg og fieldsets har samme forælder", "Ved 4+ typer: .select"],
       ["JS til at skifte typefelter", "Opfundne felter"], cls="c6"),
  comp("Sektioner, sektionslinks og hjælp", "Konfigurationssider i V6/Touch, indtil de er flyttet til ark/System (afsnit 15). 3+ sektioner får .section-nav. Kun Service/Udvikler foldes. Ét ? pr. panel åbner en native popover.", sections_demo,
       '<nav class="section-nav">…</nav>\n<section class="section"><h2 class="section-h">…<i class="section-h-dot"></i><i class="section-h-line"></i></h2><div class="section-grid">…</div></section>\n<button class="help-btn" type="button" popovertarget="help-x" aria-label="Hjælp: …">?</button><div id="help-x" popover class="help-pop">…</div>',
       ["Alene-panel uden cN + .subs.cols-2", "Underafsnit-handlinger; footer kun Gem", "Maks. 5 sektioner", "Hjælp: 2–3 sætninger + «Læs mere»"],
       ["Tom halv kolonne", "Gem-knap i underafsnit", "Hjælp på hover", "Orange ?"]),
  comp("Tilstandsafhængige handlinger", "data-state på panelet; data-show-when på handlingerne. Næste skridt er primær.", state_demo,
       '<form class="panel" data-state="unpaired">…<button class="btn primary" data-show-when="unpaired">Godkend …</button><button class="btn danger" data-show-when="approved error">Frakobl …</button>…</form>',
       ["Kun relevante handlinger", "Godkend er primær når unpaired"], ["Godkend og Frakobl samtidig"], cls="c6"),
  comp("Test af forbindelser", "Testknappen viser altid et .test-result. To linjer. Bliver stående til næste test. Gemmes ikke.", test_demo,
       '<div class="test-result" aria-live="polite"><div class="msg ok"><span><b>Læsning OK · 14:32:05 · 200 · 84 ms</b>…</span></div></div>',
       ["aria-live=polite", "Første linje: handling, tid, kode, varighed", "Anden linje: hvad der blev læst eller sendt"],
       ["Resultat kun i konsollen", "Gemme testresultatet", "Rå fejlkode som eneste tekst"], cls="c6"),
  comp("Sendes til", "Live-tabel over det, der faktisk afsendes. Samme tal som på dashboardet. Synkronisering slået fra: dæmpet forklaring.", sends_demo,
       '<table class="table">…vægtet temperatur, komfort-setpunkt…</table>\n<details class="more"><summary>Hvordan beregnes den?</summary>…</details>',
       ["Værdier fra samme kilde som dashboardet", "Bidragene summerer til den viste temperatur"], ["Opfundne felter", "Rå status som «trusted»"]),
]

_items_main.append(
  comp("Plan (Touch)", "Odins planlagte varme og Touch' forvarme/opladning, 24 t frem. Alt i varme-domænet; serierne adskilles med mønster, ikke kulør.", plan_demo,
       '<div class="plan">…<div class="plan-lane plan-lane--odin"><i class="plan-bar" style="--v:55" data-mode="dhw"></i>…</div>…</div>',
       ["Rumvarme solid, varmt vand skraveret + kant, legionella kryds-skraveret + kant", "title-tekst på hvert segment"], ["Blå til varmt vand", "Jævne orange flader under 30 %"]))

_items = ([
  comp("Zonestrimmel og zonefelt", "Overblik og omfangs-navigation i ét. Neutralt felt: afvigelse fra mål som chip i 5 trin, ventilåbning som neutral bjælke, grupper i violet. Valgt felt inverteres.", strip_demo, strip_code,
       ["Ventilbjælke 0–10 + procent (neutral); tændt zone mindst 1", "Afvigelse som chip, ikke farvet felt", "Fejl som ord i værdifeltet (\"Fejl\")"],
       ["Orange til ventilåbning eller grupper", "Farvede felter eller kanter", "Flere end 8 felter i standard-strimlen"]),
  comp("Navbar: navigation og sprog", "Midten har altid Hjem og System (tidligere Dashboard/Konfiguration). Sprogvælgeren vises kun, når buildet har flere sprog.", mode_demo, mode_code,
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
  comp("Klima-kontrol", "Zone-dashboardets hero: aktuel temperatur og målet mellem to store knapper.", climate_demo, '<div class="climate"><div class="now">21,4<small>°C</small></div><div class="target">…</div></div>',
       ["Én pr. visning", "Deaktiveret med note på grupperede medlemmer"], ["Til konfigurationsværdier (brug .stepper)"], cls="c6"),
  comp("Komfortliste med sparkline", "Aktuel/mål og 24-timers graf pr. zone. Rækken åbner zonen.", comfort_demo, '<label for="s-z1" data-state="calling"><span class="id">Z1</span><span class="name">…</span><span class="val">…</span><svg class="spark" …/></label>',
       ["Mål som trappekurve", "Akse mindst 3 °C", "Gul værdi når > 0,5 °C under mål"], ["Tekst inde i SVG'en"], cls="c6"),
  comp("Trendgraf", "SVG kun til figurer; akser og forklaring i HTML. Farver efter betydning.", trend_demo, '<svg class="trend" viewBox="0 0 240 80" preserveAspectRatio="none" role="img" aria-label="…"><polyline class="f" points="…"/><polyline class="r" points="…"/></svg>',
       ["role=img og en aria-label med pointen", "non-scaling strokes"], ["Chart-biblioteker", "Tekst i SVG"], cls="c6"),
  comp("Grupperede lister", "Sådan præsenteres alle indstillinger (i ark og på System): lille overskrift, én flade med rækker, label og hint til venstre, kontrol til højre med naturlig bredde. Ingen kort.", fields_demo, fields_code,
       ["Maks. 6 rækker pr. gruppe, 5 grupper pr. side", "Stepper 140 px, host/MAC 26ch, port 9ch", "2–3 valg: segment; flere: select", "Afhængige rækker under deres switch"],
       ["Indstillinger i bento-kort", "Kontroller strakt til fuld bredde", "Grøn switch", "Felter uden label"]),
  comp("Kompas", "Ydervægge som fire checkboxe. Bogstaverne kommer fra sprogkataloget.", compass_demo, '<div class="compass" role="group" aria-label="Ydervægge"><i class="c"></i><label data-wall="n"><input type="checkbox" aria-label="Nord"><span>N</span></label>…</div>',
       ["aria-label med fuldt navn"], ["Tekst uden sprogkatalog"], cls="c4"),
  comp("Knapper og bekræftelse", "Én primær pr. panel. Destruktive handlinger bag en indlejret bekræftelse.", btn_demo, '''<button class="btn primary" type="submit">Gem regulering</button>
<button class="btn danger" type="button" popovertarget="cf-bal">Nulstil balancering…</button>
<div id="cf-bal" popover class="confirm-pop" role="alertdialog" aria-labelledby="cf-bal-t">
  <h4 id="cf-bal-t">Nulstil balancering?</h4>
  <p>Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p>
  <div class="actions">
    <button class="btn" type="button" popovertarget="cf-bal" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="submit" name="action" value="reset_balancing">Nulstil</button>
  </div>
</div>''', ["Verbum + objekt", "\"…\" på knapper der åbner en bekræftelse"], ["\"OK\", \"Send\"", "Inline-udfoldning"], cls="c8", stage_cls="row"),
  comp("Tabel og log", "Tal højrestillet. Loggen er engelsk (fra firmwaren) og farver kilder efter betydning.", table_demo, '<div class="table-wrap"><table class="table">…<td class="num">1,08</td>…</table></div>\n<pre class="log" lang="en">…</pre>',
       ["table-wrap for vandret scroll"], ["Tabeller til layout"]),
])

def grp(id_, title, intro):
    return f'<header class="doc-h2" id="{id_}"><h3>{title}</h3><p>{intro}</p></header>'
_I=_items; _M=_items_main

# ---- Ark, faner, gem-bjælke, underside og System (DESIGN.md 15) ----
ICON_ROOM = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="3"/><path d="M4 12h8V4"/></svg>'
def srow(label, control, hint="", cls=""):
    h = f"<small>{hint}</small>" if hint else ""
    return f'<div class="setting{(" " + cls) if cls else ""}"><div class="setting-label">{label}{h}</div><div class="setting-control">{control}</div></div>'
def sstep(id_, val, unit, label):
    return f'<div class="stepper"><button type="button" data-step="-1" aria-label="Sænk {label}">−</button><span class="value"><input type="number" id="{id_}" name="{id_}" value="{val}" step="0.5"><span class="unit">{unit}</span></span><button type="button" data-step="1" aria-label="Hæv {label}">+</button></div>'
def ssub(label, value, body):
    return (f'<details class="subpage"><summary class="setting"><span class="sub-back">Tilbage</span><span class="setting-label"><span>{label}</span></span>'
            f'<span class="setting-control"><span class="muted">{value}</span></span></summary><div class="subpage-body">{body}</div></details>')
def sgroup(title, rows, extra=""):
    return f'<section class="setting-group"><h4>{title}</h4><div class="setting-list">{rows}</div>{extra}</section>'

_z1_settings = "".join([
  sgroup("Komfort", srow('<label for="z1-target">Mål</label>', sstep("z1-target", "22.0", "°C", "mål"), "Gemmes også fra overblikket"),
         '<div class="setting-list gated">'
         '<label class="setting switch"><span class="setting-label"><b style="font-weight:500">Natsænkning</b><small>22–06</small></span><input type="checkbox" role="switch" name="z1-night" checked></label>'
         '<div class="gated-body">' + srow('<label for="z1-setback">Sænk med</label>', sstep("z1-setback", "1.0", "°C", "sænkning")) + '</div></div>'),
  sgroup("Rum", srow('<label for="z1-area">Areal</label>', sstep("z1-area", "12.0", "m²", "areal")) +
         srow('<label for="z1-sensor">Temperaturføler</label>', '<select class="select" id="z1-sensor" name="z1-sensor" style="width:200px"><option>BLE · Shelly H&amp;T</option><option>Probe 3</option></select>')),
  sgroup("Gulv", srow('<label for="z1-cc">Rørafstand</label>', sstep("z1-cc", "150", "mm", "rørafstand"), "C-C") +
         srow('<label for="z1-pipe">Rørtype</label>', '<select class="select" id="z1-pipe" name="z1-pipe" style="width:180px"><option>PEX 16 mm</option><option>ALUPEX 16 mm</option></select>')),
  sgroup("Avanceret",
         ssub("Motor og kalibrering", "Lært", sgroup("Motor", srow('<label for="z1-endstop">Endestop-timeout</label>', sstep("z1-endstop", "45", "s", "timeout")))) +
         ssub("Gruppering", "Ingen", sgroup("Gruppe", srow('<label for="z1-group">Primær zone</label>', '<select class="select" id="z1-group" name="z1-group" style="width:160px"><option>Ingen</option><option>Z4</option></select>'))),
         '<p class="note">Sjældne indstillinger åbner i samme ark.</p>'),
  '<div class="actions"><button class="btn danger" type="button" popovertarget="doc-cf-z1">Nulstil og genlær Z1…</button></div>',
  '<div id="doc-cf-z1" popover class="confirm-pop" role="alertdialog" aria-labelledby="doc-cf-z1-t"><h4 id="doc-cf-z1-t">Nulstil og genlær Z1?</h4><p>Motoren kører til endestop og lærer forfra. Zonen varmer ikke imens.</p>'
  '<div class="actions"><button class="btn" type="button" popovertarget="doc-cf-z1" popovertargetaction="hide" autofocus>Annullér</button><button class="btn danger-solid" type="button" popovertarget="doc-cf-z1" popovertargetaction="hide">Nulstil</button></div></div>',
  '<footer class="savebar"><span class="save-status" id="ss-doc-z1" aria-live="polite"></span><button type="reset" class="btn">Fortryd</button><button class="btn primary" type="submit">Gem</button></footer>',
])

sheet_demo = (
  '<div class="actions"><button class="btn" type="button" popovertarget="sheet-z1">Åbn Josephine</button>'
  '<button class="btn" type="button" popovertarget="sheet-z1" data-tab="settings">Indstillinger for Josephine</button></div>'
  '<div id="sheet-z1" popover class="sheet" role="dialog" aria-labelledby="sheet-z1-t" data-hash="z1">'
  '<input class="state tab" type="radio" name="tab-z1" id="tab-z1-o" value="overview" data-hash="overblik" checked aria-label="Overblik">'
  '<input class="state tab" type="radio" name="tab-z1" id="tab-z1-h" value="history" data-hash="historik" aria-label="Historik">'
  '<input class="state tab" type="radio" name="tab-z1" id="tab-z1-s" value="settings" data-hash="indstillinger" aria-label="Indstillinger">'
  '<header class="sheet-head">'
  f'<span class="chip-icon" aria-hidden="true">{ICON_ROOM}</span>'
  '<div><h2 id="sheet-z1-t">Josephine</h2><p>1. sal · Z1 · 22,9° · 62 % åben</p></div>'
  '<button class="sheet-close" type="button" popovertarget="sheet-z1" popovertargetaction="hide" aria-label="Luk">×</button>'
  '<nav class="tabs" aria-label="Faner"><label for="tab-z1-o" data-tab="overview">Overblik</label><label for="tab-z1-h" data-tab="history">Historik</label><label for="tab-z1-s" data-tab="settings">Indstillinger</label></nav>'
  '</header><div class="sheet-body">'
  f'<section class="tab-panel" data-tab="overview">{climate_demo}'
  '<dl class="kv"><div><dt>Ventil</dt><dd>62 % åben</dd></div><div><dt>Retur</dt><dd class="c-info">27,4 °C</dd></div><div><dt>Status</dt><dd>Kalder på varme</dd></div></dl></section>'
  f'<section class="tab-panel" data-tab="history">{trend_demo}</section>'
  f'<section class="tab-panel" data-tab="settings"><form data-save="doc-z1">{_z1_settings}</form></section>'
  '</div></div>')

sheet_code = """<div id="sheet-z1" popover class="sheet" role="dialog" aria-labelledby="sheet-z1-t" data-hash="z1">
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-o" value="overview" data-hash="overblik" checked>
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-h" value="history" data-hash="historik">
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-s" value="settings" data-hash="indstillinger">
  <header class="sheet-head">
    <span class="chip-icon" aria-hidden="true"><svg …/></span>
    <div><h2 id="sheet-z1-t">Josephine</h2><p>1. sal · Z1 · 22,9° · 62 % åben</p></div>
    <button class="sheet-close" type="button" popovertarget="sheet-z1" popovertargetaction="hide" aria-label="Luk">×</button>
    <nav class="tabs"><label for="tab-z1-o" data-tab="overview">Overblik</label>…</nav>
  </header>
  <div class="sheet-body">
    <section class="tab-panel" data-tab="overview">…</section>
    <section class="tab-panel" data-tab="history">…</section>
    <section class="tab-panel" data-tab="settings">
      <form data-save="z1">
        <section class="setting-group">…</section>              <!-- maks. 5 grupper -->
        <details class="subpage"><summary class="setting">…</summary><div class="subpage-body">…</div></details>
        <button class="btn danger" popovertarget="cf-z1">Nulstil og genlær Z1…</button>   <!-- altid sidst -->
        <footer class="savebar"><span class="save-status" aria-live="polite"></span>
          <button type="reset" class="btn">Fortryd</button><button class="btn primary" type="submit">Gem</button></footer>
      </form>
    </section>
  </div>
</div>
<button popovertarget="sheet-z1" data-tab="settings">…</button>   <!-- åbner på Indstillinger (JS) -->"""

def syscat(cat, title, body, badge=""):
    b = f'<span class="badge ok">{badge}</span>' if badge else ""
    return (f'<section class="sys-cat" data-cat="{cat}"><label class="sys-back" for="c-none">System</label><header><div><small>System</small><h2>{title}</h2></div>{b}</header>'
            f'<form data-save="doc-sys-{cat}">{body}<footer class="savebar"><span class="save-status" aria-live="polite"></span><button type="reset" class="btn">Fortryd</button><button class="btn primary" type="submit">Gem</button></footer></form></section>')
sys_demo = ('<input class="state" type="radio" name="syscat" id="c-none" checked aria-label="Kategorier">'
  '<input class="state" type="radio" name="syscat" id="c-device" data-hash="enhed" aria-label="Enhed">'
  '<input class="state" type="radio" name="syscat" id="c-network" data-hash="netvaerk" aria-label="Netværk">'
  '<input class="state" type="radio" name="syscat" id="c-service" data-hash="service" aria-label="Service">'
  '<div class="sys"><nav class="sys-nav" aria-label="Kategorier"><label for="c-device">Enhed</label><label for="c-network">Netværk</label><label for="c-service">Service</label></nav><div class="sys-main">'
  + syscat("device", "Enhed", sgroup("Identitet", srow('<label for="sd-name">Navn</label>', '<input class="input w-sm" id="sd-name" name="sd-name" value="Stueetage">') + srow('<label for="sd-loc">Placering</label>', '<input class="input w-sm" id="sd-loc" name="sd-loc" value="Bryggers">')))
  + syscat("network", "Netværk", sgroup("Forbindelse", srow('<label for="sn-host">Host</label>', '<input class="input w-md" id="sn-host" name="sn-host" value="lune-v6.local">') + srow('<label for="sn-port">Port</label>', '<input class="input w-xs" id="sn-port" name="sn-port" type="number" value="80">')), "Online")
  + syscat("service", "Service", sgroup("Diagnostik", srow('<span>Oppetid</span>', '<span>3 d 4 t</span>')))
  + '</div></div>')
sys_code = """<input class="state" type="radio" name="syscat" id="c-none" checked>
<input class="state" type="radio" name="syscat" id="c-device" data-hash="enhed">
…
<div class="sys">
  <nav class="sys-nav"><label for="c-device"><svg …/>Enhed</label>…</nav>
  <div class="sys-main">
    <section class="sys-cat" data-cat="device">
      <label class="sys-back" for="c-none">System</label>      <!-- kun mobil -->
      <header><div><small>System</small><h2>Enhed</h2></div></header>
      <form data-save="device">…grupper… <footer class="savebar">…</footer></form>
    </section>
  </div>
</div>
<!-- config: "modes": ["home","sys"], "systemCategories": ["device", …] -->"""

_S = [
  comp("Ark med faner og gem-bjælke", "Alt om én ting: Overblik · Historik · Indstillinger. Native popover (Esc og klik udenfor lukker); højre side på desktop, fra bunden på mobil. Fanerne er radioer; Indstillinger er grupperede lister med én gem-bjælke. «Avanceret ›» åbner en underside i samme ark. Deep link: #z1/indstillinger.", sheet_demo, sheet_code,
       ["Ét ark pr. ting; åbnes fra tingen", "Maks. 5 grupper og 6 rækker pr. gruppe", "Farlig handling sidst, med .confirm-pop", "Gem-bjælke: clean = neutral, ingen Fortryd"],
       ["Ark i ark", "Indstillinger der gælder hele enheden (de hører på System)", "Gem-knap pr. gruppe"]),
  comp("System-side", "Kategoriliste (240 px, klæber) og indhold (maks. 640 px). Én kategori ad gangen, én gem-bjælke pr. kategori. Mobil: listen er en skærm; en kategori har «‹ System» øverst. Virker uden JS. Deep link: #system/netvaerk (kræver #m-sys på en rigtig side).", sys_demo, sys_code,
       ["Kategorier fra systemCategories i config", "Én formular pr. kategori"], ["Indstillinger for én ting i huset (de hører i dens ark)", "Kort/paneler til indstillinger"]),
]
# ---- Hjem: hovedsektion, termostat-ring, felter og varmekort (DESIGN.md 15.10) ----
def _rt(name, z, t, d, lvl, pct, st="idle"):
    k = 1 if d <= -1 else 2 if d <= -0.3 else 3 if d < 0.3 else 4 if d < 1 else 5
    chip = "" if st == "fault" else f'<span class="tile-dev" data-dev="{k}">{"+" if d > 0 else "−"}{str(abs(d)).replace(".", ",")}°</span>'
    val = "Fejl" if st == "fault" else f'{str(t).replace(".", ",")}°'
    return (f'<button class="tile" type="button" data-state="{st}" data-level="{lvl}">{LVL}<span class="tile-pct">{pct} %</span>'
            f'<span class="tile-id">{z}</span><span class="tile-name">{name}</span>{chip}<span class="tile-val">{val}</span></button>')
home_demo = ('<section class="home-hero"><div><small>God morgen</small><h2>Varmt nok overalt.</h2><p>Huset er 0,9° over målet. Odin planlægger næste varme kl. 06.</p></div>'
  '<form class="thermo" data-save="doc-house"><div class="thermo-ring" style="--v:60;--now:69" role="img" aria-label="Huset er 21,9 °C, målet er 21,0 °C">'
  '<svg viewBox="0 0 100 100" aria-hidden="true"><circle class="trk" cx="50" cy="50" r="44" pathLength="100"/><circle class="arc" cx="50" cy="50" r="44" pathLength="100"/><circle class="now" cx="50" cy="50" r="44" pathLength="100"/></svg>'
  '<div class="thermo-val"><b>21,9<small>°</small></b><span>Huset nu</span></div></div>'
  '<div class="climate"><div class="target"><button type="button" data-step="-1" aria-label="Sænk husets mål">−</button><label class="value"><small>Mål °C</small><input type="number" value="21.0" step="0.5" name="doc_house_target"></label><button type="button" data-step="1" aria-label="Hæv husets mål">+</button></div><p class="autosave" aria-live="polite"></p></div></form></section>'
  '<div class="home-tiles">'
  '<button class="home-tile" type="button"><span class="chip-icon" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M12 3c3 4 5 6.5 5 10a5 5 0 0 1-10 0c0-2 1-3.5 2-5 .5 2 1.5 3 3 3-1-3 0-6 0-8z"/></svg></span><b>Varme</b><span class="ht-status">Hviler · Odin styrer</span>'
  '<span class="ht-val">36,0° → 29,0° <small>fremløb → retur</small></span><svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true"><polyline class="f" points="0,20 60,16 120,12 180,18 240,22"/><polyline class="r" points="0,34 60,32 120,30 180,33 240,35"/></svg></button>'
  '<button class="home-tile" type="button"><span class="chip-icon" data-tone="violet" aria-hidden="true"><svg viewBox="0 0 24 24"><rect x="4" y="5" width="16" height="15" rx="2"/><path d="M4 10h16"/></svg></span><b>Næste varme</b><span class="ht-status">Planlagt af Odin</span>'
  '<span class="ht-val">kl. 06–09 <small>≈ 8,1 kWh</small></span><span class="ht-price">Elpris nu 1,82 kr/kWh <span class="scale-chip" data-scale="2">Billig</span></span>'
  '<svg class="ht-viz" viewBox="0 0 240 48" preserveAspectRatio="none" aria-hidden="true"><rect class="col" x="1" y="46" width="8" height="2"/><rect class="col on" x="141" y="18" width="8" height="30"/><rect class="col on" x="151" y="2" width="8" height="46"/><rect class="col on" x="161" y="13" width="8" height="35"/></svg></button>'
  '<button class="home-tile" type="button" data-empty><span class="chip-icon" data-tone="neutral" aria-hidden="true"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/></svg></span><b>Cirkulation</b><span class="ht-status">Ingen data fra pumpen</span><span class="ht-val">— <small>l/min</small></span><svg class="ht-viz" aria-hidden="true"></svg></button>'
  '</div>'
  '<div class="heatmap"><section class="room-group"><button class="room-group-head" type="button">Teknikrum <small>M1 · 36,0° → 29,0°</small></button><div class="room-grid">'
  + _rt("Kontor", "Z1", 22.9, 0.9, 2, 18) + _rt("Stue", "Z5", 21.4, -0.1, 6, 54, "calling") + _rt("Gang", "Z4", 19.1, -0.9, 0, 0, "fault") +
  '</div></section><section class="room-group" data-offline><button class="room-group-head" type="button">Anneks <small>M3 · 30,5° → 27,8°</small></button>'
  '<p class="offline-note">V6 er ikke tilgængelig · senest set for 2 t siden</p><div class="room-grid">' + _rt("Værksted", "Z1", 16.8, -1.2, 2, 20) +
  '</div></section><p class="heatmap-note">Chippen viser afstand til målet, bjælken hvor meget ventilen er åben.</p></div>')
home_code = """<section class="home-hero">
  <div><small>God morgen</small><h2>Varmt nok overalt.</h2><p>Én sætning om situationen.</p></div>
  <form class="thermo" data-save="house-target">
    <div class="thermo-ring" style="--v:60;--now:69" role="img" aria-label="…">   <!-- --v = mål, --now = aktuel (0–100 % af skalaen) -->
      <svg viewBox="0 0 100 100"><circle class="trk" … pathLength="100"/><circle class="arc" …/><circle class="now" …/></svg>
      <div class="thermo-val"><b>21,9<small>°</small></b><span>Huset nu</span></div>
    </div>
    <div class="climate"><div class="target">− <input name="house_target"> +</div><p class="autosave"></p></div>   <!-- autogem -->
  </form>
</section>
<div class="home-tiles">            <!-- maks. 4 pr. række -->
  <button class="home-tile" popovertarget="sheet-heat">
    <span class="chip-icon">…</span><b>Varme</b><span class="ht-status">…</span>
    <span class="ht-val">36,0° → 29,0° <small>…</small></span>
    <svg class="ht-viz">…</svg>       <!-- én visualisering; data-empty skjuler den -->
  </button>
</div>
<div class="heatmap">
  <section class="room-group" data-offline>   <!-- V6 offline: dæmpet + .offline-note -->
    <button class="room-group-head" popovertarget="sheet-m3">Anneks <small>M3 · …</small></button>
    <div class="room-grid"><button class="tile" popovertarget="sheet-r11" data-level="2">…</button></div>
  </section>
</div>"""
_S.append(comp("Hjem: hovedsektion, felter og varmekort", "Touch' Hjem (15.7/15.10): én hovedsektion med husets temperatur og mål i termostat-ringen (autogem), højst fire felter (ét tal + én visualisering; hele feltet åbner arket) og varmekortet med rum pr. styring. Uden data: «—» og ingen visualisering.", home_demo, home_code,
       ["Ringen har én farve (varme); mærket er den aktuelle temperatur", "Prisen som én linje + skala-chip", "Offline-styring: dæmpet + tekst"],
       ["Flere end 4 felter i en række", "Flere tal eller grafer i et felt", "Farvede rumfelter"]))

components = section("komponenter", "Komponenter", "Levende eksempler med den rigtige CSS, grupperet efter hvor de bruges. Fold Markup ud for koden. Fuld beskrivelse i DESIGN.md afsnit 5 og 15.", "".join([
  grp("k-navigation","Navigation","Navbar, zonestrimmel og ark: hvor man er, og hvad man ser på."), _I[1], _I[0], _M[0], _M[1], _S[0],
  grp("k-hjem","Indhold på Hjem","Overblik og hverdagshandlinger. Kort bruges her, fordi indholdet er kort."), _S[2], _I[2], _I[6], _I[4], _I[7], _I[8], _M[2], _M[9], _M[3], _I[12],
  grp("k-indstillinger","Indstillinger","I ark og på System. Altid grupperede lister, aldrig kort. Sektioner bruges i V6/Touch' konfiguration indtil migreringen."), _I[9], _S[1], _M[4], _I[10], _M[5],
  grp("k-feedback","Feedback og handlinger","Status, beskeder, knapper og bekræftelser."), _I[3], _I[5], _I[11], _M[6], _M[7], _M[8],
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
        <p class="note">Prøv: ændr felterne (prik + «2 ændringer ikke gemt»), Fortryd eller Gem. Knappen nedenfor får næste gem til at fejle.</p>
        <div class="actions"><button class="btn" type="button" id="demo-fail-toggle">Næste gem fejler</button></div>
      </section>
      <section class="panel c6"><header class="panel-head"><h3>Gem og fejl</h3></header>
        <dl class="kv">
          <div><dt>Clean/dirty</dt><dd>form[data-js], prikker, Fortryd, aria-disabled — DESIGN.md 6.1</dd></div>
          <div><dt>Autogem</dt><dd>Hjem/Dashboard .climate, 1,5 s debounce; ingen gem-knap</dd></div>
          <div><dt>Fejl nu</dt><dd>.panel.alert øverst + rød i strimmel og liste</dd></div>
          <div><dt>Snart</dt><dd>.msg.warn eller c-warn på værdien</dd></div>
          <div><dt>Destruktivt</dt><dd>.confirm-pop; titlen er spørgsmålet, knappen er verbet — aldrig OK</dd></div>
          <div><dt>Offline</dt><dd>badge i enhedspanelet; frys værdier, tøm dem ikke</dd></div>
        </dl></section>
      <section class="panel c6"><header class="panel-head"><h3>Sprog og tal</h3></header>
        <dl class="kv">
          <div><dt>Tone</dt><dd>Saglig, kort, aktiv form, sentence case</dd></div>
          <div><dt>Knapper</dt><dd>Verbum + objekt: "Gem mål" → "Gemt"</dd></div>
          <div><dt>Decimaltegn</dt><dd>Efter sprog: 21,4 / 21.4</dd></div>
          <div><dt>Enheder</dt><dd>Mellemrum: 21,4 °C, 6 m/s — kort grad i felter: 21,4°</dd></div>
          <div><dt>i18n</dt><dd>Build-time, --langs en,da, alle aria-labels oversat</dd></div>
        </dl></section>
      <section class="panel"><header class="panel-head"><h3>ESP32-budget</h3><span class="badge ok">58,7 kB · budget 69 kB</span></header>
        <dl class="metrics">
          <div class="metric"><dt>CSS</dt><dd>21,0 <small>kB gzip</small></dd></div>
          <div class="metric"><dt>Side pr. sprog</dt><dd>16,8 <small>kB gzip uden grafpunkter (25,2 med)</small></dd></div>
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
{hs_css}
</style>
</head>
<body>
<input class="state" type="checkbox" id="theme" aria-label="Skift tema">
<div class="app doc">
{(ROOT/"css/lds-svg-defs.html").read_text(encoding="utf-8")}
  <div class="navbar-wrap wrap">
    <header class="header" style="grid-template-columns:1fr auto">
      <div class="device"><summary style="pointer-events:none"><svg class="logo" viewBox="0 0 32 32" aria-hidden="true"><circle cx="16" cy="16" r="15" fill="var(--fg)"/><path d="M10 22V12M14 22V10M18 22V13M22 22V11" stroke="var(--accent)" stroke-width="2.4" stroke-linecap="round"/></svg><span class="name"><b>Lune Design System</b><small>v{T["$meta"]["version"]}</small></span></summary></div>
      <label class="icon-btn theme-btn" for="theme" title="Skift lyst/mørkt tema">
        <svg class="i-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5 19 19M5 19l1.5-1.5M17.5 6.5 19 5"/></svg>
        <svg class="i-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/></svg>
      </label>
    </header>
  </div>

  <main class="content wrap">
    <div class="doc-layout"><nav class="doc-toc" aria-label="Indhold"><b>Grundlag</b><a href="#principper">Principper</a><a href="#arkitektur">Arkitektur</a><a href="#farver">Farver</a><a href="#grundelementer">Typografi og afstand</a><b>Komponenter</b><a href="#k-navigation">Navigation</a><a href="#k-hjem">Indhold på Hjem</a><a href="#k-indstillinger">Indstillinger</a><a href="#k-feedback">Feedback og handlinger</a><b>Mønstre</b><a href="#moenstre">Mønstre og indhold</a></nav>
    <div class="view"><span id="principper"></span>{intro}{shell}{colors}{found}{components}{patterns}</div></div>
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
page = page.replace("__LUNE_FORMS__", (ROOT/"js"/"lune-forms.js").read_text(encoding="utf-8"))
out = ROOT/"docs/design-system.html"
out.write_text(page, encoding="utf-8")
print(f"{out}  ({len(page)/1024:.0f} kB)")
