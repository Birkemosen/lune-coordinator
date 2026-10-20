// =============================================================================
// Heat pump phase for V6 — valves follow the heat pump (pure C++, host-testable)
// =============================================================================
// V6's heat-pump openings (base 60 %, demand 80 %) give the heat pump a big floor
// area and lots of flow, so it can run at the lowest flow temperature. That only
// pays while the compressor runs. When it is idle, satisfied rooms are better at
// the trim floor: the buffer's remaining heat goes to the rooms that need it, and
// the circulation pump moves less water for nothing.
//
//   RUNNING   space heating with the compressor or the primary pump on, held a
//             few minutes after it stops (defrost, short pauses)
//   PRESTART  Odin plans space heat this hour, or next hour within 10 minutes:
//             the loops open first, so the heat pump starts with full flow
//   IDLE      everything else, hot water included (the primary goes to the tank)
//   UNKNOWN   no fresh heat pump data: say nothing, V6 keeps its own curve
// =============================================================================

#pragma once

#include <cmath>
#include <cstdint>

namespace lune_touch_hp_phase {

enum class Phase : uint8_t { Unknown, Running, Prestart, Idle };

constexpr uint8_t PRESTART_MIN = 10;
constexpr uint32_t RUN_HOLD_MS = 5UL * 60UL * 1000UL;
/// Asgard operation_mode for space heating (1 = hot water).
constexpr int ASGARD_SPACE_HEAT = 2;
/// Odin plan operation modes that are space heat (2 heat, 5 frost protect).
inline bool plan_is_space_heat(float heat_kw, int mode) {
  return std::isfinite(heat_kw) && heat_kw > 0.0f && (mode == 2 || mode == 5);
}

struct Input {
  bool telemetry_fresh = false;
  bool compressor_on = false;
  bool water_pump_on = false;
  int operation_mode = -1;      ///< Asgard
  bool plan_known = false;
  float plan_heat_now_kw = NAN;
  int plan_mode_now = -1;
  float plan_heat_next_kw = NAN;
  int plan_mode_next = -1;
  uint8_t minute = 0;           ///< minute of the local hour
};

struct State {
  uint32_t last_running_ms = 0;  ///< 0 = never
};

inline Phase step(State &s, const Input &in, uint32_t now_ms) {
  if (!in.telemetry_fresh)
    return Phase::Unknown;
  if (in.operation_mode == ASGARD_SPACE_HEAT && (in.compressor_on || in.water_pump_on)) {
    s.last_running_ms = now_ms == 0 ? 1 : now_ms;
    return Phase::Running;
  }
  if (s.last_running_ms != 0 && now_ms - s.last_running_ms < RUN_HOLD_MS)
    return Phase::Running;
  if (in.plan_known) {
    if (plan_is_space_heat(in.plan_heat_now_kw, in.plan_mode_now))
      return Phase::Prestart;
    if (in.minute >= 60 - PRESTART_MIN && plan_is_space_heat(in.plan_heat_next_kw, in.plan_mode_next))
      return Phase::Prestart;
  }
  return Phase::Idle;
}

inline const char *name(Phase p) {
  switch (p) {
    case Phase::Running: return "running";
    case Phase::Prestart: return "prestart";
    case Phase::Idle: return "idle";
    default: return "unknown";
  }
}

}  // namespace lune_touch_hp_phase
