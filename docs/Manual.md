# Lune Touch — installer / operator manual

Short guide for the web UI. Deep engineering notes stay in linked docs.

## House configuration

### Controllers {#controllers}

Pair and approve Lune V6 boards so Touch can import rooms and steer heat calls.
Unapproved boards stay local. Scan the LAN or add a hostname, then approve.

### Heat source {#heat-source}

Publish house temperature to the heat bridge (Asgard / Ecodan path) and optionally
sync the comfort target. Wrong host or disabled push blocks delivery.
See [whole-house flow temperature](lune_whole_house_flow_temperature.md) and
[Asgard authority](lune_asgard_authority_state_machine.md).

### Electricity price to Odin {#electricity-price-to-odin}

Odin 2.0 plans the heat pump on hourly electricity prices. Its built-in Danish price,
(spot + 0.10 €) × 1.25, predates 2026: elafgift is now 0.008 DKK/kWh and the
time-of-use grid tariff (e.g. Vores Elnet Nettarif C 0.077 / 0.231 / 0.692 / 0.231
DKK/kWh at 00–06 / 06–17 / 17–21 / 21–24 excl. VAT) sets the shape of the day. With
**Send prices to Odin** on, Touch computes the real consumer price and pushes it:

    all-in DKK/kWh = (spot + grid tariff + Energinet + elafgift + markup) × (1 + VAT)
    pushed €/kWh   = all-in / (DKK per EUR from the same spot data), 4 decimals

- **Spot** — Energi Data Service `DayAheadPrices`, DK1 or DK2. The 15-minute prices
  are averaged into local clock hours (DST: the missing spring hour copies the hour
  before; the repeated autumn hour averages all eight quarters).
- **Grid tariff** — *DataHub* (`DatahubPricelist` by the grid company's GLN and tariff
  code; default Vores Elnet `5790000610976` / `TNT1009`), *own schedule* (periods
  `{h, v}` like Odin's 24 h profile: each applies from its hour to the next, the last
  runs past midnight; DKK/kWh excl. VAT) or *none*.
- **Energinet** — transmission (40000) + system tariff (41000) from DataHub, or a
  fixed value (default 0.115 DKK/kWh).
- **Elafgift** (default 0.008), **supplier markup** (default 0) and **VAT** (default 25 %).

Touch pushes 24 hours from 00:00 today, or 48 once tomorrow's spot is published
(`POST /api/data/prices` on the Odin host set under [Heat source](#heat-source)).
Schedule: once after boot (when the clock is set), daily from 14:15 every 30 min until
tomorrow's prices are in (gives up at 20:00), and again just after 00:05, because
Odin's array starts at today 00:00. A failed push retries after 30 min; **Send prices
now** pushes at once. If a DataHub fetch fails, the last fetched tariff is used; with
none, nothing is pushed and Status shows the problem.

Turning it on sets Odin's price source to *API* (`price_source=api`); turning it off
sets it back to Odin's own *Energy-Charts* price.
API: `GET /prices`, `POST /prices/settings`, `POST /prices/push` ([api_v1.md](api_v1.md)).

### Pump {#pump}

Circulation pump host, port and ESPHome entities for flow, head and power on the
house dashboard (`GET`/`POST /circulation`).

### Weather location {#weather}

Coordinates and max preload boost for forecast-driven warm-up. Walls and wind/solar
on each room decide which spaces preload.
See [forecast preload](forecast_preload.md).

### Identity {#identity}

Device name and display idle minutes.

### Firmware {#firmware}

Check GitHub for a newer release, install it from the browser, or upload a local
`.bin`. The device reboots when flashing finishes. OTA actions live under
Configuration › Service — not in the device menu.

### Backup {#backup}

Export or import coordinator settings as JSON. Secrets are never included.
Import overwrites settings on this device (heat source, pump, weather, identity).

### Service {#service}

Commissioning checklist, command log, live diagnostics (heap, poll, OTA) and
destructive registry reset. Restart and firmware actions belong here — not in the
device menu. API detail: [api_v1](api_v1.md).

## Room configuration

### Room {#room}

Name, floor area and merge list for V6 circuits that share one comfort sensor.
Merged circuits open together and share the room target.

### Exterior walls and factors {#room-factors}

Mark exterior walls (N/E/S/W). Wind and solar factors (0–1) scale preload.
Include in house temperature when the room should weight the house average.

## Dashboard concepts

### House temperature

Weighted average of rooms marked “include in house temperature”. Coverage shows
how many approved manifolds contribute. Authority is Touch when it holds the lease.

### Preload

Forecast wind and outdoor temperature raise selected rooms ahead of cold fronts.
Cap with max preload boost under Weather. See [forecast preload](forecast_preload.md)
and [falsify heat-gain preload](falsify_heat_gain_preload.md).

### Plan vs reality

Odin planned heat demand compared with what the house actually drew.
See [Odin plan ingestion](odin_plan_ingestion.md).

### Alerts

An unreachable V6 board appears as a house alert with a link to Controllers.
Rooms keep last known values until the board answers again.

## Device menu

**About device** shows name, location, IP, MAC, firmware, ESPHome and uptime, plus
Copy diagnostics. Other Lune devices are links only. No restart, OTA or reset in
the menu — those live under Configuration › Service.
