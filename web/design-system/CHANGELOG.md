# Changelog — Lune Design System

## 2.3.7

- `.chip-icon[data-tone="water"]` med nyt token `--water-fill` (dom-water, solid i begge temaer, on-fill 5,2:1): cirkulation/flow.
- `.ht-dist`: fordeling i et Hjem-felt som donut + forklaring, én kulør (dom-water) i nuancer.
- Navbar ved scroll animeres (`--dur-slow` 360 ms; kolonnerne bevares, så bredden kan interpoleres; ingen animation ved `prefers-reduced-motion`).

## 2.3.6

- `.hchart`: graf i et ark med tal, intervalvalg (24 t / 7 d, radioer), y/x-akser i HTML og fælles `.chart-legend`; `.trend .grid`, `.trend .nowl`; `.c-heat`.
- `.fc--stack`: vejrprognosen som fem små grafer over hinanden (vejr, temperatur, sol, vind, retning) i stedet for én kombineret.
- `a.sheet-link`: ét link pr. ark til System — nederst i Indstillinger-fanen, hvis arket har en, ellers nederst i Overblik; `lune-forms.js` lukker åbne ark ved skift til en tilstand via hash.
- Navbar: smal pille ved scroll (`scroll-state`-query, ≥ 768 px).

## 2.3.5

- `.hero-fact`: egen flade og skygge over højdekurverne (nye tokens `--lift-bg`, `--lift-shadow-c`, `--lift`; kontrast fg/muted på lift-bg). Ikonbrikken fik ved en fejl tekstliniens grå farve — rettet.

## 2.3.4

- `.thermo`: håndtaget (`.knob`) sidder for enden af buen (begge = huset nu); målet vises kun under ringen. `.tg`/`.tg-edge` er fjernet.
- `.hero-waves`: højdekurver bag Hjems hovedsektion (inline-SVG, streger i varmefarven) — den eneste tilladte tekstur.
- `.home-scopes`: Hus + ét kort pr. styring med zonesøjler (`.mini`), øverst på Hjem; åbner styringens ark.

## 2.3.3

Touch' Hjem tættere på designet (`docs/design/Overview.png`).

- `.home-hero`: overskrift i to toner (`h2 .sub`), hilsen i `--accent-ink`, `.hero-facts`/`.hero-fact` (varmekilde og næste varme som genveje). Nyt token `--fs-display` (48 px).
- `.thermo`: buen viser husets temperatur, mærket (`.tg`/`.tg-edge`) er målet; «ude x°» under tallet; gradtegnet hævet. Målet er et tekstfelt med `.unit`, så «21.6» / «21,6» følger sidens sprog (før: browserens sprog).
- `.home-tile`: titel og status står samlet øverst, visualiseringen i bunden (rækken var strakt ud, når et felt manglede data). Højere `.ht-viz` (64); plan-søjler i violet (plan), vejr med flade.
- `.home-head` over felterne.
- Varmekort: `.heatmap-head` + `.heatmap-legend`; rumfelterne fylder efter areal (`--area`, `--room-min`), 5-trins ventilbjælke (`data-open`), «x % åben». Gruppe-ringen og zone-id er væk fra varmekortet. `--area` er tilføjet til de tilladte inline-variabler.

## 2.3.2

- Budget (DESIGN.md 9) hævet ud fra målinger på V6 (8 MB flash, app-partition 3,93 MB, web-UI 117 kB, 2,3 MB fri): CSS ≤ 24 kB, `lune-forms.js` ≤ 5 kB, ≤ 20 kB pr. sprogside uden grafpunkter, binder ≤ 70 kB, hele web-UI'et ≤ 160 kB gzip.

## 2.3.1

Rettelser fra migreringen af produkterne (`lune` og `lune-coordinator`, gren `lds-2.3`).

