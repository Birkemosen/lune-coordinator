# Lune Touch vægskærm — skærme, mål og widget-træ

Skærm 1024×600, RGB565. Alle mål i px. Farvenavne refererer til `theme/display-tokens.json` → `palette.dark` (mørkt tema er standard) og findes som `${lds_*}` i `theme/lune_theme.yaml` og `LDS_DARK_*` i `theme/lune_theme.h`. Fontnavne: `lds_font_{xs|sm|md|lg|xl|2xl|hero}` = 16/20/24/32/48/72/144 px.

**Fonte:** Mockupsene bruger enkelte mellemstørrelser (18, 22, 28 px). På skærmen rundes tekst til nærmeste trin i skalaen (18 → `sm` 20, 22 → `md` 24). Tal, der skal passe i et felt, får i stedet egne fonte med kun cifre og `, . ° − : /`:

| Id | px | Bruges til |
|---|---|---|
| `lds_font_d28` | 28 | Temperatur i kompakt zonefelt, værdier i zone-skærmens 2×2 |
| `lds_font_d36` | 36 | Temperatur i stort zonefelt |
| `lds_font_d44` | 44 | "°C" ved hero-tallet |
| `lds_font_d168` | 168 | Uret på nat-skærmen |

Tilføj dem i `theme/lune_theme.yaml` ved siden af de eksisterende (samme mønster som `2xl`/`hero`).

Billederne i `mockups/png/` er facit for udseendet: `Home4`, `Home2`, `Home2_Day`, `Zone`, `ZoneFault`, `Night`, `Components` (samme navne som mockup-kilderne). Denne fil er facit for mål og opbygning. `mockups/*.dc.html` er kildekoden til mockupsene og kan læses for detaljer (inline-styles), men skal ikke køres.

---

## Fælles

| Del | Mål |
|---|---|
| Skærmmargen | 16 venstre/højre/bund |
| Statuslinje | højde 64, padding 0 20, gap 24 |
| Mellemrum mellem rækker | 10 (husrække → manifolds), 8 mellem kompakte rækker, 10 mellem store |
| Kort (`lds_card`) | bg `card`, radius 20, ingen kant, ingen skygge |
| Zonefelt (`lds_tile`) | bg `raised`, radius 14 (kompakt) / 16 (stort) |
| Niveaubjælke | 5 segmenter, vandret, højde 5, gap 3, radius 3. Tændt `fg` (ventilåbning, alle zoner), slukket `seg-off`, fejl: første segment `danger` |
| Afvigelses-chip | Pille h 22, `dev-1…5` efter afstand til mål (LDS 13.4); erstatter gul temperatur og «mål» i felterne |
| Mindste tekst | 16 |
| Mindste trykflade | 64×64 |

### Statuslinje (alle oversigter)

```
row (flex, align center, gap 24, h 64, pad 0 20)
├─ label "14:32"            lds_font_lg, fg
├─ label "ons 30. september" lds_font_sm, muted
├─ row gap 10: ikon sky 26 muted · label "13,4 °C" md, fg · ikon vind 20 + label "6 m/s" sm, info
├─ (flex grow)
├─ button fejlpille (kun ved fejl)   h 44, radius 999, bg danger-bg, tekst danger, ikon advarsel 20
│     tekst "M1 · Z6 Soveværelse: motorfejl" → åbner zone-skærmen for den zone
└─ ikon wifi 24 muted
```

---

## 1. Oversigt (`Home4` = 3–4 manifolds, `Home2` = 1–2 manifolds, `Home2_Day` = lyst tema)

