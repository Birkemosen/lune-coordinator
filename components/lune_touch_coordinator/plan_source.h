#pragma once

#include "odin_plan.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::lune_touch_coordinator::plan_source {

// Dual-source plan adapter surface (Odin 1.x via Asgard + Odin 2.0 forecast).
// Downstream arming, timing-bias, GUI, and ledger consume PlanView only.

enum class SourceId : uint8_t { None = 0, AsgardDashboardV1 = 1, OdinForecastV2 = 2 };

enum class PlanSourceMode : uint8_t { Auto = 0, V1 = 1, V2 = 2 };

// Compile-time gate: v2 runtime activation requires a real /api/forecast capture
// fixture test (U3/U4). Keep false until that test is green.
#ifndef LUNE_ODIN_FORECAST_V2_ENABLED
#define LUNE_ODIN_FORECAST_V2_ENABLED 0
#endif

inline constexpr bool forecast_v2_runtime_enabled() {
  return LUNE_ODIN_FORECAST_V2_ENABLED != 0;
}

inline const char *source_id_name(SourceId id) {
  switch (id) {
    case SourceId::AsgardDashboardV1: return "asgard_dashboard_v1";
    case SourceId::OdinForecastV2: return "odin_forecast_v2";
    case SourceId::None:
    default: return "none";
  }
}

inline PlanSourceMode mode_from_name(const char *name) {
  if (name == nullptr)
    return PlanSourceMode::Auto;
  if (std::strcmp(name, "v1") == 0)
    return PlanSourceMode::V1;
  if (std::strcmp(name, "v2") == 0)
    return PlanSourceMode::V2;
  return PlanSourceMode::Auto;
}

inline const char *mode_name(PlanSourceMode mode) {
  switch (mode) {
    case PlanSourceMode::V1: return "v1";
    case PlanSourceMode::V2: return "v2";
    case PlanSourceMode::Auto:
    default: return "auto";
  }
}

// Odin 2.0 decision_reason strings (fail-closed on unknown / missing).
// If firmware delivers integers, map only from official docs/capture — never from
// string declaration order in firmware.
enum class DecisionReason : uint8_t {
  Unknown = 0,
  ThermalBuffer,
  ThermalBufferSolar,
  EnergyCost,
  Modulation,
  ComfortHard,
  ComfortSoft,
  Blocked,
};

inline DecisionReason decision_reason_from_string(const char *raw) {
  if (raw == nullptr || raw[0] == '\0')
    return DecisionReason::Unknown;
  if (std::strcmp(raw, "thermal_buffer") == 0)
    return DecisionReason::ThermalBuffer;
  if (std::strcmp(raw, "thermal_buffer_solar") == 0)
    return DecisionReason::ThermalBufferSolar;
  if (std::strcmp(raw, "energy_cost") == 0)
    return DecisionReason::EnergyCost;
  if (std::strcmp(raw, "modulation") == 0)
    return DecisionReason::Modulation;
  if (std::strcmp(raw, "comfort_hard") == 0)
    return DecisionReason::ComfortHard;
  if (std::strcmp(raw, "comfort_soft") == 0)
    return DecisionReason::ComfortSoft;
  if (std::strncmp(raw, "blocked_", 8) == 0 || std::strcmp(raw, "blocked") == 0)
    return DecisionReason::Blocked;
  return DecisionReason::Unknown;
}

inline const char *decision_reason_name(DecisionReason reason) {
  switch (reason) {
    case DecisionReason::ThermalBuffer: return "thermal_buffer";
    case DecisionReason::ThermalBufferSolar: return "thermal_buffer_solar";
    case DecisionReason::EnergyCost: return "energy_cost";
    case DecisionReason::Modulation: return "modulation";
    case DecisionReason::ComfortHard: return "comfort_hard";
    case DecisionReason::ComfortSoft: return "comfort_soft";
    case DecisionReason::Blocked: return "blocked";
    case DecisionReason::Unknown:
    default: return "unknown";
  }
}

// v2 arming from decision_reason. energy_cost/modulation are lower priority and
// config-gated (default on). DHW exclusion is applied by the caller.
inline bool may_arm_absorb_v2(DecisionReason reason, bool arm_energy_cost_modulation) {
  switch (reason) {
    case DecisionReason::ThermalBuffer:
    case DecisionReason::ThermalBufferSolar:
      return true;
    case DecisionReason::EnergyCost:
    case DecisionReason::Modulation:
      return arm_energy_cost_modulation;
    case DecisionReason::ComfortHard:
    case DecisionReason::ComfortSoft:
    case DecisionReason::Blocked:
    case DecisionReason::Unknown:
    default:
      return false;
  }
}

