# Regler for kodeagenter (Lune V6 og Lune Touch)

Kopiér denne fil ind i projektets `CLAUDE.md` / `AGENTS.md`, eller henvis til den. Den fulde begrundelse står i `DESIGN.md`.

## Altid
- Byg UI med komponenterne i `css/lune-ui.src.css`. Find den nærmeste eksisterende komponent, før du laver en ny.
- Brug kun tokens (`var(--…)`) fra `tokens/tokens.json`. Ingen hex-værdier, ingen `#fff`, ingen px-størrelser uden for skalaerne (`--space-*`, `--fs-*`, `--r-*`, `--hit`).
- Ny farve eller ny tekst/baggrund-kombination: tilføj den i `tokens.json` (med en `contrast`-linje) og kør `python tools/lds_build.py --check`.
- Hver visning har id `v-{dash|conf}-{omfang}` og ligger som barn af `main.content`. Nye omfang tilføjes i `config/<projekt>.json`, ikke i CSS.
- Indhold i 2–4 paneler (`.panel`, bredde `c4`–`c8`). Én titel og ét emne pr. panel. Formularer er `form.panel` med `data-save` og én `.btn.primary` i `.panel-foot`.
- Konfigurationssider i V6/Touch (indtil de er flyttet til ark/System): del i `.section` / `.section-grid` (DESIGN.md 4.3) med sektionslinks ved 3+ sektioner. Fold aldrig sektioner ud over Service/Udvikler.
- Dashboard: et panel mere end dobbelt så lavt som naboen flettes (`.sub`). Konfiguration: 1/2/3 spalter med naturlig panelhøjde; `cN` = én spalte, `.wide` = alle. Et panel alene i en række fylder hele bredden (ingen `cN`) med `.subs.cols-2`.
- Tæthed følger inputtypen: mus på ≥ 1024 px er kompakt (`--hit` 36 px), touch uændret (DESIGN.md 3.9). Brug tæthedstokens (`--panel-pad`, `--field-gap`, `--row-h` …), ikke faste px.
- Manglende værdier vises som «—», aldrig 0. Tomme grafer klapper sammen til én `.empty`-linje (`data-empty` på beholderen).
- Underafsnit må have egne tilstandshandlinger; panelets footer er kun Gem. Vis kun handlinger, der passer til tilstanden (`data-state` + `data-show-when`, DESIGN.md 6.1b).
- Hjælp i tre lag: gode labels/`.hint`, ét `.help-btn` (?) pr. panel med native `popover.help-pop` (klik/tryk, ikke hover), og «Læs mere» til produktets `docs/Manual.md#…`. `?` er neutral.
- Enhedsmenuen (`details.device`): «Om enhed» (identitet + kopiér diagnostik) over listen af andre enheder. Ingen driftshandlinger i menuen.
- Al tekst kommer fra i18n-kataloget, også `aria-label`, `title` og `placeholder`. Knaptekst er verbum + objekt.
- Skjul eller deaktivér aldrig gem-knapper; brug clean/dirty-mønstret i DESIGN.md 6.1. Hverdagshandlinger autogemmes; opsætning gemmes eksplicit pr. panel. Switches (`role="switch"`) i opsætning gemmes med det samme.
- Destruktive handlinger bekræftes med `.confirm-pop` (native popover), aldrig inline-udfoldning eller "OK".
- Farve har én betydning: orange = varme, blå = vejr/sensorer, grøn = ok, gul = snart opmærksomhed, rød = fejl nu, lilla = læring/gruppering. Farve er aldrig eneste signal.
- Tal: `.metric` for nøgletal, `.kv` for detaljer. Enhed i `<small>`. Decimaltegn efter sprog.
- Grafer: inline-SVG kun til figurer; alle tekster i HTML. Mål som trappekurve, temperaturakse mindst 3 °C.
- **Designændringer i produktet (V6/Touch) skal også landes her i LDS** (`css/lune-ui.src.css`, `examples/…`). Ret aldrig kun den kopierede CSS/HTML i produktrepoet.
- Touch-produktet har kun omfanget Hus. V6-boards vises som én `.strip`-række pr. board (`.boards`) på hus-dashboardet; zoner konfigureres på V6, ikke i Touch. Kun det Touch ejer (rum: medregn, vægt, vind, sol) redigeres i Touch.
- Typeafhængige felter (fx varmekilde) bruger `.typed-fields` / `.hs-fields` med lokale radioer og søskende-selektorer (`heat_source_types` / `typed_groups` i config) — ikke JavaScript.
- Testknapper skal altid vise et resultat (`.test-result`: to linjer, `aria-live`, bliver stående, gemmes ikke).
- Vis rå firmwaretekster aldrig direkte. Tilstande oversættes og får badge-farve efter betydning.