```
screen (bg, flex column)
├─ statuslinje (64)
└─ main (flex column, gap 10, pad 0 16 16)
   ├─ husrække  (lds_card, h 104, grid: 220 | 1fr | 200 | 210, gap 20, pad 12 18)
   │  ├─ kolonne: "Huset" xs muted + "● Kalder" xs accent-ink
   │  │           "21,3°" lds_font_xl fg  +  "mål 21,6°" / "7 af 16 kalder" xs muted (2 linjer)
   │  ├─ lv_chart linje 24 t (48 punkter), h 52: temp fg 2 px, mål muted (trappe)
   │  │           akse-labels "−24 t" / "nu" xs faint
   │  ├─ kolonne m. venstre streg 1 px line, pad-left 20:
   │  │           "Varmepumpe" xs muted · "34,2° → 29,8°" md fg · "Pumpe 1,2 m³/h · 38 W" xs muted
   │  └─ besked (bg info-bg, radius 12, pad 10 12): ikon vind 20 info + "11 m/s i nat." info + resten fg, xs
   └─ manifolds (flex column, deler resten ligeligt, gap 8 kompakt / 10 store)
      └─ pr. manifold: række (lds_card, grid: HEAD | 6 × 1fr, gap 8/10, pad 9 12 / 12 14)
         ├─ HEAD (124 kompakt / 150 store), kolonne, centreret lodret:
         │   "Stueetage" sm (kompakt) / md (store) 600 + "M1" xs muted
         │   "34,2° → 29,8°" xs muted
         │   (store) "ΔT 4,4 K" xs muted
         │   "● Kalder" / "● Hviler" / "● 1 fejl" xs 600 (accent-ink / muted / danger)
         └─ 6 zonefelter (tomme pladser: stiplet ramme 2 px raised, ingen tekst)
```

**Hvornår kompakt / store felter:** 1–2 manifolds → store, 3–4 → kompakte. Altid 6 kolonner.

**Dagtema (`Home2_Day`):** samme opbygning med farverne fra `palette.light`. Byg det som et skift af farvesæt (fx efter tidspunkt eller en indstilling), ikke som egne skærme.

Pixelbudget ved 4 manifolds: 600 − 64 − 104 − 10 − 16 = 406 → 4 rækker á ≈ 95 → felthøjde ≈ 77. Feltbredde ≈ 132.

### Zonefelt, kompakt (≈ 132 × 77)

```
button lds_tile (flex column, space-between, gap 4, pad 8 12 9)
├─ row: "Z1" xs 600 muted  +  "Josephine" xs 500 fg (ellipse)
├─ row: "21,4°" lds_font_d28 600 (warn hvis mål − temp > 0,5) · "/22,0" xs muted, margin-left 4
└─ niveaubjælke
```

Tjek at "21,4° /22,0" er ≤ 108 px bredt; ellers skjules "/22,0".

### Zonefelt, stort (≈ 126 × 174)

```
button lds_tile (flex column, gap 8, pad 12 14)
├─ row: "Z1" xs 600 muted + navn sm 500 (ellipse)
├─ "21,4°" lds_font_d36 600 (warn hvis mål − temp > 0,5)
├─ "mål 22,0°" xs muted
├─ (bunden) lv_chart 24 t, h 32
└─ niveaubjælke
```

### Tilstande på zonefelt

| Tilstand | Ændring |
|---|---|
| calling | segmenter = ceil(ventil%/20), mindst 1 |
| idle | 1 segment |
| fault | ID og værdi "Fejl" i danger; første segment danger; stort felt: "Motorfejl · 17,8°" xs danger |
| off | opa 50 %, værdi "Slukket" |
| gruppe primær | kant 2 px accent, ID "Z4–5" |
| gruppe medlem | stiplet kant 2 px accent (LVGL har ikke stiplede kanter: brug 2 px kant i `accent` med 50 % opa, eller 4 små hjørnemarkeringer) |
| presset | bg inv-bg, tekst inv-fg i 150 ms |

**Tryk på felt** → zone-skærmen for den zone (fejl-varianten hvis zonen er i fejl).

---

## 2. Zone, fuld skærm (`Zone`, `ZoneFault`)

