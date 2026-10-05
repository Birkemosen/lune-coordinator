# Regler for kodeagenter (Lune Touch web)

Web-dashboardet følger **Lune Design System 2** i `web/design-system/`.
Den fulde begrundelse står i `web/design-system/DESIGN.md`.

LVGL-displayet (`packages/display/`) får tema, C++-farver og brand-mærker fra søskende-repoet `../lune-design-system` (`display` i `tokens/tokens.json`) via `make design-tokens`. Ret aldrig de genererede filer i hånden.

## Altid (web)
- Byg UI med komponenterne i `web/design-system/css/lune-ui.src.css`. Find den nærmeste eksisterende komponent, før du laver en ny.
- Brug kun tokens (`var(--…)`) fra `web/design-system/tokens/tokens.json`. Ingen hex-værdier, ingen `#fff`, ingen px-størrelser uden for skalaerne (`--space-*`, `--fs-*`, `--r-*`, `--hit`).
- Ny farve eller ny tekst/baggrund-kombination: tilføj den i `tokens.json` (med en `contrast`-linje) og kør `python web/design-system/tools/lds_build.py --check`.
- Hver visning har id `v-{dash|conf}-{omfang}` og ligger som barn af `main.content`. Delte omfang: `house`, `manifold`, `zone` (hierarki hus → manifold → zone; se `web/design-system/config/touch.json`).
- Indhold i 2–4 paneler (`.panel`, bredde `c4`–`c8`). Én titel og ét emne pr. panel. Formularer er `form.panel` med `data-save` og én `.btn.primary` i `.panel-foot`.
- Lange conf-sider: del i `.section` / `.section-grid` (4.3) med sektionslinks ved 3+ sektioner — Opsætning, Varme og vejr, Enhed, Service.
- Hjælp i tre lag: gode labels/`.hint`, ét `.help-btn` (?) pr. panel med native `popover.help-pop` (klik/tryk, ikke hover), og «Læs mere» til `docs/Manual.md#…` i dette repo. Orange er varme — `?` er neutral.
- Enhedsmenuen (`details.device`): «Om enhed» (identitet + kopiér diagnostik) over listen af andre enheder. Ingen driftshandlinger i menuen.
- Al tekst kommer fra i18n-kataloget i `web/touch-ui/i18n/`, også `aria-label`, `title` og `placeholder`. Knaptekst er verbum + objekt.
- Destruktive handlinger bekræftes med `.confirm-pop`; aldrig inline-udfoldning, aldrig "OK".
- Testknapper skal altid vise et resultat (`.test-result`: to linjer, `aria-live`, bliver stående, gemmes ikke).
- Vis rå firmwaretekster aldrig direkte. Tilstande oversættes og får badge-farve efter betydning.
- Farve har én betydning: orange = varme, blå = vejr/sensorer, grøn = ok, gul = snart opmærksomhed, rød = fejl nu, lilla = læring/gruppering. Farve er aldrig eneste signal.
- Tal: `.metric` for nøgletal, `.kv` for detaljer. Enhed i `<small>`. Decimaltegn efter sprog.
- Grafer: inline-SVG kun til figurer; alle tekster i HTML.
- **Designændringer skal også landes i LDS** (`../lune-design-system` → `css/lune-ui.src.css`, `examples/…`). Ret aldrig kun den kopierede CSS i `web/design-system/` eller `web/touch-ui/dist`.
Lune Design System code lives in the repository
`Birkemosen/lune-design-system`. The shared design system and components is owned there.
Every change to UI etc. must conform into Lune Design System.

## Aldrig (web)
- Ingen sidebar, ingen modaler, ingen faner ud over tilstandspillen.
- Ingen JavaScript til navigation eller tilstand (brug radio/checkbox/`<details>`/`popover`). JS kun til live-data, +/−, submit-hook, kopiér diagnostik, placering af hjælp-popover nær `?`.
- Ingen eksterne ressourcer (fonte, CDN, billeder).
- Ingen skygger på paneler; dybde kommer fra `--card` mod `--bg`.
- Ingen versaler, ingen pynte-farver, ingen animation uden brugerhandling.
- Ingen `display: none` på `.state`-inputs.

## Før du er færdig (web)
- Kør `make touch-ui` (bygger CSS + sider).
- Kontrollér ved 360, 820 og 1440 px, lyst og mørkt tema, og med tastatur.
- Gennemgå tjeklisten i `web/design-system/DESIGN.md` afsnit 11.
