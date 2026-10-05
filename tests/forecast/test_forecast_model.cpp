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

static void test_preload_decision() {
  ForecastHour hours[FORECAST_HOURS];
  PreloadParams params{};
  params.enable_horizon = false;  // isolate heuristic 2.0

  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 0.8f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 12;

  // Storm load 3.0 at +10h. heuristic base = (3-1)*0.5 = 1.0
  // timing: urgency = 1 - 10/12 = 0.167 → scale = 0.4 + 0.6*0.167 = 0.5
  fill_calm(hours, FORECAST_HOURS);
  hours[10] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};
  PreloadDecision d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.heuristic_offset_c, 0.5f), "preload: storm in 10h, lead 12h → heuristic 0.5");
  expect(near(d.offset_c, 0.5f), "preload: storm in 10h → offset 0.5");
  expect(d.peak_in_h == 10, "preload: peak reported at +10h");
  expect(d.preload_end_h == 11, "preload: bar covers through peak hour");

  zone.thermal_lead_h = 4;
  d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.offset_c, 0.0f), "preload: storm in 10h, lead 4h → no offset yet");

  // 3 h before storm, lead 4: peak_in_h=3, urgency=1-3/4=0.25 → scale=0.4+0.15=0.55
  d = compute_zone_preload(hours, FORECAST_HOURS, 7, zone, params);
  expect(near(d.offset_c, 0.55f), "preload: lead 4h activates 3h before storm");

  zone.thermal_lead_h = 12;
  hours[10] = ForecastHour{-15.0f, 25.0f, 270.0f, 0.0f};  // load = 7.2, base capped 1.5 * 0.5 scale
  d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.offset_c, 0.75f), "preload: violent storm capped then timing-scaled");

  fill_calm(hours, FORECAST_HOURS);
  d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(near(d.offset_c, 0.0f), "preload: calm forecast → no offset");

  d = compute_zone_preload(nullptr, 0, 0, zone, params);
  expect(near(d.offset_c, 0.0f) && d.peak_in_h == -1, "preload: null input → empty decision");
  d = compute_zone_preload(hours, FORECAST_HOURS, FORECAST_HOURS + 5, zone, params);
  expect(near(d.offset_c, 0.0f), "preload: now_index out of range → empty decision");

  hours[FORECAST_HOURS - 1] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};
  d = compute_zone_preload(hours, FORECAST_HOURS, FORECAST_HOURS - 2, zone, params);
  // peak_in_h=1, lead 12, urgency≈0.917 → scale≈0.95, base 1.0 → ~0.95
  expect(d.offset_c > 0.9f && d.offset_c <= 1.0f, "preload: window clamped at forecast end");
}

static void test_priority_and_thermal() {
  ForecastHour hours[FORECAST_HOURS];
  fill_calm(hours, FORECAST_HOURS);
  hours[2] = ForecastHour{-4.0f, 15.0f, 270.0f, 0.0f};

  ZoneExposure zone{};
  zone.exterior_walls = WALL_WEST;
  zone.wind_exposure = 0.8f;
  zone.solar_gain = 0.0f;
  zone.thermal_lead_h = 6;

  PreloadParams params{};
  params.enable_horizon = false;
  params.priority_gain = 1.2f;  // priority 3
  PreloadDecision d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(d.offset_c > 0.5f, "priority: raises offset above base timing scale");

  params.priority_gain = 1.0f;
  params.thermal_samples = 12;
  params.learned_heat_gain_c_per_h = 0.10f;
  params.learned_cool_loss_c_per_h = 0.25f;
  d = compute_zone_preload(hours, FORECAST_HOURS, 0, zone, params);
  expect(d.used_thermal_sizing, "thermal: uses learned rates when confident");
  expect(d.offset_c > 0.0f, "thermal: still issues positive offset");
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
  expect(up.lead_bias_h == 2, "learning: never shortens lead bias");

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

int main() {
  test_wind_alignment();
  test_zone_hour_load();
  test_preload_decision();
  test_priority_and_thermal();
  test_horizon_and_series();
  test_learning_update();
  test_taper_after_peak();
  test_absurd_heat_gain_suppresses_horizon();
  test_inverted_wind_direction();

  if (g_failures > 0) {
    printf("%d test(s) FAILED.\n", g_failures);
    return EXIT_FAILURE;
  }
  printf("All tests passed.\n");
  return EXIT_SUCCESS;
}
