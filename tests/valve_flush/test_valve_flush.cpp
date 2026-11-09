#include "valve_flush.h"

#include <cassert>
#include <cstdio>

using namespace lune_touch_valve_flush;

int main() {
  // Minutes: 0 = default, clamped to 5..30.
  assert(minutes(0) == DEFAULT_MIN);
  assert(minutes(2) == MIN_MIN);
  assert(minutes(20) == 20);
  assert(minutes(90) == MAX_MIN);

  // Not active: nothing to do.
  Run r{};
  assert(tick(r, 1000) == Action::None);
  assert(left_s(r, 1000) == 0);

  // Start: the first tick sends, then again every RESEND_MS.
  start(r, 1, 4, 10000, 10);
  assert(r.active && r.node == 1 && r.zone == 4);
  assert(left_s(r, 10000) == 600);
  assert(tick(r, 10000) == Action::Send);
  assert(tick(r, 10000 + RESEND_MS - 1) == Action::None);
  assert(tick(r, 10000 + RESEND_MS) == Action::Send);
  assert(left_s(r, 10000 + 90000) == 510);

  // Time up: stop once, then nothing.
  assert(tick(r, 10000 + 600000) == Action::Stop);
  assert(!r.active);
  assert(tick(r, 10000 + 600001) == Action::None);

  // Stopped by hand.
  start(r, 0, 0, 5000, 0);
  assert(r.duration_ms == DEFAULT_MIN * 60000UL);
  assert(tick(r, 5000) == Action::Send);
  r.stop_requested = true;
  assert(tick(r, 6000) == Action::Stop);
  assert(!r.active && !r.stop_requested);

  // millis() wrapping during a flush.
  start(r, 0, 2, 0xFFFFF000UL, 5);
  assert(tick(r, 0xFFFFF000UL) == Action::Send);
  assert(tick(r, 0x00001000UL) == Action::None);
  assert(left_s(r, 0x00001000UL) == 300 - 8);
  assert(tick(r, static_cast<uint32_t>(0xFFFFF000UL + 300000UL)) == Action::Stop);

  std::printf("valve_flush: all tests passed\n");
  return 0;
}
