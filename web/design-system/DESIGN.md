# Lune Design System 2

Fælles designsystem for **Lune V6** (6-zoners manifold-controller) og **Lune Touch** (koordinator for 1–4 V6-boards). Dokumentet beskriver *hvorfor* og *hvordan*. Værdierne står i `tokens/tokens.json`, koden i `css/lune-ui.src.css`, og en visuel reference med levende eksempler i `docs/design-system.html`.

Når dette dokument og koden er uenige, er det en fejl i et af dem. Ret den ene, så de passer igen.

---

## 1. Principper

**1. Et blik er nok.** Brugeren står ved manifolden eller kigger kort på telefonen. De vigtigste tal er store, labels er små og står under tallet. Én aktuel temperatur pr. visning må være hero-størrelse.

**2. Farve betyder noget.** Alt er neutralt, indtil der er noget at sige. Orange betyder varme, og hver statusfarve har præcis én betydning (se 3.2). En farve bruges aldrig til pynt.

**3. Samme sted, samme ting.** Zonestrimlen står øverst i alle visninger og vælger *hvad* man ser på. Tilstandspillen vælger *hvordan*: Dashboard (se og styre) eller Konfiguration (opsætning). Z3 er altid det samme sted.

**4. Få store paneler, altid åbne.** Indhold grupperes i 2–4 paneler pr. visning. I Konfiguration grupperes paneler yderligere i sektioner (4.3). Kun det sjældne og det risikable foldes (Service/Udvikler, eller `.more` til ekspert-tuning).

**5. Tilstand uden JavaScript.** Navigation, faner, tema, sektionsfold og fold-ud er ren HTML/CSS. JavaScript bruges kun til live-data, +/−-knapper, formularer og at åbne en lukket sektion ved ankerlink, og alt virker (med færre bekvemmeligheder) uden.

**6. Tilgængelig fra start.** Al tekst har mindst 4,5:1 kontrast i begge temaer. Trykflader er mindst 44 px (48 px på touch); med mus på bred skærm er kontroller 32 px høje (3.9). Alt kan betjenes med tastatur og har synlig fokus.

---

## 2. Arkitektur og navigation

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
| Visning | Automatisk | én pr. kombination — eller delte visninger når `shared_views` | V6: `#v-{mode}-{omfang}` · Touch: `#v-{mode}-house\|manifold\|zone` |

Tilstand og omfang er to radiogrupper, der ligger **før** `.app` i HTML, i denne rækkefølge: tilstand, omfang, tema. CSS'en viser præcis den visning, der matcher begge. Reglerne genereres af `tools/lds_build.py` ud fra projektets config.

På Touch med `shared_views: true` er der kun seks visninger (`house` / `manifold` / `zone` × modes). Binderen fylder indholdet ud fra den checkede radios `id` og `data-m`. Navigationen er stadig ren CSS (`data-kind="manifold|zone"`).

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
| Strimmel | standard (én række, alle felter) | `strip--tiers`: hus + manifolds; `.substrip` med zoner når manifold/zone er valgt |
| Visninger | én pr. omfang × mode | `shared_views`: `house` / `manifold` / `zone` × mode |
| Varmekilde | — | `heat_source_types`: build-time liste (`http`, `asgard`, …); første er standard |

**Fravalgte alternativer til Touch-strimlen:** vandret scroll med 24 felter (`strip--many`) åd viewporten; wrap i flere rækker skjulte indholdet under; en overlay-menu brød «ingen modaler»-reglen. Hierarki med max 5 felter på niveau 1 og max 6 på niveau 2 holder overblikket uden scroll.
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

**Dæmpede statusflader i lyst tema** (`--info-bg`, `--ok-bg`, `--warn-bg`, `--danger-bg`, `--violet-bg`) afledes af kortet i OKLab: kortets lyshed −0,035, statuskulør med chroma 0,022 (`tokens.json` → `$tint`, udregnet i `lds_build.py`). Brug **aldrig** faste pasteller til flader, der ligger på et kort.

*Årsag:* De gamle pasteller var beregnet til den lyse sidebaggrund. På det varme grå kort (`--card`) var de lysere end kortet (flade/kort kun ~1,03–1,10:1) og så ud som huller; de kølige (info, violet) passede heller ikke til kortets varme.

Mørkt tema beholder faste hex-værdier for `-bg` (ingen tint-opskrift).

### 3.2 Farvernes betydning

| Farve | Token | Betyder | Eksempler | Aldrig |
|---|---|---|---|---|
| Orange | `accent` | Varme og afvigelse fra mål | Niveausegmenter, ventilbjælker, "Kalder"-badge, afvigelsesflade i grafer, preload-bånd, fremløbskurve | Fejl, links, pynt |
| Blå | `info` | Vejr, prognoser, sensorer, forbindelser | Vejrbesked, vindgraf, returkurve, "Temperatur fra BLE-sensor" | Handlinger |
| Grøn | `ok` | I orden | "Online", motor "Lært" | Tændt switch (den er sort/hvid) |
| Gul | `warn` | Kræver opmærksomhed snart | Mangler læring, manuel tilstand, zone > 0,5 °C under mål | Fejl der blokerer drift |
| Rød | `danger` | Fejl der kræver handling nu, destruktive handlinger | Motorfejl, "Nulstil og genlær…" | Varme, "koldt" |
| Lilla | `violet` | Læring, kalibrering, gruppering | Adaptiv balancering, lærte faktorer, "Grupperet med Z4" | — |

Regler:
- **Én betydning pr. farve.** Findes der ikke en passende betydning, er elementet neutralt.
- **Farve er aldrig det eneste signal.** Status har altid også tekst ("Motorfejl") eller form (rød prik, rødt segment + tekst).
- **Maks. én fejlflade pr. visning** (`.panel.alert`), øverst. Flere fejl samles i den.
- Status**flader** bruger altid den afledte `-bg`-token (aldrig status-blækfarven som baggrund).

