# Lune Touch web UI

Built on **Lune Design System 2.3** (`../design-system/`): Home / sheets / System (DESIGN.md 15).
Home = hero + thermostat ring, four tiles, heat map per controller; sheets for heat, next heating, weather, circulation, controllers and rooms (one sheet each, created by the binder from `<template id="tpl-mani">` / `<template id="tpl-room">`); System = device, controllers, heat source, power price, pump, weather, network, firmware and backup, service. Heat source: typed `http` / `asgard` (API: `generic_http` / `asgard`).
`ui.js` = LDS `lune-forms.js` (dirty/save, autosave, sheets, deep links) + `binder.js`. Without JavaScript the room and controller sheets do not exist: rooms and controllers are only known at runtime (from `/zones` and `/nodes`). Deep links such as `#r5/indstillinger` work once the first data has arrived. Switches in settings save at once, except those marked `data-save-now="false"` (MQTT), which wait for the save bar.
`check_fields.py` checks that every old field/action (`conf_fields.txt`) exists and the group limits hold.

```bash
make touch-ui
# → dist/{en,da}/index.html(.gz), lune-ui.css(.gz), ui.js(.gz), preview-*.html
```

Open `web/touch-ui/dist/preview-en.html` for a local mock preview.

Firmware embeds the gzip artifacts via `packages/dashboard/dashboard.yaml` (`ui_dist`).
Routes: `/`, `/en/`, `/da/`, `/lune-ui.css`, `/ui.js`, plus `/api/lune-touch/v1/*`.
