#!/bin/sh
set -eu

display=packages/display/lvgl_stability.yaml
manifold=packages/display/lvgl/manifold_card.yaml
zone_cell=packages/display/lvgl/manifold_zone_cell.yaml
zone_chip=packages/display/lvgl/zone_detail_chip.yaml
controller_chip=packages/display/lvgl/zone_detail_controller_chip.yaml
tokens=packages/display/lvgl/tokens.generated.yaml
preview=packages/display/lune_touch_preview.cpp
entrypoint=lune-touch.yaml
coordinator=components/lune_touch_coordinator/lune_touch_coordinator.cpp
touch_ui=web/touch-ui/build_ui.py
dashboard_cpp=components/lune_touch_dashboard/lune_touch_dashboard.cpp

for required in \
  'id: page_home' \
  'id: chrome_settings_button' \
  'id: dashboard_system_status_label' \
  'id: dashboard_problem_card' \
  'id: dashboard_activity_label' \
  'id: dashboard_startup_overlay' \
  'id: dashboard_startup_spinner' \
  'id: touch_display_refresh_token' \
  'id: page_home_title' \
  'id: overview_day_theme' \
  'id: page_settings' \
  'id: page_settings_weather' \
  'id: page_settings_heat' \
  'id: page_settings_timeout' \
  'id: chrome_settings_button' \
  'id: lune_v6_mark' \
  'id: lune_halo_mark' \
  'transparency: alpha_channel' \
  'pclk_frequency: 30MHz' \
  'buffer_size: 12%' \
  'full_refresh: false' \
  'update_interval: 50ms' \
  'update_interval: 5s' \
  'lds_font_d28' \
  'lds_font_d36'; do
  rg -F "$required" "$display" >/dev/null
done
rg -F 'src: lune_halo_mark' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'id: page_home_statusbar' packages/display/lvgl/home_status_bar.yaml >/dev/null
rg -F 'id: page_home_clock' packages/display/lvgl/home_status_bar.yaml >/dev/null
rg -F 'id: page_home_fault_pill' packages/display/lvgl/home_status_bar.yaml >/dev/null
rg -F 'vars: {id: page_home_wifi' packages/display/lvgl/home_status_bar.yaml >/dev/null
rg -F 'id: page_home_house' packages/display/lvgl/home_house_row.yaml >/dev/null
rg -F 'id: page_home_mrow_${node}' packages/display/lvgl/home_manifold_row.yaml >/dev/null
rg -F 'file: lvgl/home_manifold_row.yaml' "$display" >/dev/null
rg -F 'id: lds_font_d28' packages/display/lvgl/lune_theme.yaml >/dev/null
rg -F 'id: lds_font_d36' packages/display/lvgl/lune_theme.yaml >/dev/null
rg -F 'hidden: true' "$display" >/dev/null

! rg -F 'id: page_zones' "$display"
! rg -F 'id: page_system' "$display"
! rg -F 'id: page_service' "$display"
! rg -F 'id: nav_zones' "$display"
! rg -F 'id: nav_system' "$display"
! rg -F 'id: nav_service' "$display"
! rg -F 'id: chrome_menu_panel' "$display"
! rg -F 'id: nav_home' "$display"
! rg -F 'id: nav_settings' "$display"
rg -F 'lds_raised' packages/display/lvgl/tokens.generated.yaml >/dev/null
! rg -F 'id: chrome_brand_lockup' "$display"
! rg -F 'text: "TOUCH"' "$display"
rg -F 'text: "\uF013"' "$display" >/dev/null
rg -F 'id: page_settings_pane' "$display" >/dev/null
! rg -F 'id: page_settings_card' "$display"
rg -F 'bg_color: ${lds_raised}' "$zone_chip" >/dev/null
rg -F 'bg_color: ${lds_raised}' "$controller_chip" >/dev/null
rg -F 'display_set_weather_compensation_enabled' "$display" >/dev/null
rg -F 'display_set_heat_source_enabled' "$display" >/dev/null
rg -F 'display_cycle_idle_timeout' "$display" >/dev/null
rg -F 'radius: CIRCLE' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'lds_accent' "$display" >/dev/null

! rg -F 'id: zone_control_modal' "$display"
! rg -F 'zone_control_modal' "$zone_cell"

