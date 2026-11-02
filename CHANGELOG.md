# Changelog — Lune Touch

## v1.0.0-rc1 — first release candidate (2026-11-02)

First tagged release of Lune Touch: the whole-house coordinator for Lune V6 manifolds.
It turns the rooms into one house temperature and comfort target for the heat source,
and runs on the Waveshare ESP32-S3 Touch LCD 7B (wall display and web UI) or headless as
Lune Mini. If a week passes without changes, this becomes v1.0.0.

### House and rooms
- Logical rooms over V6 loops; a V6 sync group is one room with the area and heat loss
  of all its loops. Area, walls and floor are owned by V6 and shown read-only.
- Room history (hourly, 7 days) recorded on the Touch and kept across restarts, with
  charts and a 24 h forecast in the room, next heating and weather sheets (48 h / 7 d).
- Learned solar and wind factor per room; solar hold-off for sunny rooms; preload and
  floor charge ahead of wind and cold from the weather forecast.

### Heat source
- Heat-source adapters: generic HTTP (write and read URL templates, for any controller
  that takes a temperature over HTTP) and [Asgard](https://github.com/gekkekoe/esphome-ecodan-hp),
  ESPHome firmware for Mitsubishi Ecodan heat pumps, through its virtual thermostat.
- An honest house temperature and a capped, slewed comfort signal: cold rooms are not
  hidden by warm ones, and a lagging room lifts the target for its floor lag.
- Heat-loss learning and calibration of each room, with internal gains added back.
- Heat-pump check: plan deviation, mixing and short flow, with problems on Home and in
  Home Assistant.
- Optional optimizer: with [Odin](https://github.com/gekkekoe/heatpump-optimizer) Touch
  reads the heating plan and the house heat loss, can keep Odin's comfort band on the
  house target and push electricity prices (every bidding zone, Danish tariffs);
  today's expected heat-pump cost.

### Circulation
- Circulation pump readings (flow, head, power) from an ESPHome pump bridge, e.g. a
  Grundfos ALPHA2 GO.
- Buffer balance for a parallel buffer tank (heat into the tank ≤ heat out):
  recommended secondary flow 105 % of primary, warnings below 100 % and above 125 %, and
  heat into and out of the tank.
- Heat pump maximum output from the optimizer, or set in Touch.
- Rough hydraulic balance between manifolds (opt-in), loop pressure in kPa and metres;
  flow guide by house age and for this house.

### Interface
- Web UI on the Lune Design System (Home / sheets / System), Danish and English.
- Wall display: house overview, manifold rows, zone detail; partial refresh.
- Problem notifications to Home Assistant with a Lune blueprint.

Asgard and Odin are separate projects by their own authors; Lune is not affiliated with
them.

### Fixed in this candidate
- Room history survives a restart. Renaming a file fails on the Touch's SPIFFS, so every
  save was lost; the week is now written to two slots in turn, and the file an earlier
  build left behind is restored once. Storage status in `GET /diagnostics`
  (`room_history`).
- The walls conflict between Touch and V6 clears once V6's walls have been adopted.
- Circulation settings stay within five groups (pump entities on a subpage).
- Display and dashboard tests run without ripgrep.