### 3.3 Kontrast

Kravene står i `tokens.json` under `contrast` og tjekkes med:

```
python tools/lds_build.py --check
```

Tekst ≥ 4,5:1 mod den flade, den står på (primær tekst ≥ 7:1). Kort mod sidebaggrund ≥ 1,24:1. `--check` dækker også de **afledte** statusflader (tint → færdige hex før måling). Buildet advarer, hvis et token falder under. Nye farvetokens skal have en linje i `contrast`, hvis de bruges som tekst.

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

Dybde kommer fra **baggrundskontrast**, ikke skygger. Kort er `--card` på `--bg`, plus `--card-edge` (kun synlig i lyst tema). Skygger bruges kun på elementer, der svæver over indhold: enhedsmenuen og tilstandspillen på mobil.

### 3.8 Bevægelse

Kun som svar på en handling: farve- og baggrundsskift på 200 ms med `--ease`. Ingen animationer ved indlæsning, ingen scroll-effekter. `prefers-reduced-motion` slår alle overgange fra.

### 3.9 Tæthed og trykflader

Tætheden afgøres af inputtypen. Touch er komfortabel (store flader), mus på en skærm ≥ 1024 px er **kompakt**. Værdierne står i `tokens.json` (`size`, `type` og `density.compact`); `lds_build.py` genererer kompakt-reglerne.

| Token | Standard | Touch (`pointer: coarse`) | Kompakt (mus, ≥ 1024 px) |
|---|---|---|---|
| `--hit` | 44 px | 48 px | 32 px |
| `--field-gap` | 12 px | 12 px | 6 px |
| `--panel-pad` / `--panel-gap` | 24 / 16 px (mobil 16 / 16) | som standard | 18 / 14 px |
| `--fs-panel` / `--fs-view` | 18 / 32 px (mobil 18 / 18) | som standard | 15 / 26 px |
| `--row-h` (`.kv`, tabel) | 40 px | 40 px | 36 px |
| `--tile-h` (zonefelt) | naturlig | naturlig | 52 px (ID + navn på én linje, værdi under) |

- `data-density="compact|comfortable"` på `.app` tvinger tætheden (fx til skærmbilleder eller en installatør-indstilling).
- Alle kontroller bruger `--hit` til højde. Inputs har altid mindst 16 px skrift, så iOS/iPadOS ikke zoomer ind.
- Kompakt ændrer også feltbredder (5.10) og strammer luft i header, sektioner og beskeder. Touch-layoutet ved 390 px er uændret.
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

**Dashboard: kort på samme række har samme højde.** Panelets footer (`.panel-foot`) skubbes altid til bunden, så knapper flugter. Et panel, der bliver mere end dobbelt så højt som naboen, skal enten have mere indhold, stå alene i rækken eller flettes ind som `.sub` i naboen.

**Konfiguration: 1/2/3 lige spalter** (< 900 / ≥ 900 / ≥ 1280 px; en sektion med kun to paneler får højst to). Paneler har naturlig højde og stables oppefra (`align-items: start`), så ingen panel har tom bund. `cN` betyder her «én spalte»; et element uden `cN` eller med `.wide` spænder alle spalter. Der er ingen ens højde og ingen `.stack` i Konfiguration — `.stack` er udgået.

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

Lange konfigurationsvisninger grupperer paneler i `.section` under en `.section-h`. Dashboard bruger ikke sektioner.

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
- **Sektionslinks** (`.section-nav`): vises ved 3+ sektioner. Pilleformede ankerlinks; vandret scroll på mobil (samme stil som `.doc-nav`). Klæber ikke. `scroll-margin-top: var(--section-scroll-margin)` så overskrifter ikke gemmes under sticky header/strimmel.
- Dirty/fault: `data-dirty` (warn-prik) / `data-fault` (danger-prik) på sektion og tilhørende link, efter 6.1.
- **Kun det sjældne og det risikable foldes.** Kun Service og Udvikler må være `details.section` (lukket som standard). Øvrige sektioner er altid åbne. Et link til en lukket sektion åbner den med én linje `hashchange`-JS; uden JS lander ankeret på summary.
- Lukket sektion med ugemte ændringer eller fejl viser prikken i summary.

---

## 5. Komponenter

Hver komponent har: formål, markup, varianter, regler og tilgængelighed. Levende eksempler står i `docs/design-system.html`.

### 5.1 Header

Enhedsvælger til venstre, tilstandspille i midten, sprog og tema til højre.

- **Enhedsvælger** (`details.device`): logo, enhedsnavn og placering. Dropdownen (`div.device-menu`) har øverst **Om enhed** (identitet: navn, placering, IP, MAC, firmware, ESPHome-version, oppetid + knap «Kopiér diagnostik»). Derunder lister en `nav` andre Lune-enheder som almindelige links; den aktuelle har `aria-current="page"`. Menuen er kun information — ingen genstart, OTA eller nulstilling (dem ligger i Konfiguration › Service). På Touch kan et langt tryk på uret åbne det samme infosheet for installatøren (ikke synligt i hverdagen).
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
| `data-state` | `calling`, `idle`, `fault`, `blocked`, `off`, `learning` | `fault`: rødt ID, værdi og nederste segment. `blocked`: gult ID og «Blokeret». `learning`: lilla ID og «Lærer n %» — kun mens læringen kører. `off`: 50 % opacitet. |
| `data-learn` | `needed` | Gult advarselsskilt (`!`) øverst til højre. Zonen er **ikke lært** og læringen kører ikke. |
| `data-level` | `0`–`5` | Tænder segmenter nedefra (ventilåbning i trin á 20 %, eller læringsprocent mens `learning`). 0 % (eller ingen data) er slukket — orange betyder varme, og en lukket ventil varmer ikke. |
| `data-group` | `primary`, `member` | Hel orange kant (gruppens primære zone, ID som "Z4–5") eller stiplet (medlem, navn dæmpet). |
| `.is-selected` | — | Valgt uden radio-state (server-render/JS). Ellers styres valg af de genererede regler. |

