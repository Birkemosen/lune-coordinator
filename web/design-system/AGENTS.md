# Regler for kodeagenter (Lune V6 og Lune Touch)

Kopiér denne fil ind i projektets `CLAUDE.md` / `AGENTS.md`, eller henvis til den. Den fulde begrundelse står i `DESIGN.md`.

## Altid
- Byg UI med komponenterne i `css/lune-ui.src.css`. Find den nærmeste eksisterende komponent, før du laver en ny.
- Brug kun tokens (`var(--…)`) fra `tokens/tokens.json`. Ingen hex-værdier, ingen `#fff`, ingen px-størrelser uden for skalaerne (`--space-*`, `--fs-*`, `--r-*`, `--hit`).
- Ny farve eller ny tekst/baggrund-kombination: tilføj den i `tokens.json` (med en `contrast`-linje) og kør `python tools/lds_build.py --check`.
- Hver visning har id `v-{dash|conf}-{omfang}` og ligger som barn af `main.content`. Nye omfang tilføjes i `config/<projekt>.json`, ikke i CSS.
- Indhold i 2–4 paneler (`.panel`, bredde `c4`–`c8`). Én titel og ét emne pr. panel. Formularer er `form.panel` med `data-save` og én `.btn.primary` i `.panel-foot`.
- Lange conf-sider: del i `.section` / `.section-grid` (4.3) med sektionslinks ved 3+ sektioner — fx Manifold og motorer, Regulering, Forbindelser, Service.
- Fold aldrig sektioner ud over Service/Udvikler. Kun det sjældne og det risikable foldes.
- Dashboard: et panel mere end dobbelt så lavt som naboen flettes (`.sub`). Konfiguration: 1/2/3 spalter med naturlig panelhøjde (`align-items: start`); `cN` = én spalte, `.wide` = alle. `.stack` er udgået. Et panel alene i en række fylder hele bredden (ingen `cN`) med `.subs.cols-2`.
- Tæthed følger inputtypen: mus på ≥ 1024 px er kompakt (`--hit` 32 px), touch uændret (DESIGN.md 3.9). Brug tæthedstokens (`--panel-pad`, `--field-gap`, `--row-h` …), ikke faste px. Kontroller i `.field.row` har én fælles bredde (`--ctl-w`); kun lange felter (URL, skabeloner) får `.w-lg` og står under labelen.
- Manglende værdier vises som «—», aldrig 0. Tomme grafer klapper sammen til én `.empty`-linje (`data-empty` på beholderen).
- Underafsnit må have egne tilstandshandlinger; panelets footer er kun Gem.
- Vis kun handlinger, der passer til tilstanden (`data-state` + `data-show-when`, DESIGN.md 6.1b).
- Hjælp i tre lag: gode labels/`.hint`, ét `.help-btn` (?) pr. panel med native `popover.help-pop` (klik/tryk, ikke hover), og «Læs mere» til produktets `docs/Manual.md#…` (V6: `lune-v6/docs/Manual.md`, Touch: tilsvarende i coordinator-repoet). Orange er varme — `?` er neutral.
- Enhedsmenuen (`details.device`): «Om enhed» (identitet + kopiér diagnostik) over listen af andre enheder. Ingen driftshandlinger i menuen.
- Al tekst kommer fra i18n-kataloget, også `aria-label`, `title` og `placeholder`. Knaptekst er verbum + objekt.
- Skjul eller deaktivér aldrig gem-knapper; brug clean/dirty-mønstret i DESIGN.md 6.1.
- Hverdagshandlinger (Dashboard) autogemmes; opsætning (Konfiguration) gemmes eksplicit pr. panel. Switches (`role="switch"`) i conf gemmes dog med det samme ved skift.
- Destruktive handlinger bekræftes med `.confirm-pop`; aldrig inline-udfoldning, aldrig "OK".
- Farve har én betydning: orange = varme, blå = vejr/sensorer, grøn = ok, gul = snart opmærksomhed, rød = fejl nu, lilla = læring/gruppering. Farve er aldrig eneste signal.
- Tal: `.metric` for nøgletal, `.kv` for detaljer. Enhed i `<small>`. Decimaltegn efter sprog.
- Grafer: inline-SVG kun til figurer; alle tekster i HTML. Mål som trappekurve, temperaturakse mindst 3 °C.
- **Designændringer i produktet (V6/Touch) skal også landes her i LDS** (`css/lune-ui.src.css`, `examples/…`). Ret aldrig kun den kopierede CSS/HTML i produkrepoet.
- Touch har kun omfanget Hus. V6-boards vises som én `.strip`-række pr. board (`.boards`) på hus-dashboardet; zoner konfigureres på V6, ikke i Touch. Kun det Touch ejer (rum: medregn, vægt, vind, sol) redigeres i Touch.
- Typeafhængige felter (fx varmekilde) bruger `.typed-fields` / `.hs-fields` med lokale radioer og søskende-selektorer — ikke JavaScript.
- Testknapper skal altid vise et resultat (`.test-result`: to linjer, `aria-live`, bliver stående, gemmes ikke).
- Vis rå firmwaretekster aldrig direkte. Tilstande oversættes og får badge-farve efter betydning.

## Aldrig
- Ingen sidebar, ingen modaler, ingen faner ud over tilstandspillen.
- Ingen JavaScript til navigation eller tilstand (brug radio/checkbox/`<details>`/`popover`). JS kun til live-data, +/−, submit-hook, dirty/save, autogem, kopiér diagnostik, placering af hjælp-popover nær `?` og af `.confirm-pop` ved knappen, så den bliver i vinduet.
- Ingen eksterne ressourcer (fonte, CDN, billeder).
- Ingen skygger på paneler; dybde kommer fra `--card` mod `--bg`.
- Ingen versaler, ingen pynte-farver, ingen animation uden brugerhandling.
- Ingen `display: none` på `.state`-inputs.
- Ingen manifold- eller zonevisninger i Touch (Touch har ingen egne zoneindstillinger).

## Før du er færdig
- Kør `python tools/lds_build.py config/<projekt>.json` og byg siden.
- Kontrollér ved 360, 820 og 1440 px, lyst og mørkt tema, og med tastatur.
- Gennemgå tjeklisten i `DESIGN.md` afsnit 11.
