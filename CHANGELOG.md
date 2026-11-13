# Changelog — Lune Touch

## Unreleased (for v1.0.0)

### The wall display's heat-source block says what it shows
- The house row showed two coloured dots labelled «Asgard» and «Odin» and «32,0° → 28,5°» with no word on what the numbers were. It now has one status line in words — «Varmepumpe · opvarmer / varmt vand / legionella / starter snart / hviler», «… · tjek Odin» when a link needs attention (warn) or «Asgard svarer ikke» (fault) — and the heat pump's flow and return as two numbers under «Frem» and «Retur». On target (±0.3 °C) is muted green on the display too, and the web navbar keeps its full width when it sticks. LDS 2.4.0.

### V6 hears again whether the heat source runs
- Touch had stopped sending the heat-pump phase to the V6 boards on the belief that their dynamic balancing no longer used it, but V6 still does: while the heat source is idle every loop gets its design share, the load law runs only while it runs or prestarts, and without word from Touch V6 ran the load law all the time (1. sal 2026-11-13: `hp_phase: unknown` with the heat pump idle). Touch sends the phase again when «Valves follow the heat pump» is on (pushed on a change and every 5 min, 15 min TTL).

### Home: the house target lives in the house sheet; valves show their opening
- The house's target moved from Home's main section to a new house sheet (the ring, autosaved). The House card on Home shows the house temperature, the target and the deviation as the heat map's chip, and opens the sheet; Outside sits next to it. The manifold cards' bars are coloured by each room's deviation from target (height is still the valve's opening), and on target (±0.3 °C) is a muted green everywhere. Valves read «x % åben» (the pin's position over its learned travel) instead of «% flow», and the room sheet adds the lift in mm (`opening_mm` from V6). The pump's full-load head advice is gone with the system curve it was built on. Home's main section is lower, and the compact plan on «Næste varme» marks Odin's planned heat at the foot of a taller comfort band instead of in its own lane, with a taller price lane and a legend. The plan sheet's lane names take only the room they need, the lanes are taller, a dashed line marks now through every lane, and English says «expensive hour», not «dear hour». The tile and sheet «Næste varme» are now «Plan», with «Næste varme» as the label before the next block, the band lane is «Komfort», and Home no longer has the «Lige nu» header. The compact plan tells space heat (solid) from hot water (hatched), and the current hour shows what the heat pump does now, so hot water it makes outside Odin's plan is visible. LDS 2.4.0.

### Valve curves are no longer measured
- Touch measured each loop's valve curve from the circulation pump's computed flow and wrote it to V6, but the pump's flow could not resolve the pin's lift: every curve put full flow at about 1 mm. V6 has dropped curves (its opening is now the pin position in % of its learned travel, `opening_mm` in mm), so Touch no longer measures, stores, sends or uses them: the automatic and manual runs (`POST /valve-curves/measure`), `valve_probe` in `GET /heat-source`, the measured house-balance basis (`boards[].need`), the system curve behind the pump-head advice (`circulation.full_load`, `mixing.recommended_head_m`), the per-loop heat weight for UA learning (back to loop area × opening) and `pin_pct` / `full_lmin` in `GET /zones` are gone. `GET /zones` adds `opening_mm` from V6. The loop flush stays; its status moves from `valve_probe.flush` to a top-level `flush` in `GET /heat-source`, and a flush no longer requires a V6 that reports a curve.

### The plan tells a weather preload from other lifts
- Every lifted hour in the plan's band lane was violet, so a preload ahead of wind or cold looked like a lift for a lagging room or a slab charge. Hours lifted only for the weather are now blue (the weather's colour); an hour shared with a room or a charge stays violet, and the readout says why («før vind/kulde + rum halter»). LDS 2.3.33. `GET /plan` → `odin` adds `lift_src` (per hour: 1 = slab charge, 2 = room lag, 4 = weather preload).

### The heating plan hour by hour, and on Home as a wide tile
- The plan graph labelled every third hour and showed Touch's lift as a thin line, so it was hard to tell which hour Odin heats, why, and what Touch asked for. It now has a mark per hour, a price lane (the price per hour on the cheap → dear scale), Odin's comfort band per hour with the expected temperature (hours Touch lifts in violet, a dear hour Touch skips dashed) and a wind lane above the rooms. Hover, touch or the arrow keys show an hour's figures: price, planned heat, band, lift and wind. On Home, «Næste varme» is a wide tile with a compact plan (price, heat, band); the other tiles stand three in a row. LDS 2.3.32. `GET /plan` → `odin` adds `lift_floor_c`, `lift_skip` and `lift_reason`.

