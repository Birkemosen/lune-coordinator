# Changelog — Lune Touch

## v1.1.0-rc1 — first release candidate (2026-11-02)

First tagged release of Lune Touch: the whole-house coordinator for Lune V6 manifolds,
with Asgard (Ecodan) and Odin (optimizer) as the heat source. Runs on the Waveshare
ESP32-S3 Touch LCD 7B (wall display plus web UI). If a week passes without changes,
this becomes v1.1.0.

### House and rooms
- Logical rooms over V6 loops; a V6 sync group is one room with the area and UA of all
  its loops. Area, walls and floor are owned by V6 and shown read-only.
- Room history (hourly, 7 days) recorded on the Touch and kept across restarts; house
  history from Odin's hourly ledger. Charts with a 24 h forecast in the room, next
  heating and weather sheets (48 h / 7 d).
- Learned solar and wind factor per room; solar hold-off for sunny rooms; preload and
  floor charge ahead of wind and cold.

### Heat source (Asgard / Odin)
- Honest house temperature and a capped, slewed comfort signal to the heat pump; a
  lagging room lifts Odin's band for its floor lag. Odin's band can differ below and
  above the house target; Odin's setpoint can follow the house target (opt-in).
- Heat-loss learning and calibration against Odin's heat loss, with internal gains
  added back.
- Heat-pump check: plan deviation, mixing and short flow, with problems on Home and in
  Home Assistant.
- Electricity prices for every Odin bidding zone (Odin fetches or Touch pushes, with
  Danish tariffs); today's expected heat-pump cost.

### Circulation
- Alpha2 GO pump readings (flow, head, power); buffer balance by gekkekoe's rule (heat
  into the tank ≤ heat out): recommended secondary flow 105 % of primary, warnings below
  100 % and above 125 %, heat into and out of the tank.
- Heat pump maximum output from Odin, or set in Touch when Odin gives none.
- Rough hydraulic balance between manifolds (opt-in), loop pressure in kPa and metres;
  flow guide by house age and for this house.

### Interface
- Web UI on the Lune Design System (Home / sheets / System), Danish and English.
- Wall display: house overview, manifold rows, zone detail; partial refresh.
- Problem notifications to Home Assistant with a Lune blueprint.

### Fixed in this candidate
- Room history survives a restart. Renaming a file fails on the Touch's SPIFFS, so every
  save was lost; the week is now written to two slots in turn, and the file an earlier
  build left behind is restored once. Storage status in `GET /diagnostics`
  (`room_history`).
- The walls conflict between Touch and V6 clears once V6's walls have been adopted.
- Circulation settings stay within five groups (pump entities on a subpage).
- Display and dashboard tests run without ripgrep.