- `lune-forms.js`: `window.luneForms.bind(form)` / `.scan(rod)` til formularer, der tegnes efter indlæsning; `luneResnap()` og et vellykket gem gør formularens værdier til standardværdier, så Fortryd og «kassér» vender tilbage til enhedens værdier (DESIGN.md 6.1). En formular bindes kun én gang.
- CSS: `.setting[data-show-when]` vises som flex; skillelinje også mellem rækker og undersider inde i `.gated-body`; `.setting-value` (læseværdi, 600-vægt).
- DESIGN.md: Elpris i Touch' kategoriliste (15.4); «kun faner med indhold» (15.3); `lune-forms.js` som egen fil eller i produktets binder (9).
- Touch-eksemplet: rørafstand og rørtype er fjernet fra «Fra V6» (findes ikke i Touch' API); eksempeltekster med indbyggede data er nu skabeloner (`tile.planVal`, `tile.price`, `heat.odinNowVal`, `hs.testOkBody`, `alert.motorFault`).

## 2.3.0

Fase 3: Lune Touch på Hjem / ark / System.

### Nyt i LDS
- `.home-hero` + `.thermo` (termostat-ring, autogem), `.home-tiles`/`.home-tile` (maks. 4 felter, ét tal + én visualisering, `.ht-price`), `.scale-chip`, `.heatmap`/`.room-group`/`.room-grid` (rum pr. styring, offline-tilstand), `.setting-group[data-offline]` (DESIGN.md 15.10). Alle `@only touch`.
- `lune-forms.js`: delvis gem = patch (`data-save="ressource.del"` + `data-patch` → `method: "PATCH"`, `changed`), DESIGN.md 6.1. Deep links matcher kun ark og tilstande på første niveau (et ark og en System-kategori kan dele navn).
- `lds_build.py --check` fejler på enhver `var(--x)` i `css/lune-ui.src.css`, der hverken er et token, en lokal variabel eller en tilladt inline-variabel (`--v`, `--w`, `--a`, `--b`, `--deg`, `--now`, `--act`, `--bars-n`, `--fc-cols`, `--fc-dirs`, `--sub-n`, `--plan`, `--float`).
- Typefelter: aktiv pille virker også, når `.seg` ligger inde i en grupperet liste (`#x:checked ~ * .seg label[for]`).

### Forældet
- Den hierarkiske strimmel (`strip--tiers`, `.substrip`, `.tile-manifold`, `.boards`) ligger bag feature'n `tiers-strip` og fjernes i en senere version.

### Touch-eksemplet
- Skrevet forfra på produktets felter (`lune-coordinator/web/touch-ui`), en + da. Hjem: hovedsektion, Varme · Næste varme (med elpris) · Vejr · Cirkulation, varmekort pr. styring. Ark: Varme, Næste varme, Vejr, Cirkulation, Manifold (pr. styring), Rum. System: Enhed · Styringer · Varmekilde · Elpris · Cirkulationspumpe · Vejr · Netværk · Firmware og backup · Service.
- Eksempeldata: tre styringer (Anneks offline), én motorfejl (Gang), ét rum uden data (Lager).
- `config/touch.json`: `["home","sys"]`, omfang `house`, `systemCategories`; ingen `tiers`/`legacy`.
- `examples/touch/MAPPING.md`, `conf_fields.txt` (90 felter/handlinger fra produktet) og `check_fields.py`.

## 2.2.0

Byggeklodserne til Hjem / ark / System (DESIGN.md 15.9). Produkterne migreres i separate trin.

### Nyt
- `.sheet`: ark som native popover; højre side på desktop (520 px), fra bunden på mobil. `.sheet-head`, `.sheet-close`, `.chip-icon` (ikonbrik med `data-tone`).
- `.tabs`: faner som radioer + søskende-selektorer ud fra `value` (`overview|history|settings`); ingen CSS pr. ark.
- `.savebar`: klæbende gem-bjælke med dirty-mønstret; `lune-forms.js` håndterer nu alle `form[data-save]` (ikke kun `form.panel`) og `.setting`-rækker.
- `details.subpage`: «Avanceret ›»-underside i samme ark/kategori, uden JS.
- `.sys`: System-side med kategoriliste og indhold; mobil som liste → kategori med «‹ System».
- `lds_build.py`: `modes: ["home","sys"]` (`#v-home-{omfang}`, én `#v-sys`, strimlen skjult på System), `systemCategories` → kategoriregler. `dash`/`conf` bygger stadig med advarsel.
- `lune-forms.js`: `data-tab` på triggere, advarsel ved lukning af ark med ugemte ændringer og ved skift væk fra System/kategori, deep links (`#z3`, `#z3/indstillinger`, `#system/varmekilde`) med `history.replaceState`.
- Tokens: `--r-sheet`, `--sheet-w`, `--sheet-inset`, `--sys-nav-w`, `--sys-main-w`, `--setting-h`, `--float`.

### V6-eksemplet (fase 2)
- Hjem | System i stedet for Dashboard | Konfiguration. Hjem: statuslinje, fejlpanel, Varme nu, komfort pr. zone, vejrudsigt; ingen gem-knapper (målet autogemmes i zonens ark).
- Zone-ark (Overblik · Historik · Indstillinger) og manifold-ark; åbnes fra zonefelt, komfortrække, manifold-feltet og Varme nu.
- System: Enhed · Manifold og motorer · Forbindelser · Firmware og backup · Service (+ Motorlab med `--dev`).
- `examples/v6/MAPPING.md` (gammel → ny placering) og `examples/v6/check_fields.py` (alle 128 gamle felter/handlinger findes; maks. 6 rækker og 5 grupper).
- Varmetilstand og varmepumpegrænser (`heat_mode`, `heat_min_open`, `hp_*`) fra produktet er med i manifold-arket.
- Nyt i CSS: `button.tile` og `.comfort > button` (åbner ark), `.compass.inline`, selectbredde 160–220 px i grupperede lister, `.hp-limits` virker i enhver formular med `heat_mode`.

### Budget og projektspecifik CSS
- `features` i config: `@only legacy`-blokke (Dashboard/Konfiguration: sektioner, Konfigurationens spalter, «redigerer»-prikken) kommer kun med, når `"features": ["legacy"]` er sat. V6 slipper dem (gzip 22,1 → 21,0 kB); Touch beholder dem indtil fase 3.
- V6-eksemplet: `lune-forms.js` er en egen fil (`/lune-forms.js`, `LUNE_UI_JS_GZ` i `web_ui.h`), fælles for begge sprog. Preview-filerne har den stadig inline.
- Bevidst budget V6 ≤ 60 kB gzip for to sprog (DESIGN.md 9): arkene skal ligge i siden for at virke uden JS. Målt uden indlejrede grafpunkter: 58,7 kB.

### Ændret
- `.setting` på mobil: kontrollen bliver på rækken, når den kan være der; ellers brydes den under labelen (`.setting.stack` fylder altid bredden).
- `.confirm-pop` bruger `--float`.

## 2.1.1

2.1 flettet ind i repoet. 2.1 er udgangspunktet; det, 2.1 ikke dækkede, er genindført fra main og tilpasset 2.1's farveregler.

### Genindført fra main
- **Touch:** hierarkisk strimmel (`strip--tiers`, `.substrip`, `.tile-manifold` med neutrale mini-søjler), `.boards`, `config/touch.json` med `tiers`/`shared_views`/`heat_source_types`/`typed_groups`, typeafhængige felter (DESIGN.md 5.18).
- **Komponenter:** sektioner og sektionslinks (4.3), hjælp (`.help-btn`/`.help-pop`, 5.3b), `.test-result` (6.7), tilstandsafhængige handlinger (6.1b), ugemt/gem og autogem (6.1, `js/lune-forms.js`), "Om enhed", `.mono`/`.id-row`/`.copy`, filvalg, tomme grafer og «—».
- **Zonefelt:** `blocked`, `learning` (violet læringsprocent), `data-learn`, `data-charge`, `data-lease` — skilte i solide `…-fill`-toner.
- **Grafer:** `.bars`, `.plan`, `.dist` (neutral, ikke flerfarvet), vejrudsigtens sol/vindretning/timeikoner/slider, `.zc-scrub`, fremskrivningskontrakten.
- **Tæthed:** `density.compact` og tæthedstokens (`--panel-pad`, `--field-gap`, `--row-h`, `--tile-h`, `--fs-panel`, `--fs-view`); kompakt kontrolhøjde 36 px, naturlige kontrolbredder bevaret.
- **Bekræftelse:** `.confirm-pop` accepterer både `h4`/`.actions` og `.confirm-title`/`.confirm-actions`.
- **Værktøjer:** `lds_build.py` (validering, tiers, typed groups, kompakt), `lds_display.py --install` (firmwarefiler + brand), nat-farver (neutrale) og ciffer-fonte i `display`.

### Rettelser
- `.plan`: varmt vand og legionella i varme-domænet, adskilt med mønster (solid / skraveret + kant / kryds-skraveret + kant). Nyt mønster `lds-hatch-heat-dense`. Ny regel i DESIGN.md 3.2.2.
- Zonefelt 600–1023 px: ID og navn overlapper ikke længere (`auto minmax(0,1fr) auto`).
- `.kv dd` bryder lange værdier. Touch-eksemplets "Kræver opmærksomhed" viser korte statusord; forklaringen ligger i "?".
- Systemfeltet i tiers-strimlen: børn placeres ikke længere i ikke-eksisterende grid-områder.

### Projektspecifik CSS
- `/* @only touch */ … /* @end */` (og `v6`) i `css/lune-ui.src.css`; `lds_build.py` fjerner blokke, der ikke matcher `config.id`. Gzip: V6 23,1 → 20,1 kB, Touch 24,0 → 23,3 kB.

### Budget
- Bevidst budget V6 ≤ 48 kB gzip for to sprog (DESIGN.md 9). Grafpunkter hentes live, ikke indlejret.

## 2.1.0

Samler alt fra designrunden efter handoff (LUNE-UI-HANDOFF.md). **Brydende ændringer** står først; de kræver ændringer i V6- og Touch-markup.

### Brydende ændringer (kræver migrering i projekterne)

| Område | Før (2.0) | Nu (2.1) | Migrering |
|---|---|---|---|
| Navbar | `.top` klæbede med header + strimmel | `.navbar-wrap > .header` klæber alene som svævende pille; `.top` indeholder kun strimlen og scroller | Flyt `<header class="header">` ud af `.top` ind i `<div class="navbar-wrap wrap">` |
| Zonefelt | Lodret 5-segment bjælke, orange grupperammer | Vandret **10**-segment bjælke (10 %-trin), `.tile-pct` med procent, `.tile-dev` afvigelses-chip, **violette** grupper | `.lvl` med 10 `<i>`, `data-level` 0–10 (`ceil(%/10)`), tilføj `<span class="tile-pct">62 %</span>` og `<span class="tile-dev" data-dev="1–5">−0,6°</span>` |
| Bekræftelse | `<details class="confirm">` | `.confirm-pop` (native `popover`) | Se DESIGN.md 5.16; `common.cancel` i i18n |
| SVG-gradienter | Grafernes flader brugte jævn farve | Glød/skravering via `url(#lds-fade-heat)`, `url(#lds-hatch-heat)` | Indsæt `css/lds-svg-defs.html` én gang pr. side **inde i** `.app` |
| Statusfarver | Én tone pr. status | `info/ok/warn/danger/violet` = tekst; `…-fill` = solide flader (Apex-værdier) med `on-fill`/`on-warn-fill` | Knapper/brikker med statusfarve skal bruge `…-fill` |
| Varme-badge | `.badge.hot` på `accent-ink` | `heat-fill` (#E8630A) + `on-heat-fill` | Ingen markup-ændring; kommer via CSS |
| Ventilåbning | Orange segmenter/bjælker | Neutral (`currentColor`/`--fg`) | Ingen markup-ændring; fjern evt. egne orange overrides |
| Data-tokens | `data-*` havde egne værdier | `data-*` er aliaser for `dom-*` | Ingen ændring nødvendig |
| Klima-kontrol | Pile ‹ › | − / + | Skift ikoner |
| Arkitektur | Dashboard/Konfiguration | **Hjem / ark / System** (DESIGN.md 15) | Gøres som separat projektopgave (se prompt) |

### Farver og tokens
- Mørkt tema: **neutrale** (let kølige) flader i stedet for varme grå; renere orange (`#f67400`).
- Lyst tema: statusflader afledes af kortet i OKLab (`tint`-opskrift); badges er hvide piller med farvet ring.
- Ny farvemodel i tre lag (DESIGN.md 14.1): **status**, **domæner** (`dom-heat`, `dom-cool`, `dom-light`, `dom-water`, `dom-energy`, `dom-nature`, `dom-security`, `dom-plan`, `dom-media`) og **skalaer** (`scale-good-bad-1…5`, `scale-cold-warm-1…5`).
- Afstemt datapalet i OKLCH (samme lyshed og farvestyrke).
- Nye tokens: `heat-fill`, `on-heat-fill`, `…-fill`, `on-fill`, `on-warn-fill`, `danger-surface`, `dom-*`, `scale-*`.
- Regler: undgå brunt i mørkt tema (3.2.1); ingen gradienter mellem kulører, én fyldt serie pr. graf, store flader neutrale (3.2.2); statusfarvernes to toner (3.2.0).

### Komponenter
- Nyt: `.setting-group` / `.setting-list` / `.setting` (grupperede lister til alle indstillinger), `.confirm-pop`, `.navbar-wrap`, `.tile-dev`, `.tile-pct`, zonegraf `.zc` med 24 t historik + 6 t fremskrivning.
- Ændret: kompas (hus med fire vægge, neutral valgt-tilstand), fejlpanel (halv højde, ikonbrik, titel i tekstfarve), felter med naturlig bredde (`.w-xs/-sm/-md/-lg`), segmenter efter indhold, kontrolhøjde 36 px med mus.

### Værktøjer
- `tools/lds_ha.py`: Home Assistant-tema (lyst/mørkt, domæne-tilstandsfarver, energi) + **UIX**-nøgler (navbar, kort, dialoger, Geist via `uix-fonts`). Fonten ligger i `dist/home-assistant/www/fonts/` (OFL).
- `tools/oklab.py`: OKLab-hjælpere til afledte farver.
- `tools/lds_build.py`: løser `tint`-opskrifter og aliaser.
- `tools/lds_display.py`: neutrale 565-grå, violet gruppering, neutral ventilbjælke.

### Dokumentation
- DESIGN.md: afsnit 3.2.0–3.2.2, 14 (husets farvesprog, HA/UIX), 15 (Hjem/ark/System, grupperede lister, kontrolbredder, antal felter, præsentationstabel).
- AGENTS.md: regler for struktur, farver og kontroller.
- Referencesiden: indholdsfortegnelse i sektioner, komponenter på siden (ikke i kort).
