// =============================================================================
// House close margin — Touch tightens V6's heat-pump margin (pure C++, host-testable)
// =============================================================================
// In heat-pump mode a V6 keeps a warm room's loop open until the room is
// hp_overheat_margin_c above its target (0.8 °C on a heavy floor), turning it down
// gradually on the way. Each V6 only sees its own manifold: with every room on the
// ground floor 0.3–0.6 °C warm and a bedroom upstairs 1.5 °C cold, the ground floor
// kept 20–30 % open and took the water the bedroom needed (2026-11-26).
//
// While a room anywhere in the house is outside its comfort band on the cold side,
// Touch sends every V6 a tighter margin (TIGHT_MARGIN_C; V6 only ever tightens), so
// warm rooms close near their target and the flow goes to the cold room. Off again
// once every room is within COLD_OFF_K of its target. V6 falls back to its own margin
// when Touch stops sending (TTL).
// =============================================================================

#pragma once

#include <cmath>
#include <cstddef>

namespace lune_touch_close_margin {

constexpr float TIGHT_MARGIN_C = 0.3f;  // warm rooms close at target + 0.3, reopen at target
constexpr float COLD_ON_K = 0.5f;       // a room this far below its target (outside the band)
constexpr float COLD_OFF_K = 0.2f;      // released once every room is within this

struct Room {
  bool valid{false};
  float temp_c{NAN};
  float setpoint_c{NAN};
};

struct Result {
  bool active{false};
  int coldest{-1};          // index of the coldest valid room, -1 = none
  float deficit_k{NAN};     // its target − temperature
};

/// One step with hysteresis: on when the coldest room is COLD_ON_K or more below its
/// target, held while it is more than COLD_OFF_K below.
inline Result step(bool active, const Room *rooms, size_t n) {
  Result r;
  for (size_t i = 0; i < n; i++) {
    const Room &room = rooms[i];
    if (!room.valid || !std::isfinite(room.temp_c) || !std::isfinite(room.setpoint_c))
      continue;
    const float d = room.setpoint_c - room.temp_c;
    if (r.coldest < 0 || d > r.deficit_k) {
      r.coldest = static_cast<int>(i);
      r.deficit_k = d;
    }
  }
  if (r.coldest < 0)
    return r;  // nothing known: release
  r.active = r.deficit_k >= COLD_ON_K || (active && r.deficit_k > COLD_OFF_K);
  return r;
}

}  // namespace lune_touch_close_margin
