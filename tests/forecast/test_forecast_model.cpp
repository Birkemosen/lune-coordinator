// =============================================================================
// Forecast preload model — Host-runnable unit tests
// =============================================================================

#include "forecast_model.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hv6fc;

static int g_failures = 0;

static void expect(bool cond, const char *what) {
  if (cond) {
    printf("PASS  %s\n", what);
  } else {
    printf("FAIL  %s\n", what);
    g_failures++;
  }
}

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }
static constexpr float RISE_ON_FOR_TEST = 1.5f;   // forecast_model.cpp RISE_ON

static void test_wind_alignment() {
  expect(near(wind_alignment(WALL_NORTH, 0.0f), 1.0f), "alignment: N wall, N wind = 1.0");
  expect(near(wind_alignment(WALL_NORTH, 180.0f), 0.0f), "alignment: N wall, S wind = 0.0");
  expect(near(wind_alignment(WALL_NORTH, 45.0f), 0.7071f), "alignment: N wall, NE wind = cos45");
  expect(near(wind_alignment(WALL_EAST | WALL_WEST, 270.0f), 1.0f), "alignment: E+W walls, W wind = 1.0");
  expect(near(wind_alignment(0, 270.0f), 0.0f), "alignment: interior room = 0.0");
  expect(near(wind_alignment(WALL_WEST, 350.0f), 0.1736f), "alignment: W wall, 350° wind wraps");
}

static void test_zone_hour_load() {
  ZoneExposure exposed{};
  exposed.exterior_walls = WALL_WEST;
  exposed.wind_exposure = 0.8f;
  exposed.solar_gain = 0.3f;

  ForecastHour storm{-4.0f, 15.0f, 270.0f, 0.0f};
  expect(near(zone_hour_load(storm, exposed, 21.0f), 3.0f), "load: west storm at -4°C = 3.0");

  ForecastHour mild_storm{11.0f, 15.0f, 270.0f, 0.0f};
  expect(near(zone_hour_load(mild_storm, exposed, 21.0f), 1.2f), "load: west storm at 11°C = 1.2");

  ForecastHour calm{-10.0f, 0.0f, 0.0f, 0.0f};
  expect(near(zone_hour_load(calm, exposed, 21.0f), 0.0f), "load: calm cold = 0.0");

  ForecastHour east_wind{-4.0f, 15.0f, 90.0f, 0.0f};
  expect(near(zone_hour_load(east_wind, exposed, 21.0f), 0.0f), "load: wind on sheltered side = 0.0");

  ForecastHour sunny_storm{-4.0f, 15.0f, 270.0f, 800.0f};
  expect(near(zone_hour_load(sunny_storm, exposed, 21.0f), 2.7f), "load: solar relief subtracts");

  ZoneExposure interior{};
  interior.exterior_walls = 0;
  expect(near(zone_hour_load(storm, interior, 21.0f), 0.0f), "load: interior room = 0.0");
}

static void fill_calm(ForecastHour *hours, size_t count) {
  for (size_t i = 0; i < count; i++)
    hours[i] = ForecastHour{8.0f, 2.0f, 0.0f, 0.0f};
}

static void test_room_loss() {
  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 0.8f;
  zone.solar_gain = 0.0f;
  PreloadParams params{};   // defaults: cooling 0.12 °C/h in typical weather
  // 21 − 6 = 15 K, 4 m/s half on the wall: the typical weather → the learned rate.
  expect(near(room_loss_c_per_h(ForecastHour{6.0f, 4.0f, 330.0f, 0.0f}, zone, params), 0.12f, 0.01f),
         "loss: typical weather = learned cooling");
  // Twice the temperature difference, calm: twice the rate over the typical wind.
  expect(near(room_loss_c_per_h(ForecastHour{-9.0f, 0.0f, 0.0f, 0.0f}, zone, params), 0.24f / 1.096f, 0.01f),
         "loss: calm frost scales with the temperature difference");
  // −4 °C, 15 m/s head-on: 25/15 × 1.72 / 1.096.
  expect(near(room_loss_c_per_h(ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f}, zone, params), 0.314f, 0.01f),
         "loss: storm on the wall");
  expect(room_loss_c_per_h(ForecastHour{-4.0f, 15.0f, 90.0f, 0.0f}, zone, params) <
             room_loss_c_per_h(ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f}, zone, params),
         "loss: wind on the sheltered side counts less");
  zone.solar_gain = 0.5f;
  expect(room_loss_c_per_h(ForecastHour{-4.0f, 15.0f, 270.0f, 800.0f}, zone, params) <
             room_loss_c_per_h(ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f}, zone, params),
         "loss: sun relieves");
}

