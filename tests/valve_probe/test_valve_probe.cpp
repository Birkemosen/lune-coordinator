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

static void test_full_from_points() {
  // Stue (board 2 loop 5) at 6 m, 2026-11-09: coarse pass, then the refine pass.
  const Point pts[] = {{0, 4.37f},  {5, 4.40f},  {10, 4.88f}, {15, 4.85f}, {20, 6.87f}, {25, 6.87f},
                       {30, 7.52f}, {40, 7.72f}, {60, 7.81f}, {100, 7.82f}, {5, 4.57f},  {10, 4.69f},
                       {15, 4.75f}, {20, 6.95f}, {25, 6.98f}, {30, 7.53f}, {35, 7.56f}};
  const size_t n = sizeof(pts) / sizeof(pts[0]);
  Fit f = fit(pts, n);
  assert(f.ok);
  const float knee = f.full_pct, knee_delta = f.delta_lmin;
  full_from_points(f, pts, n);
  std::printf("full from points: knee %.0f %% (%.2f l/min) -> %.1f %% (%.2f l/min)\n", knee, knee_delta,
              f.full_pct, f.delta_lmin);
  assert(knee < 30.0f);
  assert(f.full_pct > 34.0f && f.full_pct < 40.0f);
  assert(f.delta_lmin > knee_delta && std::fabs(f.delta_lmin - (7.815f - f.base_lmin)) < 0.01f);

  // A curve that is really flat above its knee keeps the fit.
  Point flat[STEPS];
  for (size_t k = 0; k < STEPS; k++)
    flat[k] = {STEP_PCT[k], 20.0f + loop_flow(STEP_PCT[k], 10.0f, 30.0f, 2.5f)};
  Fit g = fit(flat, STEPS);
  const float g_full = g.full_pct;
  full_from_points(g, flat, STEPS);
  assert(std::fabs(g.full_pct - g_full) < 0.01f);

  // A failed fit is left alone.
  Fit bad{};
  full_from_points(bad, pts, n);
  assert(std::isnan(bad.full_pct));
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

static void test_plan_refine() {
  // Hobbyrum on 2026-11-03: flat to 30 %, most of the loop by 40 %.
  const Point hob[STEPS] = {{0, 21.3f},   {5, 21.19f},  {10, 21.17f}, {15, 21.15f}, {20, 21.3f},
                            {25, 21.12f}, {30, 20.94f}, {40, 21.95f}, {60, 22.26f}, {100, 22.33f}};
  float plan[MAX_STEPS];
  size_t n = plan_refine(hob, STEPS, plan, MAX_STEPS);
  assert(n >= 4);
  assert(plan[0] == 30.0f && plan[1] == 32.5f);
  for (size_t k = 1; k < n; k++)
    assert(plan[k] > plan[k - 1] && plan[k] < 60.0f);
  // A wide rise (8 → 48 %, Pejsestue) widens the steps instead of adding points.
  Point wide[STEPS];
  for (size_t k = 0; k < STEPS; k++)
    wide[k] = {STEP_PCT[k], 20.0f + loop_flow(STEP_PCT[k], 8.0f, 48.0f, 2.4f)};
  n = plan_refine(wide, STEPS, plan, MAX_STEPS);
  assert(n >= 2 && n <= 1 + MAX_REFINE);
  assert(plan[0] == 5.0f && plan[n - 1] < 60.0f);
  std::printf("refine wide: %u steps of %.1f %%\n", static_cast<unsigned>(n), plan[1] - plan[0]);
  // No clear rise: nothing to refine.
  Point flat[STEPS];
  for (size_t k = 0; k < STEPS; k++)
    flat[k] = {STEP_PCT[k], 22.0f};
  assert(plan_refine(flat, STEPS, plan, MAX_STEPS) == 0);
  // Curves from before the refine pass count as unmeasured.
  assert(counted_epoch(METHOD_EPOCH_S - 3600, true) == 0);
  assert(counted_epoch(METHOD_EPOCH_S + 60, true) == METHOD_EPOCH_S + 60);
  assert(counted_epoch(METHOD_EPOCH_S + 60, false) == 0);
}

static void test_schedule() {
  const int64_t now = 1800000000;
  assert(loop_due(0, 0, 0, now));
  assert(!loop_due(static_cast<uint32_t>(now - 86400), 0, 0, now));
  assert(loop_due(static_cast<uint32_t>(now - REMEASURE_S), 0, 0, now));
  assert(!loop_due(0, static_cast<uint32_t>(now - 3600), 1, now));
  assert(loop_due(0, static_cast<uint32_t>(now - RETRY_FAILED_S), 1, now));
  assert(!loop_due(0, static_cast<uint32_t>(now - RETRY_FAILED_S), MAX_FAILS, now));  // waits for a manual run
  assert(!loop_due(0, 0, 0, 0));
}

static void test_runs_per_day() {
  const int64_t now = 1800000000;
  uint32_t starts[MAX_RUNS_PER_DAY]{};
  assert(run_left_today(starts, now));
  note_run(starts, now - 7200);
  assert(run_left_today(starts, now));
  note_run(starts, now - 3600);
  assert(!run_left_today(starts, now));
  assert(run_left_today(starts, now + 86400 - 7200));  // the first one is a day old
  note_run(starts, now + 86400 - 7200);
  assert(starts[0] == static_cast<uint32_t>(now + 86400 - 7200) && starts[1] == static_cast<uint32_t>(now - 3600));
}

static void test_may_start() {
  const int64_t now = 1800000000;
  assert(may_start(false, false, now, now, false, 0, true));                      // idle: go
  assert(!may_start(true, false, now, now - 3600, false, 0, true));               // running: wait for a stop
  assert(may_start(true, false, now, now - RUNNING_FALLBACK_S, false, 0, true));  // never stops: measure anyway
  assert(!may_start(false, false, now, now, true, now - 3600, true));             // one remeasurement per gap
  assert(may_start(false, false, now, now, true, now - REMEASURE_GAP_S, true));
  assert(may_start(false, false, now, now, false, now - 3600, true));             // first curves back to back
  assert(!may_start(false, true, now, now, false, 0, true));                      // a cold room keeps its flow
  assert(may_start(true, true, now, now - RUNNING_FALLBACK_S, false, 0, true));   // others hold while running
  assert(!may_start(false, false, now, now, false, 0, false));                    // runs per day used
  assert(!may_start(false, false, 0, 0, false, 0, true));
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
    const float pos = r.plan[r.step];
    in.flow_lmin = 22.0f + loop_flow(pos, 8.0f, 34.0f, 2.6f);
    in.head_m = 3.4f;
    a = tick(r, in, false);
    if (a == Action::SendStep)
      sends++;
    if (a == Action::Finish || a == Action::Abort)
      break;
  }
  assert(a == Action::Finish);
  assert(r.n_steps > STEPS);  // refine pass ran
  assert(sends == r.n_steps);
  assert(r.n_pts == r.n_steps);
  assert(r.plan[STEPS] == 5.0f);  // back below the start, then up in 2.5 % steps
  assert(std::fmod(r.plan[STEPS + 1] - 5.0f, REFINE_STEP_PCT) == 0.0f && r.plan[r.n_steps - 1] < 40.0f);
  const Fit f = fit(r.pts, r.n_pts);
  assert(f.ok);
  assert(std::fabs(f.start_pct - 8.0f) <= 1.0f);
  assert(std::fabs(f.full_pct - 34.0f) <= 2.0f);
  const uint32_t total_min = (t - 1000) / 60000;
  std::printf("run: %u steps, %u min for one loop, start %.1f full %.0f\n",
              static_cast<unsigned>(r.n_steps), static_cast<unsigned>(total_min), f.start_pct, f.full_pct);
  assert(total_min <= 60);
  assert(std::fabs(mean_head(r) - 3.4f) < 1e-4f);
}