Regler:
- System-feltet står først. Zoner står i fysisk rækkefølge.
- Valgt felt **inverteres**. Ingen anden markering (ingen ekstra farve eller ramme).
- Værdien er aktuel temperatur; «Lærer n %» mens motorlæring kører (niveausegmenter i `--violet` følger procenten); «Blokeret» / «Fejl» ved de tilstande.
- Ikke lært (`data-learn="needed"`) viser temperaturen og det gule skilt. Firmwarens `CALIBRATING` på en ulært, stille zone er ikke læring i gang.
- I Konfiguration får zone-ID en orange prik (genereret af CSS).
- Mobil (< 600 px): navnet skjules; det står i visningens titel lige under.
- Touch: se 5.2.1 (hierarkisk strimmel). Brug aldrig en flad liste med mere end 7 felter.

### 5.2.1 Manifoldfelt og understrimmel

Touch bruger `.strip.strip--tiers` + `.substrip`:

**Niveau 1** (altid synlig): hus-felt (`.tile-sys`) + ét `.tile.tile-manifold` pr. board (1–4).

**Niveau 2** (`.substrip[data-m="{N}]"`): vises kun når manifold `N` eller en af dens zoner er valgt. Indeholder den manifolds zonefelter med samme `.tile`-markup som V6. Når hus er valgt, er ingen understrimmel synlig.

Manifoldfeltet viser ID (`M2`), navn, fremløb/retur og `.mini` (én lodret søjle pr. zone):
- `data-level="0–5"` → højde i trin á 20 %, farve `--accent`; `0` er 2 px i `--seg-off` (lukket ventil eller ingen data)
- `data-state="fault"` → fuld højde i `--danger`
- `data-state="blocked"` → fuld højde i `--warn`
- `data-state="learning"` → `--violet`
- `data-state="off"` → `--seg-off`
- Søjlerne er `aria-hidden`; feltet har en samlet `aria-label` fra i18n

Tilstande:
- Manifold valgt → inverteret (genereret)
- Zone valgt → forælder-valgt: inset ring 2 px i `--fg` (genereret + klasse `.is-parent` til SSR)
- Fejl i en zone → ID i `--danger`

Mobil (< 600 px): hus + manifolds i 5 lige kolonner; manifoldfeltet viser kun ID + mini-søjler.

Radioer: `#s-house`, `#s-m{N}` (`data-kind="manifold" data-m="{N}"`), `#s-m{N}z{Z}` (`data-kind="zone" data-m="{N}"`).

### 5.3 Panel

Standard-containeren for indhold.

```html
<section class="panel c7">
  <header class="panel-head"><h3>Komfort</h3><p>Sidste 24 timer</p><span class="badge hot">Kalder</span></header>
  … indhold …
  <footer class="panel-foot"><button class="btn primary" type="submit">Gem mål</button></footer>
</section>
```

- `panel-head`: titel (`h3`, `--fs-lg`), valgfri undertitel (`p`, dæmpet), valgfri `.help-btn` (?) og valgfri badge (skubbes til højre). På mobil får undertitlen sin egen linje.
- Indhold: `.sub` for et underafsnit med `h4` (lille, dæmpet), `.subs.cols-2` for to underafsnit side om side, når panelet er ≥ 560 px bredt.
- Underafsnit må have egne handlinger nederst (`.actions` — tilstandshandlinger som Frakobl / Synkronisér). Handlinger i underafsnit flugter i bunden (`margin-top: auto`). Panelets footer er kun til Gem.
- `panel-foot`: gem-handlinger, højrestillet. En `.note` til venstre (fx "Hentet 14:05").
- `details.more`: sjælden ekspert-tuning eller tekniske id'er, foldet sammen. Maks. én pr. panel (eller pr. underafsnit når panelet har `.subs`).
- Monospace-id'er: `.mono` + `.id-row` med `.btn.copy` (`data-copy` → selektor; progressiv JS; uden JS er teksten markerbar).

Varianter:

| Klasse | Brug |
|---|---|
| `.panel.alert` | Fejl der kræver handling nu. Rød kant til venstre, rød titel, rød knap. Maks. én pr. visning, altid øverst. |
| `.panel.tone-info` | Informativ fremhævning (sjælden). |
| `form.panel` | Formular. Én gem-knap i footeren. |

Regler: 2–4 paneler pr. visning. Et panel har én titel og ét emne. Paneler nestes aldrig.

### 5.3b Hjælp (tre lag)

1. **Labels og `.hint`** — altid synlige; intet klik.
2. **Ét `.help-btn` (?) pr. panel eller underafsnit** — åbner native `[popover].help-pop` ved klik/tryk (ikke hover). Højst 2–3 sætninger om *hvad* indstillingerne gør og *konsekvensen* af at ændre dem. Sentence case. Neutral knap (`--raised` / `--muted`) — orange betyder varme.
3. **«Læs mere»** — link til produktets `docs/Manual.md` (anker pr. panel) i produkrepoet (V6: `lune-v6/docs/Manual.md`; Touch: `docs/Manual.md` i `lune-coordinator`); lange forklaringer hører ikke hjemme i flash. Dybe engineering-noter forbliver i egne filer og henvises fra manualen.

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

Hjælp hører primært til i Konfiguration. På desktop placeres `.help-pop` lige under den `?` der åbnede den (progressiv JS — nødvendigt når flere knapper deler samme popover). På mobil bliver den et bundark.

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
- **`.bar`**: tynd bjælke til en procentdel (`style="--v:32%"`). Standard er `--accent` (varme). `.bar.violet` til motorlæring. Brug `role="meter"` med `aria-valuenow`, når den står alene.

