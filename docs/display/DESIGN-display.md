# Lune Design System — vægskærm (uddrag af DESIGN.md afsnit 13)

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
| Kalder | 1–5 orange segmenter |
| Hviler | 1 segment |
| Fejl | ID og "Fejl" i rødt, første segment rødt |
| Slukket | 50 % opacitet, "Slukket" |
| Gruppe | Primær: 2 px orange kant, ID "Z4–5". Medlem: stiplet kant, dæmpet navn |
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

Kontrasten tjekkes med `python tools/lds_display.py --check`.

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
