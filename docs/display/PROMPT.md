# Prompt til Cursor

Indsæt i Cursors Agent-chat (med billederne fra `docs/display/mockups/png/` vedhæftet, hvis du har dem):

```text
Byg Lune Touch' vægskærm i LVGL efter designet i docs/display/.

Billederne i docs/display/mockups/png/ og afsnittene i SCREENS.md hører sammen sådan:
Home4 = oversigt med 3–4 manifolds (kompakte felter), Home2 = oversigt med 1–2 manifolds
(store felter), Home2_Day = Home2 i lyst dagtema, Zone = zone i fuld skærm,
ZoneFault = zone med motorfejl, Night = nat/dvale, Components = palet, felttilstande,
kontroller og typeskala.

Læs først: docs/display/README.md, SCREENS.md, DESIGN-display.md, theme/lune_theme.yaml,
theme/lune_theme.h og theme/display-tokens.json. Billederne i docs/display/mockups/png/ er
facit for udseendet; SCREENS.md er facit for mål og widget-træ.

Find ud af, om Touch-firmwaren bruger ESPHome `lvgl:` eller ren LVGL i C, og byg i det,
der allerede er i brug. Find den eksisterende datamodel for hus, varmekilde, manifolds og
zoner og bind skærmene til den; opfind ikke felter — mangler noget, så lav en TODO og list det.

Byg i denne rækkefølge, og stop efter hvert trin, så jeg kan teste på skærmen:
1. Tema: inkludér lune_theme (farver, fonte inkl. tal-fontene d28/d36/d44/d168, style_definitions).
2. Oversigt (Home4 og Home2) med statuslinje, husrække og manifold-rækker med kompakte
   zonefelter (3–4 manifolds) og store felter (1–2 manifolds), valgt automatisk ud fra antal
   manifolds. Dagtemaet (Home2_Day) bygges som et andet farvesæt fra palette.light, ikke som
   en separat skærm.
3. Zone-skærm (Zone og ZoneFault) som tileview (én tile pr. zone): mål −/+ i trin á 0,5, forvalg, 24-t graf
   (lv_chart, 48 punkter, temp + mål), detaljer, zone til/fra, fejl-variant med "Nulstil fejl".
   Send mål 1,5 s efter sidste tryk.
4. Nat/dvale-skærm (Night) og tidsreglerne (60 s retur, 120 s dvale, nat 22–06).

Følg reglerne i .cursor/rules/lune-display.mdc. Mål flash- og RAM-forbrug for fontene og
rapportér dem. Afslut hvert trin med en liste over ændrede filer og hvad jeg skal se på skærmen.
```