## Struktur (DESIGN.md 15)
- Hjem / Ark / System. Indstillinger for én ting ligger i tingens ark (fane Indstillinger); enhedsindstillinger på System.
- Indstillinger er grupperede lister (maks. 6 rækker pr. gruppe, 5 grupper pr. side), ikke kort. Kontrolbredder efter tabellen i 15.6.
- Hjem: én hovedsektion, maks. 4 felter pr. række, ét hovedtal og én visualisering pr. felt.
- Config: `"modes": ["home", "sys"]` + `systemCategories`. `dash`/`conf` er udfaset.
- Ark: `div.sheet[popover]` med `.sheet-head` (ikonbrik, titel, én statuslinje, luk), åbnes med `popovertarget`. Ét ark pr. ting; aldrig ark i ark.
- Faner: radioer `input.state.tab` (value `overview|history|settings`) først i arket + `.tabs`-labels + `.tab-panel[data-tab]`. Ingen JS til at skifte fane.
- Gem-bjælke: én `.savebar` sidst i formularen pr. fane/kategori, med `.save-status`, Fortryd og Gem. Hjem autogemmer.
- Sjældne indstillinger: `details.subpage` («Avanceret ›») i samme ark/kategori.
- System: radioer `name="syscat"` (`c-{kategori}` + `c-none`) før `.sys`; `.sys-nav` + `.sys-cat[data-cat]`; `.sys-back` til mobil.
- Deep links via `data-hash` (`#z3/indstillinger`, `#system/varmekilde`).
- Touch' Hjem: `.home-hero` med `.hero-facts` (højst 2) og `.thermo` (bue = huset nu, mærke = mål; autogem), `.home-head`, maks. 4 `button.home-tile` (ét tal + én `.ht-viz`), `.heatmap` (`.heatmap-legend`, rumfelter efter areal) med `.room-group` pr. styring. Offline-styring: `data-offline` + `.offline-note`.
- Delvis gem af én ressource fra to steder: `data-save="ressource.del"` + `data-patch` (sender kun ændrede felter som PATCH).
- Ét felt pr. ting: en indstilling, der ejes af en anden enhed (fx V6' gulv i Touch), vises som læseværdi + «Redigér på V6 ›», aldrig som redigerbart felt to steder.

## Aldrig
- Ingen kontrol der fylder halv panelbredde; brug naturlig bredde og `.w-*`.
- Statusfarvens tekst-tone (`danger` osv.) bruges aldrig som knap- eller flade-farve; solide flader bruger `…-fill` med `on-fill`/`on-warn-fill`.
- Ingen gradienter mellem to kulører, ingen overlappende flader i forskellige farver, ingen flerfarvede ringe/bjælker (DESIGN.md 3.2.2).
- Ingen jævne orange/røde flader under 30 % dækning i mørkt tema (bliver brune). Brug glød-gradient (`url(#lds-fade-heat)`), skravering (`url(#lds-hatch-heat)`) eller solide farver.
- Ingen varme grå som mørke flader.
- Ingen sidebar, ingen modaler, ingen faner ud over tilstandspillen.
- Ingen JavaScript til navigation eller tilstand (brug radio/checkbox/`<details>`/`popover`). JS kun til live-data, +/−, submit-hook, ugemt/gem, autogem, kopiér diagnostik og placering af hjælp- og bekræftelses-popover (så den bliver i vinduet).
- Ingen eksterne ressourcer (fonte, CDN, billeder).
- Ingen skygger på paneler; dybde kommer fra `--card` mod `--bg`.
- Ingen versaler, ingen pynte-farver, ingen animation uden brugerhandling.
- Ingen `display: none` på `.state`-inputs.
- Ingen manifold- eller zonevisninger i Touch-produktet (Touch har ingen egne zoneindstillinger).
- Ingen stepper til port, id'er eller adresser.

## Vægskærmen (LVGL, Touch)
- Brug kun farver fra `dist/display/lune_theme.*` (RGB565-præcise). Ingen alfa, gradienter eller skygger.
- Trykflader mindst 64 px; − / + 96 px. Mindste tekst 16 px.
- Zonefelter altid i 6 kolonner pr. manifold. Ingen scroll på oversigten.
- Ingen indstillinger på skærmen ud over mål, forvalg, zone til/fra og nulstil fejl.

## Før du er færdig
- Kør `python tools/lds_build.py config/<projekt>.json` og byg siden.
- Kontrollér ved 360, 820 og 1440 px, lyst og mørkt tema, og med tastatur.
- Gennemgå tjeklisten i `DESIGN.md` afsnit 11.
