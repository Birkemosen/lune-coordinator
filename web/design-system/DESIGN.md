# Lune Design System 2

Fælles designsystem for **Lune V6** (6-zoners manifold-controller) og **Lune Touch** (koordinator for 1–4 V6-boards). Dokumentet beskriver *hvorfor* og *hvordan*. Værdierne står i `tokens/tokens.json`, koden i `css/lune-ui.src.css`, og en visuel reference med levende eksempler i `docs/design-system.html`.

Når dette dokument og koden er uenige, er det en fejl i et af dem. Ret den ene, så de passer igen.

---

## 1. Principper

**1. Et blik er nok.** Brugeren står ved manifolden eller kigger kort på telefonen. De vigtigste tal er store, labels er små og står under tallet. Én aktuel temperatur pr. visning må være hero-størrelse.

**2. Farve betyder noget.** Alt er neutralt, indtil der er noget at sige. Orange betyder varme, og hver statusfarve har præcis én betydning (se 3.2). En farve bruges aldrig til pynt.

**3. Samme sted, samme ting.** Zonestrimlen står øverst i alle visninger og vælger *hvad* man ser på. Tilstandspillen vælger *hvordan*: Dashboard (se og styre) eller Konfiguration (opsætning). Z3 er altid det samme sted.

**4. Få store paneler, altid åbne.** Indhold grupperes i 2–4 paneler pr. visning. I Konfiguration grupperes paneler yderligere i sektioner (4.3). Kun det sjældne og det risikable foldes (Service/Udvikler, eller `.more` til ekspert-tuning).

**5. Tilstand uden JavaScript.** Navigation, faner, tema, sektionsfold og fold-ud er ren HTML/CSS. JavaScript bruges kun til live-data, +/−-knapper, formularer (ugemt/gem, autogem) og at åbne en lukket sektion ved ankerlink, og alt virker (med færre bekvemmeligheder) uden.

**6. Tilgængelig fra start.** Al tekst har mindst 4,5:1 kontrast i begge temaer. Trykflader er mindst 44 px (48 px på touch); med mus på bred skærm er kontroller 36 px høje (3.9). Alt kan betjenes med tastatur og har synlig fokus.

---

## 2. Arkitektur og navigation

> **Erstattet af afsnit 15 (Hjem / ark / System).** Afsnit 2.2–2.3 beskriver den tidligere tilstandsmodel (Dashboard/Konfiguration), som stadig bruges i V6-eksemplet, indtil det er migreret. Nye skærme bygges efter afsnit 15.

### 2.1 Skallen

```
┌───────────────────────────────────────────────────────────────┐
│ [logo Lune V6 ▾]     ( Dashboard | Konfiguration )   [EN|DA] ☾│  header
│ [ System ][ Z1 ][ Z2 ][ Z3 ][ Z4–5 ][ Z5 ][ Z6 ]              │  zonestrimmel
├───────────────────────────────────────────────────────────────┤
│ Titel på omfang   undertitel                                  │  view-head
│ ┌─────────── c5 ──────────┐ ┌──────────── c7 ──────────────┐  │
│ │ panel                   │ │ panel                        │  │  bento-grid
│ └─────────────────────────┘ └──────────────────────────────┘  │  (12 kolonner)
└───────────────────────────────────────────────────────────────┘
```

Der er ingen sidebar. På mobil flytter tilstandspillen ned i bunden af skærmen (tommelfingerhøjde). Header og strimmel klæber øverst på web og tablet, men ikke på mobil.

### 2.2 To akser: tilstand × omfang

| Akse | Vælges med | Værdier | Id-konvention |
|---|---|---|---|
| Tilstand | Tilstandspillen | `dash`, `conf` (altid de to) | `#m-dash`, `#m-conf` |
| Omfang | Zonestrimlen | System + zoner (V6) eller hus → manifold → zone (Touch) | V6: `#s-sys`, `#s-z1` … · Touch: `#s-house`, `#s-m{N}`, `#s-m{N}z{Z}` |
| Visning | Automatisk | én pr. kombination — eller delte visninger når `shared_views` | V6: `#v-{tilstand}-{omfang}`, fx `#v-conf-z3` · Touch: `#v-{tilstand}-house\|manifold\|zone` |

Tilstand og omfang er to radiogrupper, der ligger **før** `.app` i HTML, i denne rækkefølge: tilstand, omfang, tema. CSS'en viser præcis den visning, der matcher begge. Reglerne genereres af `tools/lds_build.py` ud fra projektets config, så Touch kan have flere omfang end V6 uden håndskrevet CSS.

På Touch med `shared_views: true` er der kun seks visninger (`house` / `manifold` / `zone` × tilstand). Binderen fylder indholdet ud fra den checkede radios `id` og `data-m`. Navigationen er stadig ren CSS (`data-kind="manifold|zone"`).

```html
<input class="state" type="radio" name="mode"  id="m-dash" checked aria-label="Dashboard">
<input class="state" type="radio" name="mode"  id="m-conf" aria-label="Konfiguration">
<input class="state" type="radio" name="scope" id="s-sys"  checked aria-label="Manifold">
<input class="state" type="radio" name="scope" id="s-z1"   aria-label="Zone 1">
…
<input class="state" type="checkbox" id="theme" aria-label="Skift tema">
<div class="app"> … <section class="view" id="v-dash-sys"> … </section> … </div>
```

State-inputs har klassen `.state`: visuelt skjulte, men fokuserbare, så piletaster og skærmlæsere virker. Brug aldrig `display: none` på dem.

### 2.3 Hvad hører til hvilken tilstand

| Dashboard | Konfiguration |
|---|---|
| Live-værdier, grafer, status | Opsætning der sjældent ændres |
| Handlinger der hører til hverdagen: måltemperatur, nulstil en fejl der lige er sket | Hardware, følere, grupper, grænser, kalibrering |
| Advarsler og hvad de betyder | Destruktive handlinger (bag bekræftelse) |
| Ingen formularer med mere end ét felt | Formularer i paneler med én gem-knap hver |

Testen: *Ville en beboer gøre det i en almindelig uge?* Ja → Dashboard. Nej → Konfiguration.

### 2.4 Omfang pr. projekt

| | Lune V6 (`config/v6.json`) | Lune Touch (`config/touch.json`) |
|---|---|---|
| System-omfang | `sys`, vist som **System**: hele V6'en (fremløb, retur, motorer, regulering, forbindelser, service). «Manifold» bruges kun om det hydrauliske. | `house`: huset (vægtet hustemperatur, varmekilde, pumpe, energi, vejr) |
| Mellemomfang | — | `m1`–`m4`: V6-boards (manifolds), 1–6 zoner hver |
| Zone-omfang | `z1`–`z6`: fysiske kredse | `m{N}z{Z}`: zone på en manifold (op til 24) |
| Strimmel | standard (én række, alle felter) | `strip--tiers`: hus + manifolds; `.substrip` med zoner når manifold/zone er valgt (5.2.1) |
| Visninger | én pr. omfang × tilstand | `shared_views`: `house` / `manifold` / `zone` × tilstand |
| Varmekilde | — | `heat_source_types`: build-time liste (`http`, `asgard`, …); første er standard. Flere typevalg: `typed_groups` (5.18) |

**Fravalgte alternativer til Touch-strimlen:** vandret scroll eller wrap med op til 24 felter (`strip--many`) åd viewporten eller skjulte indholdet under; en overlay-menu brød «ingen modaler»-reglen. Hierarki med maks. 5 felter på niveau 1 og maks. 6 på niveau 2 holder overblikket uden scroll. `strip--many` findes stadig i CSS'en, men bruges ikke til Touch.

---

## 3. Grundelementer

### 3.1 Farver og temaer

Alle farver er tokens med en lys og en mørk værdi. De skrives som `light-dark(lys, mørk)`, og temaet vælges alene med `color-scheme` på `.app`: OS-temaet som standard, og tema-tjekboksen inverterer det. Skriv **aldrig** hex-værdier i komponent-CSS. Brug tokens.

`light-dark()` accepterer kun farver. Skal noget andet variere med temaet (en skygge, en gennemsigtighed), så lav en farvevariabel og byg værdien på den. `--card-edge` er et eksempel.

| Gruppe | Tokens | Brug |
|---|---|---|
| Flader | `--bg`, `--card`, `--raised`, `--field` | Side → kort → flade i kort → input. Maks. tre niveauer synlige på én gang. |
| Linjer | `--border`, `--field-edge`, `--card-edge` | Skillelinjer i lister, input-kanter, kortkant i lyst tema. Aldrig kant rundt om paneler ud over `--card-edge`. |
| Tekst | `--fg`, `--muted`, `--faint` | Primær, sekundær (labels, enheder), tertiær (akser, små labels). |
| Inverteret | `--inv-bg`, `--inv-fg` | Det valgte og det primære: valgt zonefelt, aktiv tilstand, primær knap, tændt switch, aktivt segment. |
| Varme | `--accent`, `--accent-ink`, `--on-accent-ink`, `--accent-glow`, `--seg-off` | Se 3.2. `--accent` er kun til flader; `--accent-ink` til tekst og badges. |
| Status | `--info`, `--ok`, `--warn`, `--danger`, `--violet` + `-bg`-varianter | Se 3.2. |

### 3.2 Farvernes betydning

| Farve | Token | Betyder | Eksempler | Aldrig |
|---|---|---|---|---|
| Orange | `accent` | Varme og afvigelse fra mål | "Kalder"-badge, afvigelsesflade i grafer, preload-bånd, fremløbskurve | Fejl, links, pynt, **ventilåbning** (den er neutral) |
| Blå | `info` | Vejr, prognoser, sensorer, forbindelser | Vejrbesked, vindgraf, returkurve, "Temperatur fra BLE-sensor" | Handlinger |
| Grøn | `ok` | I orden | "Online", motor "Lært" | Tændt switch (den er sort/hvid) |
| Gul | `warn` | Kræver opmærksomhed snart | Mangler læring, manuel tilstand, zone > 0,5 °C under mål | Fejl der blokerer drift |
| Rød | `danger` | Fejl der kræver handling nu, destruktive handlinger | Motorfejl, "Nulstil og genlær…" | Varme, "koldt" |
| Lilla | `violet` | Læring, kalibrering, gruppering | Adaptiv balancering, lærte faktorer, "Grupperet med Z4" | — |

Regler:
- **Én betydning pr. farve.** Findes der ikke en passende betydning, er elementet neutralt.
- **Farve er aldrig det eneste signal.** Status har altid også tekst ("Motorfejl") eller form (rød prik, rødt segment + tekst).
- **Maks. én fejlflade pr. visning** (`.panel.alert`), øverst. Flere fejl samles i den.

### 3.2.0 Statusfarver: tekst-tone og fyld-tone

Hver statusfarve har to toner:

| Token | Bruges til | Værdi |
|---|---|---|
| `info`, `ok`, `warn`, `danger`, `violet` | **Tekst** og små prikker | Mørkere i lyst tema, lysere og mættet i mørkt, så de altid er ≥ 4,5:1 på kort |
| `info-fill` … `violet-fill` | **Solide flader:** knapper, ikonbrikker, kanter | Apex' værdier i begge temaer: #2E72A0, #358028, #FCAA2D, #B83838, #7050B4 |
| `on-fill` / `on-warn-fill` | Tekst på fyld | Hvid (≥ 4,9:1) / næsten sort på gul (9,7:1) |
| `…-bg` | Beskeder og badges på kort | Afledt tint (se `$tint`) |
| `danger-surface` | Fejlpanel direkte på siden | Lyst: Apex' #FCF0F0; mørkt: afledt tint |

Farven bæres af de **solide** elementer, ikke af store tonede flader. Fejlpanelet har derfor titel i normal tekstfarve, en rød ikonbrik med "!", en solid rød kant og en solid rød knap med hvid tekst.

### 3.2.1 Undgå brunt i mørkt tema

Mørk orange *er* brun. Varme farver med lav dækning oven på en mørk flade blandes derfor til brun eller oliven, og hele UI'et kommer til at virke mudret. Regler:

- **Mørke flader er neutrale** (let kølige grå), ikke varme. Varmen kommer fra accentfarven, ikke fra baggrunden.
- **Orange flader i grafer er glød, ikke tone:** en lodret gradient fra ca. 50 % ved linjen til 0 (`url(#lds-fade-heat)` fra `css/lds-svg-defs.html`). Aldrig en jævn orange flade under 30 %.
- **Perioder og bånd** (preload, planlagte vinduer) markeres med **skravering** (`url(#lds-hatch-heat)`), ikke en svag orange flade.
- **Ikonbrikker er solide** i deres farve med mørk glyf. **Badges** er solide (`.badge.hot`) eller neutrale med farvet prik og tekst.
- **Statusflader** afledes af kortet i OKLab, i mørkt tema lidt lysere end kortet (se `$tint` i tokens).
- `css/lds-svg-defs.html` indsættes én gang pr. side, **inde i** `.app`, så gradienterne følger temaet.

### 3.2.2 Farver i grafer: gradienter og blanding

