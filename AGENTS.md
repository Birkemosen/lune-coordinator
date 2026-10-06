# Regler for kodeagenter (Lune Touch web)

Web-dashboardet følger **Lune Design System 2** i `web/design-system/`.
Den fulde begrundelse står i `web/design-system/DESIGN.md`.

LVGL-displayet (`packages/display/`) får tema, C++-farver og brand-mærker fra søskende-repoet `../lune-design-system` (`display` i `tokens/tokens.json`) via `make design-tokens`. Ret aldrig de genererede filer i hånden.

## Altid (web)
- Byg UI med komponenterne i `web/design-system/css/lune-ui.src.css`. Find den nærmeste eksisterende komponent, før du laver en ny.
- Brug kun tokens (`var(--…)`) fra `web/design-system/tokens/tokens.json`. Ingen hex-værdier, ingen `#fff`, ingen px-størrelser uden for skalaerne (`--space-*`, `--fs-*`, `--r-*`, `--hit`).
- Ny farve eller ny tekst/baggrund-kombination: tilføj den i `tokens.json` (med en `contrast`-linje) og kør `python web/design-system/tools/lds_build.py --check`.
- Informationsarkitektur: Hjem / ark / System (`web/design-system/DESIGN.md` afsnit 15). Visninger: `#v-home-house` og `#v-sys` under `main.content`; ark (`.sheet`) pr. ting: varme, næste varme, vejr, cirkulation, styring og rum (binderen opretter dem fra `<template id="tpl-mani">`/`tpl-room` og binder formularerne med `window.luneForms.scan`). Den hierarkiske strimmel bruges ikke.
- Indstillinger er grupperede lister (`.setting-group`, maks. 6 rækker pr. gruppe og 5 grupper pr. fane/kategori) med én `.savebar` pr. formular. Delvis gem af én ressource: `data-save="ressource.del"` + `data-patch`. Tjek med `python web/touch-ui/check_fields.py`.
- Hjem: `.home-hero` + `.thermo` (autogem), maks. 4 `.home-tile`, varmekort (`.heatmap`).
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
- Ingen sidebar, ingen modaler; faner kun i ark (`.tabs`).
- Ingen JavaScript til navigation eller tilstand (brug radio/checkbox/`<details>`/`popover`). JS kun til live-data, +/−, submit-hook, kopiér diagnostik, placering af hjælp-popover nær `?`.
- Ingen eksterne ressourcer (fonte, CDN, billeder).
- Ingen skygger på paneler; dybde kommer fra `--card` mod `--bg`.
- Ingen versaler, ingen pynte-farver, ingen animation uden brugerhandling.
- Ingen `display: none` på `.state`-inputs.

## Før du er færdig (web)
- Kør `make touch-ui` (bygger CSS + sider).
- Kontrollér ved 360, 820 og 1440 px, lyst og mørkt tema, og med tastatur.
- Gennemgå tjeklisten i `web/design-system/DESIGN.md` afsnit 11.