### 5.7 Klima-kontrol

Visningens hero på zone-dashboardet: aktuel temperatur i hero-størrelse og målet mellem to store runde knapper.

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
- Farver følger betydning: udetemperatur `--fg`, vind/retur/fremskrivning `--info`, fremløb og preload `--accent`.
- Alle grafer har `role="img"` og en `aria-label`, der siger hovedpointen ("op til 11 m/s i nat").

**Tom graf.** Uden data klapper grafen sammen til én linje: beholderen (`.sub.trend-wrap`, komfortrække m.fl.) får `data-empty`, figuren, aksen og forklaringen skjules, og `<p class="empty">` vises ved siden af overskriften. Ingen flad linje for manglende data — en sparkline kræver mindst to temperaturpunkter.

**Vejrudsigt (`.fc`, Touch):** header med ét vejrikon pr. time (`.fc-icons[data-hourly]`: sol, delvis sol, skyer, regn, sne, nat; hvert 3. under 900 px). Hovedgraf: temperatur (`--fg`, venstre akse °C) og solindstråling som flade (`.sa`/`.sl` i `--warn`, højre akse W/m² via `.fc--dual` + `.fc-y2`). Undergraf: vind (`--info`) med vindretningspile hver 3. time (`.fc-dirs`, pilen peger hvor vinden blæser hen). x-akse på faste klokkeslæt (00/06/12/18, ugedag ved midnat; `.fc-x--abs`). Altid en forklaring (`.fc-legend`). Sne udledes af nedbør ved ≤ 0,5 °C, nat af solindstråling < 5 W/m². **Slider:** en stiplet linje (`.fc-scrub`, samme stil som «Nu») følger mus/finger gennem begge grafer, og `.fc-readout` viser tidspunkt, vejrtype, temperatur, sol, vind og vindretning for den time; aflæsningen vender ved højre kant og forsvinder, når pointeren forlader grafen.

**Plan (`.plan`, Touch):** Touch' opvarmningsplan 24 t frem fra nu. To kolonner: spornavn (`.plan-lab`) og tidsakse. Øverste spor `.plan-lane--odin`: Odins planlagte varme som søjler (`.plan-bar`, `--v` = højde i % af aksens maks.; rumvarme i accent = kWh varme, `data-mode="dhw"` i `--info` og `data-mode="legionella"` stribet `--info` = kWh el) med y-akse i kWh (`.plan-lab--y` + `.plan-y`, 64 px), og Touch' løft af Odins komfortbånd som violet streg (`.plan-lift`). Et spor pr. rum (`.plan-lane`) med segmenter (`.plan-seg`, `--a`/`--b` = fra/til i timer fra nu): `data-kind="preload"` (forvarme, lys accent) og `data-kind="charge"` (opladning før vind/kulde, skraveret `--accent-ink`, så den ikke forveksles med rumvarme-søjlerne; `data-insufficient` = skraveret gul, gulvet kan ikke dække hele underskuddet). x-akse `.plan-x` som `.fc-x--abs`, men med mærke hver 3. time (hver 6. på smal skærm). Altid `.fc-legend` under (`.lbar`, `.ldhw`, `.lleg`, `.llift`, `.lpre`, `.lch`, `.lins`), og hvert segment har en title-tekst — farve er aldrig eneste signal.

**Fordeling (`.dist`, Touch):** omtrentlig andel af pumpens flow pr. manifold. Stablet bjælke `.dist-bar` med `.dist-seg` (`--w` = andel i %, farve via `data-i` 0–3: accent, info, violet, ok). Under den `.dist-rows`: farvenøgle (`.dist-key`), navn, %, l/min og en tynd bjælke (`.dist-bar--thin`) med manifoldens zoner. `.dist-note` forklarer, at tallene er omtrentlige (beregnet ud fra ventilåbning).