rg -F 'display_tokens: !include packages/display/lvgl/tokens.generated.yaml' "$entrypoint" >/dev/null
rg -F 'display_fonts: !include packages/display/lvgl/fonts.yaml' "$entrypoint" >/dev/null
rg -F 'display_theme: !include packages/display/lvgl/lune_theme.yaml' "$entrypoint" >/dev/null
rg -F 'id: lds_font_d28' packages/display/lvgl/lune_theme.yaml >/dev/null
rg -F 'id: lds_font_d168' packages/display/lvgl/lune_theme.yaml >/dev/null
rg -F 'glyphs: "æøåÆØÅäöüßÄÖÜéèêëÉ"' packages/display/lvgl/fonts.yaml >/dev/null
rg -F 'id: lune_latin_18' packages/display/lvgl/fonts.yaml >/dev/null
home_status=packages/display/lvgl/home_status_bar.yaml
home_house=packages/display/lvgl/home_house_row.yaml
home_mrow=packages/display/lvgl/home_manifold_row.yaml
home_ztile=packages/display/lvgl/home_zone_tile.yaml
rg -F 'file: lvgl/home_status_bar.yaml' "$display" >/dev/null
rg -F 'file: lvgl/home_house_row.yaml' "$display" >/dev/null
rg -F 'file: lvgl/home_manifold_row.yaml' "$display" >/dev/null
rg -F 'id: page_home_statusbar' "$home_status" >/dev/null
rg -F 'id: page_home_house' "$home_house" >/dev/null
rg -F 'id: page_home_mrow_${node}' "$home_mrow" >/dev/null
rg -F 'id: page_home_zone_${node}_${zone}' "$home_ztile" >/dev/null
rg -F 'lds_font_d28' "$home_ztile" >/dev/null
rg -F 'id: page_home_zone_${node}_${zone}_seg0' "$home_ztile" >/dev/null
rg -F 'display_controller_flow_text' "$display" >/dev/null
rg -F 'display_controller_return_text' "$display" >/dev/null
rg -F 'display_zone_overview_state' "$display" >/dev/null
rg -F 'display_calling_zone_count' "$display" >/dev/null
rg -F 'overview_day_theme' "$display" >/dev/null
rg -F 'lds_light_bg' "$display" >/dev/null
rg -F 'display_wifi_bars' "$display" >/dev/null
rg -F 'id(page_home_wifi_0)' "$display" >/dev/null
rg -F 'file: chrome_wifi.yaml' "$home_status" >/dev/null
rg -F 'id: ${id}_4' packages/display/lvgl/chrome_wifi.yaml >/dev/null
rg -F 'display_circulation_visible' "$display" >/dev/null
rg -F 'display_circulation_flow_text' "$display" >/dev/null
rg -F 'display_forecast_hour_temp' "$display" >/dev/null
rg -F 'id: page_zone_detail_home' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'x: 952' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'y: 528' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'text: "\uF015"' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'timeout: 60s' "$display" >/dev/null
rg -F 'display_active_page() == 2' "$display" >/dev/null
rg -F 'file: lvgl/zone_screen.yaml' "$display" >/dev/null
rg -F 'id: page_zone_detail' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'Komfort 22' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'Eco 20' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'Nat 18' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'id: page_zone_temp' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'id: page_zone_fault_reset' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'zone_pending_dirty' "$display" >/dev/null
rg -F 'display_reset_selected_motor_fault' "$coordinator" >/dev/null
! rg -F 'id: page_home_arc' "$display"
! rg -F 'id: page_home_plus' "$display"
! rg -F 'file: lvgl/overview_zone_circle.yaml' "$display"
! rg -F 'id: page_home_forecast' "$display"
rg -F 'hidden: true' "$display" >/dev/null
rg -F 'dashboard_startup_overlay' "$display" >/dev/null


for node in 0 1 2 3; do
  rg -F "vars: {node: $node" "$display" >/dev/null
done

for zone in 0 1 2 3 4 5; do
  rg -F "zone: $zone" "$home_ztile" >/dev/null || rg -F "zone: $zone" "$home_mrow" >/dev/null
done

