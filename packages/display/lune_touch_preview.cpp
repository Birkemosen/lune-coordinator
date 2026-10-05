#include "../../components/lune_touch_coordinator/lune_design_tokens.h"

#include <algorithm>
#include <lvgl.h>

namespace {

using namespace lune::tokens;

lv_obj_t *make_label(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t width,
                     int32_t height, uint32_t color,
                     lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, height);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
  return label;
}

lv_obj_t *make_button(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t width,
                      int32_t height, uint32_t color) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 0, LV_PART_MAIN);
  lv_obj_t *label = make_label(button, text, 0, 0, width, height, kText, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(label);
  return button;
}

lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int32_t height,
                     bool hidden) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_style_bg_color(panel, lv_color_hex(kBg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  if (hidden)
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  return panel;
}

void sketch_wifi(lv_obj_t *parent, int32_t x, int32_t y) {
  const int heights[5] = {8, 12, 16, 20, 24};
  for (int i = 0; i < 5; i++) {
    lv_obj_t *bar = make_panel(parent, x + i * 6, y + (24 - heights[i]), 4, heights[i], false);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(i < 4 ? kOk : kControlBorder), LV_PART_MAIN);
  }
}

void add_nav(lv_obj_t *screen) {
  lv_obj_t *gear = make_button(screen, "G", 8, 2, kMinTarget, kMinTarget, kBg);
  lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, LV_PART_MAIN);
}

void sketch_settings(lv_obj_t *page) {
  lv_obj_t *pane = make_panel(page, 0, kStatusH, kScreenW, 490, false);
  make_label(pane, "Settings", 24, 24, 976, 36, kText);
  make_label(pane, "HEATING", 24, 80, 976, 24, kTextMuted);
  lv_obj_t *weather = make_button(pane, "Weather compensation", 24, 108, 696, 56, kControlBg);
  lv_obj_set_style_radius(weather, 10, LV_PART_MAIN);
  lv_obj_t *heat = make_button(pane, "Heat source", 24, 172, 696, 56, kControlBg);
  lv_obj_set_style_radius(heat, 10, LV_PART_MAIN);
  make_label(pane, "DISPLAY", 24, 260, 696, 24, kTextMuted);
  lv_obj_t *timeout = make_button(pane, "Screen timeout", 24, 288, 696, 56, kControlBg);
  lv_obj_set_style_radius(timeout, 10, LV_PART_MAIN);
}

