# Lune Touch — installer / operator manual

Guide for the Touch web UI. The `?` help in the UI links here. Deep engineering notes stay
in the linked docs.

<!-- toc -->
**[The web UI](#the-web-ui)**

**[Home](#home)**

- [House temperature](#house-temperature)

**[Sheets](#sheets)**

- [Heat](#heat)
- [Next heating](#next-heating)
- [Weather and preload](#weather-and-preload)
- [Circulation](#circulation)
- [Controller](#controller)
- [Room](#room)

**[System](#system)**

- [Device](#device)
- [Controllers](#controllers)
- [Network](#network)
- [Firmware and backup](#firmware-and-backup)
- [Service](#service)

**[Device menu](#device-menu)**
<!-- /toc -->

---

## The web UI

The UI has three places, following the Lune design system:

- **Home** — the house now: temperature and target, heat, plan, weather, circulation and
  every room. The target saves by itself; nothing else to save.
- **Sheets** — one per thing: **Heat**, **Next heating**, **Weather**, **Circulation**, one per
  **controller** (V6) and one per **room**. A sheet slides in over Home, with the tabs
  **Overview**, **History** and **Settings** where they apply. Settings for that one thing live
  in its sheet, including its connection.
- **System** — what belongs to no sheet: identity, controllers (pairing), network, firmware
  and backup, service.

Each settings tab and System category has one save bar at the bottom: **Undo** and **Save**.
Switches save immediately (MQTT settings wait for Save); leaving with unsaved changes asks
first. Everything that belongs to a sheet is set in that sheet, including its connection
(under **Connection ›**); System holds only what belongs to no sheet.

---

## Home

- **House and controllers** — the **House** card (current, with the house temperature) and
  one card per V6 controller with a bar per zone showing the valve opening. A controller card
  opens its sheet; an offline controller says so.
- **The house now** — a greeting, one line about the situation (and whether the heat pump
  runs), and the thermostat ring. The ring **sets the target**: drag the handle along the arc,
  type the number in the middle, or use − / + in the ring's opening. The target moves in steps
  of 0.5 °C and saves by itself. The small mark outside the arc is the house temperature now;
  the house and outdoor temperatures are also written under the target.
- **Heat, plan and weather** — four tiles that open their sheets:
  - **Heat** — flow → return and the last 24 hours (from the heat source's own history).
  - **Next heating** — Odin's next block, its energy and today's price with a price chip.
  - **Weather** — outdoor now, wind, and the next 24 hours with preheating hatched; it says
    how many rooms have solar hold-off when any do.
  - **Circulation** — flow (m³/h), power and the split per controller. The tile turns
    yellow with **Mixing risk** when the circulation pump moves more water than the heat pump.
- **Heat map** — rooms per controller. Each room shows its temperature, a chip with the
  distance to target (blue below, neutral near, orange above), a five-step valve bar and the
  opening. Rooms are sized by floor area. A room opens its sheet; a controller heading opens
  the controller.
- **Alerts** — an unreachable V6 or heat source appears at the top with a link to the place to
  fix it. Rooms on an unreachable V6 keep their last known values, dimmed.

### House temperature

Weighted average of rooms marked "include in house temperature". Coverage shows
how many approved manifolds contribute. Authority is Touch when it holds the lease.
**How is it calculated?** in the Heat sheet shows each room's weight.

---

## Sheets

### Heat

**Overview** — flow, return and ΔT with the compressor state, and a chart for the last
**24 h** or **7 d** taken from the heat pump's own history (Asgard). Hatched bands mark where
the heat pump made **hot water** or ran **legionella**. Below: status, last push, the comfort
target's role, Odin (link, who drives the pump, this hour, comfort schedule) and what is sent
to Asgard.

**Settings** — the behaviour Touch controls: **Comfort target** (send the house target to
Asgard's climate entity), **Odin plan** (use Odin's heat windows for warm-up and preload) and
**Odin control** (let Touch raise Odin's comfort band, up to a max lift, when a room lags)
and **Odin's setpoint** (see below).

<a id="odin-sync"></a>

#### Odin's setpoint

**Follow the house target** (off by default) makes Odin aim at the same temperature as the
rooms. The house target is the rooms' targets averaged by heat loss, shown in steps of
0.5 °C. Touch writes it as the setpoint in Odin's comfort schedule with one block all day and
a band of **±** (default 0.5 °C), so Odin may only move heat within that band to save money.
If someone changes the schedule in Odin, Touch writes setpoint and band again. Lifts for wind
and a lagging room are added on top and removed again when they end.

Without it, Odin keeps its own schedule and only the comfort target sync (to Asgard) follows
the rooms.
**Connection to heat source and Odin ›** opens the connection in the same tab; one save bar
saves both.

<a id="heat-source"></a>

#### Connection

Type (**HTTP** or **Asgard**), connection, mapping and **Test read / Test send** with a result
that stays visible. Publishes the house temperature to the heat bridge (Asgard / Ecodan path)
and optionally syncs the comfort target. A wrong host or a disabled push blocks delivery.
**Advanced** holds HTTP control and optional MQTT. See
[whole-house flow temperature](lune_whole_house_flow_temperature.md) and
[Asgard authority](lune_asgard_authority_state_machine.md).


### Next heating

Odin's planned heat for the next 24 hours together with Touch's preheating and charging per
room, today's price per hour, and under **History** plan vs. actual for the last 12 hours.
*Actual* is the heat the heat pump delivered each hour (from its own heat meter via Asgard);
the current hour is marked as running.
See [Odin plan ingestion](odin_plan_ingestion.md). **Settings** holds the power price.

<a id="electricity-price-to-odin"></a>

#### Power price

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


### Weather and preload

**Overview** — the next 72 hours as five small charts: sky, temperature (preheating hatched),
sun, wind and wind direction. **History** — outdoor temperature for the last 24 hours.
**Settings** — **max preload boost**, **solar hold-off** and the location.

<a id="weather"></a>

#### Weather location

In the **Weather** sheet › **Settings**, together with the preload boost. Coordinates for the
forecast; **Estimate from network** fills latitude and longitude from the device's network
location; check them and save.


Forecast wind and outdoor temperature raise selected rooms ahead of cold fronts.
Walls, wind and solar factors on each room decide which spaces preload.
Preload starts when the load from wind and cold on a room passes 1.0:

    load = wind factor × facade alignment × (wind m/s ÷ 10) × (ΔT indoor–outdoor ÷ 10)
           − solar factor × irradiance ÷ 800

Each load unit adds 0.5 °C, at most the **max preload boost** (1.5 °C). A fully exposed room
(factor 1.0, wind straight on the facade) at 0 °C outside therefore starts preloading at about
5 m/s; a room with factor 0.5 needs about 10 m/s. Commands live for 75 minutes and are not
repeated within 30 minutes.

<a id="solar-holdoff"></a>

#### Solar hold-off

Rooms with large windows are heated by the sun. With **Solar hold-off** on (switch, saves
immediately), Touch lowers a room's target by up to 1.5 °C during the 3 hours before
forecast sun, so the floor is not full of heat when the sun arrives. It starts at 250 W/m²
and is full at 600 W/m², scaled by the room's solar factor; the sun now counts fully, the sun
3 hours ahead a third. It only acts on rooms with a solar factor above 0.35 and a facade
towards the sun, never on a room already 0.3 °C below its target, and wind and cold preload
come first.

See [forecast preload](forecast_preload.md),
[house balancing and weather](house_balancing_and_weather.md) and
[falsify heat-gain preload](falsify_heat_gain_preload.md).

### Circulation

Flow, head and power from the circulation pump, and the approximate split per controller and
zone (worked out from valve openings). A warning appears when the pump moves more water than
the heat pump while heating (mixing in the buffer tank).

**Recommended flow** is 90 % of the heat pump's measured flow while heating, rounded down to
0.5 l/min, so no return water is drawn into the supply. The warning also says what to set:
with a pump that reports its head, the constant pressure (m) that gives that flow through the
valves as they are now (head ∝ flow²), otherwise the flow to turn the pump down to. Touch only
advises — the pump is set on the pump itself. **Settings** holds the house balance
and the pump's connection.

**House balance** (*Balance between manifolds*, off by default): without balancing valves the
manifold with short, wide loops takes flow from the others. Touch estimates each loop's
pressure need from area, pipe and spacing and throttles the easy manifold; the rows show
each board's scale. V6 keeps its own split within a manifold and returns to it by itself
15 min after Touch stops sending. See
[house balancing](house_balancing_and_weather.md#4-balancing-between-manifolds).

<a id="pump"></a>

#### Pump connection

Host, port and the ESPHome entities for flow, head and power (`GET`/`POST /circulation`).
Entities are the sensor names on the pump node. Newer ESPHome addresses them by name
(`Pump Flow`), older by object id (`pump_flow`); Touch tries the other form when one gives 404.


### Controller

One sheet per V6 (M1, M2 …): status, flow and return, its rooms and a link to the V6's own
page. Zones are configured on the V6 itself, not in Touch.

### Room

**Overview** — temperature, target and status, the loops from V6 (valve, return) and the
expected next 24 hours. **History** — recent readings.

**Settings** — only what Touch owns:

- **House temperature** — include the room in the house temperature, and its weight.

<a id="room-factors"></a>

- **Weather** — wind and solar factors (0–1) that scale preload and solar hold-off.
  Touch also **learns** both from the room's own data: each hour it compares how fast the room
  warms or cools with outdoor temperature, wind on its facades, sun and the heat from its
  loops. The sheet shows the learned values and how many sunny and windy hours they rest on.
  Touch uses them only after enough hours (20 sunny, 30 windy) and only while the field is at
  its default (sun 0.30, wind 0.50); a value you set yourself always comes first.
- **From V6** — area and exterior walls, read from the V6 (read-only). **Edit on V6 ›** opens
  the zone's settings on that V6. When the V6 is offline, the values are dimmed.

---

## System

<a id="identity"></a>

### Device

Device name and display idle minutes.

### Controllers

Pair and approve Lune V6 boards so Touch can import rooms and steer heat calls.
Unapproved boards stay local. **Find controllers** scans the LAN; **Add manually** takes a
hostname. Approve, rename or remove from the list.

<a id="wifi"></a>

### Network

System › Network shows the WiFi Touch uses and its status. To change it, enter the
network name (SSID) and password and choose **Change network**. Touch tries the new
network; if it does not connect within 30 seconds, Touch goes back to the current one.
The setting survives firmware updates.

If Touch cannot reach any network (for example after a router password change), it opens
the setup network **"Lune Touch Setup"** after 5 minutes. Join it and open
`http://192.168.4.1` to enter the new password.

### Firmware and backup

<a id="firmware"></a>

**Firmware**: check GitHub for a newer release, install it from the browser, or upload a
local `.bin`. The device reboots when flashing finishes.

<a id="backup"></a>

**Backup**: export or import coordinator settings as JSON (import asks first). Secrets are
never included. Import overwrites settings on this device (heat source, pump, weather,
identity).

### Service

The house (coverage, authority, Odin link, distribution, nodes, poll, OTA), diagnostics, the
command log and **Reset the registry** (asks first). Restart and firmware actions belong on
System — not in the device menu. API detail: [api_v1](api_v1.md).

---

## Device menu

**About device** shows name, location, IP, MAC, firmware, ESPHome and uptime, plus
**Copy diagnostics**. Other Lune devices are links only. No restart, OTA or reset in
the menu — those live under System.