```
screen (bg, flex column)
├─ topbar (h 96, pad 16 20, gap 18, align center)
│  ├─ rund knap 64 (raised) ikon ‹ 30  → tilbage til oversigt
│  ├─ kolonne: "Josephine" lg 600 · "Stueetage · Z1" sm muted
│  ├─ badge "Kalder varme" (accent-ink / on-accent) eller "Motorfejl" (danger-bg / danger), h 32
│  ├─ (flex grow)
│  ├─ "1 af 16" sm muted
│  └─ runde knapper 64: ‹ forrige zone · › næste zone
└─ main (grid: 1.25fr | 1fr, gap 12, pad 0 16 16)
   ├─ venstre kort (lds_card, pad 24 28, flex column space-between)
   │  ├─ "Rumtemperatur nu" sm muted
   │  ├─ "21,4" lds_font_hero 600 + "°C" lds_font_d44 muted
   │  ├─ grid 96 | 1fr | 96, gap 16:
   │  │    rund knap 96 "−" (ikon 36) · kolonne: "Mål" sm muted + "22,0" lds_font_2xl + " °C" lg muted · rund knap 96 "+"
   │  └─ 3 piller h 64, gap 12: "Komfort 22°" · "Eco 20°" · "Nat 18°"  (aktiv = inv-bg/inv-fg, ellers raised/fg)
   └─ højre kort (lds_card, pad 22 24, flex column gap 18)
      ├─ (KUN fejl) fejlboks bg danger-bg, radius 16, pad 18 20:
      │     "Motorfejl: endestop timeout" md danger · forklaring sm fg · knap h 64 "Nulstil fejl" (bg danger, tekst bg)
      ├─ "Sidste 24 timer" sm 600 + forklaring "— temp - - mål" muted
      ├─ lv_chart h 150: 3 vandrette gitterlinjer (line), temp fg 2,5 px, mål muted trappe
      │     akse "−24 t · −12 t · nu" xs faint
      ├─ grid 2 × 2 (gap 14 20): label xs muted + værdi lds_font_d28 600
      │     Ventil 62 % · Retur 29,1 °C · Vejr-preload +0,4 °C (info) · Føler BLE (info)
      │     (fejl: Ventil 0 % (danger) · Retur 22,1 °C)
      └─ (bunden) række h 64: "Zone aktiv" sm 500 + switch (tændt: inv-bg)
```

Opførsel:
- − / + ændrer målet i trin á 0,5 °C (12–28) og nulstiller valgt forvalg.
- Forvalg sætter målet og markeres aktivt.
- Ændringer sendes 1,5 s efter sidste tryk (ingen gem-knap).
- Swipe venstre/højre eller ‹ › skifter zone (`tileview`, én tile pr. zone).
- Skærmen lukker selv efter 60 s uden berøring.

---

## 3. Nat og dvale (`Night`)

```
screen bg #000000 (0x0000), hele skærmen er én trykflade → oversigt
grid: 1fr | 380, pad 48 64, align center
├─ "22:47" lds_font_d168 500 (#8c8a84)
│  "onsdag 30. september · 11,2 °C ude" md (#5a5853)
└─ kolonne gap 22
   ├─ "Huset" sm (#5a5853) · "21,1" lds_font_2xl 500 + " °C" lg
   ├─ pr. manifold: navn (bredde 110, sm, #5a5853) + én prik 18 px pr. zone (gap 10)
   │     kalder #8a3f1c · fejl #8a4a47 · hviler #2d2c27 · slukket 40 % opa
   └─ (ved fejl) ikon advarsel + "1 fejl · tryk for at se" sm (#8a4a47)
```

Natfarverne er bevidst dæmpede og ligger uden for paletten. Tilføj dem som `night_*` i temaet, hvis de skal genbruges. Skærmens lysstyrke sænkes via baggrundslyset (fx 15 %).

---

## 4. Tidsregler

| Hændelse | Tid |
|---|---|
| Zone-skærm → oversigt uden berøring | 60 s |
| Oversigt → dæmpet/dvale | 120 s |
| Natperiode (dvale-skærm i stedet for dæmpet oversigt) | 22:00–06:00 |
| Opdatering af værdier | 5 s (eller ved push) |

Alt kommer fra `display.timing` i `theme/display-tokens.json` / `LDS_*` i headeren.

---

## 5. Data pr. skærm (det Touch skal levere)

| Data | Bruges i |
|---|---|
| Hus: vægtet temp, mål, antal kaldende zoner, 48 × (temp, mål) | husrække |
| Varmekilde: fremløb, retur, tilstand; pumpe: flow, effekt | husrække |
| Vejr: ude temp, vind, preload-besked | statuslinje, husrække |
| Pr. manifold: navn, id, fremløb, retur, tilstand | rækkehoved |
| Pr. zone: id, navn, temp, mål, ventil %, tilstand, gruppe, 48 × (temp, mål), retur, preload-offset, kilde | felter, zone-skærm |
| Fejl: zone, type, tekst | statuslinje, felt, zone-skærm |
