#include "system_curve.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_touch_system_curve;

static bool near(float a, float b, float tol = 0.05f) { return std::fabs(a - b) <= tol; }

int main() {
  // Live x: Σ flow fraction × full flow; a loop without a live flow makes it unknown.
  Loop live[3] = {{true, 4.0f, 10.0f, 1.0f, 0.5f}, {true, 2.0f, 10.0f, 1.0f, 1.0f}, {false, 0.0f, 0.0f, 1.0f, NAN}};
  assert(near(live_x(live, 3), 4.0f));
  live[1].flow_frac = NAN;
  assert(std::isnan(live_x(live, 3)));
  live[1].flow_frac = 1.4f;  // clamped to full
  assert(near(live_x(live, 3), 4.0f));

  // Full load: share = (UA / full) / board max, × balance, capped at 1.
  // Board 0: needs 10/5 = 2 and 4/4 = 1 → shares 1 and 0.5; balance 0.4 → 0.4 and 0.2.
  // Board 1: one loop, share 1, balance 1.
  Loop house[4] = {{true, 5.0f, 10.0f, 0.4f, 0.0f}, {true, 4.0f, 4.0f, 0.4f, 0.0f},
                   {true, 2.0f, 8.0f, 1.0f, 1.0f}, {false, 0.0f, 0.0f, 1.0f, NAN}};
  assert(near(full_load_x(house, 2, 2), 0.4f * 5.0f + 0.2f * 4.0f + 2.0f));
  house[1].ua_w_k = 0.0f;
  assert(std::isnan(full_load_x(house, 2, 2)));

  // Empty curve: no answer.
  Curve c{};
  assert(std::isnan(flow_at(c, 10.0f)));
  // Unusable samples change nothing.
  assert(!add(c, 10.0f, 0.5f, 4.5f, 0.1f));
  assert(!add(c, 10.0f, 15.0f, 0.2f, 0.1f));
  assert(!add(c, NAN, 15.0f, 4.5f, 0.1f));

  // Two hours at x = 9 (bin 4, centre 9) with 14.7 l/min at 4.5 m → 12.0 at 3 m.
  for (int i = 0; i < 120; i++)
    assert(add(c, 9.0f, 14.7f, 4.5f, 1.0f / 60.0f));
  assert(near(flow_at(c, 9.0f), 14.7f * std::sqrt(3.0f / 4.5f)));
  assert(near(flow_at(c, 10.5f), flow_at(c, 9.0f)));   // within a bin: nearest
  assert(std::isnan(flow_at(c, 12.0f)));               // further: unknown
  // Two hours at x = 13 (bin 6, centre 13), 15 l/min at 3 m: interpolated between.
  for (int i = 0; i < 120; i++)
    assert(add(c, 13.0f, 15.0f, 3.0f, 1.0f / 60.0f));
  const float y9 = flow_at(c, 9.0f), y13 = flow_at(c, 13.0f);
  assert(near(y13, 15.0f));
  assert(near(flow_at(c, 11.0f), (y9 + y13) / 2.0f));
  // A gap longer than MAX_SAMPLE_H counts as MAX_SAMPLE_H.
  Curve g{};
  add(g, 9.0f, 12.0f, 3.0f, 5.0f);
  assert(near(g.weight[4], MAX_SAMPLE_H, 1e-4f));
  // Old samples fade: three days later at a new value, the new one dominates.
  Curve f{};
  for (int i = 0; i < 120; i++) add(f, 9.0f, 10.0f, 3.0f, 1.0f / 60.0f);
  for (int i = 0; i < 72 * 6; i++) add(f, 30.0f, 20.0f, 3.0f, MAX_SAMPLE_H);  // time passes elsewhere
  for (int i = 0; i < 120; i++) add(f, 9.0f, 14.0f, 3.0f, 1.0f / 60.0f);
  assert(flow_at(f, 9.0f) > 12.5f);

  // Head: REF × (target / y)², 0.1 m; flow at another head.
  assert(near(head_for(15.0f, 20.0f), 5.3f, 1e-3f));
  assert(near(head_for(20.0f, 20.0f), 3.0f, 1e-3f));
  assert(std::isnan(head_for(NAN, 20.0f)));
  assert(near(flow_at_head(15.0f, 4.5f), 15.0f * std::sqrt(1.5f)));

  std::puts("system_curve: all tests passed");
  return 0;
}
