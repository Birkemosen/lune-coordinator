// =============================================================================
// Loop flush (pure C++, host-testable)
// =============================================================================
// The manifold's automatic air vents only take the air that reaches the manifold.
// A pocket out in a long 12 mm loop needs about 0.3 m/s or more to be carried along,
// which a loop throttled by dynamic balancing, or one sharing the pump with eleven
// others, does not get. A flush opens one loop fully with every other loop on every
// V6 closed, so the pump's whole head drives that loop, for a few minutes.
//
// Started by hand (Touch API), never automatically. It uses V6's valve test
// (/api/v1/valve-test). The hold is renewed every RESEND_MS; V6 releases it by its
// TTL (HOLD_TTL_S) if Touch goes away. The circulation pump is polled every
// CIRCULATION_POLL_S while it runs.
// =============================================================================

#pragma once

#include <algorithm>
#include <cstdint>

namespace lune_touch_valve_flush {

constexpr uint32_t DEFAULT_MIN = 15;
constexpr uint32_t MIN_MIN = 5;
constexpr uint32_t MAX_MIN = 30;
constexpr uint32_t HOLD_TTL_S = 300;          ///< V6 hold TTL, sent with every hold
constexpr uint32_t RESEND_MS = 60000;         ///< Well inside HOLD_TTL_S
constexpr uint16_t CIRCULATION_POLL_S = 15;   ///< Pump poll interval while a flush runs

struct Run {
  bool active{false};
  bool stop_requested{false};
  uint8_t node{0};  ///< 0-based
  uint8_t zone{0};  ///< 0-based
  uint32_t start_ms{0};
  uint32_t duration_ms{0};
  uint32_t sent_ms{0};  ///< Last hold sent; 0 = not yet
};

/// Requested minutes (0 = default), clamped to MIN_MIN..MAX_MIN.
inline uint32_t minutes(uint32_t requested) {
  return requested == 0 ? DEFAULT_MIN : std::clamp(requested, MIN_MIN, MAX_MIN);
}

inline void start(Run &r, uint8_t node, uint8_t zone, uint32_t now_ms, uint32_t mins) {
  r = Run{};
  r.active = true;
  r.node = node;
  r.zone = zone;
  r.start_ms = now_ms;
  r.duration_ms = minutes(mins) * 60000UL;
}

/// Seconds left (0 when not active).
inline uint32_t left_s(const Run &r, uint32_t now_ms) {
  if (!r.active)
    return 0;
  const uint32_t ran = now_ms - r.start_ms;
  return ran >= r.duration_ms ? 0 : (r.duration_ms - ran + 999UL) / 1000UL;
}

enum class Action { None, Send, Stop };

/// Send = open the loop and close the others (again); Stop = release every V6.
inline Action tick(Run &r, uint32_t now_ms) {
  if (!r.active)
    return Action::None;
  if (r.stop_requested || now_ms - r.start_ms >= r.duration_ms) {
    r.active = false;
    r.stop_requested = false;
    return Action::Stop;
  }
  if (r.sent_ms == 0 || now_ms - r.sent_ms >= RESEND_MS) {
    r.sent_ms = now_ms == 0 ? 1 : now_ms;
    return Action::Send;
  }
  return Action::None;
}

}  // namespace lune_touch_valve_flush
