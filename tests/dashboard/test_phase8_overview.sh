#!/bin/sh
# Lune Design System 2 — Touch web UI contracts.
# Run from the lune-coordinator repo root: sh tests/dashboard/test_phase8_overview.sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$root"

build=web/touch-ui/build_ui.py
binder=web/touch-ui/binder.js
en_i18n=web/touch-ui/i18n/en.json
da_i18n=web/touch-ui/i18n/da.json
css_src=web/design-system/css/lune-ui.src.css
touch_cfg=web/design-system/config/touch.json
agents=AGENTS.md
dash_cpp=components/lune_touch_dashboard/lune_touch_dashboard.cpp
dash_yaml=packages/dashboard/dashboard.yaml
dash_init=components/lune_touch_dashboard/__init__.py

test -f "$build"
test -f "$binder"
test -f "$en_i18n"
test -f "$da_i18n"
test -f "$css_src"
test -f "$touch_cfg"
test -f "$agents"

# Build produces Home + System + sheets (LDS 2.3)
python web/design-system/tools/lds_build.py web/design-system/config/touch.json >/dev/null
python web/touch-ui/build_ui.py >/dev/null
en=web/touch-ui/dist/en/index.html
da=web/touch-ui/dist/da/index.html
test -f "$en"
test -f "$da"
test -f web/touch-ui/dist/lune-ui.css
test -f web/touch-ui/dist/ui.js
test -f web/touch-ui/dist/lune-ui.css.gz
test -f web/touch-ui/dist/ui.js.gz

# Shell / navigation contracts (LDS 2.3: Hjem / ark / System, DESIGN.md 15)
rg -F 'id="v-home-house"' "$en" >/dev/null
rg -F 'id="v-sys"' "$en" >/dev/null
rg -F 'id="m-home"' "$en" >/dev/null
rg -F 'id="m-sys"' "$en" >/dev/null
rg -F 'id="s-house"' "$en" >/dev/null
rg -F 'class="home-hero"' "$en" >/dev/null
rg -F 'class="thermo"' "$en" >/dev/null
rg -F 'class="home-tile"' "$en" >/dev/null
rg -F 'data-bind-heatmap' "$en" >/dev/null
rg -F 'class="sys-nav"' "$en" >/dev/null
rg -F 'id="sheet-heat"' "$en" >/dev/null
rg -F 'id="tpl-mani"' "$en" >/dev/null
rg -F 'id="tpl-room"' "$en" >/dev/null
rg -F 'name="mqtt_enabled" data-save-now="false"' "$en" >/dev/null
rg -F 'class="savebar"' "$en" >/dev/null
rg -F 'hs-fields' "$en" >/dev/null
rg -F 'name="hs_type"' "$en" >/dev/null
! rg -F 'id="v-dash-' "$en" >/dev/null
! rg -F 'id="v-conf-' "$en" >/dev/null
! rg -F 'id="m-dash"' "$en" >/dev/null
! rg -F 'strip--tiers' "$en" >/dev/null
! rg -F 'sidebar' "$en" >/dev/null
! rg -F 'modal' "$en" "$binder" >/dev/null

# Forms (save keys; partial save = patch, DESIGN.md 6.1)
rg -F 'data-save="house-target"' "$en" >/dev/null
rg -F 'data-save="heat_source.connection" data-patch' "$en" >/dev/null
rg -F 'data-save="heat_source.behavior" data-patch' "$en" >/dev/null
rg -F 'data-save="weather.location" data-patch' "$en" >/dev/null
rg -F 'data-save="weather.boost" data-patch' "$en" >/dev/null
rg -F 'data-save="rooms" data-patch' "$en" >/dev/null
rg -F 'data-save="circulation"' "$en" >/dev/null
rg -F 'data-save="prices"' "$en" >/dev/null
rg -F 'data-save="settings"' "$en" >/dev/null
rg -F 'data-save="add-node"' "$en" >/dev/null
rg -F 'class="confirm-pop"' "$en" >/dev/null
rg -F 'class="test-result"' "$en" >/dev/null
rg -F 'data-bind-weight-rows' "$en" >/dev/null
rg -F 'data-bind-fc="temp"' "$en" >/dev/null

# All old fields/actions present, group limits (DESIGN.md 15.5), .w-* widths
python web/touch-ui/check_fields.py >/dev/null

# i18n completeness (key presence in both catalogs)
rg -F '"scope.house"' "$en_i18n" "$da_i18n" >/dev/null
rg -F '"nav.home"' "$en_i18n" "$da_i18n" >/dev/null
rg -F '"ctrl.title"' "$en_i18n" "$da_i18n" >/dev/null
rg -F '"rconf.save"' "$en_i18n" "$da_i18n" >/dev/null
rg -F '"hs.typeAsgard"' "$en_i18n" "$da_i18n" >/dev/null
rg -F '"room.fromV6"' "$en_i18n" "$da_i18n" >/dev/null

# Binder talks to existing API
rg -F "/api/lune-touch/v1" "$binder" >/dev/null
rg -F "lune:save" "$binder" >/dev/null
rg -F "/heat-source/settings" "$binder" >/dev/null
rg -F "/weather/settings" "$binder" >/dev/null
rg -F "detail.changed" "$binder" >/dev/null
rg -F "/nodes/scan" "$binder" >/dev/null
rg -F 'data-step' "$binder" >/dev/null

# Firmware serving cutover
rg -F 'ui_dist' "$dash_yaml" >/dev/null
rg -F 'LUNE_TOUCH_HAS_UI' "$dash_cpp" "$dash_init" >/dev/null
rg -F '/lune-ui.css' "$dash_cpp" >/dev/null
rg -F '/ui.js' "$dash_cpp" >/dev/null
rg -F 'LUNE_TOUCH_UI_EN' "$dash_cpp" >/dev/null
! rg -F 'dashboard.js' "$dash_yaml" >/dev/null
! rg -F 'DASHBOARD_HTML' "$dash_cpp" >/dev/null

# Old SPA must be gone
! test -d web/dashboard-src
! test -f web/dashboard.js

# No external fonts / banned patterns in new UI
! rg -F 'fonts.googleapis.com' web/touch-ui web/design-system/css >/dev/null
! rg -n '#[0-9a-fA-F]{3,8}' web/touch-ui/binder.js >/dev/null

# Gzip budget note: CSS + en + da pages (JS separate). Fail only if absurdly large.
css_gz=$(wc -c < web/touch-ui/dist/lune-ui.css.gz | tr -d ' ')
en_gz=$(wc -c < web/touch-ui/dist/en/index.html.gz | tr -d ' ')
da_gz=$(wc -c < web/touch-ui/dist/da/index.html.gz | tr -d ' ')
pages_total=$((css_gz + en_gz + da_gz))
# Soft ceiling: 80 KiB for CSS+2 pages (sheets for 4 controllers + 24 rooms in the page)
if [ "$pages_total" -gt 81920 ]; then
  echo "gzip CSS+pages too large: $pages_total bytes" >&2
  exit 1
fi
echo "ok touch-ui contracts (home/sheets/system, gzip css+pages=${pages_total} B, css=${css_gz} en=${en_gz} da=${da_gz})"