static void test_flow_at_ref_head() {
  // Stueetage Hobbyrum: 1.92 l/min at 2.8 m is 1.99 at 3 m; at 3.4 m it would read 2.13.
  assert(std::fabs(flow_at_ref_head(1.92f, 2.8f) - 1.987f) < 0.01f);
  assert(std::fabs(flow_at_ref_head(1.92f * std::sqrt(3.4f / 2.8f), 3.4f) - flow_at_ref_head(1.92f, 2.8f)) < 1e-3f);
  assert(flow_at_ref_head(2.0f, REF_HEAD_M) == 2.0f);
  assert(flow_at_ref_head(2.0f, NAN) == 2.0f);  // no head reading: as measured
  assert(flow_at_ref_head(2.0f, 0.1f) == 2.0f);
  Run r;
  assert(std::isnan(mean_head(r)));
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

static void test_run_interrupted_in_refine() {
  // Josephine 2026-11-12: the heat pump stopped at step 16 of 17 and the run was lost.
  Run r;
  start(r, 1, 0, 1000);
  TickInput in;
  in.conditions_ok = true;
  in.now_ms = 1000;
  tick(r, in, true);
  uint32_t t = 1000;
  Action a = Action::None;
  for (int guard = 0; guard < 10000 && !(r.refine_from != 0 && r.step == r.refine_from + 2); guard++) {
    t += 15000;
    in.now_ms = t;
    in.has_flow_sample = true;
    in.flow_lmin = 22.0f + loop_flow(r.plan[r.step], 8.0f, 34.0f, 2.6f);
    a = tick(r, in, false);
    assert(a != Action::Finish && a != Action::Abort);
  }
  in.conditions_ok = false;  // heat pump stops
  assert(tick(r, in, false) == Action::Finish && r.stage == Stage::Off);
  const Fit f = fit(r.pts, r.n_pts);
  assert(f.ok && std::fabs(f.start_pct - 8.0f) <= 2.0f);
}

int main() {
  test_fit_recovers_curve();
  test_fit_rejects_flat_or_noisy();
  test_full_from_points();
  test_plan_refine();
  test_schedule();
  test_may_start();
  test_runs_per_day();
  test_run_sequence();
  test_run_aborts();
  test_run_interrupted_in_refine();
  test_flow_at_ref_head();
  std::printf("Valve probe tests passed.\n");
  return 0;
}
