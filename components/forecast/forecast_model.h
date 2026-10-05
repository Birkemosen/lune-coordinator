// =============================================================================
// HV6 Forecast Model — pure C++, host-testable (no ESP-IDF / ESPHome deps)
// =============================================================================
// Touch firmware builds this file from lune_touch_coordinator/ (ESPHome only
// copies that component). Keep components/forecast/forecast_model.* in sync
// for host tests and the legacy forecast package.
// Wind-direction-aware per-zone preload model with:
//   1) Heuristic 2.0 — timing ramp, taper, optional thermal sizing
//   2) Light plant-horizon — min offset that holds comfort over the lead window
//   3) Closed-loop learning helpers — gain scale / lead bias from outcomes
// Tested by tests/forecast/test_forecast_model.cpp (make test-forecast).
// =============================================================================

#pragma once

#include <cstdint>
#include <cstddef>

namespace hv6fc {

static constexpr size_t FORECAST_HOURS = 48;
static constexpr size_t HORIZON_HOURS = 24;

// Touch-owned N/E/S/W bitmask values. V6 does not store this room geometry.
static constexpr uint8_t WALL_NORTH = 1 << 0;
static constexpr uint8_t WALL_EAST = 1 << 1;
static constexpr uint8_t WALL_SOUTH = 1 << 2;
static constexpr uint8_t WALL_WEST = 1 << 3;

struct ForecastHour {
  float temp_c = 0.0f;
  float wind_speed_ms = 0.0f;
  float wind_dir_deg = 0.0f;   ///< Meteorological: direction the wind comes FROM
  float shortwave_wm2 = 0.0f;  ///< Global horizontal irradiance
};

struct ZoneExposure {
  uint8_t exterior_walls = 0;  ///< Touch-owned N|E|S|W bitmask
  float wind_exposure = 0.5f;  ///< 0..1 — how exposed the facade is (shelter, terrain)
  float solar_gain = 0.3f;     ///< 0..1 — passive solar relief through glazing
  uint8_t thermal_lead_h = 4;  ///< How long before a load peak charging must start
};

struct PreloadParams {
  float indoor_ref_c = 21.0f;     ///< Reference indoor temp for the cold term
  float load_threshold = 1.0f;    ///< Load units before preload kicks in
  float gain_c_per_load = 0.5f;   ///< °C offset per load unit above threshold
  float max_offset_c = 1.5f;      ///< Model cap (per-zone firmware clamps still apply)
  float priority_gain = 1.0f;     ///< Touch: 1 + 0.1×(priority−1)
  float learned_heat_gain_c_per_h = 0.0f;
  float learned_cool_loss_c_per_h = 0.0f;
  uint16_t thermal_samples = 0;
  float learned_gain_scale = 1.0f;  ///< Closed-loop multiplier on gain (clamped 0.5..1.5)
  int8_t lead_bias_h = 0;           ///< Closed-loop lead extension only (0..8)
  float current_temp_c = 21.0f;     ///< Start temp for horizon sim
  float scheduled_setpoint_c = 21.0f;
  float comfort_band_c = 0.5f;      ///< ± around scheduled for horizon / chart band
  bool enable_horizon = true;
  bool has_current_temp = false;
};

struct PreloadDecision {
  float offset_c = 0.0f;          ///< Setpoint offset to apply now (0 = none)
  float peak_load = 0.0f;         ///< Max load found inside the lead window
  int8_t peak_in_h = -1;          ///< Hours until that peak (-1 = no data)
  float timing_scale = 0.0f;      ///< 0..1 ramp toward peak
  float heuristic_offset_c = 0.0f; ///< Offset before horizon refine
  float min_temp_c = 0.0f;        ///< Horizon min predicted temp
  bool horizon_ok = false;        ///< Horizon kept comfort with chosen offset
  bool used_horizon = false;
  bool used_thermal_sizing = false;
  uint8_t active_lead_h = 0;      ///< Lead used (configured + bias)
  uint8_t preload_start_h = 0;    ///< Inclusive hour offset for active preload bar
  uint8_t preload_end_h = 0;      ///< Exclusive hour offset for active preload bar
};

/// Optional chart series filled by fill_preload_series (not stored on ESP decisions).
struct PreloadSeries {
  uint8_t count = 0;
  float expected_temp_c[HORIZON_HOURS]{};
  float scheduled_setpoint_c[HORIZON_HOURS]{};
  float comfort_min_c[HORIZON_HOURS]{};
  float comfort_max_c[HORIZON_HOURS]{};
  bool preload_active[HORIZON_HOURS]{};
};

struct PreloadOutcome {
  float undershoot_c = 0.0f;  ///< How far below comfort_min during event
  float overshoot_c = 0.0f;   ///< How far above comfort_max
  bool preload_was_active = false;
};

struct LearningUpdate {
  float gain_scale = 1.0f;
  int8_t lead_bias_h = 0;
};

/// Max over the zone's exterior walls of cos(angle between the wind source
/// direction and the wall's outward normal), floored at 0. A zone with no
/// exterior walls returns 0 (interior room — no direct wind load).
float wind_alignment(uint8_t exterior_walls, float wind_dir_deg);

/// Dimensionless weather load on a zone for one forecast hour.
/// wind term (10 m/s reference) × cold term (10 K ΔT reference) − solar relief
/// (800 W/m² reference), floored at 0.
float zone_hour_load(const ForecastHour &hour, const ZoneExposure &exposure, float indoor_ref_c);

/// Effective lead hours: configured thermal_lead_h plus closed-loop bias (never below configured).
uint8_t active_thermal_lead_h(const ZoneExposure &exposure, const PreloadParams &params);

/// Scan the active lead window and decide the preload offset for this zone.
PreloadDecision compute_zone_preload(const ForecastHour *hours, size_t count, size_t now_index,
                                     const ZoneExposure &exposure, const PreloadParams &params);

/// Fill up to HORIZON_HOURS of chart series from now_index using the decision's offset.
void fill_preload_series(const ForecastHour *hours, size_t count, size_t now_index,
                         const ZoneExposure &exposure, const PreloadParams &params,
                         const PreloadDecision &decision, PreloadSeries *out);

/// Closed-loop update from a completed preload / storm outcome. Never shortens lead below 0 bias.
LearningUpdate update_preload_learning(float current_gain_scale, int8_t current_lead_bias_h,
                                       const PreloadOutcome &outcome);

}  // namespace hv6fc
