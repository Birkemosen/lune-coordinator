# Lune Touch — installer / operator manual

Short guide for the web UI. Deep engineering notes stay in linked docs.

<!-- toc -->
**[House configuration](#house-configuration)**

- [Controllers](#controllers)
- [Heat source](#heat-source)
- [Electricity price to Odin](#electricity-price-to-odin)
- [Pump](#pump)
- [Weather location](#weather-location)
- [Identity](#identity)
- [WiFi](#wifi)
- [Firmware](#firmware)
- [Backup](#backup)
- [Service](#service)

**[Room configuration](#room-configuration)**

- [Room](#room)
- [Exterior walls and factors](#exterior-walls-and-factors)

**[Dashboard concepts](#dashboard-concepts)**

- [House temperature](#house-temperature)
- [Preload](#preload)
- [Plan vs reality](#plan-vs-reality)
- [Alerts](#alerts)

**[Device menu](#device-menu)**
<!-- /toc -->

## House configuration

### Controllers
Pair and approve Lune V6 boards so Touch can import rooms and steer heat calls.
Unapproved boards stay local. Scan the LAN or add a hostname, then approve.

### Heat source
Publish house temperature to the heat bridge (Asgard / Ecodan path) and optionally
sync the comfort target. Wrong host or disabled push blocks delivery.
See [whole-house flow temperature](lune_whole_house_flow_temperature.md) and
[Asgard authority](lune_asgard_authority_state_machine.md).

### Electricity price to Odin
Odin 2.0 plans the heat pump on hourly electricity prices. With **Manage Odin's
electricity price** on, pick one of two models:

**Odin fetches itself.** Touch only writes Odin's own price settings
(`POST /dashboard/set`): `price_mode` (spot or fixed), `price_source`
(Energy-Charts or ENTSO-E; Odin switches to the other if one fails), `ec_bzn` (the
bidding zone), `fixed_price` (€/kWh) and, when you enter one, `entsoe_token`. Odin then
adds its own energy tax and VAT per zone. The panel shows Odin's current values (read
from `/dashboard/state`; its token only as set / not set). Odin's Danish row is
outdated (0.10 €/kWh instead of the 2026 elafgift of 0.008 DKK/kWh) and no zone has a
grid tariff — for DK1/DK2 use the second model.

**Touch calculates.** Touch fetches the prices, computes the consumer price and pushes it
(`POST /api/data/prices`, Odin's `price_source` is set to `api`):

    all-in = (spot + grid tariff + system tariff + energy tax + markup) × (1 + VAT)
    pushed €/kWh = all-in / exchange rate, 4 decimals

- **Zone** — Odin's zone list (Benelux, DE/AT/CH, France & Iberia, British Isles,
  Scandinavia & Finland, Eastern Europe, Italy).
- **Spot** — *Energi Data Service* (DK1/DK2 only; DKK and EUR, exchange rate from the
  same data), *Energy-Charts* (any zone, free), *ENTSO-E* (any zone, needs the token)
  or a *fixed* €/kWh. Energy-Charts and ENTSO-E fall back to each other (ENTSO-E only
  when a token is stored). 15/30/60-minute prices are averaged into local clock hours
  (DST: the missing spring hour copies the hour before; the repeated autumn hour
  averages both).
- **Currency** — all amounts below are per kWh excl. VAT in one currency: EUR or a local
  one (DKK, SEK, NOK, CHF, PLN, CZK, HUF, RON, BGN, GBP, RSD) with an exchange rate per
  euro (approximate defaults, editable).
- **Grid tariff** — *DataHub* (DK only: grid company GLN + tariff code, default Vores
  Elnet `5790000610976` / `TNT1009`), *own schedule* (periods `{h, v}` like Odin's 24 h
  profile; the last runs past midnight) or *none*.
- **System and transmission tariff** — *DataHub* (DK only: Energinet 40000 + 41000) or a
  fixed value (0 if not billed separately).
- **Energy tax**, **supplier markup**, **VAT**. **Apply zone defaults** fills currency,
  rate, energy tax, VAT and sources from Odin's table (Denmark: elafgift 0.008 DKK, 25 %,
  DataHub tariffs); nothing changes until you save.

The ENTSO-E token is stored write-only: no page or API returns it.

Touch pushes 24 hours from 00:00 today, or 48 once tomorrow's spot is published.
Schedule: once after boot, daily from 14:15 every 30 min until tomorrow's prices are in
(gives up at 20:00), and again just after 00:05. A failed push retries after 30 min;
**Send prices now** pushes at once. If a DataHub fetch fails, the last fetched tariff is
used. Turning the feature off from "Touch calculates" sets Odin back to `energy_charts`.
API: `GET /prices`, `GET /prices/zone-defaults/{zone}`, `POST /prices/settings`,
`POST /prices/push` ([api_v1.md](api_v1.md)).

### Pump
Circulation pump host, port and ESPHome entities for flow, head and power on the
house dashboard (`GET`/`POST /circulation`).

<a id="weather"></a>

### Weather location

Coordinates and max preload boost for forecast-driven warm-up. Walls and wind/solar
on each room decide which spaces preload.
See [forecast preload](forecast_preload.md).

### Identity
Device name and display idle minutes.

### WiFi

System › Network shows the network Touch uses and its status. To change it, enter the
network name (SSID) and password and choose **Change network**. Touch tries the new
network; if it does not connect within 30 seconds, Touch goes back to the current one.
The setting survives firmware updates.

If Touch cannot reach any network (for example after a router password change), it opens
the setup network **"Lune Touch Setup"** after 5 minutes. Join it and open
`http://192.168.4.1` to enter the new password.

### Firmware
Check GitHub for a newer release, install it from the browser, or upload a local
`.bin`. The device reboots when flashing finishes. OTA actions live under
Configuration › Service — not in the device menu.

### Backup
Export or import coordinator settings as JSON. Secrets are never included.
Import overwrites settings on this device (heat source, pump, weather, identity).

### Service
Commissioning checklist, command log, live diagnostics (heap, poll, OTA) and
destructive registry reset. Restart and firmware actions belong here — not in the
device menu. API detail: [api_v1](api_v1.md).

## Room configuration

### Room
Name, floor area and merge list for V6 circuits that share one comfort sensor.
Merged circuits open together and share the room target.

<a id="room-factors"></a>

### Exterior walls and factors

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