rg -F 'display_select_zone(${node}, ${zone})' "$home_ztile" >/dev/null
rg -F 'id: page_zone_detail_chip_${index}' "$zone_chip" >/dev/null
rg -F 'display_select_zone(' "$zone_chip" >/dev/null
rg -F 'text: "Z${human}"' "$zone_chip" >/dev/null
rg -F 'id: page_zone_detail_controller_${index}' "$controller_chip" >/dev/null
rg -F 'display_select_controller(${index})' "$controller_chip" >/dev/null
rg -F 'controller_picker_open' "$controller_chip" >/dev/null
rg -F 'id: controller_picker_open' "$display" >/dev/null
rg -F 'text: "C${human}"' "$controller_chip" >/dev/null
for index in 0 1 2 3; do
  rg -F "id: page_zone_detail_controller_${index}" packages/display/lvgl/zone_screen.yaml >/dev/null
done
for index in 0 1 2 3 4 5; do
  rg -F "id: page_zone_detail_chip_${index}" packages/display/lvgl/zone_screen.yaml >/dev/null
done
! rg -F 'page_zone_detail_chip_6' packages/display/lvgl/zone_screen.yaml
! rg -F 'page_zone_detail_dot_' packages/display/lvgl/zone_screen.yaml
! rg -F 'page_zone_detail_halo_' packages/display/lvgl/zone_screen.yaml
test -f packages/display/lvgl/lune_halo.png
test -f packages/display/lvgl/lune_v6_mark.png
test -f packages/display/lvgl/lune_touch_lockup.png
test -f packages/display/lvgl/weather_sun.png
test -f packages/display/lvgl/weather_moon.png
test -f packages/display/lvgl/weather_cloud.png
test -f packages/display/lvgl/weather_partly.png
test -f packages/display/lvgl/weather_partly_night.png
test -f packages/display/lvgl/weather_rain.png
test -f packages/display/lvgl/weather_snow.png
rg -F 'file: ../packages/display/lvgl/lune_halo.png' "$display" >/dev/null
rg -F 'id: weather_sky_sun' "$display" >/dev/null
rg -F 'id: weather_sky_rain' "$display" >/dev/null
rg -F 'id: weather_sky_partly_night' "$display" >/dev/null
rg -F 'precipitation,cloud_cover' "$coordinator" >/dev/null
rg -F 'display_forecast_hour_wind' "$display" >/dev/null
rg -F '"%02d:00"' "$coordinator" >/dev/null
rg -F 'display_visible_node_count' "$display" >/dev/null
rg -F 'page_zone_detail_controller_0' "$display" >/dev/null
rg -F '"C1"' "$preview" >/dev/null
rg -F '"V61-Z6"' "$preview" >/dev/null
rg -F 'tile_w' "$preview" >/dev/null
rg -F 'id: page_zone_detail_plus' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'id: page_zone_plus' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'id: page_zone_minus' packages/display/lvgl/zone_screen.yaml >/dev/null
rg -F 'controller_picker_open' "$display" >/dev/null
rg -F 'zone_pending_dirty' "$display" >/dev/null
rg -F 'display_set_selected_target' "$display" >/dev/null
rg -F 'radius: 16' "$zone_chip" >/dev/null
rg -F 'radius: 16' "$controller_chip" >/dev/null
python3 - <<'PY'
from pathlib import Path
text = Path('packages/display/lvgl/zone_screen.yaml').read_text()
start = text.index('id: page_zone_detail_face\n')
chunk = text[start:start+220]
if 'x: 388\n' not in chunk or 'width: 248\n' not in chunk:
    raise SystemExit('FAIL halo face is not in the disc hub')
print('zone detail face centered ok')
PY
python3 - <<'PY'
from pathlib import Path
text = Path('packages/display/lvgl/zone_screen.yaml').read_text()
start = text.index('id: page_zone_detail\n')
chunk = text[start:start+400]
for required in ('x: 0\n', 'y: 0\n', 'width: 1024\n', 'height: 600\n'):
    if required not in chunk:
        raise SystemExit(f'FAIL zone detail is not full-screen: missing {required!r}')
print('zone detail fullscreen ok')
PY
python3 - <<'PY'
from pathlib import Path
text = Path('packages/display/lvgl/zone_screen.yaml').read_text()
start = text.index('id: page_zone_detail_halo\n')
chunk = text[start:start+220]
for required in ('src: lune_halo_mark\n', 'x: 312\n', 'y: 52\n'):
    if required not in chunk:
        raise SystemExit(f'FAIL halo does not fill the corona footprint: missing {required!r}')