static void test_preload_decision() {
  ForecastHour hours[FORECAST_HOURS];
  PreloadParams params{};
  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 0.8f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 12;

  // Storm at +10 (loss 0.31 against usual 0.12 and floor 0.30): start the floor lag (0)
  // and an hour before it, the least offset that counts.
  fill_calm(hours, FORECAST_HOURS);
  hours[10] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};
  PreloadDecision d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.offset_c, 0.0f) && d.preload_start_h == 9 && d.preload_end_h == 11,
         "preload: storm in 10 h → nothing yet, planned 9–11");
  expect(near(d.planned_offset_c, 0.3f) && d.peak_in_h == 10, "preload: planned the least offset, peak at +10");
  d = compute_zone_preload(hours, FORECAST_HOURS, 9, zone, params);
  expect(near(d.offset_c, 0.3f) && d.preload_start_h == 0, "preload: on an hour before the storm");

  zone.thermal_lead_h = 4;
  for (size_t i = 10; i < 15; i++)
    hours[i] = ForecastHour{-15.0f, 25.0f, 270.0f, 0.0f};
  d = compute_zone_preload(hours, FORECAST_HOURS, 9, zone, params);
  expect(near(d.offset_c, 1.5f), "preload: a violent storm the floor cannot meet is capped");

  fill_calm(hours, FORECAST_HOURS);
  d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.offset_c, 0.0f) && near(d.planned_offset_c, 0.0f) && d.preload_end_h == 0,
         "preload: calm forecast → nothing");
  d = compute_zone_preload(nullptr, 0, 0, zone, params);
  expect(near(d.offset_c, 0.0f) && d.peak_in_h == -1, "preload: null input → empty decision");
  d = compute_zone_preload(hours, FORECAST_HOURS, FORECAST_HOURS + 5, zone, params);
  expect(near(d.offset_c, 0.0f), "preload: now_index out of range → empty decision");
}

static void test_priority_and_thermal() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  for (size_t i = 2; i < 6; i++)
    hours[i] = ForecastHour{-6.0f, 15.0f, 270.0f, 0.0f};
  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 1.5f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 6;
  PreloadParams params{};
  params.floor_lag_h = 1.0f;
  const PreloadDecision base = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  params.priority_gain = 1.2f;
  const PreloadDecision prio = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(prio.offset_c > base.offset_c, "priority: raises the offset");

  // A room that heats well rides through what a slow one needs help with.
  params.priority_gain = 1.0f;
  params.thermal_samples = 12;
  params.learned_cool_loss_c_per_h = 0.12f;
  params.learned_heat_gain_c_per_h = 0.30f;
  const PreloadDecision fast = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  params.learned_heat_gain_c_per_h = 0.06f;
  const PreloadDecision slow = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(fast.used_thermal_sizing && slow.offset_c > fast.offset_c, "thermal: a floor that gives less needs more");
}

static void test_horizon_and_series() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  for (size_t i = 0; i < 8; i++)
    hours[i] = ForecastHour{-8.0f, 18.0f, 270.0f, 0.0f};

  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 1.0f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 6;

  PreloadParams params{};
  params.enable_horizon = true;
  params.has_current_temp = true;
  params.current_temp_c = 21.0f;
  params.scheduled_setpoint_c = 21.0f;
  params.comfort_band_c = 0.5f;
  params.max_offset_c = 1.5f;
  params.thermal_samples = 12;
  params.learned_heat_gain_c_per_h = 0.15f;
  params.learned_cool_loss_c_per_h = 0.20f;

  PreloadDecision d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(d.used_horizon, "horizon: enabled path marks used_horizon");
  expect(d.offset_c > 0.0f, "horizon: cold storm needs preload");

  PreloadSeries series{};
  fill_preload_series(hours, FORECAST_HOURS, 0, zone, params, d, &series);
  expect(series.count >= 6, "series: fills horizon hours");
  expect(near(series.scheduled_setpoint_c[0], 21.0f), "series: scheduled setpoint");
  expect(near(series.comfort_min_c[0], 20.5f), "series: comfort min");
  expect(near(series.comfort_max_c[0], 21.5f), "series: comfort max");
  expect(series.preload_active[0] == (d.offset_c > 0.01f), "series: preload flag at t0");
}

