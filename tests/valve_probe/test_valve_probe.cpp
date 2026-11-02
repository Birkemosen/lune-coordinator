// Host tests for valve_probe.h (valve curve measurement).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "valve_probe.h"

using namespace lune_touch_valve_probe;

static float loop_flow(float pos, float start, float full, float lmin) {
  return lmin * ramp(pos, start, full);
}

static void test_fit_recovers_curve() {
  Point pts[STEPS];
  // 22 l/min from the other loops, this loop 2.6 l/min, starts at 8 %, full at 34 %.
  const float noise[STEPS] = {0.1f, -0.1f, 0.05f, -0.05f, 0.1f, 0.0f, -0.1f, 0.05f, -0.05f, 0.0f};
  for (size_t k = 0; k < STEPS; k++)
    pts[k] = {STEP_PCT[k], 22.0f + loop_flow(STEP_PCT[k], 8.0f, 34.0f, 2.6f) + noise[k]};
  const Fit f = fit(pts, STEPS);
  std::printf("fit start %.1f full %.0f delta %.2f r2 %.3f\n", f.start_pct, f.full_pct, f.delta_lmin, f.r2);
  assert(f.ok);
  assert(std::fabs(f.start_pct - 8.0f) <= 3.0f);
  assert(std::fabs(f.full_pct - 34.0f) <= 5.0f);
  assert(std::fabs(f.delta_lmin - 2.6f) <= 0.3f);
}

static void test_fit_rejects_flat_or_noisy() {
  Point pts[STEPS];
  for (size_t k = 0; k < STEPS; k++)
    pts[k] = {STEP_PCT[k], 22.0f + ((k % 2) ? 0.3f : -0.3f)};
  assert(!fit(pts, STEPS).ok);  // no loop flow to see
  for (size_t k = 0; k < STEPS; k++)
    pts[k] = {STEP_PCT[k], 22.0f + loop_flow(STEP_PCT[k], 8.0f, 34.0f, 2.0f) + ((k % 2) ? 1.5f : -1.5f)};
  assert(!fit(pts, STEPS).ok);  // noise as large as the loop
  assert(!fit(pts, 3).ok);
}

static void test_schedule() {
  const int64_t now = 1800000000;
  assert(loop_due(0, 0, now));
  assert(!loop_due(static_cast<uint32_t>(now - 86400), 0, now));
  assert(loop_due(static_cast<uint32_t>(now - REMEASURE_S), 0, now));
  assert(!loop_due(0, static_cast<uint32_t>(now - 3600), now));
  assert(loop_due(0, static_cast<uint32_t>(now - RETRY_FAILED_S), now));
  assert(!loop_due(0, 0, 0));
}

static void test_run_sequence() {
  Run r;
  start(r, 1, 2, 1000);
  TickInput in;
  in.conditions_ok = true;
  in.now_ms = 1000;
  assert(tick(r, in, true) == Action::SendStep && r.step == 0);
  uint32_t t = 1000;
  size_t sends = 1;
  Action a = Action::None;
  for (int guard = 0; guard < 10000; guard++) {
    t += 15000;
    in.now_ms = t;
    in.has_flow_sample = true;
    const float pos = STEP_PCT[r.step];
    in.flow_lmin = 22.0f + loop_flow(pos, 8.0f, 34.0f, 2.6f);
    a = tick(r, in, false);
    if (a == Action::SendStep)
      sends++;
    if (a == Action::Finish || a == Action::Abort)
      break;
  }
  assert(a == Action::Finish);
  assert(sends == STEPS);
  assert(r.n_pts == STEPS);
  const Fit f = fit(r.pts, r.n_pts);
  assert(f.ok);
  const uint32_t total_min = (t - 1000) / 60000;
  std::printf("run: %u min for one loop\n", static_cast<unsigned>(total_min));
  assert(total_min <= 25);
}

static void test_run_aborts() {
  Run r;
  start(r, 0, 0, 0);
  TickInput in;
  in.conditions_ok = true;
  tick(r, in, true);
  in.now_ms = 30000;
  in.has_flow_sample = true;
  in.flow_lmin = 22.0f;
  assert(tick(r, in, false) == Action::None);
  in.conditions_ok = false;  // heat pump starts
  assert(tick(r, in, false) == Action::Abort && r.stage == Stage::Off);

  start(r, 0, 0, 0);
  in.conditions_ok = true;
  in.has_flow_sample = false;
  tick(r, in, true);
  in.now_ms = SAMPLE_STALE_MS + 1000;
  assert(tick(r, in, false) == Action::Abort);  // pump reading stale
}

int main() {
  test_fit_recovers_curve();
  test_fit_rejects_flat_or_noisy();
  test_schedule();
  test_run_sequence();
  test_run_aborts();
  std::printf("Valve probe tests passed.\n");
  return 0;
}
