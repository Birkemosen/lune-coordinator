#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome::lune_touch_coordinator::odin_plan {

// Asgard's approved ODIN route publishes the prior/current day plus its forecast.
static constexpr size_t HORIZON_HOURS = 72;

// These are the only operating-mode values documented by ODIN's optimizer
// source. The plan is advisory only: none of these values may create a Touch
// room command, change physical-temperature aggregation, or infer defrost.
enum class OperationMode : uint8_t { OFF, DHW_ON, HEAT_ON, COOL_ON, UNAVAILABLE };

inline OperationMode to_operation_mode(int raw) {
  switch (raw) {
    case 1: return OperationMode::DHW_ON;
    case 2: return OperationMode::HEAT_ON;
    case 3: return OperationMode::COOL_ON;
    // Odin 2.0 (MODE_MAP in its dashboard): 5 = frost protect (space heat),
    // 6 = legionella (a DHW cycle — never an absorb window).
    case 5: return OperationMode::HEAT_ON;
    case 6: return OperationMode::DHW_ON;
    case 255: return OperationMode::UNAVAILABLE;
    default: return OperationMode::OFF;
  }
}

inline const char *operation_mode_name(OperationMode mode) {
  switch (mode) {
    case OperationMode::DHW_ON: return "DHW on";
    case OperationMode::HEAT_ON: return "heating on";
    case OperationMode::COOL_ON: return "cooling on";
    case OperationMode::UNAVAILABLE: return "unavailable";
    case OperationMode::OFF:
    default: return "off";
  }
}

inline bool may_drive_room_control(OperationMode) { return false; }
inline bool defrost_state_is_known() { return false; }

// Absorb-arm only when ODIN plans space heat. DHW (mode 1) must never arm —
// FTC redirects to the tank and open floor loops are pointless.
inline bool may_arm_absorb(float heat_production_kw, int operation_mode_raw) {
  return std::isfinite(heat_production_kw) && heat_production_kw > 0.0f &&
         to_operation_mode(operation_mode_raw) == OperationMode::HEAT_ON;
}

// idx_now = today_start_index + current_hour; never hardcode 24.
inline bool resolve_idx_now(uint8_t today_start_index, uint8_t current_hour, uint8_t *idx_now) {
  if (idx_now == nullptr || current_hour >= 24 || today_start_index >= HORIZON_HOURS)
    return false;
  const unsigned sum = static_cast<unsigned>(today_start_index) + current_hour;
  if (sum >= HORIZON_HOURS)
    return false;
  *idx_now = static_cast<uint8_t>(sum);
  return true;
}

struct Snapshot {
  bool success{false};
  uint8_t current_hour{0};
  uint8_t today_start_index{0};
  uint8_t current_index{0};
  float sched_base[HORIZON_HOURS]{};
  float sched_min[HORIZON_HOURS]{};
  float sched_max[HORIZON_HOURS]{};
  float expected_begin_temp[HORIZON_HOURS]{};
  float expected_end_temp[HORIZON_HOURS]{};
  float prices[HORIZON_HOURS]{};
  float heat_production[HORIZON_HOURS]{};
  float actual_prod[HORIZON_HOURS]{};  // optional history; NAN where future/missing
  bool has_actual_prod{false};
  int operation_mode[HORIZON_HOURS]{};  // Raw code retained for diagnostics.
};

inline bool valid_target_bounds(float minimum, float target, float maximum) {
  return std::isfinite(minimum) && std::isfinite(target) && std::isfinite(maximum) &&
         minimum >= 5.0f && maximum <= 35.0f && minimum <= target && target <= maximum;
}

inline bool validate(const Snapshot &plan) {
  uint8_t idx = 0;
  if (!plan.success || !resolve_idx_now(plan.today_start_index, plan.current_hour, &idx) ||
      plan.current_index != idx)
    return false;
  for (size_t i = 0; i < HORIZON_HOURS; ++i) {
    if (!valid_target_bounds(plan.sched_min[i], plan.sched_base[i], plan.sched_max[i]) ||
        !std::isfinite(plan.expected_begin_temp[i]) || !std::isfinite(plan.expected_end_temp[i]) ||
        !std::isfinite(plan.prices[i]) || !std::isfinite(plan.heat_production[i]))
      return false;
  }
  return true;
}

// Odin 2.0 (`/dashboard/odin` on Odin itself): 72 slots = yesterday, today,
// tomorrow (today starts at index 24), no current_hour / today_start_index, and
// past slots may be null where telemetry was missing. Only the plan from now on
// must be complete. Captured from fw 2.0.0-23 (tests/odin_plan/fixtures).
static constexpr uint8_t ODIN2_TODAY_START_INDEX = 24;

inline bool validate_future(const Snapshot &plan) {
  uint8_t idx = 0;
  if (!plan.success || !resolve_idx_now(plan.today_start_index, plan.current_hour, &idx) ||
      plan.current_index != idx)
    return false;
  for (size_t i = idx; i < HORIZON_HOURS; ++i) {
    if (!valid_target_bounds(plan.sched_min[i], plan.sched_base[i], plan.sched_max[i]) ||
        !std::isfinite(plan.expected_begin_temp[i]) || !std::isfinite(plan.prices[i]) ||
        !std::isfinite(plan.heat_production[i]))
      return false;
  }
  return true;
}

// True when the current plan hour is OFF (heat-off). Unavailable plan → false
// (caller decides whether missing plan freezes trim via FreezeInputs).
inline bool current_hour_is_heat_off(const Snapshot &plan) {
  if (!validate(plan))
    return false;
  return to_operation_mode(plan.operation_mode[plan.current_index]) == OperationMode::OFF;
}

inline float current_target_c(const Snapshot &plan) {
  return validate(plan) ? plan.sched_base[plan.current_index] : NAN;
}

}  // namespace esphome::lune_touch_coordinator::odin_plan
