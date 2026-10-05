# Lune Touch web UI

Built on **Lune Design System 2** (`../design-system/`).
Navigation: house → manifold → zone (`strip--tiers`, shared views). Heat source: typed `http` / `asgard` (API: `generic_http` / `asgard`).

```bash
make touch-ui
# → dist/{en,da}/index.html(.gz), lune-ui.css(.gz), ui.js(.gz), preview-*.html
```

Open `web/touch-ui/dist/preview-en.html` for a local mock preview.

Firmware embeds the gzip artifacts via `packages/dashboard/dashboard.yaml` (`ui_dist`).
Routes: `/`, `/en/`, `/da/`, `/lune-ui.css`, `/ui.js`, plus `/api/lune-touch/v1/*`.
