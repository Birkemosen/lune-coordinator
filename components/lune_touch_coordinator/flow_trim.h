#pragma once

// Valve-saturation Setpoint Bias trim. Pure C++ — host-testable.
// Rate: ≤ 0.1 °C / 30 min, clamp ±1.0 °C. Freeze during DHW / Legionella /
// heat-off plan hours / defrost / degraded house signal / forecast offset on
// the critical zone. Unconfigured freeze inputs fail closed (hold).

#include "coordinator_model.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace esphome::lune_touch_coordinator::flow_trim {

static constexpr float STEP_C = 0.1f;
static constexpr float MAX_ABS_BIAS_C = 1.0f;
static constexpr uint32_t STEP_INTERVAL_MS = 30UL * 60UL * 1000UL;

enum class Mode : uint8_t { OFF = 0, SHADOW = 1, ACTIVE = 2 };

inline const char *mode_name(Mode mode) {
  switch (mode) {
    case Mode::ACTIVE: return "active";
    case Mode::SHADOW: return "shadow";
    case Mode::OFF:
    default: return "off";
  }
}

inline Mode mode_from_name(const char *name) {
  if (name == nullptr) return Mode::OFF;
  if (name[0] == 'a' || name[0] == 'A') return Mode::ACTIVE;
  if (name[0] == 's' || name[0] == 'S') return Mode::SHADOW;
  return Mode::OFF;
}

enum class FreezeReason : uint8_t {
  NONE = 0,
  MODE_OFF,
  DHW,
  LEGIONELLA,
  HEAT_OFF,
  DEFROST,
  DEGRADED_QUALITY,
  FORECAST_CRITICAL,
  FREEZE_INPUT_UNKNOWN,
};

inline const char *freeze_reason_name(FreezeReason reason) {
  switch (reason) {
    case FreezeReason::MODE_OFF: return "mode_off";
    case FreezeReason::DHW: return "dhw";
    case FreezeReason::LEGIONELLA: return "legionella";
    case FreezeReason::HEAT_OFF: return "heat_off";
    case FreezeReason::DEFROST: return "defrost";
    case FreezeReason::DEGRADED_QUALITY: return "degraded_quality";
    case FreezeReason::FORECAST_CRITICAL: return "forecast_critical";
    case FreezeReason::FREEZE_INPUT_UNKNOWN: return "freeze_input_unknown";
    case FreezeReason::NONE:
    default: return "none";
  }
}

struct FreezeInputs {
  // Tri-state: known_true / known_false / unknown. Unknown → freeze.
  enum class Tri : int8_t { UNKNOWN = -1, FALSE = 0, TRUE = 1 };
  Tri dhw{Tri::UNKNOWN};
  Tri legionella{Tri::UNKNOWN};
  Tri defrost{Tri::UNKNOWN};
  bool odin_plan_available{false};
  bool odin_heat_off{false};  // current hour OFF; only considered when plan available
  bool house_quality_healthy{false};
  bool forecast_offset_on_critical{false};
};

struct HouseDemand {
  ::lune_touch::HeatRecommendation recommendation{::lune_touch::HeatRecommendation::HOLD};
  bool any_fresh{false};
  size_t contributing_rooms{0};
};

struct StepResult {
  float target_bias_c{0.0f};
  bool stepped{false};
  bool frozen{false};
  FreezeReason freeze_reason{FreezeReason::NONE};
  float delta_c{0.0f};
};

struct Controller {
  Mode mode{Mode::SHADOW};
  float bias_c{0.0f};
  float last_confirmed_bias_c{0.0f};
  uint32_t last_step_ms{0};
  bool has_confirmed{false};

  void reseed_from_readback(float asgard_bias_c) {
    if (!std::isfinite(asgard_bias_c))
      return;
    if (asgard_bias_c > MAX_ABS_BIAS_C)
      asgard_bias_c = MAX_ABS_BIAS_C;
    if (asgard_bias_c < -MAX_ABS_BIAS_C)
      asgard_bias_c = -MAX_ABS_BIAS_C;
    bias_c = asgard_bias_c;
    last_confirmed_bias_c = asgard_bias_c;
    has_confirmed = true;
  }