- **Ingen gradienter mellem to kulører** (fx blå → orange). Komplementære farver blandes til en grå, mudret midte på skærmen. Gradienter går kun inden for én kulør: fra farven til gennemsigtig (glød) eller fra lys til mørk tone.
- **Divergerende skalaer** (koldere/varmere end mål) er to énfarvede ramper, der mødes i neutral: blå ← neutral → orange. Vis dem som trin (fx 5 felter), ikke som én glidende gradient. Skal der interpoleres, så i OKLab (`linear-gradient(in oklab, …)` i CSS; SVG-gradienter interpolerer i sRGB, så tilføj mellemtrin beregnet i OKLab).
- **Kun én fyldt serie pr. graf.** Andre serier er linjer. Overlappende flader i forskellige farver blandes til gråviolet.
- **Serier i samme domæne adskilles med mønster (solid, skraveret, kant), ikke kulør.** Rumvarme, varmt vand og legionella er alle varme: solid `heat-fill`, skravering (`url(#lds-hatch-heat)`) + 1 px kant, og tæt kryds-skravering (`url(#lds-hatch-heat-dense)`) + kant. En anden kulør ville sige, at det er en anden slags ting.
- **Flerfarvede ringe og bjælker er forbudt.** En værdi har én farve efter sin betydning (termostat-ringen: orange for varme).
- **Farvemængde: store flader er neutrale, farve kommer i små, intense doser.** Mængder (ventilåbning, samlet åbning, fremdrift) er neutrale (`--fg`); farve er forbeholdt betydning (varme, afvigelse, status). En væg af farvede felter bliver mat og udvasket; farve virker, når den skiller sig ud fra noget neutralt. Brug farvede chips, kanter (3 px øverst), tal og bjælker frem for farvede felter.
- **Lyse toner til tekst og linjer, dybe toner til flader.** De lyse domæne-/tekst-toner (L ≈ 0,74 i mørkt) ser pastel ud som store flader. Ikonbrikker og knapper bruger de dybe fyld-toner (domænernes lyse-tema-værdier, statusfarvernes `-fill`) med hvid glyf.
- **Datapaletten er afstemt** (se `$data` i tokens): samme lyshed og næsten samme farvestyrke, kun kuløren skifter. En ny datafarve skal ligge i samme bånd.

### 3.3 Kontrast

Kravene står i `tokens.json` under `contrast` og tjekkes med:

```
python tools/lds_build.py --check
```

Tekst ≥ 4,5:1 mod den flade, den står på (primær tekst ≥ 7:1). Kort mod sidebaggrund ≥ 1,24:1 i lyst tema. `--check` dækker også de **afledte** statusflader (tint → færdige hex før måling). Buildet advarer, hvis et token falder under. Nye farvetokens skal have en linje i `contrast`, hvis de bruges som tekst.

### 3.4 Typografi

Én skrifttype: Geist, med systemfonten som fallback (se 9). Vægte: 400 brødtekst, 500 labels og knapper, 600 titler og tal. Ingen andre.

| Token | Størrelse | Brug |
|---|---|---|
| `--fs-xs` | 11 px | Zone-ID, akser, badges, hints |
| `--fs-sm` | 13 px | Labels, knapper, tabeller, undertitler |
| `--fs-md` | 15 px | Brødtekst, feltværdier |
| `--fs-lg` | 18 px | Paneltitler, værdi i zonefelt |
| `--fs-xl` | 24 px | Nøgletal (`.metric`) |
| `--fs-2xl` | 32 px | Visningstitel, mål i klima-kontrol |
| `--fs-hero` | 64 px | Én aktuel temperatur pr. visning |
| `--fs-panel` | 18 px (kompakt 15) | Paneltitel (`h3`) |
| `--fs-view` | 32 px (mobil 18, kompakt 26) | Visningstitel (`h2`) |

Regler:
- Tal bruger altid `font-variant-numeric: tabular-nums` (sat på `.app`), så live-tal ikke hopper.
- Store tal har negativ letter-spacing (−0,02 til −0,05 em); enheden står efter tallet i `<small>` med `--muted` og normal spacing.
- **Sentence case** overalt. Ingen versaler, heller ikke i labels.
- Én hero pr. visning. Hvis to tal konkurrerer om at være vigtigst, er det ene et `.metric`.

### 3.5 Afstand

8-punkts skala med et halvt trin: `--space-1` (4) … `--space-7` (48).

| Hvor | Afstand |
|---|---|
| Mellem kort i grid | `--space-3` (12) |
| Indvendig luft i panel | `--panel-pad` (24; mobil 16; kompakt 18) |
| Mellem blokke i panel | `--panel-gap` (16; kompakt 14) |
| Mellem felter i et underafsnit | `--field-gap` (12; kompakt 6) |
| Label → kontrol | `min(--space-2, --field-gap)` (8; kompakt 6) |
| Sideluft (`--pad`) | 24 web, 16 mobil |

### 3.6 Radier

Radius følger hierarki: jo større flade, jo større radius, og alt man trykker på er en pille.

| Token | Værdi | Brug |
|---|---|---|
| `--r-card` | 16 px | Paneler |
| `--r-tile` | 14 px | Zonefelter |
| `--r-field` | 12 px | Inputs, beskeder, log, dropdown-menu |
| `--r-pill` | 999 px | Knapper, steppere, segmenter, tilstand, badges, sprogvælger |

### 3.7 Dybde

Dybde kommer fra **baggrundskontrast**, ikke skygger. Kort er `--card` på `--bg`, plus `--card-edge` (kun synlig i lyst tema). Skygger bruges kun på elementer, der svæver over indhold: enhedsmenuen, tilstandspillen på mobil og genvejene i Hjems hovedsektion (`.hero-fact`, `--lift-bg` + `--lift`, fordi de ligger over højdekurverne).

### 3.8 Bevægelse

Kun som svar på en handling: farve- og baggrundsskift på 200 ms med `--ease`. Ingen animationer ved indlæsning, ingen scroll-effekter. `prefers-reduced-motion` slår alle overgange fra.

### 3.9 Tæthed og trykflader

Tætheden afgøres af inputtypen. Touch er komfortabel (store flader), mus på en skærm ≥ 1024 px er **kompakt**. Værdierne står i `tokens.json` (`size`, `type` og `density.compact`); `lds_build.py` genererer kompakt-reglerne.

| Token | Standard | Touch (`pointer: coarse`) | Kompakt (mus, ≥ 1024 px) |
|---|---|---|---|
| `--hit` | 44 px | 48 px | 36 px |
| `--field-gap` | 12 px | 12 px | 6 px |
| `--panel-pad` / `--panel-gap` | 24 / 16 px (mobil 16 / 16) | som standard | 18 / 14 px |
| `--fs-panel` / `--fs-view` | 18 / 32 px (mobil 18 / 18) | som standard | 15 / 26 px |
| `--row-h` (`.kv`, tabel) | 40 px | 40 px | 36 px |
| `--tile-h` (zonefelt) | naturlig | naturlig | mindst 52 px |

- `data-density="compact|comfortable"` på `.app` tvinger tætheden (fx til skærmbilleder eller en installatør-indstilling).
- Alle kontroller bruger `--hit` til højde. Inputs har altid mindst 16 px skrift, så iOS/iPadOS ikke zoomer ind.
- Kompakt strammer luft i header, sektioner, beskeder og paneler. Kontrolbredderne er de samme i alle tætheder (naturlig bredde, 15.6). Touch-layoutet ved 390 px er uændret.
- Nye kompakt-regler skrives i `css/lune-ui.src.css` mellem `@compact:begin` og `@compact:end` (CSS-nesting relativt til `.app`; start hver regel med `&`).

---

## 4. Layout

### 4.1 Container og grid

Header, strimmel og indhold ligger i `.wrap` (maks. `--container` = 1200 px, centreret). Visninger er et 12-kolonne grid. Paneler vælger bredde med en klasse; uden klasse fylder de hele rækken.

| Klasse | Kolonner | Typisk brug |
|---|---|---|
| (ingen) | 12 | Advarsel, vejrudsigt, motor-panel |
| `c7` + `c5` | 7 + 5 | Primært + sekundært indhold |
| `c6` + `c6` | 6 + 6 | To ligeværdige formularer |
| `c4` ×3 | 4 + 4 + 4 | Tre små statuspaneler |
| `c8` + `c4` | 8 + 4 | Stor graf + kort liste |

Under 900 px fylder alle paneler hele bredden.

**Dashboard: kort på samme række har samme højde.** Panelets footer (`.panel-foot`) skubbes altid til bunden, så knapper flugter. Et panel, der bliver mere end dobbelt så højt som naboen, skal enten have mere indhold (fx en graf, som "Varme nu"), stå alene i rækken eller flettes ind som `.sub` i naboen.

**Konfiguration: 1/2/3 lige spalter** (< 900 / ≥ 900 / ≥ 1280 px; en sektion med kun to paneler får højst to). Paneler har naturlig højde og stables oppefra (`align-items: start`), så ingen panel har tom bund. `cN` betyder her «én spalte»; et element uden `cN` eller med `.wide` spænder alle spalter. Der er ingen ens højde i Konfiguration.

**Et panel, der står alene i en række, fylder hele bredden** (ingen `cN`) og lægger sine underafsnit side om side (`.subs.cols-2`). Kan det ikke fylde to underafsnit, flettes det ind i et andet panel.

### 4.2 Brudpunkter

| Bredde | Ændring |
|---|---|
| < 600 px | Zonefelter viser kun ID og temperatur med vandret niveaubjælke; system-felt som smal række; metrics 2×2 |
| < 768 px | Tilstandspille i bunden; header og strimmel klæber ikke; mindre titler |
| < 900 px | Alle paneler fuld bredde |
| < 1024 px | Kompakte zonefelter |
| ≥ 1024 px med mus | Kompakt tæthed (3.9) |
| ≥ 1280 px | Konfiguration i tre spalter |

### 4.3 Sektioner (kun Konfiguration)

Lange konfigurationsvisninger i V6 og Touch grupperer paneler i `.section` under en `.section-h`, indtil de er flyttet til ark og System (afsnit 15). Dashboard bruger ikke sektioner.

```html
<nav class="section-nav" aria-label="Sektioner">
  <a href="#sec-manifold">Manifold og motorer</a>
  …
</nav>
<section class="section" id="sec-manifold" aria-labelledby="sec-manifold-h">
  <h2 class="section-h" id="sec-manifold-h">Manifold og motorer
    <i class="section-h-dot" aria-hidden="true"></i>
    <i class="section-h-line" aria-hidden="true"></i>
  </h2>
  <div class="section-grid">…paneler c4–c8…</div>
</section>
<details class="section" id="sec-service">
  <summary class="section-h">Service <span class="badge">2 paneler</span>
    <i class="section-h-dot" aria-hidden="true"></i>
    <i class="section-h-line" aria-hidden="true"></i>
  </summary>
  <div class="section-grid">…</div>
</details>
```

Regler:
- Maks. 5 sektioner pr. visning. Har en sektion kun ét panel, flyttes panelet eller sektionen fjernes (fuldbredde informationspaneler er den eneste bløde undtagelse).
- Rækkefølge: mest brugte først; Service og Udvikler altid sidst.
- `.section-h`: sentence case, `--fs-sm` / 600 / `--muted`, linje i `--border`, `--space-6` over / `--space-3` under; første sektion ingen luft over.
- **Sektionslinks** (`.section-nav`): vises ved 3+ sektioner. Pilleformede ankerlinks; vandret scroll på mobil. Klæber ikke. `scroll-margin-top: var(--section-scroll-margin)`, så overskrifter ikke gemmes under navbaren.
- Ugemt/fejl: `data-dirty` (warn-prik) / `data-fault` (danger-prik) på sektion og tilhørende link, efter 6.1.
- **Kun det sjældne og det risikable foldes.** Kun Service og Udvikler må være `details.section` (lukket som standard). Et link til en lukket sektion åbner den med én linje `hashchange`-JS; uden JS lander ankeret på summary.
- Den ældre `.section-head` (tekst + linje) findes kun til midlertidig kompatibilitet (Touch-eksemplet).

---

## 5. Komponenter

Hver komponent har: formål, markup, varianter, regler og tilgængelighed. Levende eksempler står i `docs/design-system.html`.

### 5.1 Navbar

En svævende pille (`.navbar-wrap > .header`), der klæber øverst og følger med ned: enhedsvælger til venstre, navigation i midten, værktøjer til højre. Baggrunden er kortfarven med let gennemsigtighed og sløring (ikke på mobil), lys topkant og blød skygge. Strimlen ligger under og scroller med indholdet.

Samme komponent bruges uden for Lune (fx et HA-dashboard): midten indeholder så visninger (Hjem, Varme, Energi, Lys) i stedet for tilstandene.

- **Enhedsvælger** (`details.device`): logo, enhedsnavn og placering. Dropdownen (`div.device-menu`) har øverst **Om enhed** (`.device-about`: navn, placering, IP, MAC, firmware, ESPHome-version, oppetid + knap «Kopiér diagnostik»). Derunder lister en `nav` andre Lune-enheder som almindelige links; den aktuelle har `aria-current="page"`, en enhed der ikke kan nås har `data-offline` (grå prik). Menuen er kun information — ingen genstart, OTA eller nulstilling (dem ligger i Service). På Touch kan et langt tryk på uret åbne det samme infosheet for installatøren.
- **Tilstandspille** (`nav.mode`): to `<label>`s for `#m-dash` og `#m-conf`, hver med ikon og tekst. Aktiv er inverteret. Der er altid præcis to.
- **Sprogvælger** (`nav.lang`): kun hvis buildet har mere end ét sprog. Almindelige links til `/en/`, `/da/`, med `hreflang` og `lang`.
- **Tema** (`label.theme-btn` for `#theme`): sol i mørkt tema, måne i lyst.

### 5.2 Zonestrimmel og zonefelt

Overblik og omfangs-navigation i ét. Står i begge tilstande.

```html
<nav class="strip" aria-label="Vælg zone eller manifold">
  <label class="tile tile-sys" for="s-sys">
    <b>Manifold</b><span class="big">34,2° / 29,8°</span><small>ΔT 4,4° · kalder</small>
  </label>
  <label class="tile" for="s-z1" data-state="calling" data-level="4">
    <span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span>
    <span class="tile-id">Z1</span>
    <span class="tile-name">Josephine</span>
    <span class="tile-val">21,4°</span>
  </label>
</nav>
```