print('zone detail halo size ok')
PY
python3 - <<'PY'
from pathlib import Path
text = Path('packages/display/lvgl_stability.yaml').read_text()
start = text.index('id: chrome_settings_button\n')
chunk = text[start:start+220]
for required in ('x: 8\n', 'y: 2\n', 'width: 48\n', 'height: 48\n'):
    if required not in chunk:
        raise SystemExit(f'FAIL settings gear is not top-left: missing {required!r}')
print('settings gear ok')
PY
python3 - <<'PY'
from pathlib import Path
text = Path('packages/display/lvgl_stability.yaml').read_text()
start = text.index('id: page_settings_pane\n')
chunk = text[start:start+220]
for required in ('x: 0\n', 'y: 52\n', 'width: 1024\n', 'height: 490\n'):
    if required not in chunk:
        raise SystemExit(f'FAIL settings pane is not full-width: missing {required!r}')
print('settings pane beside menu ok')
PY
rg -F 'width: 823' "$manifold" >/dev/null
rg -F 'border_width: 0' "$zone_cell" >/dev/null
rg -F 'border_width: 0' "$manifold" >/dev/null
rg -F 'radius: 0' "$zone_cell" >/dev/null
rg -F 'radius: 0' "$manifold" >/dev/null
rg -F 'id: dashboard_m${node}_z${zone}_bar' "$zone_cell" >/dev/null
rg -F 'id: dashboard_m${node}_z${zone}_temperature_label' "$zone_cell" >/dev/null
rg -F 'id: dashboard_m${node}_z${zone}_button' "$zone_cell" >/dev/null

for required in \
  'heating_summary_text' \
  'alarm_summary_text' \
  'display_refresh_token' \
  'display_header_text' \
  'display_zone_mask' \
  'display_node_zone_count' \
  'display_zone_slot_used' \
  'display_manifold_page' \
  'display_manifold_page_count' \
  'display_set_manifold_page' \
  'display_manifold_text' \
  'display_zone_name_text' \
  'display_zone_temperature_text' \
  'display_zone_setpoint_text' \
  'display_zone_valve_pct' \
  'display_zone_status_color' \
  'display_zone_status_icon' \
  'display_controller_flow_text' \
  'display_controller_return_text' \
  'display_circulation_visible' \
  'display_circulation_flow_text' \
  'display_circulation_head_text' \
  'display_circulation_power_text' \
  'display_circulation_status_text' \
  'display_circulation_status_color' \
  'display_heat_source_text' \
  'display_forecast_text' \
  'display_forecast_visible_hours' \
  'display_forecast_hour_label' \
  'display_forecast_hour_temp' \
  'display_forecast_hour_wind' \
  'display_forecast_hour_sky' \
  'display_forecast_hour_color' \
  'display_forecast_age_text' \
  'display_forecast_age_color' \
  'display_problem_visible' \
  'display_problem_text' \
  'display_selected_room_text' \
  'display_selected_room_name' \
  'display_zone_detail_meta_text' \
  'display_wifi_bars' \
  'display_wifi_color' \
  'display_enabled_zone_count' \
  'display_calling_zone_count' \
  'display_zone_overview_state' \
  'display_visible_node_count' \
  'display_selected_zone_ordinal' \
  'display_step_selected_zone' \
  'display_select_controller' \
  'display_select_zone' \
  'display_adjust_primary_target' \
  'display_boost_primary_room' \
  'display_away_primary_room' \
  'display_house_target_c' \
  'display_house_temperature_c' \
  'display_adjust_house_target' \
  'display_active_page' \
  'display_set_active_page' \
  'display_heat_source_enabled' \
  'display_set_heat_source_enabled' \
  'display_weather_compensation_enabled' \
  'display_set_weather_compensation_enabled' \
  'display_idle_timeout_text' \
  'display_cycle_idle_timeout' \
  'display_system_fact_text' \
  'display_override_active' \
  'display_override_text' \
  'display_set_selected_target'; do
  rg -F "$required" "$coordinator" >/dev/null
done