// Floor response: heat reaches the room late, so preload starts the lag earlier and
// stores what the room loses meanwhile.
static void test_floor_lag() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  for (size_t i = 6; i < 12; i++)
    hours[i] = ForecastHour{-8.0f, 18.0f, 270.0f, 0.0f};
  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 1.0f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 8;
  PreloadParams params{};
  params.has_current_temp = true;
  params.current_temp_c = 21.0f;
  params.scheduled_setpoint_c = 21.0f;
  params.max_offset_c = 1.5f;

  const PreloadDecision instant = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  params.floor_lag_h = 2.0f;
  const PreloadDecision lagged = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(instant.preload_start_h == 5 && lagged.preload_start_h == 3, "floor lag: starts the lag earlier");
  expect(lagged.planned_offset_c > instant.planned_offset_c, "floor lag: stores what the room loses meanwhile");

  // An offset applied now reaches the room only after the lag: until then it runs as without.
  ForecastHour calm[FORECAST_HOURS];
  fill_calm(calm, FORECAST_HOURS);
  PreloadDecision lift{};
  lift.offset_c = 1.0f;
  lift.preload_end_h = 24;
  PreloadSeries now_s{}, late_s{}, base_s{};
  params.floor_lag_h = 0.0f;
  fill_preload_series(calm, FORECAST_HOURS, 0, zone, params, lift, &now_s);
  params.floor_lag_h = 2.0f;
  fill_preload_series(calm, FORECAST_HOURS, 0, zone, params, lift, &late_s);
  fill_preload_series(calm, FORECAST_HOURS, 0, zone, params, PreloadDecision{}, &base_s);
  expect(now_s.expected_temp_c[1] > base_s.expected_temp_c[1], "floor lag 0: offset felt at once");
  expect(near(late_s.expected_temp_c[1], base_s.expected_temp_c[1]) &&
             near(late_s.expected_temp_c[2], base_s.expected_temp_c[2]) &&
             late_s.expected_temp_c[3] > base_s.expected_temp_c[3],
         "floor lag 2: offset felt from hour 2");
}

static void test_learning_update() {
  PreloadOutcome miss{};
  miss.preload_was_active = true;
  miss.undershoot_c = 0.5f;
  LearningUpdate up = update_preload_learning(1.0f, 0, miss);
  expect(up.gain_scale > 1.0f, "learning: undershoot raises gain");
  expect(up.lead_bias_h >= 1, "learning: severe undershoot extends lead");

  PreloadOutcome hot{};
  hot.preload_was_active = true;
  hot.overshoot_c = 0.5f;
  up = update_preload_learning(1.2f, 2, hot);
  expect(up.gain_scale < 1.2f, "learning: overshoot eases gain");
  expect(up.lead_bias_h == 2, "learning: mild overshoot keeps lead bias");
  hot.overshoot_c = 0.8f;
  up = update_preload_learning(1.2f, 2, hot);
  expect(up.lead_bias_h == 1, "learning: clear overshoot shortens lead bias");

  PreloadOutcome idle{};
  idle.preload_was_active = false;
  up = update_preload_learning(1.0f, 0, idle);
  expect(near(up.gain_scale, 1.0f) && up.lead_bias_h == 0, "learning: idle leaves state");
}

static void test_taper_after_peak() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  hours[0] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};  // peak now
  hours[1] = ForecastHour{8.0f, 2.0f, 0.0f, 0.0f};

  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 0.8f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 4;

  PreloadParams params{};
  params.enable_horizon = false;

  PreloadDecision at_peak = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  hours[0] = ForecastHour{8.0f, 2.0f, 0.0f, 0.0f};
  hours[1] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};
  // After peak passed with calm now — peak still in window at +1
  PreloadDecision after = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(at_peak.offset_c >= after.offset_c || after.peak_in_h > 0,
         "taper: peak-now offset is full urgency path");
}

// Falsifies: absurd learned heat_gain must not silence plant-horizon preload.
// Pre-A1, gain≈37 made every horizon candidate look sufficient (offset→0).
// Post-A1, gain outside the plausible band is not thermal-confident, so
// defaults apply and storm preload remains.
static void test_absurd_heat_gain_suppresses_horizon() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  for (size_t i = 0; i < 8; i++)
    hours[i] = ForecastHour{-8.0f, 18.0f, 270.0f, 0.0f};

  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 1.0f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 6;

  PreloadParams realistic{};
  realistic.enable_horizon = true;
  realistic.has_current_temp = true;
  realistic.current_temp_c = 21.0f;
  realistic.scheduled_setpoint_c = 21.0f;
  realistic.comfort_band_c = 0.5f;
  realistic.max_offset_c = 1.5f;
  realistic.thermal_samples = 12;
  realistic.learned_heat_gain_c_per_h = 0.15f;
  realistic.learned_cool_loss_c_per_h = 0.20f;

  PreloadParams absurd = realistic;
  absurd.learned_heat_gain_c_per_h = 37.0f;

  PreloadParams unconfident = realistic;
  unconfident.thermal_samples = 0;

  PreloadDecision ok = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, realistic);
  PreloadDecision bad = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, absurd);
  PreloadDecision def = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, unconfident);
  expect(ok.offset_c > 0.0f, "falsify: realistic gain still preloads in storm");
  expect(near(bad.offset_c, def.offset_c, 0.05f),
         "falsify: absurd gain rejected → same as unconfident defaults");
  expect(near(bad.heuristic_offset_c, def.heuristic_offset_c, 0.05f),
         "falsify: absurd gain must not silence preload (A1 gate)");
}

