#include "forecast_model.h"

#include <cmath>

namespace hv6fc {

static constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
static constexpr float WIND_REF_MS = 10.0f;     // stiff breeze
static constexpr float COLD_REF_K = 10.0f;      // ΔT giving cold term 1.0
static constexpr float SOLAR_REF_WM2 = 800.0f;  // clear-sky midday irradiance
static constexpr float TIMING_FLOOR = 0.40f;    // minimum scale once threshold crossed
static constexpr float DEFAULT_HEAT_GAIN = 0.18f;
static constexpr float DEFAULT_COOL_LOSS = 0.12f;
static constexpr uint16_t THERMAL_CONFIDENCE_SAMPLES = 6;
// Concrete-slab floor heating: τ ≈ 8–80 h ⇒ heat gain roughly 1/τ ≈ 0.0125–0.125 °C/h
// under unit driving; allow a modest band for weather-scaled learning.
static constexpr float MAX_PLAUSIBLE_HEAT_GAIN = 0.35f;
static constexpr float MIN_PLAUSIBLE_HEAT_GAIN = 0.05f;
static constexpr float MAX_PLAUSIBLE_COOL_LOSS = 0.40f;
static constexpr float MIN_PLAUSIBLE_COOL_LOSS = 0.02f;

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

static float timing_scale_for_(int8_t peak_in_h, uint8_t lead_h) {
  if (lead_h == 0)
    return 1.0f;
  // Near peak → full; far edge of window → floor. urgency = 1 at peak, 0 at lead edge.
  const float urgency = clampf_(1.0f - static_cast<float>(peak_in_h) / static_cast<float>(lead_h), 0.0f,
                                1.0f);
  return TIMING_FLOOR + (1.0f - TIMING_FLOOR) * urgency;
}

static float taper_scale_(const ForecastHour *hours, size_t /*count*/, size_t now_index,
                          const ZoneExposure &exposure, const PreloadParams &params, float peak_load,
                          int8_t peak_in_h) {
  if (peak_in_h > 0 || peak_load <= params.load_threshold)
    return 1.0f;
  // At/after peak: reduce offset as current load falls below peak.
  const float now_load = zone_hour_load(hours[now_index], exposure, params.indoor_ref_c);
  if (peak_load <= 0.01f)
    return 0.0f;
  return clampf_(now_load / peak_load, 0.0f, 1.0f);
}

static float heuristic_offset_(float peak_load, float timing_scale, float taper,
                               const PreloadParams &params, bool *used_thermal) {
  const float above = peak_load - params.load_threshold;
  if (above <= 0.0f)
    return 0.0f;

  const float gain_scale = clampf_(params.learned_gain_scale, 0.5f, 1.5f);
  const float priority = fmaxf_(0.5f, params.priority_gain);
  float offset = above * params.gain_c_per_load * priority * gain_scale;

  if (thermal_confident_(params)) {
    // Size from cool-loss vs heat-gain: how many °C of boost to counter extra weather loss.
    const float severity = above;
    const float thermal = (params.learned_cool_loss_c_per_h / params.learned_heat_gain_c_per_h) *
                          severity * 0.5f;
    offset = fmaxf_(offset, thermal);
    if (used_thermal)
      *used_thermal = true;
  }

  // Cap the full-urgency offset first, then apply timing/taper so early-window
  // preloads stay below max_boost rather than saturating immediately.
  offset = fminf_(params.max_offset_c, fmaxf_(0.0f, offset));
  return offset * timing_scale * taper;
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

static float simulate_min_temp_(const ForecastHour *hours, size_t /*count*/, size_t now_index,
                                size_t last, const ZoneExposure &exposure,
                                const PreloadParams &params, float offset_c, float *out_series,
                                size_t series_cap, uint8_t *out_count) {
  float temp = params.has_current_temp && finite_(params.current_temp_c) ? params.current_temp_c
                                                                         : params.scheduled_setpoint_c;
  const float heat = heat_gain_rate_(params);
  const float cool = cool_loss_rate_(params);
  const float target = params.scheduled_setpoint_c + offset_c;
  const float temp_lo = fminf_(params.scheduled_setpoint_c - 4.0f, 5.0f);
  const float temp_hi = params.scheduled_setpoint_c + fmaxf_(params.max_offset_c, 1.5f) + 2.0f;
  float min_temp = temp;
  size_t written = 0;

  for (size_t i = now_index; i <= last; i++) {
    if (out_series != nullptr && written < series_cap)
      out_series[written++] = temp;
    const float outdoor = hours[i].temp_c;
    const float load = zone_hour_load(hours[i], exposure, params.indoor_ref_c);
    const float calling = temp < target - 0.05f ? 1.0f : 0.0f;
    if (calling > 0.0f) {
      // Calling: heat gain minus weather-scaled loss (capped so one hour stays physical).
      temp += heat - cool * fminf_(2.0f, fmaxf_(0.2f, load));
    } else {
      // Idle: coast slowly toward outdoor — never unbounded free-fall.
      const float drift = clampf_((outdoor - temp) * 0.04f, -0.12f, 0.06f);
      temp += drift;
    }
    temp = clampf_(temp, temp_lo, temp_hi);
    if (temp < min_temp)
      min_temp = temp;
  }
  if (out_count)
    *out_count = static_cast<uint8_t>(written);
  return min_temp;
}

static float refine_with_horizon_(const ForecastHour *hours, size_t count, size_t now_index,
                                  size_t last, const ZoneExposure &exposure,
                                  const PreloadParams &params, float heuristic_offset,
                                  PreloadDecision *decision) {
  if (!params.enable_horizon || heuristic_offset <= 0.01f) {
    decision->used_horizon = false;
    decision->horizon_ok = heuristic_offset <= 0.01f;
    decision->min_temp_c =
        params.has_current_temp ? params.current_temp_c : params.scheduled_setpoint_c;
    return heuristic_offset;
  }

  const float band = fmaxf_(0.1f, params.comfort_band_c);
  const float floor_temp = params.scheduled_setpoint_c - band;
  const float step = fmaxf_(0.25f, params.max_offset_c / 6.0f);

  float best = heuristic_offset;
  bool found = false;
  for (float cand = 0.0f; cand <= params.max_offset_c + 0.001f; cand += step) {
    const float use = fminf_(params.max_offset_c, cand);
    const float min_t =
        simulate_min_temp_(hours, count, now_index, last, exposure, params, use, nullptr, 0, nullptr);
    if (min_t >= floor_temp - 0.05f) {
      best = use;
      decision->min_temp_c = min_t;
      decision->horizon_ok = true;
      found = true;
      break;
    }
    decision->min_temp_c = min_t;
  }
  if (!found) {
    best = fminf_(params.max_offset_c, fmaxf_(heuristic_offset, params.max_offset_c));
    decision->horizon_ok = false;
    decision->min_temp_c =
        simulate_min_temp_(hours, count, now_index, last, exposure, params, best, nullptr, 0, nullptr);
  }
  decision->used_horizon = true;
  return best;
}

PreloadDecision compute_zone_preload(const ForecastHour *hours, size_t count, size_t now_index,
                                     const ZoneExposure &exposure, const PreloadParams &params) {
  PreloadDecision decision;
  if (hours == nullptr || count == 0 || now_index >= count)
    return decision;

  const uint8_t lead = active_thermal_lead_h(exposure, params);
  decision.active_lead_h = lead;
  const size_t last = min_size_(count - 1, now_index + static_cast<size_t>(lead));

  for (size_t i = now_index; i <= last; i++) {
    const float load = zone_hour_load(hours[i], exposure, params.indoor_ref_c);
    if (load > decision.peak_load) {
      decision.peak_load = load;
      decision.peak_in_h = static_cast<int8_t>(i - now_index);
    }
  }
  if (decision.peak_in_h < 0)
    decision.peak_in_h = 0;

  bool used_thermal = false;
  decision.timing_scale = timing_scale_for_(decision.peak_in_h, lead);
  const float taper =
      taper_scale_(hours, count, now_index, exposure, params, decision.peak_load, decision.peak_in_h);
  decision.heuristic_offset_c =
      heuristic_offset_(decision.peak_load, decision.timing_scale, taper, params, &used_thermal);
  decision.used_thermal_sizing = used_thermal;

  decision.offset_c = refine_with_horizon_(hours, count, now_index, last, exposure, params,
                                           decision.heuristic_offset_c, &decision);

  if (decision.offset_c > 0.01f) {
    // Preload bar: from now through peak (inclusive), at least 1 h when active.
    decision.preload_start_h = 0;
    decision.preload_end_h =
        static_cast<uint8_t>(fmaxf_(1.0f, static_cast<float>(decision.peak_in_h) + 1.0f));
    if (decision.preload_end_h > lead)
      decision.preload_end_h = lead;
    if (decision.preload_end_h == 0)
      decision.preload_end_h = 1;
  }
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

  const uint8_t lead = decision.active_lead_h > 0 ? decision.active_lead_h
                                                  : active_thermal_lead_h(exposure, params);
  const size_t horizon = min_size_(HORIZON_HOURS, count - now_index);
  const size_t last = min_size_(count - 1, now_index + horizon - 1);
  const float band = fmaxf_(0.1f, params.comfort_band_c);
  const float scheduled = params.scheduled_setpoint_c;

  uint8_t sim_count = 0;
  simulate_min_temp_(hours, count, now_index, last, exposure, params, decision.offset_c,
                     out->expected_temp_c, HORIZON_HOURS, &sim_count);
  out->count = sim_count;

  for (uint8_t i = 0; i < out->count; i++) {
    out->scheduled_setpoint_c[i] = scheduled;
    out->comfort_min_c[i] = scheduled - band;
    out->comfort_max_c[i] = scheduled + band;
    out->preload_active[i] =
        decision.offset_c > 0.01f && i >= decision.preload_start_h && i < decision.preload_end_h;
  }
  (void)exposure;
  (void)lead;
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
  }
  return update;
}

}  // namespace hv6fc