### A lift may cover dear hours; it skips one only when heating earlier is cheaper, heat loss included
- Every lift left the dearest third of its window unlifted whenever prices swung by 10 %, whether or not the house could ride through on heat stored earlier. Now an hour is skipped only when its price is above that of an earlier lifted hour of the window plus the heat lost while the house holds that heat (`(k − i) / τ`, τ = Odin's thermal mass / heat loss, about 167 h here). The window's first hour always stays lifted.

### Weather preload tells the heat source too
- V6 preloaded rooms ahead of wind or cold, but the heat source never heard of it: Odin's band moved only for a slab charge or a room that already lagged, and a heat source without Odin got nothing. On 2026-11-12 V6 preloaded Laura by +1.5 °C while Odin heard only of her 1.6 K lag. Now a preload lifts Odin's band from now through the hour after the forecast peak by the largest room offset (its floor by the preloading share of the rooms, as for a lag, and the dearest hours skipped), and on the other routes it counts like an open slab charge in the demand-led target, the curve shift and the heat request. `odin.reason` can include `preload`.
- Touch's preload decisions started over at every restart and were not in Home Assistant. Five entities keep the history: *Weather preload rooms*, *Weather preload offset*, *Weather preload* (rooms, offsets, hours to the peak), *Heat source lift* and *Heat source lift reason*.

### A lagging room opens Odin's band instead of forcing it, and a lift skips the dearest hours
- Laura 1.6 K behind (2026-11-12) lifted the whole Odin band by 1.5 °C for the next 6 h, right across the evening tariff peak, and warmed the rest of the house with it. A room-lag lift now raises the band's top by the lift but its floor only by `floor_share` (2 × the share of the house in lagging rooms, 0.2–1), over 8 h when that share is below 1, so Odin may heat more where it is cheap and V6 sends the heat to the room. Every lift leaves the dearest third of its window unlifted when prices swing by 10 % or more (`skip_dearest`). `/heat-source/control` → `odin.wanted` / `odin.applied` add `floor_share` and `skip_mask` (bit h = wall-clock hour h); the strategy has `lagging_share`.

### A valve curve's full point is where the loop has 95 % of its flow
- The fitted ramp bends where most of a loop's flow has arrived, but the insert keeps adding flow above the bend while the loop's own pipe takes over, and V6 opened a loop asked for full flow to 110 % of the bend. Stue at 6 m: fitted full at 23 %, so V6 opened it to 25 % and it passed 75 % of what it passes fully open; 95 % comes at 37 %. The full flow is now the mean of the points near the highest opening (less the fitted base), and the full point is where the measured points first reach 95 % of it (`full_from_points`, never below the fit's own). Curves from before 2026-11-09 23:00 UTC count as unmeasured, so every loop is measured again.
- A run started while the heat source rested aborted on its first step: the pump was polled every 120 s then, longer than the 90 s a run waits for a reading, and the fast poll began only after the next fetch. It begins when the run starts.
- Every run aborted about a minute in once nothing but the floor loops was on the circuit: the readiness check asked for more than 3 l/min, and with every loop closed but the one at 0–5 % the pump moves about 1.5 l/min. A run still starts only above 3 l/min; once running, a fresh pump reading is enough.
- A run interrupted in its refine pass (the heat source starting or stopping, a stale pump reading) fits the curve from the points so far instead of discarding them: the coarse pass is complete by then. Josephine lost a run at step 16 of 17 when the heat pump stopped.

### A loop can be flushed to clear trapped air
- The manifold's automatic air vents only take air that reaches the manifold; a pocket out in a long 12 mm loop needs about 0.3 m/s or more to move, which a throttled loop sharing the pump with eleven others does not get. `POST /valve-curves/flush` (`node`, `zone`, `minutes` 5–30, default 15) opens that loop fully and closes every other loop on every V6 through V6's valve test, renewing the hold every minute; `POST /valve-curves/flush/stop` ends it early. Never automatic, never during a valve-curve run (and no run starts during a flush); pump-head advice and the system curve pause while it holds the valves. `/heat-source` → `valve_probe.flush` (`active`, `node`, `zone`, `left_s`); the navbar badge says «Udlufter kreds» with the loop and minutes left.

### Pump head advice is for full load, on a learned system curve
- The advice scaled the live operating point (head × (recommended / live flow)²), so it answered for the valves as they were: with warm rooms nearly closed it asked for 7.3 m at 15.7 l/min. Touch now learns how the pump's flow (at 3 m) follows Σ loop flow × the loop's measured full flow (`system_curve.h`, time-weighted means per 2 l/min bin, fading over three days, stored every 6 h), takes the house at full load (every loop at its design share × house balance), and gives the head for the recommended flow there: about 5 m in the author's house. The advice says what the pump gives at full load, that the live flow is no yardstick, and while the curve has no data near full load that Touch is still learning. `/heat-source` → `circulation.full_load` (`l_min`, `ref_l_min`, `x_l_min`, `x_now_l_min`).

### The navbar says what Touch is doing
- Next to language and theme, a badge shows Touch's current activity: testing valves (with the loop and step on wide screens), heat source offline, hot water / legionella / heating, starting soon (Odin plans heat within 10 min) or idle. Hidden without heat-source data (LDS 2.3.31).

### No pump-head advice while valve curves are measured
- The recommended pump head scales the live head by (recommended flow / live flow)². While a valve-curve run holds the other loops shut, the live flow is one loop's (3.9 l/min), and the advice came out at 111 m. It is left out («—») while a run holds the valves.

### Energy prices reach Odin again
- Energi Data Service now answers its tariff list (DatahubPricelist) once a minute per client and refuses a second call with HTTP 429. Touch fetched the grid tariff and Energinet's system tariff 1.2 s apart, so the system tariff was refused on every try and no price was pushed after a restart (`system_http_429`). With both tariffs from DataHub they now come in one call.

### Room heat loss (UA) is learned
- A room's daily UA came from a day's totals kept only in RAM, so every restart (and Touch is flashed often) lost the day and left it short of the 20 h a day needs; no room had learned UA. The totals are stored hourly with their date and come back after a restart; a stored day from before midnight is learned when the date changes.
- The pump's heat is shared between rooms by each loop's heat, l/min (flow × the loop's measured full flow) × (supply − the loop's return, or the manifold's ΔT), when every flowing loop reports it; before, by loop area × flow, which spreads it evenly over loops of equal area whatever they carry.

### Room heat-up timing is learned again
- A room's heat-up and cool-down rates (which set its learned lead and size its preload) were measured between two polls, 15–60 s apart, and every such pair was rejected as shorter than the 30 min a rate needs. Only a poll gap of half an hour got through, which is exactly the probe-reconnect case the check is meant to stop. Most rooms had 0–2 samples, and the learned lead stayed 0. Rates are now measured over one-hour windows of polls with one calling state throughout; a window with a gap of more than 15 min starts over.
- A stored model outside the plausible range (Kontor: 36.7 °C/h cooling with 65535 samples, Køkken 86.7 °C/h) is cleared and learned again.
- A preload is judged once, when it ends, by where it left the room. Before, it was judged at every recompute, which happens on every forecast fetch and settings edit, while the room was still warming: each judgement counted as undershoot and could add an hour of lead. A clear overshoot (> 0.6 K) now also shortens the lead bias, which could only ever grow.

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
- Measuring closes every other loop, which shuts the floors out of a buffer tank while the heat source is off: re-measuring all twelve loops held them closed for almost ten hours in one day. An automatic run now starts only when no room is below its target, at most two a day, and a loop that fails twice in a row waits for a manual run (its valve may need relearning). Failed tries and the day's runs are kept across restarts; the probe's loop list shows `fails`.
- House balance from measurements: once every loop has a measured full flow, a board's need is its most demanding loop's heat demand (V6's UA) per litre of full flow, instead of the pipe-friction estimate; V6 uses the same shares inside the board. Each board has a weight (50–150 %, 100 = as computed) in the circulation sheet to move heat between floors beyond what the model sees. `POST /heat-source/settings` with `house_balance_weight_node` and `house_balance_weight_pct`; `house_balance.basis`, `boards[].need` and `boards[].weight_pct` in `GET /heat-source`.
- Each loop is measured alone: every other loop on every V6 closes during the run (V6 `valve-test` `others_pct`), and the full flow is converted to a pump head of 3 m. A loop passes far more with its neighbours closed (Hobbyrum 1.1 vs 1.9 l/min at the same head), so curves measured with the others holding did not compare between loops or days, and a changed pump head moved the shares. While the heat source runs (the one-day fallback) the others hold as before. Curves from before this count as unmeasured and are measured again.
- Buffer tank setting in the circulation sheet (none, in series, three-port, four-port), `buffer_tank` in `POST /circulation/settings` and `GET /heat-source` → `circulation`. It decides what the circulation pump's flow against the heat pump's means: with a four-port tank (hydraulic separator, the default) a lower secondary flow returns the heat pump's own water (short flow) and a much higher one dilutes the supply (mixing); with a three-port tank (supply straight from the heat pump, tank on the return with its top on the supply tee) a lower secondary flow only charges the tank (`mixing.state` "charging", no problem) and the recommended flow is 5 % below the heat pump's; in series or without a tank there is one flow and nothing is judged.
- Settings were lost at every restart once the default NVS partition (20 KB, shared with ESPHome's preferences and the PHY calibration) ran full: every save failed with `ESP_ERR_NVS_NOT_ENOUGH_SPACE` and Touch kept the change only in RAM. Touch's settings, weather and ledger namespaces now live on the 64 KB `touchreg` partition; the first boot moves them there (a copy marked complete, then the default copy is erased). A save that runs out of space logs each namespace's use.
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