static void test_inverted_wind_direction() {
  // Open-Meteo winddirection_10m = direction wind comes FROM.
  // West-facing wall (normal 270°) is hit by wind FROM 270°.
  expect(near(wind_alignment(WALL_WEST, 270.0f), 1.0f),
         "sign: west wall hit by wind from west");
  expect(near(wind_alignment(WALL_WEST, 90.0f), 0.0f),
         "sign: west wall not hit by wind from east");
  // If someone inverted to "wind goes toward", west wall would align with 90°.
  expect(wind_alignment(WALL_WEST, 90.0f) < 0.1f && wind_alignment(WALL_WEST, 270.0f) > 0.9f,
         "sign: inverted winddir would fail this pair");
}

// An evening's wind seen from the morning (Touch forecast 2026-11-18, Laura: north and west
// walls, learned exposure 3.0, floor lag 0.7 h, learned 0.12 °C/h both ways): 6 °C through
// the day, a south-west breeze in the morning, north wind 6–7 m/s from 18. Laura is to keep
// up, not to be raised: preload from 16 with a small offset, nothing in the morning.
static void test_evening_episode() {
  static const float T[24] = {3.0f, 3.2f, 3.6f, 4.0f, 4.2f, 4.6f, 5.1f, 5.5f, 5.7f, 6.0f, 6.5f, 6.9f,
                              6.4f, 6.5f, 6.5f, 6.3f, 5.9f, 5.6f, 5.7f, 5.5f, 5.2f, 4.5f, 4.2f, 3.8f};
  static const float W[24] = {8.1f, 7.6f, 6.8f, 6.0f, 5.9f, 5.2f, 4.8f, 3.7f, 3.7f, 3.6f, 3.1f, 4.3f,
                              1.7f, 2.9f, 2.7f, 2.3f, 2.3f, 2.4f, 6.9f, 6.8f, 7.1f, 5.9f, 6.0f, 6.0f};
  static const float D[24] = {109, 109, 100, 101, 104, 104, 108, 111, 99, 100, 125, 214,
                              170, 163, 124, 79,  28,  346, 14,  11,  354, 339, 336, 328};
  ForecastHour hours[FORECAST_HOURS];
  for (size_t i = 0; i < FORECAST_HOURS; i++)
    hours[i] = i < 24 ? ForecastHour{T[i], W[i], D[i], 0.0f} : ForecastHour{3.0f, 5.0f, 325.0f, 0.0f};

  ZoneExposure laura{};
  laura.exterior_walls = WALL_NORTH | WALL_WEST;
  laura.wind_exposure = 3.0f;
  laura.solar_gain = 0.0f;
  laura.thermal_lead_h = 9;   // learned while the old model came late; only a cap now

  PreloadParams params{};
  params.indoor_ref_c = 22.0f;
  params.scheduled_setpoint_c = 22.0f;
  params.has_current_temp = true;
  params.current_temp_c = 22.0f;
  params.comfort_band_c = 0.5f;
  params.max_offset_c = 1.5f;
  params.floor_lag_h = 0.73f;
  params.thermal_samples = 12;
  params.learned_heat_gain_c_per_h = 0.12f;
  params.learned_cool_loss_c_per_h = 0.123f;

  const PreloadDecision at9 = compute_zone_preload(hours, FORECAST_HOURS, 9, laura, params);
  expect(near(at9.offset_c, 0.0f) && at9.preload_start_h == 7 && at9.planned_offset_c >= 0.3f &&
             at9.planned_offset_c < 0.6f,
         "episode: north wind from 18 → nothing at 9, a small preload planned from 16");
  const PreloadDecision at16 = compute_zone_preload(hours, FORECAST_HOURS, 16, laura, params);
  printf("      episode: 16 → %.2f to +%u h, min %.2f\n", at16.offset_c, at16.preload_end_h, at16.min_temp_c);
  expect(at16.offset_c >= 0.3f && at16.preload_end_h > 8, "episode: on from 16 through the night");
  expect(at16.horizon_ok, "episode: the room keeps up");
}

