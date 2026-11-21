#include "forecast_model.h"

#include <cmath>

namespace hv6fc {

static constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
static constexpr float WIND_REF_MS = 10.0f;     // stiff breeze
static constexpr float COLD_REF_K = 10.0f;      // ΔT giving cold term 1.0
static constexpr float SOLAR_REF_WM2 = 800.0f;  // clear-sky midday irradiance
static constexpr float DEFAULT_HEAT_GAIN = 0.18f;
static constexpr float DEFAULT_COOL_LOSS = 0.12f;
static constexpr uint16_t THERMAL_CONFIDENCE_SAMPLES = 6;
// Concrete-slab floor heating: τ ≈ 8–80 h ⇒ heat gain roughly 1/τ ≈ 0.0125–0.125 °C/h
// under unit driving; allow a modest band for weather-scaled learning.
static constexpr float MAX_PLAUSIBLE_HEAT_GAIN = 0.35f;
static constexpr float MIN_PLAUSIBLE_HEAT_GAIN = 0.05f;
static constexpr float MAX_PLAUSIBLE_COOL_LOSS = 0.40f;
static constexpr float MIN_PLAUSIBLE_COOL_LOSS = 0.02f;
static constexpr float MAX_FLOOR_LAG_H = 5.0f;
// Hours below the threshold that still belong to the same weather episode.
static constexpr size_t EPISODE_GAP_H = 2;
// Calibrated loss (room_loss_c_per_h): the conditions a room's cooling rate is learned in,
// on average (November: 21 °C inside, about 6 °C out, 4 m/s at an angle), and the slab
// charge's wind factor (slab_charge.h WIND_LOSS_FACTOR), which learned exposures are on.
static constexpr float TYPICAL_DT_K = 15.0f;
static constexpr float TYPICAL_WIND_MS = 4.0f;
static constexpr float TYPICAL_ALIGN = 0.5f;
static constexpr float WIND_LOSS_FACTOR = 0.6f;
// A preload episode: loss this many times the room's usual cooling, or above its floor,
// reached by a rise of at least (RISE_ON − 1) × the usual cooling within the floor's
// response window (RISE_WINDOW_MIN..MAX hours, twice the floor lag and an hour).
static constexpr float RISE_ON = 1.5f;
static constexpr int RISE_WINDOW_MIN_H = 3;
static constexpr int RISE_WINDOW_MAX_H = 6;
// Smallest offset worth sending: V6 must notice it and call.
static constexpr float MIN_OFFSET_C = 0.3f;

// <algorithm> is avoided so the file compiles with the host toolchain too.
static inline float fmaxf_(float a, float b) { return a > b ? a : b; }
static inline float fminf_(float a, float b) { return a < b ? a : b; }
static inline float clampf_(float v, float lo, float hi) { return fminf_(hi, fmaxf_(lo, v)); }
static inline size_t min_size_(size_t a, size_t b) { return a < b ? a : b; }
static inline bool finite_(float v) { return std::isfinite(v); }

float wind_alignment(uint8_t exterior_walls, float wind_dir_deg) {
  struct Wall {
    uint8_t bit;
    float normal_deg;
  };
  static constexpr Wall WALLS[] = {
      {WALL_NORTH, 0.0f}, {WALL_EAST, 90.0f}, {WALL_SOUTH, 180.0f}, {WALL_WEST, 270.0f}};

  float best = 0.0f;
  for (const Wall &wall : WALLS) {
    if (!(exterior_walls & wall.bit))
      continue;
    const float delta = (wind_dir_deg - wall.normal_deg) * DEG_TO_RAD;
    best = fmaxf_(best, std::cos(delta));
  }
  return fmaxf_(0.0f, best);
}

float zone_hour_load(const ForecastHour &hour, const ZoneExposure &exposure, float indoor_ref_c) {
  const float align = wind_alignment(exposure.exterior_walls, hour.wind_dir_deg);
  const float wind_term = exposure.wind_exposure * align * (hour.wind_speed_ms / WIND_REF_MS);
  const float cold_term = fmaxf_(0.0f, indoor_ref_c - hour.temp_c) / COLD_REF_K;
  const float solar_relief = exposure.solar_gain * (fmaxf_(0.0f, hour.shortwave_wm2) / SOLAR_REF_WM2);
  return fmaxf_(0.0f, wind_term * cold_term - solar_relief);
}

uint8_t active_thermal_lead_h(const ZoneExposure &exposure, const PreloadParams &params) {
  const uint8_t configured = exposure.thermal_lead_h == 0 ? 4 : exposure.thermal_lead_h;
  int lead = static_cast<int>(configured) + static_cast<int>(params.lead_bias_h);
  if (lead < static_cast<int>(configured))
    lead = static_cast<int>(configured);
  if (lead > 24)
    lead = 24;
  return static_cast<uint8_t>(lead);
}

static bool thermal_confident_(const PreloadParams &params) {
  // Absurd gains (probe jumps / short Δt) must not drive plant-horizon: they make
  // every candidate offset look sufficient and collapse preload to zero.
  return params.thermal_samples >= THERMAL_CONFIDENCE_SAMPLES &&
         finite_(params.learned_heat_gain_c_per_h) &&
         params.learned_heat_gain_c_per_h >= MIN_PLAUSIBLE_HEAT_GAIN &&
         params.learned_heat_gain_c_per_h <= MAX_PLAUSIBLE_HEAT_GAIN &&
         finite_(params.learned_cool_loss_c_per_h) &&
         params.learned_cool_loss_c_per_h >= MIN_PLAUSIBLE_COOL_LOSS &&
         params.learned_cool_loss_c_per_h <= MAX_PLAUSIBLE_COOL_LOSS;
}

static float lag_h_(const PreloadParams &params) {
  return finite_(params.floor_lag_h) ? clampf_(params.floor_lag_h, 0.0f, MAX_FLOOR_LAG_H) : 0.0f;
}

static float heat_gain_rate_(const PreloadParams &params) {
  if (thermal_confident_(params))
    return params.learned_heat_gain_c_per_h;
  return DEFAULT_HEAT_GAIN;
}

static float cool_loss_rate_(const PreloadParams &params) {
  if (thermal_confident_(params))
    return params.learned_cool_loss_c_per_h;
  return DEFAULT_COOL_LOSS;
}

float room_loss_c_per_h(const ForecastHour &hour, const ZoneExposure &exposure, const PreloadParams &params) {
  // The room's cooling rate is learned with the valve shut in whatever weather came
  // (TYPICAL_DT_K / TYPICAL_WIND_MS); scale it to this hour's temperature difference
  // and wind on the walls (the slab charge's wind factor, which the learned exposure
  // is on), less what the sun gives.
  const float dt = fmaxf_(0.0f, params.indoor_ref_c - hour.temp_c) / TYPICAL_DT_K;
  const float exposure_k = WIND_LOSS_FACTOR * fmaxf_(0.0f, exposure.wind_exposure);
  const float wind = 1.0f + exposure_k * wind_alignment(exposure.exterior_walls, hour.wind_dir_deg) *
                                fmaxf_(0.0f, hour.wind_speed_ms) / WIND_REF_MS;
  const float typical = 1.0f + exposure_k * TYPICAL_ALIGN * TYPICAL_WIND_MS / WIND_REF_MS;
  const float sun = fminf_(0.8f, clampf_(exposure.solar_gain, 0.0f, 1.0f) * fmaxf_(0.0f, hour.shortwave_wm2) / SOLAR_REF_WM2);
  return cool_loss_rate_(params) * dt * wind / typical * (1.0f - sun);
}

// Expected room temperature with V6 holding setpoint + offset (the offset felt after the
// floor lag): heating at its learned capacity (the net rise it was learned at plus the
// cooling it overcame) against this hour's loss; shut, it cools by the loss.
static float simulate_min_temp_(const ForecastHour *hours, size_t now_index, size_t last,
                                const ZoneExposure &exposure, const PreloadParams &params,
                                const PreloadDecision &decision, float *out_series, size_t series_cap,
                                uint8_t *out_count) {
  float temp = params.has_current_temp && finite_(params.current_temp_c) ? params.current_temp_c
                                                                         : params.scheduled_setpoint_c;
  const float capacity = heat_gain_rate_(params) + cool_loss_rate_(params);
  const float lag = lag_h_(params);
  float min_temp = temp;
  size_t written = 0;
  for (size_t i = now_index; i <= last; i++) {
    if (out_series != nullptr && written < series_cap)
      out_series[written++] = temp;
    const size_t k = i - now_index;
    const bool lifted = decision.offset_c > 0.01f && k >= decision.preload_start_h &&
                        k < decision.preload_end_h && static_cast<float>(k) + 1.0f > lag;
    const float target = params.scheduled_setpoint_c + (lifted ? decision.offset_c : 0.0f);
    const float loss = room_loss_c_per_h(hours[i], exposure, params);
    if (temp < target - 0.05f)
      temp = capacity > loss ? fminf_(target, temp + capacity - loss) : temp + capacity - loss;
    else
      temp -= loss;
    temp = clampf_(temp, params.scheduled_setpoint_c - 6.0f, params.scheduled_setpoint_c + 4.0f);
    min_temp = fminf_(min_temp, temp);
  }
  if (out_count)
    *out_count = static_cast<uint8_t>(written);
  return min_temp;
}

PreloadDecision compute_zone_preload(const ForecastHour *hours, size_t count, size_t now_index,
                                     const ZoneExposure &exposure, const PreloadParams &params) {
  PreloadDecision decision;
  if (hours == nullptr || count == 0 || now_index >= count)
    return decision;

  const uint8_t lead = active_thermal_lead_h(exposure, params);
  decision.active_lead_h = lead;
  decision.used_thermal_sizing = thermal_confident_(params);
  const size_t horizon_last = min_size_(count - 1, now_index + HORIZON_HOURS - 1);
  const float base = cool_loss_rate_(params);
  const float capacity = heat_gain_rate_(params) + base;
  // The goal is not to raise the room but to keep it from falling behind when the loss
  // climbs (2026-11-18: the model stored 1.5 K from 07 for wind at 22, while the room only
  // fell from 18 because no heat came until 21:30, cooling 0.3 K/h as this loss says).
  // An episode is the hours the loss is RISE_ON × what the room usually loses, or more
  // than its floor gives; a dip of up to EPISODE_GAP_H hours does not end it.
  // It starts only where the loss climbs fast: by (RISE_ON − 1) × the usual cooling above
  // the lowest loss of the hours before it, within twice the floor lag and an hour
  // (2026-11-21: a steady 4–6 m/s west wind at 0–3 °C held the loss at ~1.8 × usual for a
  // day and preheated six rooms; the floors keep up with wind that stays, it is calm turning
  // to storm within a few hours they cannot follow). The hours before now come from the
  // forecast's past hours; without them the usual cooling is the reference, as before.
  const auto wants = [&](float loss) { return loss > RISE_ON * base || loss > capacity; };
  const int window = static_cast<int>(
      clampf_(std::round(2.0f * (lag_h_(params) + 1.0f)), static_cast<float>(RISE_WINDOW_MIN_H),
              static_cast<float>(RISE_WINDOW_MAX_H)));
  const auto before = [&](size_t i) {
    float lo = INFINITY;
    for (int k = 1; k <= window && static_cast<int>(i) - k >= 0; k++)
      lo = fminf_(lo, room_loss_c_per_h(hours[i - k], exposure, params));
    return finite_(lo) ? lo : base;
  };
  size_t onset = count, ep_end = count, last_on = count;
  float peak_loss = 0.0f, deficit = 0.0f, ref = base;
  for (size_t i = now_index; i <= horizon_last; i++) {
    const float loss = room_loss_c_per_h(hours[i], exposure, params);
    if (!wants(loss)) {
      if (onset != count && i - last_on > EPISODE_GAP_H)
        break;
      continue;
    }
    if (onset == count) {
      const float prior = before(i);
      if (loss - prior <= (RISE_ON - 1.0f) * base)
        continue;   // the loss is high but has been for a while: the floor is on it
      onset = i;
      ref = fmaxf_(base, prior);
    }
    last_on = i;
    ep_end = i + 1;
    deficit += fmaxf_(0.0f, loss - fmaxf_(capacity, ref));
    if (loss > peak_loss) {
      peak_loss = loss;
      decision.peak_in_h = static_cast<int8_t>(i - now_index);
    }
  }
  // The weather load (zone_hour_load) at the peak, as before, for the API and the charts.
  if (onset != count) {
    decision.peak_load = zone_hour_load(hours[now_index + decision.peak_in_h], exposure, params.indoor_ref_c);
  } else {
    for (size_t i = now_index; i <= min_size_(horizon_last, now_index + lead); i++) {
      const float load = zone_hour_load(hours[i], exposure, params.indoor_ref_c);
      if (load > decision.peak_load) {
        decision.peak_load = load;
        decision.peak_in_h = static_cast<int8_t>(i - now_index);
      }
    }
    if (decision.peak_in_h < 0)
      decision.peak_in_h = 0;
  }

  if (onset != count) {
    // Ahead by what the room loses beyond its usual while the floor answers (the lag and
    // an hour to get the heat in), plus what the floor cannot give at all.
    const float lag = lag_h_(params);
    const float gain_scale = clampf_(params.learned_gain_scale, 0.5f, 1.5f);
    const float priority = fmaxf_(0.5f, params.priority_gain);
    float offset = ((peak_loss - ref) * (lag + 1.0f) + deficit) * gain_scale * priority;
    offset = clampf_(offset, MIN_OFFSET_C, params.max_offset_c);
    decision.heuristic_offset_c = offset;
    decision.planned_offset_c = offset;
    // From the floor lag and an hour before the loss climbs, never more than the lead.
    const int onset_in_h = static_cast<int>(onset - now_index);
    const int ahead = static_cast<int>(std::round(lag)) + 1;
    const int start_in_h = onset_in_h > ahead ? onset_in_h - (ahead < lead ? ahead : lead) : 0;
    decision.preload_start_h = static_cast<uint8_t>(start_in_h);
    decision.preload_end_h = static_cast<uint8_t>(
        fminf_(static_cast<float>(HORIZON_HOURS), static_cast<float>(ep_end - now_index)));
    if (start_in_h == 0) {
      decision.offset_c = offset;
      decision.timing_scale = 1.0f;
    }
  }

  const size_t last = onset != count ? min_size_(horizon_last, ep_end) : min_size_(horizon_last, now_index + lead);
  decision.used_horizon = true;
  decision.min_temp_c = simulate_min_temp_(hours, now_index, last, exposure, params, decision, nullptr, 0, nullptr);
  decision.horizon_ok = decision.min_temp_c >= params.scheduled_setpoint_c - fmaxf_(0.1f, params.comfort_band_c) - 0.05f;
  return decision;
}

void fill_preload_series(const ForecastHour *hours, size_t count, size_t now_index,
                         const ZoneExposure &exposure, const PreloadParams &params,
                         const PreloadDecision &decision, PreloadSeries *out) {
  if (out == nullptr)
    return;
  *out = PreloadSeries{};
  if (hours == nullptr || count == 0 || now_index >= count)
    return;

  const size_t horizon = min_size_(HORIZON_HOURS, count - now_index);
  const size_t last = min_size_(count - 1, now_index + horizon - 1);
  const float band = fmaxf_(0.1f, params.comfort_band_c);
  const float scheduled = params.scheduled_setpoint_c;

  uint8_t sim_count = 0;
  simulate_min_temp_(hours, now_index, last, exposure, params, decision, out->expected_temp_c, HORIZON_HOURS,
                     &sim_count);
  out->count = sim_count;

  for (uint8_t i = 0; i < out->count; i++) {
    out->scheduled_setpoint_c[i] = scheduled;
    out->comfort_min_c[i] = scheduled - band;
    out->comfort_max_c[i] = scheduled + band;
    out->preload_active[i] =
        decision.offset_c > 0.01f && i >= decision.preload_start_h && i < decision.preload_end_h;
  }
}

LearningUpdate update_preload_learning(float current_gain_scale, int8_t current_lead_bias_h,
                                       const PreloadOutcome &outcome) {
  LearningUpdate update;
  update.gain_scale = clampf_(current_gain_scale, 0.5f, 1.5f);
  update.lead_bias_h = current_lead_bias_h < 0 ? 0 : current_lead_bias_h;
  if (update.lead_bias_h > 8)
    update.lead_bias_h = 8;

  if (!outcome.preload_was_active)
    return update;

  // Undershoot → raise gain / extend lead slightly; overshoot → ease gain.
  if (outcome.undershoot_c > 0.15f) {
    update.gain_scale = clampf_(update.gain_scale + 0.05f, 0.5f, 1.5f);
    if (outcome.undershoot_c > 0.4f && update.lead_bias_h < 8)
      update.lead_bias_h = static_cast<int8_t>(update.lead_bias_h + 1);
  } else if (outcome.overshoot_c > 0.35f) {
    update.gain_scale = clampf_(update.gain_scale - 0.04f, 0.5f, 1.5f);
    // ...and shorten a lead that came too early (it could only ever grow).
    if (outcome.overshoot_c > 0.6f && update.lead_bias_h > 0)
      update.lead_bias_h = static_cast<int8_t>(update.lead_bias_h - 1);
  }
  return update;
}

}  // namespace hv6fc