  static FreezeReason evaluate_freeze(Mode mode, const FreezeInputs &freeze) {
    if (mode == Mode::OFF)
      return FreezeReason::MODE_OFF;
    if (freeze.dhw == FreezeInputs::Tri::UNKNOWN ||
        freeze.legionella == FreezeInputs::Tri::UNKNOWN ||
        freeze.defrost == FreezeInputs::Tri::UNKNOWN)
      return FreezeReason::FREEZE_INPUT_UNKNOWN;
    if (freeze.dhw == FreezeInputs::Tri::TRUE)
      return FreezeReason::DHW;
    if (freeze.legionella == FreezeInputs::Tri::TRUE)
      return FreezeReason::LEGIONELLA;
    if (freeze.defrost == FreezeInputs::Tri::TRUE)
      return FreezeReason::DEFROST;
    if (freeze.odin_plan_available && freeze.odin_heat_off)
      return FreezeReason::HEAT_OFF;
    if (!freeze.house_quality_healthy)
      return FreezeReason::DEGRADED_QUALITY;
    if (freeze.forecast_offset_on_critical)
      return FreezeReason::FORECAST_CRITICAL;
    return FreezeReason::NONE;
  }

  // Aggregate room recommendations: any RAISE → raise; all LOWER (with fresh) → lower; else hold.
  static ::lune_touch::HeatRecommendation aggregate_rooms(
      const ::lune_touch::HeatRecommendation *room_recs, const bool *fresh, size_t count) {
    if (room_recs == nullptr || count == 0)
      return ::lune_touch::HeatRecommendation::HOLD;
    bool any_raise = false;
    bool any_fresh = false;
    bool all_lower = true;
    size_t fresh_count = 0;
    for (size_t i = 0; i < count; i++) {
      if (fresh != nullptr && !fresh[i])
        continue;
      any_fresh = true;
      fresh_count++;
      if (room_recs[i] == ::lune_touch::HeatRecommendation::RAISE)
        any_raise = true;
      if (room_recs[i] != ::lune_touch::HeatRecommendation::LOWER)
        all_lower = false;
    }
    if (!any_fresh)
      return ::lune_touch::HeatRecommendation::HOLD;
    if (any_raise)
      return ::lune_touch::HeatRecommendation::RAISE;
    if (all_lower && fresh_count > 0)
      return ::lune_touch::HeatRecommendation::LOWER;
    return ::lune_touch::HeatRecommendation::HOLD;
  }

  StepResult step(const HouseDemand &demand, const FreezeInputs &freeze, uint32_t now_ms) {
    StepResult result;
    result.target_bias_c = bias_c;
    const FreezeReason reason = evaluate_freeze(mode, freeze);
    if (reason != FreezeReason::NONE) {
      result.frozen = true;
      result.freeze_reason = reason;
      return result;
    }
    if (!demand.any_fresh || demand.contributing_rooms == 0)
      return result;

    float delta = 0.0f;
    if (demand.recommendation == ::lune_touch::HeatRecommendation::RAISE)
      delta = STEP_C;
    else if (demand.recommendation == ::lune_touch::HeatRecommendation::LOWER)
      delta = -STEP_C;
    if (delta == 0.0f)
      return result;

    if (last_step_ms != 0 && (now_ms - last_step_ms) < STEP_INTERVAL_MS)
      return result;

    float next = bias_c + delta;
    if (next > MAX_ABS_BIAS_C)
      next = MAX_ABS_BIAS_C;
    if (next < -MAX_ABS_BIAS_C)
      next = -MAX_ABS_BIAS_C;
    if (std::fabs(next - bias_c) < 0.001f)
      return result;

    bias_c = next;
    last_step_ms = now_ms;
    result.target_bias_c = bias_c;
    result.stepped = true;
    result.delta_c = delta;
    return result;
  }

  // Shadow mode computes the same step but callers must not write to Asgard.
  bool should_write_asgard() const { return mode == Mode::ACTIVE; }
};

}  // namespace esphome::lune_touch_coordinator::flow_trim