Komponenter: `.spark` (zonegraf, 36 px høj), `.trend` (fremløb/retur, 120 px), `.zchart` (zone 24 t + 6 t fremskrivning), `.fc` (vejrudsigt: ikonrække á 3 t, temperatur, vind, fælles x-akse á 3 t, á 6 t på smal skærm — Touch), `.bars` (plan vs. virkelighed), `.plan` (Touch' opvarmningsplan).

**Zonegraf (`.zchart`)**: 24 timer historik + 6 timer fremskrivning på halvtimes-punkter (48 + 12). Fortid: temperatur `--fg`, måltrappe `--muted`. Fremskrivning: stiplet `--info` med usikkerhedsbånd (`.pj` / `.pb`). Nøgletal «Forventet kl. {time}» kun når der findes en fremskrivning; ellers forklaringstekst `zchart.noForecast`. Hint `zchart.projHint` under forklaringen. V6 binderen beregner fremskrivningen; firmwaren leverer kun historik (`temp`, `sp_plan`) — ingen `fc` / `fc_lo` / `fc_hi`. Ved hover/touch over plottet: stiplet mus-slider (`.zchart-scrub`) og nøgletal opdateres til værdien ved tidspunktet (`zchart.at` i fortid, `zchart.expected` i fremskrivning).

**Fremskrivning (dæmpet lineær — fælles V6/Touch-kontrakt).** Implementér identisk i binderen (JS) og på Touch (C):

1. Brug de seneste 8 punkter (4 timer) af `temp`. Kræv mindst 6 gyldige punkter uden huller; ellers ingen fremskrivning.
2. Hældning \(b\) (°C pr. halvtime) ved mindste kvadraters metode over punkterne. Residualernes spredning = \(\sigma\).
3. Dæmpning \(\varphi = 0{,}85\) pr. halvtime. For \(k = 1\ldots 12\):
   \[
   T(k) = T_{\mathrm{nu}} + b \cdot (\varphi + \varphi^2 + \cdots + \varphi^k)
   \]
4. Klip \(T(k)\) til \([\mathrm{mål\_plan}(k) - 3{,}0;\ \mathrm{mål\_plan}(k) + 1{,}5]\).
5. Usikkerhedsbånd: \(\pm(\max(\sigma, 0{,}05)\cdot\sqrt{k} + 0{,}05)\) °C.
6. Ingen fremskrivning, når zonen er slukket. Ved motorfejl beregnes den som normalt (temperaturen falder typisk); fejlbeskeden siger, at fremskrivningen antager, at fejlen fortsætter (`zchart.faultStrong`).

Reference: `lune-v6/web/binder-src/projection.js` (+ `projection.test.js`: stigende / faldende / flad / &lt; 6 punkter).

**Søjlegraf (`.bars`)**: planlagt varme i `--muted` (søjle `.plan`), faktisk i `--accent` (`.act`). Højder via `--plan` / `--act` (0–100). Akser og forklaring i HTML (`.bars-legend`). Antal kolonner: `--bars-n`. Bruges på Touch hus-dashboard når varmekilden er Asgard (Odin).

Datakontrakt: hver polyline får en `points`-streng i koordinatsystemet fra `viewBox`. Firmwaren eller binderen genererer strengene; markeringsattributter (`data-bind-spark="z1"`, `data-bind-trend`, `data-bind-zchart="z1"`, `data-bind-fc`) viser, hvad der skal opdateres.

### 5.9b Sektionsheader

Lange konfigurationssider deles med **sektioner** (4.3): `.section` / `.section-h` / `.section-grid`, evt. `.section-nav`. Den ældre `.section-head` (tekst + linje) findes stadig til midlertidig kompatibilitet, men nye sider skal bruge 4.3.

```html
<section class="section" id="sec-service" aria-labelledby="sec-service-h">
  <h2 class="section-h" id="sec-service-h">Service
    <i class="section-h-dot" aria-hidden="true"></i>
    <i class="section-h-line" aria-hidden="true"></i>
  </h2>
  <div class="section-grid">…</div>
</section>
```

Tekst i sentence case. Kun Service/Udvikler må foldes (`details.section`).

### 5.10 Felter

```html
<div class="field row"><label for="probe_flow">Fremløbsprobe</label><select class="select" id="probe_flow">…</select></div>
```

- `.field`: label over kontrol. `.field.row`: label og kontrol på samme række.
  - Standard/touch: kontrollen har samme bredde som den aktive halvdel af en 2-valg-`.seg` (`calc((100% − 8px) / 2)`).
  - Kompakt (3.9): label ⟷ kontrol, kontrollen højrestillet. **Alle kontroller i en række har samme bredde** (`--ctl-w` = 5 × `--hit` = 160 px): input, select og stepper; `.seg` har den som minimum. Forskellige bredder pr. felt gav et rodet udtryk. `.w-lg` (URL, lange skabeloner) står under labelen i fuld bredde; `.w-xs/.w-sm/.w-md` er aliaser for `--ctl-w`. Et `.seg` med label over står på én række.
- Hint i `<span class="hint">` inde i label.
- `.input`, `.select`: `--hit` høje, `--field`-baggrund, `--field-edge`-kant. Native validering (`pattern`, `min`, `max`) viser rød kant via `:user-invalid`, og gem-knappen dæmpes.
- Filvalg: brug `label.input.file` med skjult `input[type=file].sr-only`, `.file-pick` og `.file-name` — native file-controls kan ikke centreres pålideligt. Filnavn opdateres progressivt ved `change`.
- `.pair`, `.pair.wide-first`: to felter side om side (host/port, MAC + Scan).

### 5.11 Stepper

Pilleformet −/værdi/+ til tal med et naturligt trin (areal, grænser, interval). Uden JS er feltet et almindeligt tal-input.

Brug `input type="number"` med `min`, `max`, `step`. Enheden i `<span class="unit">`. Knapperne har `aria-label` ("Sænk areal").

Stepper kun til værdier med et naturligt trin; ikke til port, id'er eller adresser. Port, id og adresse er `input type="number"` eller tekst med `min`/`max`.

### 5.12 Switch og gating

`<label class="switch">` med tekst og `<input type="checkbox" role="switch">`. Tændt er inverteret (sort/hvid), ikke grøn.

Hvis en switch styrer resten af et afsnit: læg den som første barn i `.gated` og resten i `.gated-body`. Når den er slukket, gråner indholdet ud og kan ikke betjenes.

### 5.13 Segment

`.seg`: 2–3 gensidigt udelukkende valg (NO/NC, Statisk/Adaptiv, Probe/BLE). Aktivt valg er inverteret. Ved 4+ valg: brug `.select`.

### 5.14 Kompas

`.compass`: ydervægge N/Ø/S/V som fire runde checkboxe i et kompaskors. Valgt er orange (`--accent-ink`). Bogstaverne kommer fra sprogkataloget.

### 5.15 Knapper

| Klasse | Brug |
|---|---|
| `.btn.primary` | Den ene primære handling i et panel (gem), og «Nulstil fejl» når zonen har en fejl. Inverteret når der er noget at gemme. |
| `.btn` | Sekundære handlinger ("Hent nu", "Scan", "Stop", "Annullér"). |
| `.btn.danger` | Åbner en bekræftelse. Teksten slutter med «…». |
| `.btn.danger-solid` | Den bekræftede destruktive handling i `.confirm-pop`. Baggrund `--danger`, tekst `--bg`. |

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

Destruktive handlinger bekræftes med en native `popover` (`.confirm-pop`), ikke en modal og ikke en indlejret udfoldning. Popoveren ligger i top-laget, så siden ikke flytter sig, når den åbnes. Den ligger **inde i den formular**, handlingen hører til, så `.btn.danger-solid` er `type="submit"` og sender `name="action"`.

```html
<button class="btn danger" type="button" popovertarget="confirm-bal">Nulstil balancering…</button>
<div id="confirm-bal" popover class="confirm-pop" role="alertdialog"
     aria-labelledby="confirm-bal-t" aria-describedby="confirm-bal-d">
  <p class="confirm-title" id="confirm-bal-t">Nulstil balancering?</p>
  <p id="confirm-bal-d">Rydder alle lærte faktorer. Prior-værdierne bruges igen.</p>
  <div class="confirm-actions">
    <button class="btn" type="button" popovertarget="confirm-bal" popovertargetaction="hide" autofocus>Annullér</button>
    <button class="btn danger-solid" type="submit" name="action" value="reset_balancing" popovertarget="confirm-bal" popovertargetaction="hide">Nulstil</button>
  </div>
</div>
```

Anatomi:

| Del | Regel |
|---|---|
| Åbner | `.btn.danger`, `type="button"`, `popovertarget`. Teksten slutter med «…». |
| Dialog | `popover` (auto), `role="alertdialog"`, `aria-labelledby` på titlen, `aria-describedby` på brødteksten. |
| Titel | Spørgsmål med handling og objekt: «Nulstil og genlær Z2?». |
| Brødtekst | Hvad der går tabt, og hvad der sker bagefter. |
| Annullér | Sekundær `.btn`, `popovertargetaction="hide"`, `autofocus`. |
| Farlig knap | `.btn.danger-solid`. Samme verbum som titlen («Nulstil», «Genstart», «Frakobl»). Aldrig «OK». |

Fokus lander på Annullér, så Enter efter åbning lukker uden at gemme. Esc og klik udenfor lukker også uden submit (`popover="auto"`). Skærmlæseren annoncerer titlen via `aria-labelledby`.

Den farlige knap har `popovertargetaction="hide"`. Når `lune-forms.js` stopper den native submit, kører den skjuling ikke, så scriptet lukker `.confirm-pop` selv, når handlingen er accepteret.

Placering: scriptet lægger popoveren ved knappen, under den når der er plads, ellers over, og klemmer den ind i vinduet, så titel og knapper ikke klippes. Uden script er den centreret. Backdrop er 25 % sort. Under 600 px er popoveren et ark fra bunden med `safe-area-inset-bottom`, og scriptet rører den ikke.

### 5.17 Tabel og log

- `.table` i `.table-wrap` (scroller vandret på smalle skærme). Tal højrestillet med `.num`.
- `.log`: monospace, maks. 240 px høj. Kilder kan farves efter betydning (`<span class="info">forecast</span>`). Log-tekst er altid engelsk (kommer fra firmwaren) og har `lang="en"`.

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

Dashboard-paneler sætter `data-hs-type="http|asgard"`; CSS skjuler `.hs-type-*` der ikke matcher.

---

## 6. Mønstre

### 6.1 Gem

Hverdagshandlinger (Dashboard) gemmes automatisk; opsætning (Konfiguration) gemmes eksplicit pr. panel. **Undtagelse:** `.switch` (`role="switch"`) i konfiguration gemmes med det samme ved skift — de er tilstandshandlinger, ikke felter man «udfylder» før gem.

**Eksplicit gem (Konfiguration).** Panelet er formularen (`form.panel`) med `data-save="nøgle"`. Én `.btn.primary` i footeren, plus `<button type="reset">` (Fortryd) og et `.save-status` (`aria-live="polite"`).

| Tilstand | Knap | Footer |
|---|---|---|
| clean | Primær neutral (raised/muted), `aria-disabled="true"`; klik gør intet | Fortryd skjult (kun når JS har sat `data-js`) |
| dirty | Normal primær | Fortryd synlig + `rt.unsaved.one` / `rt.unsaved.other` i `--warn` |
| saving | `rt.saving`, `aria-busy="true"` | — |
| saved | `rt.savedOk` ✓ i 3 s → clean | Knaptekst er bekræftelsen; ingen ekstra `.msg` |
| error | Forbliver dirty og primær | `.save-status` med `rt.saveFailed` (+ årsag); feltværdier bevares |

Uden JS: knappen er altid primær, Fortryd er synlig og nulstiller felterne.

**Tre niveauer af indikation** (farve `--warn`, aldrig eneste signal):

1. **Felt** — ændrede `.field` / `.switch` / `.seg` / `.compass` får `data-dirty` (6 px prik efter label) og `aria-describedby` til panelets `.save-status`.
2. **Panel** — footeren som i tabellen. Antallet tæller felter (én radiogruppe = ét felt).
3. **På tværs** — `.tile[for="s-…"]` og tilstandspillens «Konfiguration» får `data-dirty`, når der er ugemte ændringer i den tilhørende `#v-conf-*`-visning. `beforeunload` advarer ved navigation væk (`rt.leaveUnsaved`).

Submit sender `lune:save` med `{key, data, form, auto}`. API-laget kalder `form.luneSaved(true|false, besked?)`.

**Autogem (Dashboard).** Måltemperaturen (`.climate`) gemmes 1,5 s efter sidste ændring — ingen gem-knap. Status under målet: `rt.autoSaving` → `rt.autoSaved` → tom; ved fejl `rt.autoFailed` + `rt.retry`.

### 6.1b Tilstandsafhængige handlinger

Vis kun de handlinger, der giver mening i den aktuelle tilstand; den naturlige næste handling er primær. Sæt `data-state` på panelet (`unpaired` | `approved` | `pending` | `error`) fra binderen. CSS viser/skjuler via `[data-show-when~="…"]`. Uden JS bruges den tilstand, siden er renderet med.

| Tilstand | Handlinger |
|---|---|
| Ikke parret (`unpaired`) | «Godkend …» er `.btn.primary`; ingen frakobling |
| Godkendt (`approved`) | Ingen «Godkend»; «Frakobl …» (`.btn.danger` der åbner `.confirm-pop`) |
| Venter/forbinder (`pending`) | Statusbadge + evt. «Annullér»; ingen andre handlinger |
| Fejl/ingen forbindelse (`error`) | «Prøv igen» (`.btn`) + «Frakobl …»; `.msg.bad` med årsag |

Eksempel: Lune Touch-panelet i V6 Connect. Samme mønster til andre parrings-/forbindelsespaneler (BLE-sensorer, varmekilde, Touch-styringer).

### 6.2 Fejl og advarsler

| Alvor | Hvor |
|---|---|
| Fejl der kræver handling | `.panel.alert` øverst i visningen + rød tilstand i zonefeltet + rødt i komfortlisten |
| Kræver opmærksomhed snart | `.msg.warn` i det relevante panel eller `c-warn` på værdien |
| Information | `.msg.info` eller `.badge.info` |

En fejl på manifold-niveau viser et alert-panel i system-omfanget med et link ("Åbn Z6") til zonen. I zonens egen visning viser alert-panelet handlingen ("Nulstil fejl"). «Nulstil fejl» er `.btn.primary` og vises kun mens zonen har en fejl (`data-bind-show`), både i alert-panelet og i motor-footeren. Der er ingen deaktiveret variant.

Fejltekster siger hvad der skete og hvad man gør: "Motoren nåede ikke endestop på 45 s. Tjek aktuatoren, og nulstil så fejlen." De undskylder ikke og er aldrig vage.

### 6.3 Grupperede zoner

- Primær zone: ID "Z4–5", hel orange kant. Medlem: ID "Z5", stiplet kant, dæmpet navn.
- Medlemmets dashboard: målet er låst, med note og link til den primære.
- Medlemmets konfiguration: `.msg.violet` øverst forklarer hvad der styres hvor.

### 6.4 Live-data

- Elementer der opdateres live har `data-bind="nøgle"` (fx `z1.temp`, `manifold.flow`).
- Zonefelter og komfortrækker har `data-state`, `data-level` og `data-learn`, som binderen opdaterer. Under motorlæring er `data-level` læringsprocent i trin á 20 %, og motorpanelet viser `.bar.violet`.
- Strenge der skrives ved runtime, hentes fra `<script type="application/json" id="i18n">` (se 8).
- Offline: vis `rt.offline` som `.badge` i enhedspanelet; frys værdierne i stedet for at tømme dem.

### 6.5 Tomme tilstande

**Manglende værdier vises som «—», aldrig 0** (heller ikke `0,0 °C` eller `0 %`). `null` fra firmwaren må ikke tvinges til et tal; farve (fx `c-warn`) sættes kun på en rigtig måling.

En tom tilstand er en opfordring: den siger hvad der mangler, og hvor det gøres. Fx: "Ingen zoner konfigureret. Navngiv zoner på Lune V6; Touch importerer dem automatisk." med en knap til det rette sted.

### 6.6 Integrationer med typer

Når en integration har flere backends (fx varmekilde: Generisk HTTP / Asgard):

1. **Config** — tilføj type-id'et i `heat_source_types` i `config/touch.json` (første er standard). Ukendte id'er fejler buildet.
2. **Felter** — nyt `fieldset.typed-fields[data-type="…"]` med kun de nøgler firmwaren accepterer. Opfind ikke felter; marker huller med `<!-- TODO: … -->`.
3. **Dashboard** — blok med klassen `.hs-type-{id}` inde i panelet med `data-hs-type`. Fælles status (badge, seneste push, setpunkt) står uden for type-blokkene.
4. **i18n** — labels, hints, help-popover og aria for den nye type i en/da.
5. **API-mapping** — UI-id `http` svarer til firmwarens `generic_http` ved save (produkt-binder).

### 6.7 Test af forbindelser

En knap, der prøver en forbindelse (Test læsning, Test afsendelse), skal altid vise et resultat. Resultatet ligger i `<div class="test-result" aria-live="polite">` under knapperne, i det underafsnit handlingen hører til — ikke i gem-footeren.

| Tilstand | Indhold |
|---|---|
| Kører | Teksten «Tester…» og `aria-busy="true"` på knappen. Ingen `.msg` endnu. |
| Ok | `.msg.ok`. Første linje: handling, klokkeslæt, HTTP-kode og varighed (`Læsning OK · 14:32:05 · 200 · 84 ms`). Anden linje: hvad der blev læst eller sendt (værdi, entity). |
| Fejl | `.msg.bad`. Første linje: status, klokkeslæt og årsag (timeout, HTTP-kode, DNS). Anden linje: hvad man tjekker («Tjek host og port …»). |

Resultatet bliver stående, indtil næste test køres. Det gemmes ikke og indgår ikke i dirty-tællingen. Farven er aldrig eneste signal: ok er grøn tekst, fejl er rød tekst, og begge linjer kan læses uden farve.

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
| CSS + én side pr. sprog, gzippet: mål ≤ 40 kB i alt for to sprog | Flash-plads og indlæsningstid |
| Ikoner som SVG-sprite (`<symbol>` + `<use>`) | Ét sted, genbrugt |
| Tilstand i CSS (radio/checkbox, `<details>`) | Ingen JS-framework |
| JS kun til: live-data, +/−, submit-hook, luk dropdown | Virker uden JS |
| Grafer som SVG-punkter genereret på enheden eller i binderen | Intet chart-bibliotek |

Geist er første valg i font-stakken, men hentes ikke. Ønskes den, kan en latin-subset (woff2, ca. 30 kB) lægges i flash med `@font-face` og `font-display: swap`.

Nuværende størrelse (V6-eksemplet, en + da): CSS 17,1 kB + 2 × 19 kB sider = 55 kB gzip — over målet; se handoff.

---

## 10. Lune Touch

Touch bruger samme skal, komponenter og regler. Touch tilføjer **ingen egne zoneindstillinger** — zoner konfigureres på hver V6 — så Touch har kun omfanget **Hus** (Dashboard + Konfiguration) og ingen manifold-/zonevisninger.

| | Dashboard | Konfiguration |
|---|---|---|
| **Hus** | Én **V6-række pr. board** (`.boards` › `.strip`: System-felt med navn og fremløb/retur + zonefelter, samme markup som V6' strimmel; felterne linker til V6'ens egen side med `#s-sys` / `#s-zN`). Derunder husklima (mål, autogem), varmekilde med cirkulationspumpe som underafsnit, vejrudsigt. Advarsler øverst. | Styringer (V6-boards, navn kan overstyres), varmekilde (type + felter), **Rum** (det Touch ejer pr. rum: medregn i hustemperatur, vægt, vind, sol), pumpe, vejrplacering, identitet/backup, service. |

- Board-navnet er Touch-navnet, hvis det er sat, ellers V6'ens eget (navn, ellers placering — `device_name` / `device_location` i V6 `/api/v1/zones`), ellers «Unavngivet».
- Vægge, areal og gulv ejes af V6; Touch spejler dem og redigerer dem ikke.
- Varmekildetyper sættes ved build (`heat_source_types`). Runtime vælger typen i Konfiguration › Hus. Felter følger firmwaren (`asgard` / `generic_http` via UI-id `http`).
- Forbindelsesfejl på varmekilde: `.panel.alert` øverst i hus-visningen med «Åbn varmekilde» + fejlbadge i panelet.
- Kommandolog og diagnostik hører til Konfiguration › Service. Diagnostik viser oversatte værdier, aldrig rå firmware-strenge.
- Den hierarkiske strimmel (5.2.1) findes stadig i designsystemet, men Touch-produktet bruger den ikke længere.

---

## 11. Tjekliste for en ny visning eller komponent

- [ ] Hører det til Dashboard eller Konfiguration (2.3)?
- [ ] Bruger det eksisterende komponenter, før der laves nye?
- [ ] Kun tokens, ingen hex-værdier eller px-tal uden for skalaerne?
- [ ] Har hver farve en betydning fra 3.2, og er farve aldrig eneste signal?
- [ ] 2–4 paneler, én titel og ét emne pr. panel, én gem-knap pr. formular?
- [ ] Alene-panel i en række: fuld bredde (ingen cN) med `.subs.cols-2` — ikke tom halv kolonne?
- [ ] Underafsnit-handlinger nederst; footer kun Gem?
- [ ] Konfiguration: sektioner (4.3), maks. 5, sektionslinks ved 3+, kun Service/Udvikler foldet?
- [ ] Dashboard: små paneler ved høje naboer flettet som `.sub`? Konfiguration: naturlig højde, ingen `.stack`?
- [ ] Kompakt ved 1440 px med mus: steppere ≤ 160 px, inputs med `.w-*`, ingen panel med > 48 px tom bund? Touch ved 390 px uændret?
- [ ] Manglende værdier som «—» og tomme grafer som én `.empty`-linje?
- [ ] Tilstandsafhængige handlinger: kun relevante knapper, næste skridt er primær (6.1b)?
- [ ] Ugemte ændringer markeret (felt/panel/sektion/tværs)? Autogem på dashboard?
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
  config/touch.json         Touch: tiers, manifolds, heat_source_types, shared_views
  tools/lds_build.py        tokens + tilstandsregler → dist/<projekt>/lune-ui.css
  tools/build_docs.py       → docs/design-system.html
  examples/v6/              V6-reference
  examples/touch/           Touch-reference (hierarki + varmekilde)
```

```
python tools/lds_build.py --check             # kontrast
python tools/lds_build.py config/v6.json      # dist/v6/lune-ui.css
python tools/lds_build.py config/touch.json   # dist/touch/lune-ui.css
python tools/build_docs.py                    # docs/design-system.html
python examples/v6/build_ui.py --langs en,da
python examples/touch/build_ui.py --langs en,da --preview --hs-type asgard
```

Ændringer i tokens eller komponenter: ret kilden, kør builds, og kontrollér referencesiderne i begge temaer.

---

## 13. Vægskærm (Touch display)

LVGL-displayet bruger `display` i `tokens/tokens.json` (palet, nat, typeskala, mål, tider) og tjekkes med `python tools/lds_display.py --check`. `--install <produktrepo>` genererer firmwarefilerne ud fra produktets `lds.yaml`; brand-mærkerne (seksrørs-halo) kommer fra `tokens/brand.json` via `tools/lds_brand.py`. `line` og `seg-off` er forblandede og håndplukket som varm grå, så RGB565 ikke giver grønt stik. Web-CSS og vægskærm deler betydning (3.2); hex kan afvige lidt pga. RGB565.

### 13.1 Varmekilde på huskortet

På husets kort:

- Vis varmekildens navn efter type: «Varmepumpe (Asgard)» eller «Varmekilde (HTTP)».
- Vis fremløb/retur kun når typen leverer dem (Asgard); Generic HTTP viser dem kun hvis status-URL har givet værdier.

### 13.5 Autogem på vægskærmen

Vægskærmen (Touch display) gemmer måltemperaturen automatisk **1,5 s** efter sidste ændring — samme debounce som V6-dashboardets `.climate`-autogem (6.1). Brugeren ser en kort «Gemmer…» / «Gemt»-status; der er ingen eksplicit gem-knap til hverdagsjusteringer.

### 13.6 Lys skærmpalet

Den lyse vægskærm-palet følger samme regel som web (3.1): statusflader er mørkere end kortet og arver kortets varme med svag statuskulør — ikke kølige pasteller. `raised` er håndplukket (`#d6d2ce` / `0xD699`), så 16-bit kvantisering ikke giver grønt stik. Ret ikke display-hex i produkrepoet; ret `display.palette` her og kør `--check`.