| Attribut | Værdier | Virkning |
|---|---|---|
| `data-state` | `calling`, `idle`, `fault`, `blocked`, `off`, `learning` | `fault`: rødt ID, værdi og første segment. `blocked`: gult ID og «Blokeret». `learning`: violet ID og «Lærer n %», og bjælken viser læringsprocenten i violet — kun mens læringen kører. `off`: 50 % opacitet. |
| `data-learn` | `needed` | Gult `!`-skilt øverst til højre. Zonen er **ikke lært**, og læringen kører ikke. |
| `data-charge` | (tom), `insufficient` | `↑`-skilt: Touch lader gulvet op før vind/kulde; `insufficient` = gult, gulvet kan ikke dække hele underskuddet. `!` (mangler læring) vinder. |
| `.tile-sys[data-lease]` | `none`, `refused` | Gult `!`: V6 uden Touch-lease kører lokalt. |
| `data-level` | `0`–`10` | Tænder segmenter i den vandrette ventilbjælke fra venstre (ventilåbning i trin á 10 %; `.tile-pct` viser præcis procent) i feltets tekstfarve: ventilåbning er en mængde, ikke et varmesignal. Tændt zone har mindst 1. |
| `.tile-dev[data-dev]` | `1`–`5` | Chip med afvigelse fra mål (fx "−0,6°") i 5 trin fra `scale-cold-warm`. Trin 3 (±0,3°) er neutral. Skjules under 1024 px. |
| `data-group` | `primary`, `member` | Hel violet ring (gruppens primære zone, ID som "Z4–5") eller stiplet violet ring (medlem, navn dæmpet). Violet = gruppering. |
| `.is-selected` | — | Valgt uden radio-state (server-render/JS). Ellers styres valg af de genererede regler. |

Skiltene er solide fyld-toner (`heat-fill` / `warn-fill` med `on-…`-tekst) og tager afvigelses-chippens plads.

Regler:
- System-feltet står først. Zoner står i fysisk rækkefølge.
- Valgt felt **inverteres**. Ingen anden markering (ingen ekstra farve eller ramme).
- Værdien er aktuel temperatur; «Lærer n %» mens motorlæring kører; «Blokeret» / «Fejl» ved de tilstande.
- Ikke lært (`data-learn="needed"`) viser temperaturen og det gule skilt. Firmwarens `CALIBRATING` på en ulært, stille zone er ikke læring i gang.
- I Konfiguration får zone-ID en orange prik (genereret af CSS).
- Mobil (< 600 px): navnet skjules; det står i visningens titel lige under.
- Touch: se 5.2.1 (hierarkisk strimmel). Brug aldrig en flad liste med mere end 7 felter.

### 5.2.1 Manifoldfelt og understrimmel

> **Forældet i 2.2.** Touch bruger nu Hjem med varmekort (15.10). CSS'en ligger bag feature'n `tiers-strip` (`"features": ["tiers-strip"]` i config) og fjernes i en senere version.

Touch bruger `.strip.strip--tiers` + `.substrip`:

**Niveau 1** (altid synlig): hus-felt (`.tile-sys`) + ét `.tile.tile-manifold` pr. board (1–4).

**Niveau 2** (`.substrip[data-m="{N}"]`): vises kun, når manifold `N` eller en af dens zoner er valgt. Indeholder den manifolds zonefelter med samme `.tile`-markup som V6. Når hus er valgt, er ingen understrimmel synlig.

Manifoldfeltet viser ID (`M2`), navn, fremløb/retur og `.mini` (én lodret søjle pr. zone). Søjlerne er ventilåbning og derfor neutrale (tekstfarve), som zonefeltets bjælke:
- `data-level="0–5"` → højde i trin á 20 %; `0` er 2 px i `--seg-off` (lukket ventil eller ingen data)
- `data-state="fault"` → fuld højde i `--danger`; `blocked` → fuld højde i `--warn`; `learning` → `--violet`; `off` → `--seg-off`
- Søjlerne er `aria-hidden`; feltet har en samlet `aria-label` fra i18n

Systemfeltet kan vise fremløb/retur som stak (`.temps` › `.temp.flow` / `.temp.ret` med `.temp-lab` + `.tile-val`); uden måling `data-empty` og «—».

Tilstande:
- Manifold valgt → inverteret (genereret)
- Zone valgt → forælder-valgt: inset ring 2 px i `--fg` (genereret + klasse `.is-parent` til server-render)
- Fejl i en zone → ID i `--danger`

Mobil (< 600 px): hus + manifolds i 5 lige kolonner; manifoldfeltet viser kun ID + mini-søjler.

Radioer: `#s-house`, `#s-m{N}` (`data-kind="manifold" data-m="{N}"`), `#s-m{N}z{Z}` (`data-kind="zone" data-m="{N}"`). Reglerne genereres af `lds_build.py` ud fra `tiers` + `manifolds` i `config/touch.json`.

### 5.3 Panel

Standard-containeren for indhold.

```html
<section class="panel c7">
  <header class="panel-head"><h3>Komfort</h3><p>Sidste 24 timer</p><span class="badge hot">Kalder</span></header>
  … indhold …
  <footer class="panel-foot"><button class="btn primary" type="submit">Gem mål</button></footer>
</section>
```

- `panel-head`: titel (`h3`, `--fs-panel`), valgfri undertitel (`p`, dæmpet), valgfri `.help-btn` (?, 5.3b) og valgfri badge (skubbes til højre). På mobil får undertitlen sin egen linje.
- Indhold: `.sub` for et underafsnit med `h4` (lille, dæmpet), `.subs.cols-2` for to underafsnit side om side, når panelet er ≥ 560 px bredt.
- Underafsnit må have egne handlinger nederst (`.actions` — tilstandshandlinger som Frakobl / Synkronisér). De flugter i bunden (`margin-top: auto`). Panelets footer er kun til Gem.
- `panel-foot`: gem-handlinger, højrestillet. En `.note` til venstre (fx "Hentet 14:05").
- `details.more`: sjælden ekspert-tuning eller tekniske id'er, foldet sammen. Maks. én pr. panel (eller pr. underafsnit, når panelet har `.subs`).
- Monospace-id'er: `.mono` + `.id-row` med `.btn.copy` (`data-copy` → selektor; progressiv JS; uden JS er teksten markerbar).

Varianter:

| Klasse | Brug |
|---|---|
| `.panel.alert` | Fejl der kræver handling nu. Kompakt: titel og tekst til venstre, knap til højre (én linje på bred skærm). Baggrund `danger-surface`, solid rød kant (`danger-fill`), rød ikonbrik med "!", titel i normal tekstfarve, knap i `danger-fill` med hvid tekst. Maks. én pr. visning, altid øverst. |
| `.panel.tone-info` | Informativ fremhævning (sjælden). |
| `form.panel` | Formular. Én gem-knap i footeren. |

Regler: 2–4 paneler pr. visning. Et panel har én titel og ét emne. Paneler nestes aldrig.

### 5.3b Hjælp (tre lag)

1. **Labels og `.hint`** — altid synlige; intet klik.
2. **Ét `.help-btn` (?) pr. panel eller underafsnit** — åbner native `[popover].help-pop` ved klik/tryk (ikke hover). Højst 2–3 sætninger om *hvad* indstillingerne gør og *konsekvensen* af at ændre dem. Sentence case. Neutral knap (`--raised` / `--muted`) — orange betyder varme.
3. **«Læs mere»** — link til produktets `docs/Manual.md` (anker pr. panel) i produktrepoet (V6: `lune-v6/docs/Manual.md`; Touch: `docs/Manual.md` i `lune-coordinator`); lange forklaringer hører ikke hjemme i flash.

```html
<header class="panel-head">
  <h3>Manifold</h3>
  <button class="help-btn" type="button" popovertarget="help-manifold" aria-label="Hjælp: Manifold">?</button>
</header>
<div id="help-manifold" popover class="help-pop">
  <p>…</p>
  <a href="https://github.com/Birkemosen/lune/blob/main/lune-v6/docs/Manual.md#manifold">Læs mere</a>
</div>
```

Hjælp hører til indstillinger. På desktop placeres `.help-pop` lige under den `?`, der åbnede den (CSS anchor eller progressiv JS — nødvendigt, når flere knapper deler samme popover). På mobil bliver den et bundark.

### 5.4 Badge

Lille pille med prik til status i en panelheader eller ved en overskrift.

`.badge` (neutral), `.badge.hot` (varme: "Kalder"), `.badge.info`, `.ok`, `.warn`, `.bad`, `.violet`.

Højst ét badge pr. panelheader. Ordet er kort ("Online", "Lært", "Adaptiv", "+0,4 °C").

### 5.5 Besked

Forklarer *hvad der sker og hvorfor* inde i et panel.

```html
<p class="msg info"><span><b>Vinden øges til 11 m/s i nat.</b> Z1 og Z5 forvarmes fra kl. 20 til 07.</span></p>
```

Varianter: `info`, `ok`, `warn`, `bad`, `violet`. Første sætning fed og konkret; resten forklarer konsekvens eller handling. Maks. én besked pr. panel.

### 5.6 Tal: metric, kv, bar

- **`.metrics` / `.metric`**: store tal med label under. 2–4 pr. række (2 pr. række på mobil).
  ```html
  <dl class="metrics"><div class="metric"><dt>Fremløb</dt><dd>34,2 <small>°C</small></dd></div>…</dl>
  ```
- **`.kv`**: nøgle/værdi-liste med skillelinjer. Til sekundære detaljer. Værdien må have en statusklasse (`c-ok`, `c-warn`, `c-info` …).
- **`.bar`**: tynd bjælke til en procentdel (`style="--v:32%"`). Neutral (`--fg`), fordi det er en mængde; `.bar.violet` til motorlæring. Brug `role="meter"` med `aria-valuenow`, når den står alene.

### 5.7 Klima-kontrol

Visningens hero på zone-dashboardet: aktuel temperatur i hero-størrelse og målet mellem to store runde knapper med − og + (ikke pile: pile betyder navigation).

```html
<div class="climate">
  <div class="now">21,4<small>°C</small></div>
  <div class="target">
    <button type="button" data-step="-1" aria-label="Sænk måltemperatur">…</button>
    <label class="value"><small>Mål °C</small><input type="number" name="z1_target" value="22.0" min="16" max="28" step="0.5"></label>
    <button type="button" data-step="1" aria-label="Hæv måltemperatur">…</button>
  </div>
</div>
```

Kun én pr. visning. Grupperede medlemszoner viser den deaktiveret med en note og et link til den primære zone.

### 5.8 Komfortliste med sparkline

Alle zoner med aktuel/mål og 24-timers graf. Hver række er et `<label>` for zonens scope-radio, så et klik åbner zonen.

- Fuld linje: temperatur (`--fg`). Stiplet trappe: mål (`--muted`). Flade imellem: afvigelse (`--accent`, rød ved fejl).
- Værdien bliver gul (`c-warn`), når temperaturen er > 0,5 °C under målet.
- Smalt panel: grafen under navnet. Bredt panel (≥ 560 px): på samme række; værdikolonnen har fast bredde, så graferne flugter.

### 5.9 Grafer

Alle grafer er inline-SVG uden bibliotek.

Regler:
- **Kun figurer i SVG.** Akser, labels, "Nu", ugedage og forklaringer er HTML, så de ikke forvrænges, når SVG'en strækkes (`preserveAspectRatio="none"`).
- Streger har `vector-effect: non-scaling-stroke`.
- Temperaturakser spænder over **mindst 3 °C**, så støj ikke ligner dramatik.
- Mål tegnes som **trappe**, ikke skrå linjer (skemaskift er trin).
- Fortiden tones ned (`.past`), "nu" er en stiplet lodret linje.
- Farver følger betydning: udetemperatur `--fg`, vind, retur og fremskrivning `--info`/`--data-return`, sol `--data-light`, fremløb og preload `--accent`.
- Alle grafer har `role="img"` og en `aria-label`, der siger hovedpointen ("op til 11 m/s i nat").

**Tom graf.** Uden data klapper grafen sammen til én linje: beholderen (`.sub.trend-wrap`, komfortrække m.fl.) får `data-empty`, figuren, aksen og forklaringen skjules, og `<p class="empty">` vises ved siden af overskriften. Ingen flad linje for manglende data — en sparkline kræver mindst to temperaturpunkter.

**Vejrudsigt (`.fc`, Touch):** header med ét vejrikon pr. time (`.fc-icons[data-hourly]`: sol, delvis sol, skyer, regn, sne, nat; hvert 3. under 900 px). Hovedgraf: temperatur (venstre akse °C) og solindstråling som glød (`.sa` med `url(#lds-fade-light)` + linje `.sl` i `--data-light`, højre akse W/m² via `.fc--dual` + `.fc-y2`). Undergraf: vind med vindretningspile hver 3. time (`.fc-dirs`, pilen peger, hvor vinden blæser hen). x-akse på faste klokkeslæt (`.fc-x--abs`, mærke hver 3. time, `span.m` skjules på smal skærm). Altid en forklaring (`.fc-legend`). Sne udledes af nedbør ved ≤ 0,5 °C, nat af solindstråling < 5 W/m². **Slider:** en stiplet linje (`.fc-scrub`) følger mus/finger gennem graferne, og `.fc-readout` viser tidspunkt, vejrtype, temperatur, sol, vind og vindretning for den time; aflæsningen vender ved højre kant (`.flip`) og forsvinder, når pointeren forlader grafen.

**Plan (`.plan`, Touch):** Touch' opvarmningsplan 24 t frem fra nu. To kolonner: spornavn (`.plan-lab`) og tidsakse. Øverste spor `.plan-lane--odin`: Odins planlagte varme som søjler (`.plan-bar`, `--v` = højde i %), alle i varme-domænet og adskilt med mønster (3.2.2): rumvarme solid `heat-fill`, `data-mode="dhw"` (varmt vand) skraveret med 1 px orange kant, `data-mode="legionella"` tæt kryds-skraveret med kant. Søjlerne er HTML, så skraveringen er CSS-udgaven af `lds-hatch-heat` / `lds-hatch-heat-dense` med y-akse i kWh (`.plan-lab--y` + `.plan-y`), og Touch' løft af Odins komfortbånd som violet streg (`.plan-lift`). Et spor pr. rum (`.plan-lane`) med segmenter (`.plan-seg`, `--a`/`--b` = fra/til i timer fra nu): `data-kind="preload"` (forvarme, let skravering) og `data-kind="charge"` (opladning før vind/kulde, tæt skravering i varme, så den ikke forveksles med rumvarme-søjlerne; `data-insufficient` = skraveret gul). Ingen jævne orange flader under 30 % (3.2.1). x-akse `.plan-x` som `.fc-x--abs`. Altid `.fc-legend` under (`.lbar` rumvarme, `.ldhw` varmt vand, `.lleg` legionella, `.llift` løft, `.lpre` forvarme, `.lch` opladning, `.lins` utilstrækkelig) med samme mønstre som søjlerne, og hvert segment har en title-tekst.