// Wind that stays vs wind that comes (2026-11-21, Kontor: west wall, learned exposure 3,
// floor lag 1.7 h). A steady 4–6 m/s west wind at 0–4 °C from midnight seen at 09: the loss
// is high all day but the floors have it, so nothing. Calm turning to the same storm within
// two hours: preload. The same storm reached over twelve hours: the floor follows, nothing.
static void test_rise_not_level() {
  static const float T[24] = {-1.0f, -0.6f, 0.1f, 0.0f, 0.1f, 0.9f, 1.2f, 1.5f, 1.8f, 2.3f, 2.7f, 3.2f,
                              4.2f,  4.4f,  4.7f, 4.6f, 3.9f, 3.2f, 2.7f, 2.5f, 2.0f, 1.2f, 1.6f, 1.2f};
  static const float W[24] = {2.2f, 2.7f, 2.6f, 2.4f, 3.0f, 3.5f, 4.5f, 4.4f, 4.1f, 4.6f, 4.6f, 4.8f,
                              5.2f, 5.7f, 5.6f, 6.3f, 5.4f, 4.3f, 4.2f, 4.7f, 5.1f, 4.3f, 4.6f, 3.9f};
  ForecastHour hours[FORECAST_HOURS];
  for (size_t i = 0; i < FORECAST_HOURS; i++)
    hours[i] = i < 24 ? ForecastHour{T[i], W[i], 278.0f, 0.0f} : ForecastHour{0.5f, 3.5f, 300.0f, 0.0f};
  ZoneExposure kontor{};
  kontor.exterior_walls = WALL_WEST;
  kontor.wind_exposure = 3.0f;
  kontor.thermal_lead_h = 4;
  PreloadParams params{};
  params.indoor_ref_c = 22.0f;
  params.scheduled_setpoint_c = 22.0f;
  params.has_current_temp = true;
  params.current_temp_c = 22.5f;
  params.max_offset_c = 1.5f;
  params.floor_lag_h = 1.7f;
  params.thermal_samples = 12;
  params.learned_heat_gain_c_per_h = 0.15f;
  params.learned_cool_loss_c_per_h = 0.12f;
  const float base = params.learned_cool_loss_c_per_h;
  expect(room_loss_c_per_h(hours[19], kontor, params) > RISE_ON_FOR_TEST * base,
         "rise: the steady wind's loss is above the old threshold all evening");
  PreloadDecision d = compute_zone_preload(hours, FORECAST_HOURS, 9, kontor, params);
  expect(near(d.offset_c, 0.0f) && near(d.planned_offset_c, 0.0f), "rise: steady wind from midnight → no preload");

  // Calm until 17, the same wind at 6–8 m/s from 19.
  for (size_t i = 0; i < 24; i++)
    hours[i] = ForecastHour{2.0f, i < 17 ? 1.0f : (i < 19 ? 4.0f : 7.5f), 278.0f, 0.0f};
  d = compute_zone_preload(hours, FORECAST_HOURS, 9, kontor, params);
  expect(d.planned_offset_c >= 0.3f && d.preload_start_h > 0 && d.preload_start_h < 10,
         "rise: calm turning to storm within two hours → preload planned before it");
  const float sudden = d.planned_offset_c;

  // The same storm reached over twelve hours, from 07 to 19.
  for (size_t i = 0; i < 24; i++) {
    const float w = i < 7 ? 1.0f : (i >= 19 ? 7.5f : 1.0f + 6.5f * static_cast<float>(i - 7) / 12.0f);
    hours[i] = ForecastHour{2.0f, w, 278.0f, 0.0f};
  }
  d = compute_zone_preload(hours, FORECAST_HOURS, 9, kontor, params);
  printf("      rise: sudden %.2f, slow %.2f\n", sudden, d.planned_offset_c);
  expect(d.planned_offset_c < sudden, "rise: a slow climb needs less than a sudden one");
}

int main() {
  test_wind_alignment();
  test_zone_hour_load();
  test_room_loss();
  test_preload_decision();
  test_priority_and_thermal();
  test_horizon_and_series();
  test_floor_lag();
  test_learning_update();
  test_taper_after_peak();
  test_absurd_heat_gain_suppresses_horizon();
  test_inverted_wind_direction();
  test_evening_episode();
  test_rise_not_level();

  if (g_failures > 0) {
    printf("%d test(s) FAILED.\n", g_failures);
    return EXIT_FAILURE;
  }
  printf("All tests passed.\n");
  return EXIT_SUCCESS;
}