rg -F 'max_value: 5' "$zone_cell" >/dev/null
! rg -F 'id: dashboard_bottom_status_bar' "$display"
! rg -F 'id: dashboard_heat_source_status_label' "$display"
! rg -F 'id: dashboard_weather_status_label' "$display"
rg -F 'DISPLAY_ICON_OK[] = "\xEF\x80\x8C"' "$coordinator" >/dev/null
rg -F 'DISPLAY_ICON_RIGHT[] = "\xEF\x81\x94"' "$coordinator" >/dev/null
rg -F 'DISPLAY_ICON_WARNING[] = "\xEF\x81\xB1"' "$coordinator" >/dev/null
rg -F 'DISPLAY_ICON_WIND[] = "\xEF\x81\xB4"' "$coordinator" >/dev/null
rg -F '%s %s | sent %s | confirmed %s' "$coordinator" >/dev/null
rg -F '%s %.1f C | %s %.1f m/s | %s' "$coordinator" >/dev/null
rg -F 'text: "\uF054 --.- C"' "$zone_cell" >/dev/null
display_text=$(sed -n '/display_header_text() const/,/display_selected_room_text() const/p' "$coordinator")
# Legacy Montserrat widgets must not use glyphs absent from that font.
# Overview (Geist) may use · → − ● from lds_font_* extras.
if printf '%s' "$display_text" | rg '[●→↑↓↗·−]' >/dev/null || rg '[●→↑↓↗·−]' "$zone_cell" "$zone_chip" "$controller_chip" packages/display/lvgl/overview_circulation.yaml >/dev/null; then
  echo 'FAIL local display strings contain glyphs absent from LVGL Montserrat' >&2
  exit 1
fi
rg -F 'const size_t visible_node_count = std::min<size_t>(4, model_.node_count());' "$coordinator" >/dev/null
rg -F 'valve_level' "$coordinator" >/dev/null
rg -F 'lv_obj_t *pages[3] = {' "$display" >/dev/null
rg -F 'const uint8_t page_ids[3] = {0, 2, 3};' "$display" >/dev/null
rg -F 'const uint8_t mask = node == nullptr ? 0 : 0x3F' "$coordinator" >/dev/null
rg -F 'std::min<uint8_t>(display_manifold_page_, display_page_count - 1)' "$coordinator" >/dev/null
rg -F 'active_page' "$display" >/dev/null
token_body=$(sed -n '/display_refresh_token() const/,/display_header_text() const/p' "$coordinator")
! printf '%s' "$token_body" | rg -F 'poll_generation_'
! printf '%s' "$token_body" | rg -F 'last_seen_ms'
! printf '%s' "$token_body" | rg -F 'last_write_ms'
rg -F 'return display_refresh_token_cache_' "$coordinator" >/dev/null
! rg -F 'Not mapped' "$zone_cell"

rg -F 'data-save="heat-source"' "$touch_ui" >/dev/null
rg -F 'data-save="circulation"' "$touch_ui" >/dev/null
rg -F 'public, max-age=31536000, immutable' "$dashboard_cpp" >/dev/null
rg -F 'STATIC_CHUNK_SIZE = 1024' "$dashboard_cpp" >/dev/null
rg -F 'memcpy(chunk, data + offset, to_send)' "$dashboard_cpp" >/dev/null
rg -F 'LUNE_TOUCH_HAS_UI' "$dashboard_cpp" >/dev/null
rg -F '/lune-ui.css' "$dashboard_cpp" >/dev/null

if rg -F 'full_refresh: true' "$display" >/dev/null; then
  echo 'FAIL full-screen redraw reintroduces RGB flicker and tearing' >&2
  exit 1
fi

if rg -F 'id: dashboard_startup_progress' "$display" >/dev/null; then
  echo 'FAIL startup duration is unknown; do not show invented determinate progress' >&2
  exit 1
fi

if rg -F 'dashboard_zone_row_' "$display" >/dev/null; then
  echo 'FAIL the local display must address physical manifold/zone cells' >&2
  exit 1
fi