**Fordeling (`.dist`, Touch):** omtrentlig andel af pumpens flow pr. manifold. Flow er en mængde, og bjælker er aldrig flerfarvede (3.2.2): `.dist-bar` med `.dist-seg` (`--w` = andel i %) i `--fg` med faldende styrke (`data-i` 0–3) og en tynd skillelinje. Under den `.dist-rows`: nøgle (`.dist-key`, samme styrke), navn, %, l/min og en tynd bjælke (`.dist-bar--thin`) med manifoldens zoner. `.dist-note` forklarer, at tallene er omtrentlige (beregnet ud fra ventilåbning).

**Plan vs. virkelighed (`.bars`)**: planlagt varme neutral (`.plan`, `--seg-off`), faktisk i `--accent` (`.act`). Højder via `--plan` / `--act` (0–100). Akser og forklaring i HTML (`.bars-legend`). Antal kolonner: `--bars-n`. Bruges på Touch' hus-dashboard, når varmekilden er Asgard (Odin).

Komponenter: `.spark` (zonegraf, 36 px høj), `.trend` (fremløb/retur, 120 px), `.zc` (zone 24 t + 6 t fremskrivning), `.fc` (vejrudsigt), `.bars` (plan vs. virkelighed), `.plan` (Touch' opvarmningsplan), `.dist` (flowfordeling).

Datakontrakt: hver polyline får en `points`-streng i koordinatsystemet fra `viewBox`. Firmwaren eller binderen genererer strengene; markeringsattributter (`data-bind-spark="z1"`, `data-bind-trend`, `data-bind-zc="z1"`, `data-bind-fc`) viser, hvad der skal opdateres.

#### Zonegraf: 24 t historik + 6 t prognose (`.zc`)

Fuld bredde nederst på zone-dashboardet. Én tidsakse fra −24 t til +6 t; "nu" ligger ved 47/60 af bredden (halvtimepunkter).

| Lag | Udtryk | Token |
|---|---|---|
| Temperatur (fortid) | fuld linje 2,2 px | `--fg` (rød ved fejl) |
| Mål (effektivt, fortid) | stiplet trappe | `--muted` |
| Afvigelse | flade mellem temperatur og mål | `--accent` 22 % |
| Preload-perioder | lodret bånd | `--accent` 10 % |
| Fremtid | tonet baggrund | `--raised` 45 % |
| Fremskrivning | stiplet linje + usikkerhedsbånd der vokser med tiden | `--info` |
| Planlagt mål | stiplet trappe fortsætter | `--muted` |
| Ventilåbning | søjlestrimmel under grafen (18 px) | `--accent` 55 % |

Over grafen fire nøgletal: gns. afvigelse 24 t, tid under mål (> 0,3 °C under), forventet temperatur om 6 t (kun med fremskrivning), gns. ventilåbning. Under forklaringen en note om, hvad fremskrivningen bygger på. Ved fejl en `.msg.bad`, der siger hvad fremskrivningen antager.

**Fremskrivning (dæmpet lineær — fælles V6/Touch-kontrakt).** Implementér identisk i binderen (JS) og på Touch (C):

1. Brug de seneste 8 punkter (4 timer) af `temp`. Kræv mindst 6 gyldige punkter uden huller; ellers ingen fremskrivning.
2. Hældning b (°C pr. halvtime) ved mindste kvadraters metode over punkterne. Residualernes spredning = σ.
3. Dæmpning φ = 0,85 pr. halvtime. For k = 1…12: T(k) = T_nu + b·(φ + φ² + … + φ^k).
4. Klip T(k) til [mål_plan(k) − 3,0 ; mål_plan(k) + 1,5].
5. Usikkerhedsbånd: ±(max(σ, 0,05)·√k + 0,05) °C.
6. Ingen fremskrivning, når zonen er slukket. Ved motorfejl beregnes den som normalt (temperaturen falder typisk); fejlbeskeden siger, at fremskrivningen antager, at fejlen fortsætter.

Firmwaren leverer kun historik (`temp`, `sp_plan`); binderen beregner fremskrivningen. Reference: `lune-v6/web/binder-src/projection.js` (+ `projection.test.js`: stigende / faldende / flad / < 6 punkter). Ved hover/touch over plottet: stiplet slider (`.zc-scrub`), og nøgletallet viser værdien ved tidspunktet («Kl. {tid}» i fortid, «Forventet kl. {tid}» i fremskrivning).

Regler: aksen spænder mindst 3 °C. Linjen hedder "Fremskrivning", ikke "Prognose", fordi den kun forlænger den seneste udvikling og ikke kender skemaskift eller preload. Uden nok data vises kun planlagt mål i fremtidsdelen og teksten "Ingen fremskrivning endnu". Farven er altid `--info`, fordi det er en forudsigelse, ikke en måling.

### 5.10 Felter

> Indstillinger i ark og på System præsenteres som grupperede lister (`.setting-group`, afsnit 15.5). `.field`/`.field.row` nedenfor bruges kun i formularer uden for indstillinger (fx på Hjem).

```html
<div class="field row"><label for="probe_flow">Fremløbsprobe</label><select class="select" id="probe_flow">…</select></div>
```

- `.field`: label over kontrol. `.field.row`: label til venstre, kontrol til højre med **naturlig bredde**, når panelet er ≥ 320 px (container query). Kontroller fylder aldrig halv panelbredde: stepper 168 px, select 160–280 px, input maks. 320 px; brug `.w-xs` (9ch, port/tal), `.w-sm` (16ch), `.w-md` (26ch, host/MAC), `.w-lg` (44ch, URL). Segmenter har bredde efter indhold. På mobil fylder alt bredden.
- Hint i `<span class="hint">` inde i label.
- `.input`, `.select`: `--hit` høje, `--field`-baggrund, `--field-edge`-kant. Native validering (`pattern`, `min`, `max`) viser rød kant via `:user-invalid`, og gem-knappen dæmpes.
- `.pair`, `.pair.wide-first`: to felter side om side (host/port, MAC + Scan).
- Filvalg: brug `label.input.file` med skjult `input[type=file].sr-only`, `.file-pick` og `.file-name` — native file-controls kan ikke centreres pålideligt. Filnavnet opdateres progressivt ved `change`.

### 5.11 Stepper

Pilleformet −/værdi/+ til tal med et naturligt trin (areal, grænser, interval). Uden JS er feltet et almindeligt tal-input.

Brug `input type="number"` med `min`, `max`, `step`. Enheden i `<span class="unit">`. Knapperne har `aria-label` ("Sænk areal").

Stepper kun til værdier med et naturligt trin; ikke til port, id'er eller adresser. De er `input type="number"` eller tekst med `min`/`max`.

### 5.12 Switch og gating

`<label class="switch">` med tekst og `<input type="checkbox" role="switch">`. Tændt er inverteret (sort/hvid), ikke grøn.

Hvis en switch styrer resten af et afsnit: læg den som første barn i `.gated` og resten i `.gated-body`. Når den er slukket, gråner indholdet ud og kan ikke betjenes.

### 5.13 Segment

`.seg`: 2–3 gensidigt udelukkende valg (NO/NC, Statisk/Adaptiv, Probe/BLE). Aktivt valg er inverteret. Ved 4+ valg: brug `.select`.

### 5.14 Kompas

`.compass`: et hus i midten med fire vægge som piller (vandrette for N/S, lodrette for V/Ø). Valgt væg er inverteret (neutral, ikke orange: ydervægge er ikke varme). Bogstaverne kommer fra sprogkataloget.

### 5.15 Knapper

| Klasse | Brug |
|---|---|
| `.btn.primary` | Den ene primære handling i et panel (gem), og «Nulstil fejl», når zonen har en fejl. Inverteret, når der er noget at gemme. |
| `.btn` | Sekundære handlinger ("Hent nu", "Scan", "Stop", "Annullér"). |
| `.btn.danger` | Åbner en bekræftelse. Teksten slutter med «…». |
| `.btn.danger-solid` | Den bekræftede destruktive handling i `.confirm-pop`. `danger-fill` med `on-fill`-tekst. |

Tekst er verbum + objekt: "Gem regulering", "Nulstil fejl", ikke "OK" eller "Send". Samme handling hedder det samme overalt.

**Clean / dirty for gem-knappen.** Skjul eller deaktivér aldrig gem-knappen (`disabled` kan ikke få fokus; skjulte knapper får panelet til at hoppe). Med JS (`form[data-js]`):

| Tilstand | Udtryk |
|---|---|
| Clean | Primærknappen er raised/muted, `aria-disabled="true"`, titel `rt.nothingToSave`. Klik gør intet. |
| Dirty | Normal inverteret primær. |
| Saving | Tekst `rt.saving`, `aria-busy="true"`. |
| Saved | Tekst `rt.savedOk` + ✓ i 3 s, derefter clean. |

Uden JS er knappen altid inverteret primær.

### 5.16 Bekræftelse

Destruktive handlinger bruger en bekræftelses-popover (`.confirm-pop`, native `popover`, ingen JS):

```html
<button class="btn danger" type="button" popovertarget="cf-bal">Nulstil balancering…</button>
<div id="cf-bal" popover class="confirm-pop" role="alertdialog" aria-labelledby="cf-bal-t">
  <h4 id="cf-bal-t">Nulstil balancering?</h4>
  <p>Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p>
  <div class="actions">
    <button class="btn" type="button" popovertarget="cf-bal" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="submit" name="action" value="reset_balancing">Nulstil</button>
  </div>
</div>
```

Første knap slutter med "…". Titlen er et spørgsmål med handling og objekt, teksten siger præcis hvad der går tabt. Annullér har fokus (Enter er sikkert); Esc og klik udenfor lukker. Popoveren ligger i formularen, så den farlige knap sender den rigtige `name="action"`. Mobil: ark fra bunden. Aldrig inline-udfoldning, aldrig "OK".

Anatomi:

| Del | Regel |
|---|---|
| Åbner | `.btn.danger`, `type="button"`, `popovertarget`. Teksten slutter med «…». |
| Dialog | `popover` (auto), `role="alertdialog"`, `aria-labelledby` på titlen, `aria-describedby` på brødteksten. |
| Titel | `h4` (eller `p.confirm-title` i eksisterende V6/Touch-markup). Spørgsmål med handling og objekt: «Nulstil og genlær Z2?». |
| Brødtekst | Hvad der går tabt, og hvad der sker bagefter. |
| Knapper | `.actions` (eller `.confirm-actions`). Annullér: sekundær `.btn`, `popovertargetaction="hide"`, `autofocus`. |
| Farlig knap | `.btn.danger-solid`. Samme verbum som titlen («Nulstil», «Genstart», «Frakobl»). Aldrig «OK». |

Den farlige knap har `popovertargetaction="hide"`. Når `lune-forms.js` stopper den native submit, kører den skjuling ikke, så scriptet lukker `.confirm-pop` selv, når handlingen er accepteret.

Placering: uden script er popoveren centreret. Scriptet lægger den ved knappen, under den når der er plads, ellers over, og klemmer den ind i vinduet, så titel og knapper ikke klippes. Uden script kan markup forankre den med inline `position-anchor`. Under 600 px er popoveren et ark fra bunden med `safe-area-inset-bottom`, og scriptet rører den ikke.

### 5.17 Tabel og log

- `.table` i `.table-wrap` (scroller vandret på smalle skærme). Tal højrestillet med `.num`.
- `.log`: monospace, maks. 240 px høj. Kilder kan farves efter betydning (`<span class="info">forecast</span>`). Log-tekst er altid engelsk (kommer fra firmwaren) og har `lang="en"`.
- Redigerbare tal i en tabel (fx Rum): `td.num .input` (smalle, højrestillede felter). Navn med redigering: `.name-edit`.

### 5.18 Typeafhængige felter

Når en indstilling har en type (fx varmekilde `http` | `asgard`), vises typespecifikke felter **uden JavaScript**:

```html
<input class="state" type="radio" name="hs_type" id="hs-http" value="http">
<input class="state" type="radio" name="hs_type" id="hs-asgard" value="asgard" checked>
<div class="seg" role="radiogroup" aria-label="Type">
  <label for="hs-http"><span>Generisk HTTP</span></label>
  <label for="hs-asgard"><span>Asgard (Odin)</span></label>
</div>
<fieldset class="hs-fields typed-fields" data-type="http">…</fieldset>
<fieldset class="hs-fields typed-fields" data-type="asgard">…</fieldset>
```

Regler genereres af `lds_build.py` fra `heat_source_types`: `.typed-fields { display: none }` og `#hs-{type}:checked ~ .typed-fields[data-type="{type}"] { display: grid }`. Virker uden `:has()`. Ved 4+ typer: brug `.select` i stedet for `.seg` (5.13).

Andre typevalg i samme formular (fx kilde for nettarif: DataHub / skema / ingen) bruger samme mønster med egne radio-id'er: tilføj gruppen i `typed_groups` i `config/<projekt>.json` (`{"gt": ["datahub", "schedule", "none"]}` → `#gt-datahub` …). Gruppens radioer, `.seg` og `fieldset.typed-fields` skal have samme forælder (søskende-selektorer), så grupper ikke påvirker hinanden.

Dashboard-paneler sætter `data-hs-type="http|asgard"`; CSS skjuler `.hs-type-*`, der ikke matcher.

---

## 6. Mønstre

### 6.1 Gem

Hverdagshandlinger (Hjem/Dashboard) gemmes automatisk; opsætning gemmes eksplicit pr. panel eller ark. **Undtagelse:** `.switch` (`role="switch"`) i opsætningen gemmes med det samme ved skift — de er tilstandshandlinger, ikke felter man «udfylder» før gem.

**Eksplicit gem.** Panelet er formularen (`form.panel`) med `data-save="nøgle"`. Én `.btn.primary` i footeren, plus `<button type="reset">` (Fortryd) og et `.save-status` (`aria-live="polite"`).

| Tilstand | Knap | Footer |
|---|---|---|
| clean | Primær neutral (raised/muted), `aria-disabled="true"`; klik gør intet | Fortryd skjult (kun når JS har sat `data-js`) |
| dirty | Normal primær | Fortryd synlig + `rt.unsaved.one` / `rt.unsaved.other` i `--warn` |
| saving | `rt.saving`, `aria-busy="true"` | — |
| saved | `rt.savedOk` ✓ i 3 s → clean | Knapteksten er bekræftelsen; ingen ekstra `.msg` |
| error | Forbliver dirty og primær | `.save-status` med `rt.saveFailed` (+ årsag); feltværdier bevares |

Uden JS: knappen er altid primær, Fortryd er synlig og nulstiller felterne.

**Tre niveauer af indikation** (farve `--warn`, aldrig eneste signal):

1. **Felt** — ændrede `.field` / `.switch` / `.seg` / `.compass` får `data-dirty` (6 px prik efter label) og `aria-describedby` til panelets `.save-status`.
2. **Panel** — footeren som i tabellen. Antallet tæller felter (én radiogruppe = ét felt).
3. **På tværs** — `.tile[for="s-…"]`, sektioner/sektionslinks og tilstandspillens «Konfiguration» får `data-dirty`, når der er ugemte ændringer i den tilhørende visning. `beforeunload` advarer ved navigation væk (`rt.leaveUnsaved`).

Submit sender `lune:save` med `{key, data, form, auto}`. API-laget kalder `form.luneSaved(true|false, besked?)`. Scriptet er `js/lune-forms.js`.

**Standardværdier.** Når binderen har malet enhedens værdier i en formular, kalder den `form.luneResnap()`: værdierne bliver formularens standardværdier (`defaultValue`/`defaultChecked`/`defaultSelected`), så Fortryd og «kassér» ved lukning af et ark vender tilbage til enhedens værdier, ikke til HTML'ens eksempeldata. Det samme sker efter et vellykket gem. Formularer, der tegnes efter indlæsning (fx et ark pr. rum), bindes med `window.luneForms.bind(form)` eller `window.luneForms.scan(rod)`.

**Delvis gem = patch.** Når én ressource vises to steder (fx varmekildens forbindelse på System og dens adfærd i Varme-arket), har hver formular sin undernøgle — `data-save="heat_source.connection"` og `data-save="heat_source.behavior"` — og `data-patch`. `lune:save` får så `resource` (`heat_source`), `part` (`connection`), `method: "PATCH"` og `changed` (kun de ændrede felter). API-laget sender en PATCH til ressourcens endpoint med `changed`; felter, formularen ikke viser, røres ikke. Formularer uden `data-patch` sender hele formularen (`method: "POST"`).

**Autogem (Hjem/Dashboard).** Måltemperaturen (`.climate`) gemmes 1,5 s efter sidste ændring — ingen gem-knap. Status under målet (`.autosave`): `rt.autoSaving` → `rt.autoSaved` → tom; ved fejl `rt.autoFailed` + `rt.retry`.

### 6.1b Tilstandsafhængige handlinger

Vis kun de handlinger, der giver mening i den aktuelle tilstand; den naturlige næste handling er primær. Sæt `data-state` på panelet (`unpaired` | `approved` | `pending` | `error`) fra binderen. CSS viser/skjuler via `[data-show-when~="…"]`. Uden JS bruges den tilstand, siden er renderet med.

| Tilstand | Handlinger |
|---|---|
| Ikke parret (`unpaired`) | «Godkend …» er `.btn.primary`; ingen frakobling |
| Godkendt (`approved`) | Ingen «Godkend»; «Frakobl …» (`.btn.danger`, der åbner `.confirm-pop`) |
| Venter/forbinder (`pending`) | Statusbadge + evt. «Annullér»; ingen andre handlinger |
| Fejl/ingen forbindelse (`error`) | «Prøv igen» (`.btn`) + «Frakobl …»; `.msg.bad` med årsag |

Eksempel: Lune Touch-panelet i V6 Connect. Samme mønster til andre parrings-/forbindelsespaneler (BLE-sensorer, varmekilde, Touch-styringer).

### 6.2 Fejl og advarsler

| Alvor | Hvor |
|---|---|
| Fejl der kræver handling | `.panel.alert` øverst i visningen + rød tilstand i zonefeltet + rødt i komfortlisten |
| Kræver opmærksomhed snart | `.msg.warn` i det relevante panel eller `c-warn` på værdien |
| Information | `.msg.info` eller `.badge.info` |

En fejl på manifold-niveau viser et alert-panel i system-omfanget med et link ("Åbn Z6") til zonen. I zonens egen visning viser alert-panelet handlingen ("Nulstil fejl"). «Nulstil fejl» er `.btn.primary` og vises kun, mens zonen har en fejl (`data-bind-show`), både i alert-panelet og i motor-footeren. Der er ingen deaktiveret variant.

Fejltekster siger hvad der skete og hvad man gør: "Motoren nåede ikke endestop på 45 s. Tjek aktuatoren, og nulstil så fejlen." De undskylder ikke og er aldrig vage.

### 6.3 Grupperede zoner

- Primær zone: ID "Z4–5", hel violet ring. Medlem: ID "Z5", stiplet violet ring, dæmpet navn (5.2).
- Medlemmets dashboard: målet er låst, med note og link til den primære.
- Medlemmets konfiguration: `.msg.violet` øverst forklarer hvad der styres hvor.

### 6.4 Live-data

- Elementer der opdateres live har `data-bind="nøgle"` (fx `z1.temp`, `manifold.flow`).
- Zonefelter og komfortrækker har `data-state`, `data-level` og `data-learn`, som binderen opdaterer. Under motorlæring er `data-level` læringsprocenten, og motorpanelet viser `.bar.violet`.
- Strenge der skrives ved runtime, hentes fra `<script type="application/json" id="i18n">` (se 8).
- Offline: vis `rt.offline` som `.badge` i enhedspanelet; frys værdierne i stedet for at tømme dem.

### 6.5 Tomme tilstande

En tom tilstand er en opfordring: den siger hvad der mangler, og hvor det gøres. Fx: "Ingen zoner konfigureret. Navngiv zoner på Lune V6; Touch importerer dem automatisk." med en knap til det rette sted.

**Manglende værdier vises som «—», aldrig 0** (heller ikke `0,0 °C` eller `0 %`). `null` fra firmwaren må ikke tvinges til et tal; farve (fx `c-warn`) sættes kun på en rigtig måling. Tomme grafer: se 5.9.

### 6.6 Integrationer med typer

Når en integration har flere backends (fx varmekilde: Generisk HTTP / Asgard):

1. **Config** — tilføj type-id'et i `heat_source_types` i `config/touch.json` (første er standard). Ukendte id'er fejler buildet.
2. **Felter** — nyt `fieldset.typed-fields[data-type="…"]` med kun de nøgler, firmwaren accepterer. Opfind ikke felter; markér huller med `<!-- TODO: … -->`.
3. **Dashboard** — blok med klassen `.hs-type-{id}` inde i panelet med `data-hs-type`. Fælles status (badge, seneste push, setpunkt) står uden for type-blokkene.
4. **i18n** — labels, hints, hjælp og aria for den nye type i en/da.
5. **API-mapping** — UI-id `http` svarer til firmwarens `generic_http` ved gem (produkt-binder).

### 6.7 Test af forbindelser

En knap, der prøver en forbindelse (Test læsning, Test afsendelse), skal altid vise et resultat. Resultatet ligger i `<div class="test-result" aria-live="polite">` under knapperne, i det underafsnit handlingen hører til — ikke i gem-footeren.

| Tilstand | Indhold |
|---|---|
| Kører | Teksten «Tester…» (`data-state="running"`) og `aria-busy="true"` på knappen. Ingen `.msg` endnu. |
| Ok | `.msg.ok`. Første linje: handling, klokkeslæt, HTTP-kode og varighed (`Læsning OK · 14:32:05 · 200 · 84 ms`). Anden linje: hvad der blev læst eller sendt (værdi, entity). |
| Fejl | `.msg.bad`. Første linje: status, klokkeslæt og årsag (timeout, HTTP-kode, DNS). Anden linje: hvad man tjekker («Tjek host og port …»). |

Resultatet bliver stående, indtil næste test køres. Det gemmes ikke og indgår ikke i dirty-tællingen. Farven er aldrig eneste signal: begge linjer kan læses uden farve.

### 6.8 Firmware-tilstande

Rå firmwareværdier vises aldrig (`trusted`, `confirmed`, `unreachable`, fejlkoder). Tilstanden oversættes via i18n, og badge-farven følger betydningen: grøn = ok nu, neutral = venter, gul = snart opmærksomhed (kun sammen med en advarselstekst), rød = fejl nu. Gul bruges ikke som pynt eller som standard for «ikke godkendt endnu».

---

## 7. Indhold og sprog

- **Tone:** saglig, venlig, kort. Aktiv form. Ingen fyldord.
- **Navngivning:** efter hvad brugeren forstår ("Fremløb", "Returføler"), ikke hvordan systemet er bygget. Faglige termer, som installatører bruger (NO/NC, C-C, ripples), bevares.
- **Handlinger:** verbum + objekt, samme ord i knap og bekræftelse ("Gem mål" → "Gemt").
- **Tal:** decimaltegn efter sprog (21,4 / 21.4). Temperaturer med én decimal. Procent uden decimaler.
- **Enheder:** mellemrum før enheder (`21,4 °C`, `6 m/s`), undtagen det korte grad-tegn i zonefelter og komfortliste (`21,4°`).
- **Tid:** 24-timers ur. Relative tider for nyligt ("12 s siden"), klokkeslæt for resten ("Hentet 14:05").
- **Brugerdata** (zonenavne, enhedsnavn) oversættes aldrig.

---

## 8. Internationalisering

Sprog vælges ved build, ikke i browseren.

- Kataloger: `i18n/<sprog>.json`, flade nøgler efter område: `mode.*`, `state.*`, `common.*`, `dash.*`, `csys.*`, `cz.*`, `rt.*` (runtime).
- Meta-nøgler med `_`: `_dec` (decimaltegn), `_days` (ugedage), `_walls` (kompasbogstaver), `_h` (time-enhed) osv.
- Build: `--langs en,da` (standard; første = fallback). Én færdigoversat side pr. sprog. Mangler en nøgle, bruges engelsk og buildet advarer.
- Sprogskift er links til `/en/` og `/da/`. Firmwaren vælger sprog for `/` ud fra cookie → `Accept-Language` → standard (`lune_ui_pick()` i `web_ui.h`).
- Alt oversættes: synlig tekst, `aria-label`, `title`, `placeholder`.
- Runtime-strenge (`rt.*`, `state.*`) indlejres som JSON-blok til binderen.

---

## 9. ESP32 og budget

| Regel | Hvorfor |
|---|---|
| Ingen eksterne requests (fonte, CDN, billeder) | Enheden kører uden internet |
| CSS + `lune-forms.js` + én side pr. sprog, gzippet: V6 ≤ 69 kB for to sprog, hele web-UI'et inkl. binder ≤ 160 kB (se budgettet nedenfor) | Flash-plads og indlæsningstid |
| Ikoner som SVG-sprite (`<symbol>` + `<use>`) | Ét sted, genbrugt |
| Tilstand i CSS (radio/checkbox, `<details>`) | Ingen JS-framework |
| JS kun til: live-data, +/−, submit-hook, ugemt/gem og autogem (`js/lune-forms.js`), kopiér diagnostik, placering af hjælp- og bekræftelses-popover, luk dropdown. `lune-forms.js` serveres som egen fil eller lægges i produktets binder (én kopi for alle sprog), aldrig inline i hver sprogside | Virker uden JS |
| Grafer som SVG-punkter genereret på enheden eller i binderen | Intet chart-bibliotek |

Geist er første valg i font-stakken, men hentes ikke. Ønskes den, kan en latin-subset (woff2, ca. 30 kB) lægges i flash med `@font-face` og `font-display: swap`.

**Budget (LDS 2.3.2), målt på enheden:** V6 er en ESP32-S3 med 8 MB flash og to app-partitioner á 3.932.160 B. Firmwaren med web-UI er 1.633.792 B; web-UI'et i den (binder med `lune-forms.js` 59,4 kB + CSS 21,6 kB + to sider 18,0 og 18,4 kB) er 117 kB gzip, så der er ca. 2,3 MB (58 %) fri i app-partitionen (kilder: `packages/board/esp32-s3.yaml`, `partitions.csv`, `firmware.map` i `lune`). Flash er altså ikke grænsen; det er indlæsningstiden over ESP32-wifi og plads til, at firmwaren kan vokse. Budgettet er derfor: **CSS ≤ 24 kB, `lune-forms.js` ≤ 5 kB, ≤ 20 kB pr. sprogside uden grafpunkter (i alt ≤ 69 kB for to sprog), produktets binder ≤ 70 kB og hele web-UI'et ≤ 160 kB gzip** (ca. 4 % af app-partitionen). Arkene skal ligge i siden for at virke uden JavaScript; Dashboard/Konfiguration-komponenterne ligger i `@only legacy`-blokke, som V6 ikke får med (12).

Målt for 2.2 (V6-eksemplet, en + da): CSS 21,0 kB + JS 4,1 kB + 25,1 + 25,3 kB sider = 75,4 kB gzip. Heraf er ca. 8,4 kB pr. side indlejrede grafpunkter (`points` i komfortlisten, trend og zonegraf). **Grafpunkter hentes live af binderen** fra firmwarens historik og indlejres ikke i siden; siden sendes med tomme `points` og `data-empty`, indtil data er hentet (5.9). Uden de indlejrede punkter er eksemplet 21,0 + 4,1 + 16,7 + 16,9 = 58,7 kB — inden for budgettet. Eksemplet indlejrer dem kun for at kunne vises uden enhed.

---

## 10. Lune Touch

Touch bruger samme skal, komponenter og regler. Touch tilføjer **ingen egne zoneindstillinger** — zoner konfigureres på hver V6 — så Touch-produktet har kun omfanget **Hus** (Dashboard + Konfiguration) og ingen manifold-/zonevisninger.

| | Dashboard | Konfiguration |
|---|---|---|
| **Hus** | Én **V6-række pr. board** (`.boards` › `.strip`: System-felt med navn og fremløb/retur + zonefelter, samme markup som V6' strimmel; felterne linker til V6'ens egen side med `#s-sys` / `#s-zN`). Derunder husklima (mål, autogem), varmekilde med cirkulationspumpe som underafsnit, vejrudsigt, plan og fordeling. Advarsler øverst. | Styringer (V6-boards, navn kan overstyres), varmekilde (type + felter, 5.18), **Rum** (det Touch ejer pr. rum: medregn i hustemperatur, vægt, vind, sol), pumpe, vejrplacering, identitet/backup, service. |

- Board-navnet er Touch-navnet, hvis det er sat, ellers V6'ens eget (navn, ellers placering — `device_name` / `device_location` i V6 `/api/v1/zones`), ellers «Unavngivet».
- Vægge, areal og gulv ejes af V6; Touch spejler dem og redigerer dem ikke.
- Varmekildetyper sættes ved build (`heat_source_types`). Runtime vælger typen i Konfiguration › Hus. Felter følger firmwaren (`asgard` / `generic_http` via UI-id `http`).
- Husets kort viser varmekildens navn efter type («Varmepumpe (Asgard)» eller «Varmekilde (HTTP)»), og fremløb/retur kun, når typen leverer dem (Asgard; Generic HTTP kun hvis status-URL har givet værdier).
- Forbindelsesfejl på varmekilde: `.panel.alert` øverst i hus-visningen med «Åbn varmekilde» + fejlbadge i panelet.
- Kommandolog og diagnostik hører til Konfiguration › Service. Diagnostik viser oversatte værdier, aldrig rå firmware-strenge (6.8).
- Den hierarkiske strimmel (5.2.1) og `config/touch.json` (`tiers`) bruges af Touch-eksemplet; Touch-produktet bruger den ikke længere.

---

## 11. Tjekliste for en ny visning eller komponent

- [ ] Hører det til Dashboard eller Konfiguration (2.3)?
- [ ] Bruger det eksisterende komponenter, før der laves nye?
- [ ] Kun tokens, ingen hex-værdier eller px-tal uden for skalaerne?
- [ ] Har hver farve en betydning fra 3.2, og er farve aldrig eneste signal?
- [ ] 2–4 paneler, én titel og ét emne pr. panel, én gem-knap pr. formular?
- [ ] Alene-panel i en række: fuld bredde (ingen cN) med `.subs.cols-2` — ikke tom halv kolonne?
- [ ] Underafsnit-handlinger nederst; footer kun Gem?
- [ ] Konfiguration (indtil ark/System): sektioner (4.3), maks. 5, sektionslinks ved 3+, kun Service/Udvikler foldet?
- [ ] Dashboard: små paneler ved høje naboer flettet som `.sub`? Konfiguration: naturlig højde?
- [ ] Kompakt ved 1440 px med mus: kontroller med naturlig bredde og `.w-*`, ingen panel med > 48 px tom bund? Touch ved 390 px uændret?
- [ ] Manglende værdier som «—» og tomme grafer som én `.empty`-linje?
- [ ] Tilstandsafhængige handlinger: kun relevante knapper, næste skridt er primær (6.1b)?
- [ ] Ugemte ændringer markeret (felt/panel/sektion/tværs)? Autogem på Hjem/dashboard?
- [ ] Tekst ≥ 4,5:1 i begge temaer (`--check` for nye tokens)?
- [ ] Trykflader bruger `--hit`; fungerer med tastatur og synlig fokus?
- [ ] Testet ved 360, 390, 820, 1024 og 1440 px i begge temaer?
- [ ] Alle strenge i katalogerne (også aria-labels), ingen hardkodet tekst?
- [ ] Virker det uden JavaScript (bortset fra live-data)?
- [ ] Inden for budgettet i 9?
- [ ] Touch: kun Hus; V6-rækker på dashboardet; typefelter via `.typed-fields` uden JS?
- [ ] Stepper kun til tal med et naturligt trin (ikke port, id eller adresse)?
- [ ] Test af en forbindelse viser `.test-result` (kører, ok eller fejl), som bliver stående og ikke gemmes?
- [ ] Rå firmwareværdier er oversat, og badge-farven følger betydningen?

---

## 12. Filer og arbejdsgang

```
lune-design-system/
  DESIGN.md                 dette dokument
  AGENTS.md                 korte regler til kodeagenter (Claude Code m.fl.)
  tokens/tokens.json        værdier (farver, type, afstand …) + kontrastkrav
  css/lune-ui.src.css       komponenter og layout; to genererede områder
  config/v6.json            omfang for V6 (sys + z1–z6)
  config/touch.json         Touch: tiers, manifolds, heat_source_types, typed_groups, shared_views
  tools/lds_build.py        tokens + tilstandsregler → dist/<projekt>/lune-ui.css
  tools/build_docs.py       → docs/design-system.html (visuel reference)
  tools/lds_display.py      → dist/display/lune_theme.{h,yaml} (vægskærm, LVGL); --install <produktrepo>
  tools/lds_brand.py        brand-mærker (seksrørs-halo) fra tokens/brand.json
  tools/lds_ha.py           → dist/home-assistant/themes/lune.yaml (Home Assistant)
  js/lune-forms.js          ugemt/gem, autogem, placering af popovers (progressiv)
  examples/v6/              V6-dashboardet bygget på systemet (i18n, web_ui.h)
  examples/touch/           Touch-reference (hierarki + varmekilde)
```

```
python tools/lds_build.py --check             # kontrast
python tools/lds_build.py config/v6.json      # dist/v6/lune-ui.css
python tools/lds_build.py config/touch.json   # dist/touch/lune-ui.css
python tools/build_docs.py                    # docs/design-system.html
python tools/lds_display.py                   # vægskærmens LVGL-tema
python examples/v6/build_ui.py --langs en,da  # V6-sider + web_ui.h
python examples/touch/build_ui.py --langs en,da --preview --hs-type asgard
```

**Projektspecifik CSS.** Komponenter, som kun ét produkt bruger, står i `css/lune-ui.src.css` mellem `/* @only touch */` (eller `v6`; flere med komma) og `/* @end */`. `lds_build.py` fjerner blokke, hvis id ikke er projektets `config.id`; referencesiden får alle. Blokke indlejres ikke og omslutter altid hele regler. Touch-only: hierarkisk strimmel, `.boards`, `.bars`, `.plan`, `.dist`, vejrudsigtens Touch-udvidelser, typefelter, `.section-head`. V6-only: `.zc`, `.hp-limits`, probe-layout. Features (`"features"` i config) virker på samme måde: `@only legacy` = Dashboard/Konfiguration-modellen (sektioner, sektionslinks, Konfigurationens 1/2/3 spalter, «redigerer»-prikken), som kun configs med `"features": ["legacy"]` får med.

Ændringer i tokens eller komponenter: ret kilden, kør builds, og kontrollér referencesiderne i begge temaer. **Designændringer i produkterne (V6/Touch) lander her først**; ret aldrig kun den kopierede CSS/HTML i et produktrepo.

---

## 13. Vægskærmen på Lune Touch (LVGL)

Touch har en 1024×600 berøringsskærm med 16-bit farver (RGB565), der hænger på væggen. Den bruger samme farvebetydning, zonefelter og tone som web-UI'et, men er bygget til afstand, et hurtigt tryk og at være tændt hele døgnet. Mockups: canvasset "Lune Touch – vægskærm 1024×600".

### 13.1 Principper for væggen

- **Læsbar på 2–3 meter.** Hustemperaturen og zonetemperaturerne er de største ting på skærmen. Mindste tekst er 16 px.
- **Mørkt som standard.** Skærmen lyser i et rum døgnet rundt; mørk baggrund blænder ikke. Lyst dagtema er valgfrit.
- **Ét tryk til en zone, ét tryk tilbage.** Ingen menuer, ingen indstillinger. Opsætning sker i web-UI'et.
- **Viser altid hele huset.** Alle manifolds og alle zoner står på oversigten samtidig, uden scroll.
- **Går selv tilbage.** Zone-skærmen lukker efter 60 s uden berøring. Efter 2 min dæmpes skærmen; om natten vises dvale-skærmen.

### 13.2 Skærmene

| Skærm | Indhold | Hvordan man kommer dertil |
|---|---|---|
| Oversigt | Statuslinje, Huset-kort, én række pr. manifold med dens zonefelter | Start; tryk på dvale-skærmen; tilbage fra en zone |
| Zone (fuldskærm) | Aktuel temperatur, mål med − / +, tre forvalg, 24-t graf, ventil/retur/preload, zone til/fra | Tryk på et zonefelt; ‹ › skifter zone |
| Zone med fejl | Som zone, men fejlboks med "Nulstil fejl" øverst i højre kort | Tryk på fejlfelt eller fejlpillen i statuslinjen |
| Dvale/nat | Ur, hustemperatur, én prik pr. zone, eventuel fejl | Efter 2 min uden berøring (nat: 22–06) |

### 13.3 Layout (px)

```
0 ┌──────────────────────────────────────────────────────────────┐
  │ 14:32 ons 30. sep   ☁ 13,4 °C  ≋ 6 m/s     [⚠ M1·Z6: fejl]  ⌔ │ statuslinje 64
64├──────────────────────────────────────────────────────────────┤
  │ Huset ● Kalder │ graf 24 t        │ Varmepumpe   │ vejrbesked│ husrække 104
  │ 21,3° mål 21,6 │                  │ 34,2° → 29,8°│           │
  ├────────────┬─────┬─────┬─────┬──────┬─────┬─────┤             │
  │ Stueetage  │ Z1  │ Z2  │ Z3  │ Z4–5 │ Z5  │ Z6  │  manifold-  │
  │ M1         │21,4°│20,8°│22,6°│21,1° │21,0°│Fejl │  rækker     │
  │ 34,2°→29,8°│▬▬▬▭▭│▬▭▭▭▭│ …                       │  (1–4)      │
  │ ● Kalder   │     │     │                         │             │
600└────────────┴─────┴─────┴─────────────────────────┘
   16 margen · 10 mellem rækker · kort r=20 · felter r=14
```

- **Huset er en vandret række øverst**, samme form som manifold-rækkerne: hustemperatur og mål, 24-t graf, varmepumpe og pumpe, vejrbesked. Så får zonefelterne hele skærmens bredde.
- **Manifoldens navn, ID, fremløb/retur og tilstand står i en kolonne til venstre i rækken** (124 px kompakt, 150 px store felter), så felternes højde går til indhold.
- Zonefelter står **altid i 6 kolonner**, så Z1–Z6 flugter på tværs af manifolds. Tomme pladser vises som stiplede huller.
- **1–2 manifolds:** store felter (temperatur 36 px, mål, 24-t graf). **3–4 manifolds:** kompakte felter (temperatur 28 px med mål, ingen graf).
- Rækkerne deler højden ligeligt; ingen scroll. Budget ved 4 manifolds: 600 − 64 − 104 − 10 − 16 = 406 px til fire rækker á ca. 95 px.

### 13.4 Zonefelt på skærmen

Samme betydning som web-strimlen, men på væggen ligger niveaubjælken **vandret under tallet** (5 segmenter á 20 % ventilåbning), som på mobil. Det giver feltet fuld bredde til temperatur og mål.

| Tilstand | Udseende |
|---|---|
| Kalder | 1–5 tændte segmenter (tekstfarve, ikke orange) |
| Hviler | 1 segment |
| Fejl | ID og "Fejl" i rødt, første segment rødt |
| Slukket | 50 % opacitet, "Slukket" |
| Gruppe | Primær: 2 px violet kant, ID "Z4–5". Medlem: violet markering, dæmpet navn |
| Under mål > 0,5 °C | Temperaturen i gul |
| Presset | Inverteret i 150 ms |

### 13.5 Trykflader og kontroller

| Element | Størrelse |
|---|---|
| Alt man trykker på | mindst 64 × 64 px |
| − / + for mål | 96 px runde |
| ‹ tilbage, ‹ › forrige/næste zone | 64 px runde |
| Forvalg (Komfort, Eco, Nat) og handlinger | 64 px høje piller |
| Afstand mellem trykflader | mindst 12 px |

Mål ændres i trin á 0,5 °C og sendes til Touch 1,5 s efter sidste tryk (ingen gem-knap på skærmen). Forvalgene er et forslag: de kræver, at Touch får tre forvalgsværdier i konfigurationen.

### 13.6 Farver i RGB565

Skærmen kan kun vise 65.536 farver. Paletten i `tokens.json → display.palette` er de farver, skærmen faktisk viser.

- **Brug paletten præcis.** Værdierne er valgt, så de ligger på en 565-farve; andre værdier afrundes og kan få farvestik.
- **Grå er håndplukket.** Almindelig afrunding giver varme grå et grønt eller lilla skær (grøn har 6 bit, rød og blå 5). Kort og felter bruger 565-værdier, hvor kanalerne balancerer.
- **Ingen alfa, ingen gradienter.** Dæmpede baggrunde (fejl, info) er forblandede fuldfarver. Gradienter giver striber i 565, og alfa-blanding koster CPU.
- **Ingen skygger.** `shadow_width: 0` overalt; LVGL-skygger er dyre at tegne. Dybde kommer fra `card` på `bg` og `raised` på `card`.
- Tekst på `raised` bruger `muted`, ikke `faint`.

Kontrasten og hex ↔ RGB565 tjekkes med `python tools/lds_display.py --check`.

Den lyse palet følger samme regel som web (3.1): statusflader er mørkere end kortet og afledt af det — ikke kølige pasteller. `raised` i lyst tema er håndplukket (`#d6d2ce` / `0xD699`), så 16-bit kvantisering ikke giver grønt stik. Ret ikke display-hex i produktrepoet; ret `display.palette` her og kør `--check`.

### 13.7 Typografi

Geist, genereret til LVGL (ESPHome `font:` med `gfonts`, eller `lv_font_conv`), 4 bpp.

| Token | px | Brug |
|---|---|---|
| `xs` | 16 | Labels, akser, badges |
| `sm` | 20 | Knaptekst, statuslinje |
| `md` | 24 | Zonenavne i zone-skærmen |
| `lg` | 32 | Ur, titler |
| `xl` | 48 | Temperaturer i store felter |
| `2xl` | 72 | Mål i zone-skærmen (kun cifre) |
| `hero` | 144 | Aktuel temperatur i zone-skærmen (kun cifre) |

De to største fonte indeholder kun `0–9 , . ° − :` for at spare flash.

### 13.8 Grafer

- 24 timer med ét punkt pr. halve time (48 punkter): `lv_chart` af typen linje med to serier, temperatur (`fg`, 2 px) og mål (`muted`, stiplet eller tyndere).
- Afvigelsesfladen fra web-graferne kræver et draw-event eller et `lv_canvas`; den kan udelades på skærmen uden at miste betydning.
- Aksen spænder over mindst 3 °C, og målet tegnes som trappe, præcis som på web.

### 13.9 LVGL-opbygning

| Del | LVGL |
|---|---|
| Oversigt | Én side (`page`) med flex-kolonne: statuslinje, derefter grid `296px 1fr` |
| Manifold-række | `obj` med stil `lds_card`, grid med 6 kolonner |
| Zonefelt | `button` med stil `lds_tile`; segmenter som 5 små `obj` (`lds_seg_on/off/fault`) |
| Zone-skærm | `tileview` med én tile pr. zone (swipe og ‹ › skifter), åbnes over oversigten |
| Mål | `label` med `lds_font_2xl` + to `button` (`lds_btn_round`, 96 px) |
| Zone til/fra | `switch` (tændt: `inv_bg`) |
| Dvale | Egen side, sort baggrund, lysstyrke via baggrundslys |

`dist/display/lune_theme.yaml` er en ESPHome-pakke med farver, fonte og `style_definitions`; `lune_theme.h` har de samme værdier som C-konstanter. Begge genereres af `tools/lds_display.py`. Stilnavne og egenskaber i YAML-pakken skal tjekkes mod jeres ESPHome-version.

### 13.10 Sprog

Skærmen bruger de samme kataloger og nøgler som web-UI'et, og sproget vælges ved build (første sprog i listen). Brugerdata (zone- og manifoldnavne) oversættes aldrig.

### 13.11 Firmwarefiler og brand

`display` i `tokens.json` har palet, nat-farver (`night`, dæmpede neutrale grå til dvale), typeskala (inkl. rene ciffer-størrelser `d28`–`d168`), mål og tider. `python tools/lds_display.py --install <produktrepo>` genererer firmwarefilerne ud fra produktets `lds.yaml` (schema 2: `display-header` → `lune_theme.h`, `display-yaml` → `tokens.generated.yaml`, `display-cpp` → `lune_design_tokens.h`, `brand-png`, `brand-svg`, `logo`); `--install … --check` diff'er mod de committede filer (`make design-verify`). Firmwarefilerne bruger den udfoldede 565-farve (det skærmen viser). Brand-mærkerne (seksrørs-halo) kommer fra `tokens/brand.json` via `tools/lds_brand.py`. Erstatter det gamle `Birkemosen/lds`.

---

## 14. Hele huset: Home Assistant og andre systemer

LDS er temaet for hele huset, ikke kun Lune. Andre systemer bruger de samme tokens og den samme farvebetydning, genereret fra `tokens/tokens.json`.

### 14.1 Husets farvesprog: tre lag

Farver i hele huset (Lune, Home Assistant, dashboards) betyder noget, og betydningen ligger i tre adskilte lag. Et element bruger kun ét lag ad gangen.

**Lag 1: Status** — *er det i orden?* Kun i badges, beskeder, fejl og bekræftelser.

| Token | Betyder |
|---|---|
| `ok` | I orden, låst, online |
| `warn` | Kræver opmærksomhed snart (ulåst, mangler læring) |
| `danger` | Fejl der kræver handling nu, alarm udløst |
| `info` | Neutral information |

**Lag 2: Domæner** — *hvad handler det om?* Ikoner, felter, linjer og flader i grafer. Én kulør pr. domæne, samme lyshed og farvestyrke (OKLCH), så ingen domæner råber højere end andre (undtagen varme, der er Lunes hovedsignal).

| Token | Domæne | Eksempler |
|---|---|---|
| `dom-heat` | Varme | Opvarmning, fremløb, varmt vand, gas |
| `dom-cool` | Kulde | Køling, retur, frost, strøm fra nettet |
| `dom-light` | Lys og sol | Lamper tændt, sol over horisonten, solproduktion |
| `dom-water` | Vand og luft | Cirkulation, vandforbrug, ventilation, fugt, ventiler |
| `dom-energy` | Energi | Egenproduktion, eksport til nettet, fossilfri strøm, sparet energi |
| `dom-nature` | Natur og liv | Planter, have, vanding, personer hjemme |
| `dom-security` | Sikkerhed | Alarm tilkoblet, døre/vinduer, bevægelse |
| `dom-plan` | Plan og automatik | Odin, tidsplaner, scener, automationer, robotstøvsuger |
| `dom-media` | Medier | Musik, TV, højttalere |

**Lag 3: Skalaer** — *hvor godt / hvor varmt i forhold til en norm?* Altid 5 trin, aldrig en glidende gradient (se 3.2.2).

| Token | Trin 1 → 5 | Bruges til |
|---|---|---|
| `scale-good-bad-1…5` | grøn → gulgrøn → gul → orange → rød | Elpris (billig → dyr), luftkvalitet, batteri (fuld → tom), signal |
| `scale-cold-warm-1…5` | blå → lyseblå → neutral → lys orange → orange | Temperatur i forhold til mål (varmekortet) |

**Grøn betyder "godt for huset" i alle tre lag:** status `ok`, `dom-energy` (egenproduktion, eksport, billig strøm), `dom-nature` (planter, liv) og godt-enden af `scale-good-bad`. De tre grønne er skilt ad på kulør og lyshed: energi er dybere smaragdgrøn, natur lysere og mere gulgrøn, status ok imellem.

Regler:
- Domænefarver bruges til ikoner, linjer og flader, ikke til brødtekst (i lyst tema er nogle under 4,5:1, men alle ≥ 3:1 som grafik).
- Et domæne har samme farve overalt: lamper er altid varm gul, uanset om de vises i HA, på et dashboard eller i en graf.
- Er noget både et domæne og en status (fx en alarm, der er udløst), vinder status.
- Nye domæner skal have en kulør mindst 25° fra de eksisterende og ligge i samme lysheds- og farvestyrkebånd.

### 14.2 Home Assistant

`python tools/lds_ha.py` → `dist/home-assistant/themes/lune.yaml`: ét tema "Lune" med lyst og mørkt tilstand.

- **Primærfarven er inverteret** (lys på mørk, mørk på lys), som LDS' primære knapper. **Accentfarven er `dom-energy` (grøn)**: skydere, valgt menupunkt og fremhævninger, så HA får hjemmets grønne tone uden at orange mister betydningen "varme".
- **Tilstande følger domænerne:** varme orange, køling blå, auto grøn, ventilation og fugt turkis, lys varm gul, planter og personer hjemme lysegrøn, alarm indigo, medier magenta, automationer violet. Låse følger status: låst grøn, ulåst gul, fastlåst rød.
- **Energi:** sol varm gul, eksport og fossilfri grøn, strøm fra nettet blå, batteri violet, gas orange, vand turkis.
- Kontakter tændt = inverteret, ikke farvet.
- **Kort:** radius 16, ingen skygge, hårfin kant i lyst tema.
- **Font:** Geist via `dist/home-assistant/www/lune-font.js` (`frontend: extra_module_url`), eller selvhostet.

Installation:
```yaml
frontend:
  themes: !include_dir_merge_named themes
```
Kopiér `dist/home-assistant/themes/lune.yaml` til `/config/themes/` og `dist/home-assistant/www/fonts/Geist-Variable.woff2` til `/config/www/fonts/`. Genindlæs temaer (`frontend.reload_themes`) og vælg "Lune" under profilen.

**Med UI eXtension (UIX)** — efterfølgeren til card-mod — får HA også resten af LDS direkte fra temaet:

| UIX-nøgle | Hvad |
|---|---|
| `uix-theme: Lune` | Aktiverer UIX-styling for temaet |
| `uix-fonts` | Indlæser Geist (selvhostet, variabel vægt 100–900) |
| `uix-card` | LDS-dybde på alle kort: lys topkant og bløde lagdelte skygger |
| `uix-root-yaml` | Topbjælken som svævende, let gennemsigtig pille (navbar, 5.1) |
| `uix-dialog` | More-info og andre dialoger som svævende ark med store radier (detaljearket) |

Uden UIX ignoreres nøglerne, og temaet virker stadig (farver, radier). `lune-font.js` er kun nødvendig uden UIX. HA's interne markup ændrer sig mellem versioner, så selektorerne i `uix-root-yaml` (`.header`, `.toolbar`) skal tjekkes med browserens inspektør efter større HA-opdateringer.

### 14.3 Dashboards i Home Assistant

Brug samme opbygning som Lune Touch: **overblik → detaljer → indstillinger**.

- Sektions-visning med tile-kort. Ét tal pr. kort.
- Tryk på et kort åbner HA's more-info-dialog med graf og historik. Det er detaljearket.
- Indstillinger ligger bag tandhjulet, aldrig på overblikket.
- Kortfarver følger datapaletten (fremløb orange, retur blå).

Se `examples/home-assistant/varme-dashboard.yaml`.

### 14.4 Kendte begrænsninger

HA's temavariabler ændrer sig mellem versioner. De fleste variabler i temaet er stabile; `ha-font-family-body`, `ha-badge-border-radius` og `state-*-color`/`rgb-state-*` skal tjekkes mod din version (ukendte variabler ignoreres bare). Graffarver i HA's indbyggede grafer kan ikke styres fuldt af et tema; brug fx apexcharts-card med LDS-farverne, hvis graferne skal matche helt.

---

## 15. Informationsarkitektur: hjem, ark og system

Erstatter tilstandspillen Dashboard/Konfiguration (afsnit 2.2–2.3). I stedet for to parallelle verdener har UI'et ét hjem, hvor man kan grave ned, og én side til systemet.

### 15.1 Tre niveauer

| Niveau | Hvad | Hvordan man kommer dertil | Eksempler |
|---|---|---|---|
| **Hjem** (overblik) | Det vigtigste nu + hverdagshandlinger (autogem) | Navbar › Hjem | Hustemperatur og mål, varmefelter, varmekort |
| **Ark** (pr. ting) | Alt om én ting: overblik, historik, **dens** indstillinger | Tryk på tingen på Hjem | Et rum, en manifold, varmepumpen, vejret, pumpen |
| **System** (side) | Det der gælder hele enheden | Navbar › System | Enhed, styringer, varmekilde-forbindelse, netværk, firmware, backup, service |

Testen for, hvor en indstilling hører til: *Hører den til én ting, man kan pege på i huset?* → arket for den ting. *Gælder den enheden eller forbindelser?* → System. *Ændres den i en almindelig uge?* → også som hurtig kontrol på Hjem.

### 15.2 Navbar

Midten har altid de samme to punkter: **Hjem** og **System** (på Touch og i HA kan der komme flere visninger, fx Energi). Zonestrimlen/manifold-felterne vælger omfang på Hjem; de vises ikke på System.

### 15.3 Arket

- Glider ind fra højre (bredde **520 px**, svævende med 12 px afstand og radius 24), ark fra bunden på mobil. Native `popover` eller `<dialog>`; Esc og klik udenfor lukker.
- Hoved: ikonbrik, navn, én linje med status (fx "1. sal · Z1 · 22,9° · 62 % åben"), luk-knap.
- **Faner:** Overblik · Historik · Indstillinger. Åbnes fra et tal eller en graf → Overblik; fra et tandhjul eller et "Indstillinger"-link → Indstillinger.
- **Kun faner med indhold.** Overblik er altid med; Historik kun når der findes historik (en graf eller en opsummering), Indstillinger kun når tingen har indstillinger. Et ark med kun Overblik viser ingen fanebjælke-tomhed: fanerne udelades eller står alene på Overblik. Opfind ikke en graf for at fylde en fane.
- Indstillinger i arket bruger grupperede lister (15.5), én gem-bjælke nederst (klæber), farlige handlinger sidst.
- Sjældne indstillinger åbner som en ny side **inde i** arket (med tilbage-pil), ikke i et nyt ark.

### 15.4 System-siden

- To kolonner på bred skærm: kategoriliste til venstre (**240 px**, klæber), indhold til højre (**maks. 640 px**, venstrestillet). Mobil: kategorilisten er en side; tryk åbner kategorien.
- Kategorier (V6): Enhed · Manifold og motorer · Forbindelser · Firmware og backup · Service. (Touch): Enhed · Styringer · Varmekilde · Elpris · Cirkulationspumpe · Vejr · Netværk · Firmware og backup · Service. Udvikler kun i dev-builds, sidst.
- Én kategori ad gangen; én gem-bjælke pr. kategori.

### 15.5 Indstillinger præsenteres som grupperede lister — ikke kort

Indstillinger er ikke dashboard-indhold, så de bruger **ikke** bento-kort. De bruger grupperede lister (som iOS/Homey-indstillinger):

- En **gruppe** = lille overskrift (13 px, dæmpet) + én afrundet flade (`--raised`, radius 14) med rækker adskilt af hårfine linjer.

```html
<section class="setting-group">
  <h4>Måling</h4>
  <div class="setting-list">
    <div class="setting">
      <div class="setting-label"><label for="probe">Fremløbsprobe</label><small>hint</small></div>
      <div class="setting-control"><select class="select" id="probe">…</select></div>
    </div>
  </div>
</section>
<!-- Gating: .setting-list.gated med label.setting.switch først og afhængige rækker i .gated-body -->
<!-- Lang værdi: .setting.stack lægger kontrollen under labelen i fuld bredde -->
```
- En **række** = label (14 px, 500) og evt. hint under (12 px, dæmpet) til venstre, kontrol til højre med naturlig bredde. Min. højde 52 px.
- **Maks. 6 rækker pr. gruppe, maks. 5 grupper pr. side/fane.** Mere end det → flyt til "Avanceret" (en række med › der åbner en underside).
- Rækkefølge: det oftest ændrede først; afhængige felter lige under den switch der styrer dem (gating).
- Læse-værdier (fx "Sendes nu") står i samme gruppeform, med værdien til højre i 600-vægt og tid dæmpet.

### 15.6 Bredde på kontroller

| Kontrol | Bredde |
|---|---|
| Stepper | 140 px (ark/system), 168 px (paneler) |
| Talfelt uden trin (port, id) | 9ch (`.w-xs`) |
| Kort tekst (navn, lat/lon) | 16ch (`.w-sm`) |
| Host, MAC, entity | 22–26ch (`.w-md`) |
| URL, lang variabel | 28–44ch (`.w-lg`), eller under labelen i fuld bredde |
| Select | 160–220 px, efter længste valgmulighed |
| Segment | Efter indhold; 2–3 valg |
| Switch | 46 × 28 px |

Ingen kontrol strækkes for at fylde rækken. På mobil (< 600 px) må en kontrol under labelen fylde bredden.

### 15.7 Hjem: hvor mange kort, og hvad står på dem

- **Én hovedsektion** øverst (husets temperatur og mål + én sætning om situationen).
- **Maks. 4 felter i én række** (desktop), 2 på tablet, 1 på mobil. Ét felt = én ting.
- **Hvert felt:** ikonbrik + titel + én statuslinje; **ét** hovedtal (+ evt. ét sekundært tal); **én** lille visualisering. Hele feltet åbner tingens ark.
- **Derefter rummene** (varmekort eller rumliste) og højst én sektion mere. Alt andet ligger i arkene.
- Ingen indstillinger på Hjem ud over hverdagshandlinger (mål, til/fra), og de autogemmes.

### 15.8 Hvordan information præsenteres

| Information | Form |
|---|---|
| Det vigtigste tal i en visning | Hero (én pr. visning) |
| Nøgletal | Metric: tal + enhed i `<small>` + label under |
| Detaljer | kv-række: label venstre, værdi højre, maks. ~4 ord |
| Tilstand | Badge (ét pr. hoved), eller statuslinje under titlen |
| Situationen i hele huset | Én sætning (statuslinje) |
| Udvikling over tid | Linjegraf (én fyldt serie) |
| Plan / mængde pr. tidsrum | Søjler |
| Fordeling | Donut eller stablet bjælke (maks. 4 dele) |
| Afvigelse fra mål | Skala-chip i 5 trin |
| Mængde (åbning, fremdrift) | Neutral segmentbjælke eller bjælke + procent |
| Forklaring | "?"-popover eller hint; aldrig som værdi |
| Teknisk/diagnostik | Ark › Avanceret eller System › Service |

### 15.9 Markup: ark, faner, gem-bjælke, underside og System

**Tilstande i config.** `"modes": ["home", "sys"]`. `home` har en visning pr. omfang (`#v-home-{omfang}`, som før); `sys` har **én** visning, `#v-sys`, uanset omfang, og strimlen skjules på System. Radioerne er `#m-home` / `#m-sys` i navbarens pille («Hjem» / «System»). `systemCategories` (liste af id'er) genererer System-sidens kategoriregler. Configs med `dash`/`conf` bygger stadig, men `lds_build.py` advarer.

**Ark (`.sheet`).** Native `popover` (auto): Esc og klik udenfor lukker. Åbnes af ethvert element med `popovertarget`. Desktop: fast i højre side (`--sheet-w` 520 px, `--sheet-inset` 12 px fra kanterne, `--r-sheet` 24, `--float`-skygge, `::backdrop` 35 % sort). Under 600 px: ark fra bunden, maks. 92vh, safe-area nederst. Arket selv scroller; hoved og gem-bjælke klæber.

```html
<div id="sheet-z1" popover class="sheet" role="dialog" aria-labelledby="sheet-z1-t" data-hash="z1">
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-o" value="overview" data-hash="overblik" checked aria-label="Overblik">
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-h" value="history"  data-hash="historik" aria-label="Historik">
  <input class="state tab" type="radio" name="tab-z1" id="tab-z1-s" value="settings" data-hash="indstillinger" aria-label="Indstillinger">
  <header class="sheet-head">
    <span class="chip-icon" data-tone="…" aria-hidden="true"><svg …/></span>
    <div><h2 id="sheet-z1-t">Josephine</h2><p>1. sal · Z1 · 22,9° · 62 % åben</p></div>
    <button class="sheet-close" type="button" popovertarget="sheet-z1" popovertargetaction="hide" aria-label="{i18n: Luk}">×</button>
    <nav class="tabs" aria-label="{i18n}"><label for="tab-z1-o" data-tab="overview">Overblik</label>…</nav>
  </header>
  <div class="sheet-body">
    <section class="tab-panel" data-tab="overview">…</section>
    <section class="tab-panel" data-tab="history">…</section>
    <section class="tab-panel" data-tab="settings"><form data-save="z1">… <footer class="savebar">…</footer></form></section>
  </div>
</div>
```

- **Ikonbrik** (`.chip-icon`): dyb fyld-tone med hvid glyf; `data-tone` = `info` / `ok` / `violet` / `warn` / `danger` / `neutral` (standard er varme).
- **Faner** (`.tabs`): radioerne står først i arket; panelerne vises ud fra radioens `value` (`overview` / `history` / `settings`) — der genereres ingen CSS pr. ark. Uden JS åbner arket på Overblik. En trigger med `data-tab="settings"` åbner det på Indstillinger (JS sætter radioen før popoveren vises).
- **Ugemte ændringer:** lukkes et ark med en ændret formular, spørger `lune-forms.js` (`rt.leaveUnsaved`); fortryder man, åbnes arket igen, ellers nulstilles formularen. Uden JS lukker det bare.

**Gem-bjælke (`.savebar`).** Sidst i formularen, én pr. fane eller System-kategori. Klæber nederst. Følger dirty-mønstret (6.1): clean = neutral `.btn.primary` med `aria-disabled`, ingen Fortryd; dirty = «n ændringer ikke gemt» i `--warn` + Fortryd + primær Gem.

```html
<footer class="savebar">
  <span class="save-status" id="ss-z1" aria-live="polite"></span>
  <button type="reset" class="btn">Fortryd</button>
  <button class="btn primary" type="submit">Gem</button>
</footer>
```

**Underside («Avanceret ›»).** En række, der åbner sjældne indstillinger i samme ark eller kategori. `details.subpage` — uden JS. Når den er åben, skjules resten af fanen, og rækken bliver til «‹ Tilbage».

```html
<details class="subpage">
  <summary class="setting"><span class="sub-back">Tilbage</span>
    <span class="setting-label"><span>Motor og kalibrering</span></span>
    <span class="setting-control"><span class="muted">Lært</span></span></summary>
  <div class="subpage-body"><section class="setting-group">…</section></div>
</details>
```

**System-side (`.sys`).** Kategoriliste (`--sys-nav-w` 240 px, klæber) + indhold (maks. `--sys-main-w` 640 px, venstrestillet). Kategorierne er radioer (`name="syscat"`, `id="c-{kategori}"` og `id="c-none"`), som står lige før `.sys` (eller før `.app`). `c-none`: desktop viser første kategori, mobil viser listen som en skærm; en valgt kategori viser `.sys-back` («‹ System», label for `c-none`) øverst.

```html
<input class="state" type="radio" name="syscat" id="c-none" checked aria-label="{i18n}">
<input class="state" type="radio" name="syscat" id="c-device" data-hash="enhed" aria-label="{i18n}">
<div class="sys">
  <nav class="sys-nav" aria-label="{i18n}"><label for="c-device"><svg …/>Enhed</label>…</nav>
  <div class="sys-main">
    <section class="sys-cat" data-cat="device">
      <label class="sys-back" for="c-none">System</label>
      <header><div><small>System</small><h2>Enhed</h2></div><span class="badge ok">…</span></header>
      <form data-save="device">…grupper… <footer class="savebar">…</footer></form>
    </section>
  </div>
</div>
```

**Deep links** (progressiv, `lune-forms.js`). `data-hash` på ark, faneradioer, `#m-sys` og kategoriradioer bestemmer adressen: `#z3` åbner arket, `#z3/indstillinger` på fanen, `#system/varmekilde` System på kategorien. Adressen opdateres ved navigation (`history.replaceState`). Skift væk fra System eller en kategori med ugemte ændringer advarer.

### 15.10 Hjem: hovedsektion, termostat-ring, felter og varmekort

Touch' Hjem (15.7) bygges af disse komponenter (`@only touch` i CSS'en):

- **`.home-scopes`**: øverst. Huset som `div.scope[aria-current="page"]` (inverteret, «Hus» + hustemperatur) og ét `button.scope` pr. styring (M-id, navn, `.mini` med én lodret søjle pr. zone = ventilåbning i 5 trin `data-level="0–5"`, fejl `data-state="fault"`). Kortet åbner styringens ark. Offline: `data-offline` dæmper søjlerne, og `<small>` får «· Offline». Mobil: to kort pr. række, uden søjler.

- **`.home-hero`**: `.hero-text` med `<small>` hilsen, `h2` overskrift i to toner (situationen + `<span class="sub">` om varmekilden, dæmpet; tom = skjult), `p` uddybning og nederst `.hero-facts` med højst to `button.hero-fact` (varmekilde, næste varme; åbner tingens ark; skjult under 768 px, hvor felterne står lige under). Overskriften er `--fs-display` (48 px), under 1100 px `--fs-2xl`. Bag hovedsektionen ligger `svg.hero-waves` (højdekurver fra builderen, tynde streger i varmefarven, ingen flader, beskåret med `slice`): systemets eneste tekstur, kun her. **`.thermo`**: husets temperatur og mål i en 270°-ring. Ringen har én farve (varme, 3.2.2): sporet er neutralt, buen er husets temperatur nu (`--now`, 0–100 % af skalaen) med et håndtag (`.knob`) for enden. Målet står under ringen, ikke på den, så bue og mærke aldrig peger to steder hen. I midten: «Huset nu», tallet og «ude x°» (tom = skjult). Målet ændres med − / + under ringen (`.climate .target`) og autogemmes; feltet er et tekstfelt (`inputmode="decimal"`, `data-min`/`data-max`/`data-step-size`) med `<span class="unit">°</span>`, så decimaltegnet følger sproget og ikke browserens. Ringen har `role="img"` med en aria-label, der siger temperatur og mål.
- **`.home-head`**: lille linje + titel over felterne («Lige nu / Varme, plan og vejr»).
- **`.home-tiles` › `button.home-tile`**: højst fire felter i en række (to på tablet, ét på mobil). Hvert felt er én ting og åbner tingens ark (`popovertarget`): `.chip-icon` + `b` titel + `.ht-status` (én linje) + `.ht-val` (ét hovedtal, evt. ét sekundært tal i `<small>`) + `.ht-viz` (én lille SVG, 240 × 64, i bunden af feltet; plan-søjler i violet over en stiplet basislinje, vejr som flade med skraveret forvarmning). Valgfrit `.ht-price` til én ekstra linje, fx aktuel elpris med `.scale-chip[data-scale="1–5"]` (`scale-good-bad`). Uden data: `data-empty` skjuler visualiseringen, og tallet er «—».
- **`.heatmap` › `.room-group`**: rum grupperet pr. styring. `button.room-group-head` (navn + `<small>` med M-id og fremløb → retur) åbner styringens ark; `.room-grid` har rummenes felter (`button.tile`), som åbner rummets ark: navn + afvigelses-chip (5.2), temperatur, 5-trins ventilbjælke (`data-open="0–5"`, 20 % pr. segment) og «x % åben» / «lukket». Felterne fylder efter rummets areal (`style="--area:<m²>"`, mindst `--room-min`); zone-id og gruppering vises i rummets ark, ikke her. `.heatmap-head` har lille linje + titel og `.heatmap-legend` (chippens 5 trin og ventilbjælken). Styring offline: `data-offline` dæmper de seneste værdier, og `.offline-note` siger «V6 er ikke tilgængelig · senest set …». En note (`.heatmap-note`) forklarer chip og bjælke.

**Grafer i ark.** `.hchart` (fx Varme: fremløb/retur fra varmekildens egen historik): tal øverst (`.metrics` med `.c-heat`/`.c-info` + status-badge), intervalvalg som radioer + labels (`.hchart-range`, 24 t / 7 d, ingen JS), én `.hchart-panel[data-range]` pr. interval med y-akse (`.hchart-y`), `svg.trend` (`.grid`, `.dt`-glød under fremløb, `.r`, stiplet `.nowl`) og x-akse i HTML, og fælles `.chart-legend`. Uden data: `data-empty` på panelet. Vejrprognosen er `.fc.fc--stack`: én lille graf pr. størrelse over hinanden (vejr, temperatur med forvarmning, sol, vind, vindretning) med navn og spænd i venstre kolonne (`.fc-lab`), fælles x-akse og nu-linje; på mobil står navnet over grafen.

**Link til indstillinger.** Står en tings forbindelse eller enhedsindstillinger på System, slutter arket med `a.sheet-link` («Indstillinger for varmekilde ›», `href="#system/<kategori>"`). `lune-forms.js` lukker åbne ark, når et link skifter tilstand.

**Navbar ved scroll.** På ≥ 768 px bliver `.header` kun så bred som indholdet, når `.navbar-wrap` sidder fast øverst (`container-type: scroll-state` + `@container scroll-state(stuck: top)`); uden understøttelse forbliver den bred.

Rum-arket på Touch viser kun det, Touch ejer (medregn i hustemperaturen, vægt, vind, sol). V6' egne felter, som Touch' API leverer (areal, ydervægge), står som læseværdier (`.setting-value`) i gruppen «Fra V6 (<styring>, Z<n>)» med «Redigér på V6 ›» (ny fane, `http://<v6>/#z<n>/indstillinger`); er V6'en offline, er gruppen dæmpet (`.setting-group[data-offline]`) med samme note.

