// =============================================================================
// Room reached target — has a room been at its target in the last 24 h?
// (pure C++, host-testable; feeds the `room_comfort` problem in problems.h)
// =============================================================================
// A room "reaches" its target when temp ≥ target − TOLERANCE_C (0.3 °C), with the
// target that applies at that moment (a night setback counts as reached when the
// room is at the setback). The problem is there when:
//
//   heating is active and the room has a target, and has been observed so for
//   at least WINDOW_MS (24 h) — since boot, since heating became active, or since
//   the room's own target (comfort setpoint) changed — and
//   the room has not reached its target at any time in the last 24 h, and
//   there are readings in at least MIN_HOURS_WITH_DATA of the last 24 hours
//   (missing data never raises).
//
// It clears as soon as the room reaches its target (or the conditions above no
// longer hold). `max_c` is the highest temperature in the last 24 h, for the message.
// =============================================================================

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lune_touch_room_reached {

constexpr uint32_t HOUR_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t WINDOW_MS = 24UL * HOUR_MS;
constexpr uint8_t WINDOW_H = 24;
constexpr float TOLERANCE_C = 0.3f;
constexpr uint8_t MIN_HOURS_WITH_DATA = 20;
/// A change of the room's own target larger than this restarts the observation.
constexpr float TARGET_CHANGE_C = 0.05f;

struct Input {
  bool heating_active = false;
  bool has_target = false;
  float target_c = NAN;        ///< target that applies now (schedule included)
  float comfort_c = NAN;       ///< the room's own target; a change restarts observation
  bool has_temp = false;       ///< fresh, plausible reading
  float temp_c = NAN;
};

struct State {
  uint32_t observed_since_ms = 0;   ///< 0 = not observing
  float comfort_c = NAN;
  bool reached_recent = false;      ///< reached within the last WINDOW_MS
  uint32_t last_reached_ms = 0;
  float hour_max[WINDOW_H]{};
  uint32_t hour_idx[WINDOW_H]{};    ///< hour number + 1 of the bucket (0 = empty)
  float last_target_c = NAN;
};

struct Result {
  bool not_reached = false;   ///< the problem condition
  bool observing = false;
  float max_c = NAN;          ///< highest reading in the last 24 h
  uint8_t hours_with_data = 0;
};

inline uint32_t nz(uint32_t ms) { return ms == 0 ? 1 : ms; }

inline void reset_window(State &s) {
  for (uint8_t i = 0; i < WINDOW_H; i++) {
    s.hour_idx[i] = 0;
    s.hour_max[i] = 0.0f;
  }
}

inline void add_reading(State &s, float temp_c, uint32_t now_ms) {
  const uint32_t h = now_ms / HOUR_MS + 1;
  const uint8_t i = static_cast<uint8_t>(h % WINDOW_H);
  if (s.hour_idx[i] != h) {
    s.hour_idx[i] = h;
    s.hour_max[i] = temp_c;
  } else if (temp_c > s.hour_max[i]) {
    s.hour_max[i] = temp_c;
  }
}

inline void window_stats(const State &s, uint32_t now_ms, float *max_c, uint8_t *hours) {
  const uint32_t now_h = now_ms / HOUR_MS + 1;
  float m = NAN;
  uint8_t n = 0;
  for (uint8_t i = 0; i < WINDOW_H; i++) {
    if (s.hour_idx[i] == 0 || now_h - s.hour_idx[i] >= WINDOW_H)
      continue;
    n++;
    if (!std::isfinite(m) || s.hour_max[i] > m)
      m = s.hour_max[i];
  }
  *max_c = m;
  *hours = n;
}

inline Result step(State &s, const Input &in, uint32_t now_ms) {
  Result r;
  if (s.reached_recent && now_ms - s.last_reached_ms >= WINDOW_MS)
    s.reached_recent = false;
  const bool target_ok = in.has_target && std::isfinite(in.target_c);
  if (!in.heating_active || !target_ok) {
    s.observed_since_ms = 0;
  } else {
    const bool comfort_changed = std::isfinite(in.comfort_c) && std::isfinite(s.comfort_c) &&
                                 std::fabs(in.comfort_c - s.comfort_c) > TARGET_CHANGE_C;
    if (comfort_changed) {
      // A new target is a new question: start over.
      s.observed_since_ms = 0;
      s.reached_recent = false;
      reset_window(s);
    }
    if (std::isfinite(in.comfort_c))
      s.comfort_c = in.comfort_c;
    if (s.observed_since_ms == 0)
      s.observed_since_ms = nz(now_ms);
    s.last_target_c = in.target_c;
  }
  const bool temp_ok = in.has_temp && std::isfinite(in.temp_c);
  if (temp_ok) {
    add_reading(s, in.temp_c, now_ms);
    if (target_ok && in.temp_c >= in.target_c - TOLERANCE_C) {
      s.reached_recent = true;
      s.last_reached_ms = now_ms;
    }
  }
  window_stats(s, now_ms, &r.max_c, &r.hours_with_data);
  r.observing = s.observed_since_ms != 0;
  r.not_reached = r.observing && now_ms - s.observed_since_ms >= WINDOW_MS && !s.reached_recent &&
                  r.hours_with_data >= MIN_HOURS_WITH_DATA;
  return r;
}

}  // namespace lune_touch_room_reached