# No colour literals outside the generated tokens file (allow CH422G bitmasks).
for file in "$display" "$manifold" "$zone_cell" "$zone_chip" "$controller_chip" "$preview" \
            packages/display/lvgl/overview_zone_circle.yaml \
            packages/display/lvgl/overview_controller_chip.yaml \
            packages/display/lvgl/overview_forecast_hour.yaml \
            packages/display/lvgl/overview_circulation.yaml \
            packages/display/lvgl/overview_manifold_mark.yaml \
            packages/display/lvgl/overview_manifold_arms.yaml \
            packages/display/lvgl/chrome_menu_item.yaml \
            packages/display/lvgl/settings_toggle_row.yaml \
            packages/display/lvgl/settings_value_row.yaml; do
  if rg -n '0x[0-9A-Fa-f]{6}' "$file" | rg -v 'initial_output_bits' >/dev/null; then
    echo "FAIL colour literal outside tokens in $file" >&2
    rg -n '0x[0-9A-Fa-f]{6}' "$file" | rg -v 'initial_output_bits' >&2 || true
    exit 1
  fi
done
rg -F 'lds_accent' "$tokens" >/dev/null
! rg -F 'lt_color_' packages/display
rg -F 'lune_design_tokens.h' "$preview" >/dev/null

# Every button declares width and height of at least 48.
python3 - <<'PY'
from pathlib import Path
import re
files = [
    Path('packages/display/lvgl_stability.yaml'),
    Path('packages/display/lvgl/manifold_card.yaml'),
    Path('packages/display/lvgl/manifold_zone_cell.yaml'),
    Path('packages/display/lvgl/zone_detail_chip.yaml'),
    Path('packages/display/lvgl/zone_detail_controller_chip.yaml'),
    Path('packages/display/lvgl/overview_zone_circle.yaml'),
    Path('packages/display/lvgl/chrome_menu_item.yaml'),
    Path('packages/display/lvgl/settings_toggle_row.yaml'),
    Path('packages/display/lvgl/settings_value_row.yaml'),
    Path('packages/display/lvgl/zone_screen.yaml'),
    Path('packages/display/lvgl/home_status_bar.yaml'),
    Path('packages/display/lvgl/home_zone_tile.yaml'),
]
pattern = re.compile(r'(?m)^([ \t]*)(?:- )?button:\n((?:^\1[ \t]+.*\n)+)')
for path in files:
    text = path.read_text()
    for match in pattern.finditer(text):
        block = match.group(2)
        widths = [int(v) for v in re.findall(r'(?m)^\s+width:\s*(\d+)', block)]
        heights = [int(v) for v in re.findall(r'(?m)^\s+height:\s*(\d+)', block)]
        if not widths or not heights:
            raise SystemExit(f'FAIL button missing size in {path}')
        if widths[0] < 48 or heights[0] < 48:
            raise SystemExit(f'FAIL button below 48px in {path}: {widths[0]}x{heights[0]}')
print('button sizes ok')
PY

for required in \
  'id: display_awake' \
  'timeout: 60s' \
  'lvgl.pause:' \
  'condition: lvgl.is_paused' \
  'lvgl.resume:' \
  'digital_write(2, want_on)' \
  'timeout: 120s' \
  'timeout: 300s' \
  'display_idle_timeout_ms() == 0' \
  'consume_display_wake_request()' \
  'lv_disp_trig_activity(nullptr)' \
  'return id(display_awake);'; do
  rg -F "$required" "$display" >/dev/null
done
rg -F 'display_idle_timeout_ms() const' "$coordinator" >/dev/null
rg -F 'request_display_wake' "$coordinator" >/dev/null
rg -F 'consume_display_wake_request' "$coordinator" >/dev/null
rg -F 'disp_idle' "$coordinator" >/dev/null
rg -F 'invalid_display_idle_timeout' "$coordinator" >/dev/null
rg -F 'on_release:' "$display" >/dev/null
rg -n 'on_idle:' "$display" >/dev/null
idle_block=$(awk '/on_idle:/{flag=1} flag{print} /^  bottom_layer:/{exit}' "$display")
if printf '%s' "$idle_block" | rg -F 'digital_write' >/dev/null; then
  echo 'FAIL backlight I2C must not run from the LVGL on_idle path' >&2
  exit 1
fi
if ! printf '%s' "$idle_block" | rg -F 'display_awake) = false' >/dev/null; then
  echo 'FAIL idle timeout must request the screen off without touching widgets' >&2
  exit 1
fi
if ! printf '%s' "$idle_block" | rg -F 'display_set_active_page(0)' >/dev/null; then
  echo 'FAIL unused zone detail must return to overview' >&2
  exit 1
fi

echo 'PASS local Touch display uses LDS v2 shell with overview, zone-detail, and coalesced partial refresh'
