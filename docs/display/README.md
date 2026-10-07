# Lune Touch vægskærm — handoff til Cursor

> **Mockups er fra før LDS 2.3.8.** Billederne i `mockups/png/` har den gamle palet (varme grå,
> orange ventilbjælke, gul temperatur). Siden LDS 2.3.8 bruger vægskærmen kølige neutrale grå, en neutral
> ventilbjælke og en afvigelses-chip i 5 trin (`dev-1…5`). Opbygning og mål i mockups gælder stadig;
> for farver er temaet (`theme/`, `packages/display/lvgl/tokens.generated.yaml`) og
> [`SCREENS.md`](SCREENS.md) facit.

Alt Cursor skal bruge for at bygge vægskærmen i LVGL. Designet er lavet på et Claude-canvas, som Cursor ikke kan åbne, så her er indholdet som filer.

| Fil | Hvad |
|---|---|
| `SCREENS.md` | Hver skærm som widget-træ med mål i px, tilstande, opførsel og data |
| `DESIGN-display.md` | Reglerne for vægskærmen (uddrag af designsystemets afsnit 13) |
| `theme/lune_theme.yaml` | ESPHome-pakke: farver, Geist-fonte, LVGL style_definitions |
| `theme/lune_theme.h` | Samme værdier som C-konstanter |
| `theme/display-tokens.json` | Kilden til temaet (palet med RGB565-koder, typeskala, mål, tider) |
| `mockups/*.dc.html` | Mockup-kildekoden fra canvasset; kun til at aflæse mål, kan ikke køres uden canvas-runtime |
| `mockups/png/` | Billeder af skærmene (navne i tabellen nedenfor) |
| `.cursor/rules/lune-display.mdc` | Regler, Cursor automatisk bruger, når den arbejder i display-filer |
| `PROMPT.md` | Prompten til Cursor |

## Sådan

1. **Læg mappen i Touch-repoet** som `docs/display/`, og flyt `.cursor/rules/lune-display.mdc` til repoets rod (`.cursor/rules/`). Ret stierne i reglen, hvis du placerer mappen et andet sted.
2. **Læg billederne af skærmene** i `docs/display/mockups/png/` med disse navne:

   | Billede | Skærm | Mockup-kilde |
   |---|---|---|
   | `Home4.png` | Oversigt, 3–4 manifolds (kompakte felter) | `mockups/Home4.dc.html` |
   | `Home2.png` | Oversigt, 1–2 manifolds (store felter) | `mockups/Home2.dc.html` |
   | `Home2_Day.png` | Oversigt, 1–2 manifolds, dagtema (lyst) | `mockups/Home2_Day.dc.html` |
   | `Zone.png` | Zone i fuld skærm | `mockups/Zone.dc.html` |
   | `ZoneFault.png` | Zone med motorfejl | `mockups/ZoneFault.dc.html` |
   | `Night.png` | Nat og dvale | `mockups/Night.dc.html` |
   | `Components.png` | Palet, felttilstande, kontroller, typeskala | `mockups/Components.dc.html` |

   Billedets filtype er ligegyldig (`.png`/`.jpg`), bare navnet passer.
3. **Åbn Cursors Agent**, vedhæft billederne, og indsæt prompten fra `PROMPT.md`. Den bygger i fire trin og stopper efter hvert, så du kan teste på skærmen.

## Godt at vide

- Mål og farver i `SCREENS.md` og temaet er det præcise facit. Billederne viser helheden, men Cursor læser dem kun omtrentligt.
- YAML-pakken er ikke testet mod jeres ESPHome-version. Bed Cursor tjekke stilnavne og egenskaber som det første.
- Mockupsene bruger eksempeldata (manifold- og zonenavne). Forvalgene Komfort/Eco/Nat findes ikke i Touch i dag.
- Ændrer du designet, så opdatér tokens i designsystemet og kør `tools/lds_display.py` igen. Kopiér den nye `dist/display/` ind her.
