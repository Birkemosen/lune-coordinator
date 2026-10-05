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