void sketch_home(lv_obj_t *page) {
  constexpr int side = 24;
  constexpr int gap = 12;
  constexpr int rail_w = 72;
  constexpr int rail_x = kScreenW - 16 - rail_w;
  constexpr int chip_top = 64;
  constexpr int chip_w = 80;
  constexpr int chip_h = 80;
  constexpr int mark_w = 108;
  constexpr int mark_h = 80;
  constexpr int tile_gap_x = 8;
  constexpr int tile_gap_y = 8;
  constexpr int avail_w = rail_x - side - gap;
  constexpr int stage_bottom = 444;
  constexpr int groups = 2;
  constexpr int row_h = (stage_bottom - chip_top - gap * (groups - 1)) / groups;
  constexpr int zcount = 6;
  constexpr int zcols = 3;
  constexpr int zrows = 2;
  constexpr int tiles_x = side + mark_w + gap;
  constexpr int chip_x = side + avail_w - chip_w;
  constexpr int tiles_w = chip_x - tiles_x - gap;
  constexpr int tile_w = (tiles_w - tile_gap_x * (zcols - 1)) / zcols;
  constexpr int tile_h = (row_h - tile_gap_y * (zrows - 1)) / zrows;
  const char *zone_names[6] = {"KONTOR", "V61-Z2", "V61-Z3", "KOKKEN", "V61-Z5", "V61-Z6"};
  for (int i = 0; i < 2; i++) {
    const int row_y = chip_top + i * (row_h + gap);
    lv_obj_t *mark = make_panel(page, side, row_y + (row_h - mark_h) / 2, mark_w, mark_h, false);
    lv_obj_set_style_bg_color(mark, lv_color_hex(kControlBg), LV_PART_MAIN);
    make_label(mark, "V6", 36, 28, 36, 24, kText);
    lv_obj_t *chip = make_panel(page, chip_x, row_y, chip_w, std::max(chip_h, row_h), false);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, LV_PART_MAIN);
    make_label(chip, i == 0 ? "C1" : "C2", 0, 0, 80, 20, kText);
    make_label(chip, i == 0 ? "34.1" : "31.2", 20, 24, 60, 22, kSeriesHeat);
    make_label(chip, i == 0 ? "30.6" : "28.9", 20, 50, 60, 22, kSeriesCool);
    constexpr int pipe_w = 4;
    constexpr int pipe_tip_x = 97;
    const int arm_x = side + pipe_tip_x;
    const int mark_y = row_y + (row_h - mark_h) / 2;
    const int top_y = row_y;
    const int bot_y = row_y + row_h - pipe_w;
    const int mark_top = mark_y + 7;
    const int mark_bot = mark_y + 73;
    lv_obj_t *top_v = make_panel(page, arm_x, std::min(top_y, mark_top), pipe_w,
                                 std::max(pipe_w, std::abs(mark_top - top_y) + pipe_w), false);
    lv_obj_set_style_radius(top_v, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(top_v, lv_color_hex(kThermalReturn), LV_PART_MAIN);
    lv_obj_t *bot_v = make_panel(page, arm_x, std::min(bot_y, mark_bot), pipe_w,
                                 std::max(pipe_w, std::abs(bot_y - mark_bot) + pipe_w), false);
    lv_obj_set_style_radius(bot_v, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bot_v, lv_color_hex(kThermalSupply), LV_PART_MAIN);
    for (int zone = 0; zone < zcount; zone++) {
      const int zcol = zone % zcols;
      const int zrow = zone / zcols;
      const uint32_t status = zone == 1 && i == 0 ? kWarn : kOk;
      lv_obj_t *tile = make_panel(page, tiles_x + zcol * (tile_w + tile_gap_x),
                                  row_y + zrow * (tile_h + tile_gap_y), tile_w, tile_h, false);
      lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_t *bar = make_panel(tile, 0, 8, 3, tile_h - 16, false);
      lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
      lv_obj_set_style_bg_color(bar, lv_color_hex(status), LV_PART_MAIN);
      if (zone == 1 && i == 0) {
        const int heights[3] = {6, 10, 8};
        for (int b = 0; b < 3; b++) {
          lv_obj_t *icon = make_panel(tile, 12 + b * 4, 18 - heights[b], 3, heights[b], false);
          lv_obj_set_style_bg_color(icon, lv_color_hex(status), LV_PART_MAIN);
        }
      } else {
        lv_obj_t *dot = make_panel(tile, 14, 8, 10, 10, false);
        lv_obj_set_style_radius(dot, 5, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(status), LV_PART_MAIN);
      }
      make_label(tile, zone_names[zone], 32, 6, std::max(24, tile_w - 44), 20, kTextMuted);
      make_label(tile, i == 0 ? "21.5" : "19.4", 12, tile_h - 40, std::max(24, tile_w - 48), 32, kText);
      make_label(tile, "C", tile_w - 36, tile_h - 28, 32, 20, kTextMuted, LV_TEXT_ALIGN_RIGHT);
    }
  }
  lv_obj_t *circ = make_panel(page, rail_x, 64, rail_w, 340, false);
  lv_obj_set_style_bg_opa(circ, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_t *circ_rule = make_panel(circ, 0, 8, 3, 324, false);
  lv_obj_set_style_radius(circ_rule, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circ_rule, lv_color_hex(kOk), LV_PART_MAIN);
  make_label(circ, "P", 8, 16, 60, 24, kOk, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "OK", 8, 40, 60, 20, kOk, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "14.0", 8, 84, 60, 24, kSeriesHeat, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "L/min", 8, 106, 60, 16, kTextMuted, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "3.1", 8, 140, 60, 24, kText, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "m", 8, 162, 60, 16, kTextMuted, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "42", 8, 196, 60, 24, kText, LV_TEXT_ALIGN_CENTER);
  make_label(circ, "W", 8, 218, 60, 16, kTextMuted, LV_TEXT_ALIGN_CENTER);
  lv_obj_t *forecast = make_panel(page, side, 444, kScreenW - side - 16, 156, false);
  lv_obj_set_style_bg_opa(forecast, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_t *fx_rule = make_panel(forecast, 0, 8, 3, 140, false);
  lv_obj_set_style_radius(fx_rule, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fx_rule, lv_color_hex(kAccent), LV_PART_MAIN);
  make_label(forecast, "5m", 16, 8, 80, 20, kTextMuted);
  for (int i = 0; i < 12; i++) {
    const int x = 16 + i * 80;
    lv_obj_t *sky = make_panel(forecast, x + 22, 40, 32, 32, false);
    lv_obj_set_style_radius(sky, i < 3 ? 16 : 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sky, lv_color_hex(i < 3 ? kText : kSeriesSolar), LV_PART_MAIN);
    make_label(forecast, i < 3 ? "-2" : "+1", x, 74, 76, 22, i < 3 ? kSeriesCool : kSeriesHeat,
               LV_TEXT_ALIGN_CENTER);
    make_label(forecast, i < 3 ? "NW 8" : "SW 3", x, 96, 76, 18, kTextMuted, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *rule = make_panel(forecast, x + 8, 118, 60, 1, false);
    lv_obj_set_style_bg_color(rule, lv_color_hex(kSeparator), LV_PART_MAIN);
    make_label(forecast, i == 0 ? "13:00" : "14:00", x, 122, 76, 18,
               i == 0 ? kAccentForest : kTextMuted, LV_TEXT_ALIGN_CENTER);
  }
}

void sketch_zone_detail(lv_obj_t *page) {
  make_label(page, "FIRST FLOOR", 24, 64, 280, 36, kTextMuted, LV_TEXT_ALIGN_LEFT);
  lv_obj_t *halo = make_panel(page, 312, 52, 400, 467, false);
  lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_t *face = make_panel(page, 388, 117, 248, 248, false);
  lv_obj_set_style_radius(face, 124, LV_PART_MAIN);
  make_label(face, "22.0", 4, 72, 240, 56, kText, LV_TEXT_ALIGN_CENTER);
  make_label(face, "CURRENT 19.0", 4, 148, 240, 28, kTextMuted, LV_TEXT_ALIGN_CENTER);
  lv_obj_t *plus = make_button(page, "+", 732, 117, kMinTarget, kMinTarget, kTextFaint);
  lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_t *minus = make_button(page, "-", 732, 317, kMinTarget, kMinTarget, kTextFaint);
  lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  const char *controllers[2] = {"C2", "C1"};
  lv_obj_t *popup = make_button(page, controllers[0], 24, 450, 72, 64, kTextFaint);
  lv_obj_set_style_radius(popup, 16, LV_PART_MAIN);
  lv_obj_t *dock = make_button(page, controllers[1], 24, 520, 72, 64, kAccent);
  lv_obj_set_style_radius(dock, 16, LV_PART_MAIN);
  const char *zones[6] = {"Z1", "Z2", "Z3", "Z4", "Z5", "Z6"};
  for (int i = 0; i < 6; i++) {
    lv_obj_t *chip = make_button(page, zones[i], 112 + i * 78, 520, 72, 64,
                                 i == 0 ? kAccentForest : kTextFaint);
    lv_obj_set_style_radius(chip, 16, LV_PART_MAIN);
  }
  lv_obj_t *home = make_button(page, "H", 952, 528, 48, 48, kBg);
  lv_obj_set_style_bg_opa(home, LV_OPA_TRANSP, LV_PART_MAIN);
}

// Host preview of the overview + zone-detail Lune Touch shell for the 1024x600 panel.
static void ui_init(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  make_label(screen, "House status  |  4 manifolds online", 64, 15, 768, 24, kText);
  sketch_wifi(screen, 916, 12);
  make_label(screen, "Live", 840, 15, 72, 24, kOk);
  make_label(screen, "13:16", 956, 15, 56, 24, kTextMuted, LV_TEXT_ALIGN_RIGHT);

  lv_obj_t *hairline = lv_obj_create(screen);
  lv_obj_set_pos(hairline, 0, 51);
  lv_obj_set_size(hairline, kScreenW, 1);
  lv_obj_set_style_bg_color(hairline, lv_color_hex(kSeparator), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hairline, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(hairline, 0, LV_PART_MAIN);

  lv_obj_t *page_home = make_panel(screen, 0, 0, kScreenW, kScreenH, false);
  lv_obj_t *page_zone_detail = make_panel(screen, 0, 0, kScreenW, kScreenH, true);
  lv_obj_t *page_settings = make_panel(screen, 0, 0, kScreenW, kScreenH, true);
  (void)page_zone_detail;
  (void)page_settings;
  sketch_home(page_home);
  sketch_zone_detail(page_zone_detail);
  sketch_settings(page_settings);
  add_nav(screen);
}

}  // namespace

#ifdef LVGL_LIVE_PREVIEW
extern "C" void lvgl_live_preview_init(void) { ui_init(); }
#endif