struct PlanView {
  SourceId source{SourceId::None};
  bool success{false};
  uint8_t current_hour{0};
  uint8_t today_start_index{0};
  uint8_t current_index{0};
  float sched_base[odin_plan::HORIZON_HOURS]{};
  float sched_min[odin_plan::HORIZON_HOURS]{};
  float sched_max[odin_plan::HORIZON_HOURS]{};
  float expected_begin_temp[odin_plan::HORIZON_HOURS]{};
  float expected_end_temp[odin_plan::HORIZON_HOURS]{};
  float prices[odin_plan::HORIZON_HOURS]{};
  float heat_production[odin_plan::HORIZON_HOURS]{};
  // Plan-vs-reality heat: v1 actual_prod, v2 actual_heat_prod (not cooling).
  float actual_heat[odin_plan::HORIZON_HOURS]{};
  bool has_actual_heat{false};
  int operation_mode[odin_plan::HORIZON_HOURS]{};
  DecisionReason decision_reason[odin_plan::HORIZON_HOURS]{};
  bool has_decision_reason{false};
  // Freshness: prefer solved timestamp when present; else last_run fingerprint.
  bool has_solved_at{false};
  uint32_t solved_at_unix{0};
  float last_run_execution_ms{NAN};
  float last_run_evaluated_nodes{NAN};
  float last_run_total_cost{NAN};
  bool has_last_run{false};
};

inline bool validate_v1_layout(const PlanView &plan) {
  odin_plan::Snapshot snap{};
  snap.success = plan.success;
  snap.current_hour = plan.current_hour;
  snap.today_start_index = plan.today_start_index;
  snap.current_index = plan.current_index;
  for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
    snap.sched_base[i] = plan.sched_base[i];
    snap.sched_min[i] = plan.sched_min[i];
    snap.sched_max[i] = plan.sched_max[i];
    snap.expected_begin_temp[i] = plan.expected_begin_temp[i];
    snap.expected_end_temp[i] = plan.expected_end_temp[i];
    snap.prices[i] = plan.prices[i];
    snap.heat_production[i] = plan.heat_production[i];
    snap.operation_mode[i] = plan.operation_mode[i];
  }
  return odin_plan::validate(snap);
}

// v2 layout must be derived from a real capture (U3). Until then this always
// fails closed so the adapter cannot be activated on guesses.
inline bool validate_v2_layout(const PlanView &plan) {
  if (!forecast_v2_runtime_enabled())
    return false;
  if (!plan.success || !plan.has_decision_reason)
    return false;
  // Placeholder: real indexing rules land with the capture fixture test.
  return validate_v1_layout(plan);
}

inline PlanView from_v1_snapshot(const odin_plan::Snapshot &snap) {
  PlanView view{};
  view.source = SourceId::AsgardDashboardV1;
  view.success = snap.success;
  view.current_hour = snap.current_hour;
  view.today_start_index = snap.today_start_index;
  view.current_index = snap.current_index;
  view.has_actual_heat = snap.has_actual_prod;
  for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
    view.sched_base[i] = snap.sched_base[i];
    view.sched_min[i] = snap.sched_min[i];
    view.sched_max[i] = snap.sched_max[i];
    view.expected_begin_temp[i] = snap.expected_begin_temp[i];
    view.expected_end_temp[i] = snap.expected_end_temp[i];
    view.prices[i] = snap.prices[i];
    view.heat_production[i] = snap.heat_production[i];
    view.actual_heat[i] = snap.actual_prod[i];
    view.operation_mode[i] = snap.operation_mode[i];
    view.decision_reason[i] = DecisionReason::Unknown;
  }
  return view;
}

// Unified arm predicate: v1 uses heat+mode; v2 uses decision_reason (+ DHW deny).
inline bool may_arm_absorb(const PlanView &plan, size_t index, bool arm_energy_cost_modulation) {
  if (index >= odin_plan::HORIZON_HOURS)
    return false;
  const auto mode = odin_plan::to_operation_mode(plan.operation_mode[index]);
  if (mode == odin_plan::OperationMode::DHW_ON)
    return false;
  if (plan.source == SourceId::OdinForecastV2) {
    if (!plan.has_decision_reason)
      return false;
    return may_arm_absorb_v2(plan.decision_reason[index], arm_energy_cost_modulation);
  }
  // v1 and unknown sources: legacy rule only.
  return odin_plan::may_arm_absorb(plan.heat_production[index], plan.operation_mode[index]);
}

}  // namespace esphome::lune_touch_coordinator::plan_source
