# Changelog — Lune Touch

## Unreleased (for v1.0.0)

### Valve curves measured from the pump
- V6's dynamic balancing turns each loop's flow into an opening through a valve curve
  (where flow starts, where it is full). With a circulation pump connected, Touch
  measures it: every V6 holds its valves and one loop steps through its stroke while
  Touch reads the pump's flow; the fitted curve is written to the V6. A run holds
  every valve (balancing pauses), so Touch measures while the heat source is not
  running, and during a run only if it has not stopped for a day (a start, stop or
  hot-water switch during a run aborts it). One loop at a time, five minutes apart;
  each loop again after 30 days, at most one remeasurement a day. Without a pump
  reading V6 keeps its default curve.
- The measurement also sends each loop's full flow (l/min) to V6, which keeps it with the curve. Loops measured before V6 stored it are measured again (as first measurements). The circulation sheet's split per controller then uses each loop's measured flow instead of counting every loop as equal.
- The measurement is finer: after the coarse steps (0, 5 … 30, 40, 60, 100 %) Touch goes back below the rise and climbs through it in 2.5 % steps (at most 8; a wider rise gets wider steps), 90 s per point instead of 60 s, so start and full points no longer fall in the 30–40–60 % gaps. About 50 min per loop. Curves from before this count as unmeasured and are measured again.
- House balance from measurements: once every loop has a measured full flow, a board's need is its most demanding loop's heat demand (V6's UA) per litre of full flow, instead of the pipe-friction estimate; V6 uses the same shares inside the board. Each board has a weight (50–150 %, 100 = as computed) in the circulation sheet to move heat between floors beyond what the model sees. `POST /heat-source/settings` with `house_balance_weight_node` and `house_balance_weight_pct`; `house_balance.basis`, `boards[].need` and `boards[].weight_pct` in `GET /heat-source`.
- The old registry shapes that Touch reads once at boot to migrate now share one scratch buffer in PSRAM instead of ~95 KB of internal RAM as static buffers. Internal RAM was so tight that the HTTP server could fail to start at boot (port 80 closed, the dashboard unreachable).
- «Følg varmepumpen» and «Loops now» are gone: V6's heat-pump law (dynamic balancing) does not use the heat pump's phase, so Touch no longer sends it.
- `POST /api/lune-touch/v1/valve-curves/measure` (`node`, `zone`) measures that loop
  next. Status and the curves in `GET /heat-source`
  (`valve_probe`).

### Lune Mini on a standard ESP32-S3
- Lune Mini, the coordinator without a display, has its own board profiles: ESP32-S3
  N16R8 (16 MB flash, 8 MB octal PSRAM, `lune-mini.yaml`) and N8R8 (8 MB flash, own
  partition table with 3 MB OTA slots, `lune-mini-n8r8.yaml`). The 7-inch panel
  options are gone from Mini; Touch is unchanged.
- The OTA slot in `GET /diagnostics` follows the board.

### Public firmware images
- `make release-firmware` builds public images for Touch, Mini N16R8 and Mini N8R8
  (`<name>-<version>.factory.bin` and `.ota.bin`) from `configurations/*-release.yaml`.
- Secrets moved to `packages/network/build-secrets.yaml` (WiFi seed, API encryption key,
  OTA password), which only the development entrypoints include. A public image carries
  none of the build host's secrets; Home Assistant sets the API key when it adds the
  device.

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
