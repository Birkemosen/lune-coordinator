// =============================================================================
// Valve curve measurement (pure C++, host-testable)
// =============================================================================
// V6's dynamic balancing turns each loop's relative flow into an opening through
// a valve curve: the opening where flow starts and where it is full. Manifold
// valve inserts open quickly and differ per loop, so Touch measures the curve from
// the circulation pump's flow, which only Touch sees:
//
//   1. every other loop on every V6 closes (V6 /valve-test, others_pct 0), so each
//      loop is measured alone: a loop's flow depends on what else is open (the
//      manifold's supply pipe), and the curves must compare across loops and days.
//      While the heat source runs (the fallback below) the others hold instead;
//   2. the test loop steps through STEP_PCT; after each step settles, the pump's
//      flow is averaged (MEASURE_MS);
//   3. the coarse steps only bracket the rise, so a refine pass goes back to the
//      last coarse step below the fitted start and climbs in REFINE_STEP_PCT steps
//      to just past the fitted full point (plan_refine);
//   4. fit flow = base + delta · ramp(opening; start, full) by least squares over
//      every point;
//   5. a good fit (delta and R² large enough) is written to V6 with its time and its
//      full flow at REF_HEAD_M (flow ∝ √head), so a changed pump setting does not
//      change the loop's share.
//
// With a constant-pressure pump the change in total flow is the loop's own flow,
// whether the heat source runs or not. But a run holds every valve, so balancing is
// off while it lasts: Touch measures while the heat source is not running, and only
// falls back to measuring during a run after RUNNING_FALLBACK_S without a pause (a
// heat source that never stops). A buffer tank keeps the floors warm while the heat
// source is off, and closing the others shuts them out of it, so an automatic run
// also waits until no room is below its target, and at most MAX_RUNS_PER_DAY runs
// start in 24 h. One loop at a time, PAUSE_MS apart, until every loop has a curve;
// then each loop again after REMEASURE_S, at most one remeasurement per
// REMEASURE_GAP_S so they spread out. A loop that fails MAX_FAILS times in a row waits
// for a manual run (its valve may need relearning). A change of heat-source phase during a run
// (start, stop, hot water) moves temperatures and the pump's flow estimate, so it
// aborts the run (no result), as do a stale pump reading and a V6 that does not
// answer; V6 releases the hold by its TTL.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_valve_probe {

constexpr size_t STEPS = 10;  ///< Coarse steps
constexpr float STEP_PCT[STEPS] = {0, 5, 10, 15, 20, 25, 30, 40, 60, 100};
constexpr float REFINE_STEP_PCT = 2.5f;
constexpr size_t MAX_REFINE = 8;  ///< Points inside the rise; a wider rise gets wider steps
constexpr size_t MAX_STEPS = STEPS + 1 + MAX_REFINE;
constexpr uint32_t SETTLE_FIRST_MS = 150000;  ///< First step of a pass: the valve may travel far
constexpr uint32_t SETTLE_MS = 75000;
constexpr uint32_t MEASURE_MS = 90000;  ///< The pump's flow jitters 0.1–0.2 l/min; a loop is 1–2
constexpr uint32_t SAMPLE_STALE_MS = 90000;   ///< No pump reading this long → abort
constexpr uint32_t HOLD_TTL_S = 300;          ///< V6 hold TTL, refreshed every step
constexpr uint16_t CIRCULATION_POLL_S = 15;   ///< Pump poll interval while measuring
constexpr float MIN_DELTA_LMIN = 0.8f;        ///< Smallest loop flow worth a curve
constexpr float MIN_R2 = 0.8f;
constexpr uint32_t PAUSE_MS = 5UL * 60UL * 1000UL;  ///< Between runs: balancing resumes
constexpr uint32_t REMEASURE_S = 30UL * 86400UL;
constexpr uint32_t RETRY_FAILED_S = 86400UL;
constexpr uint32_t RUNNING_FALLBACK_S = 86400UL;  ///< Running this long without a stop → measure anyway
constexpr uint32_t REMEASURE_GAP_S = 86400UL;     ///< Between automatic remeasurements
constexpr size_t MAX_RUNS_PER_DAY = 2;            ///< Automatic runs started per 24 h
constexpr uint8_t MAX_FAILS = 2;                  ///< Failed tries in a row before a loop waits for a manual run
constexpr size_t MAX_SAMPLES = 16;
constexpr float REF_HEAD_M = 3.0f;  ///< Full flow is reported at this pump head
/// Curves measured before the current method (each loop alone, flow at REF_HEAD_M)
/// count as unmeasured, so every loop is measured again without waiting REMEASURE_S.
constexpr uint32_t METHOD_EPOCH_S = 1793878200;  // 2026-11-05 11:30 UTC

/// Loop flow at REF_HEAD_M from flow measured at `head_m` (flow ∝ √head). No head
/// reading: the flow as measured.
inline float flow_at_ref_head(float lmin, float head_m) {
  if (!std::isfinite(lmin) || !std::isfinite(head_m) || head_m < 0.5f)
    return lmin;
  return lmin * std::sqrt(REF_HEAD_M / head_m);
}

struct Point {
  float pos_pct{NAN};
  float flow_lmin{NAN};
};

struct Fit {
  bool ok{false};
  float start_pct{NAN};
  float full_pct{NAN};
  float base_lmin{NAN};
  float delta_lmin{NAN};
  float r2{NAN};
};

inline float ramp(float x, float start, float full) {
  if (x <= start)
    return 0.0f;
  if (x >= full)
    return 1.0f;
  return (x - start) / (full - start);
}

/// Least-squares fit of flow = base + delta · ramp(pos; start, full) over a grid of
/// start (0..50 %, 0.5 steps) and full (start + 10 .. 100 %, 1 % steps).
inline Fit fit(const Point *pts, size_t n) {
  Fit best;
  if (pts == nullptr || n < 4)
    return best;
  float mean_f = 0.0f;
  size_t m = 0;
  for (size_t k = 0; k < n; k++) {
    if (std::isfinite(pts[k].pos_pct) && std::isfinite(pts[k].flow_lmin)) {
      mean_f += pts[k].flow_lmin;
      m++;
    }
  }
  if (m < 4)
    return best;
  mean_f /= static_cast<float>(m);
  float sst = 0.0f;
  for (size_t k = 0; k < n; k++)
    if (std::isfinite(pts[k].pos_pct) && std::isfinite(pts[k].flow_lmin))
      sst += (pts[k].flow_lmin - mean_f) * (pts[k].flow_lmin - mean_f);
  float best_sse = INFINITY;
  for (float s = 0.0f; s <= 50.0f; s += 0.5f) {
    for (float f = s + 10.0f; f <= 100.0f; f += 1.0f) {
      float mr = 0.0f;
      for (size_t k = 0; k < n; k++)
        if (std::isfinite(pts[k].pos_pct) && std::isfinite(pts[k].flow_lmin))
          mr += ramp(pts[k].pos_pct, s, f);
      mr /= static_cast<float>(m);
      float cov = 0.0f, var = 0.0f;
      for (size_t k = 0; k < n; k++) {
        if (!std::isfinite(pts[k].pos_pct) || !std::isfinite(pts[k].flow_lmin))
          continue;
        const float dr = ramp(pts[k].pos_pct, s, f) - mr;
        cov += dr * (pts[k].flow_lmin - mean_f);
        var += dr * dr;
      }
      if (var < 1e-6f)
        continue;
      const float delta = cov / var;
      const float base = mean_f - delta * mr;
      float sse = 0.0f;
      for (size_t k = 0; k < n; k++) {
        if (!std::isfinite(pts[k].pos_pct) || !std::isfinite(pts[k].flow_lmin))
          continue;
        const float e = pts[k].flow_lmin - (base + delta * ramp(pts[k].pos_pct, s, f));
        sse += e * e;
      }
      if (sse < best_sse - 1e-6f) {
        best_sse = sse;
        best.start_pct = s;
        best.full_pct = f;
        best.base_lmin = base;
        best.delta_lmin = delta;
      }
    }
  }
  if (!std::isfinite(best_sse))
    return best;
  best.r2 = sst > 1e-6f ? 1.0f - best_sse / sst : 0.0f;
  best.ok = best.delta_lmin >= MIN_DELTA_LMIN && best.r2 >= MIN_R2;
  return best;
}

/// Refine plan after the coarse pass: the last coarse step at or below the fitted
/// start (approached from above, so it is measured again after a long settle), then
/// REFINE_STEP_PCT steps up to, not including, the first coarse step at or above the
/// fitted full point (or 5 points past it, whichever is lower). At most MAX_REFINE inner points; a wider rise gets steps of a
/// multiple of REFINE_STEP_PCT. Returns the number of steps written (0: no clear rise,
/// nothing to refine).
inline size_t plan_refine(const Point *pts, size_t n, float *out, size_t max) {
  const Fit f = fit(pts, n);
  if (!f.ok || out == nullptr || max < 2)
    return 0;
  float lower = 0.0f, upper = 100.0f;
  for (size_t k = 0; k < STEPS; k++) {
    if (STEP_PCT[k] <= f.start_pct)
      lower = std::max(lower, STEP_PCT[k]);
    if (STEP_PCT[k] >= f.full_pct)
      upper = std::min(upper, STEP_PCT[k]);
  }
  // The coarse step above the rise can be far (40 → 60 %): stop a little past the fit.
  upper = std::min(upper, std::ceil((f.full_pct + 5.0f) / REFINE_STEP_PCT) * REFINE_STEP_PCT);
  const float span = upper - lower;
  float step = REFINE_STEP_PCT;
  const size_t limit = std::min(MAX_REFINE, max - 1);
  while (std::ceil(span / step - 1e-3f) - 1.0f > static_cast<float>(limit))
    step += REFINE_STEP_PCT;
  size_t m = 0;
  out[m++] = lower;
  for (float p = lower + step; p < upper - 1e-3f && m < max; p += step)
    out[m++] = p;
  return m;
}

/// The measured time that counts for scheduling: 0 (unmeasured) for a curve without
/// its full flow or from before METHOD_EPOCH_S.
inline uint32_t counted_epoch(uint32_t measured_epoch_s, bool has_full_flow) {
  return has_full_flow && measured_epoch_s >= METHOD_EPOCH_S ? measured_epoch_s : 0;
}

/// Is a loop due? `measured_epoch_s` from V6 (0 = never), `failed_epoch_s` and
/// `fails` (tries in a row) from Touch's own memory of failed tries (0 = none).
inline bool loop_due(uint32_t measured_epoch_s, uint32_t failed_epoch_s, uint8_t fails, int64_t now_epoch_s) {
  if (now_epoch_s <= 0 || fails >= MAX_FAILS)
    return false;
  const int64_t now = now_epoch_s;
  if (failed_epoch_s != 0 && now - static_cast<int64_t>(failed_epoch_s) < RETRY_FAILED_S)
    return false;
  return measured_epoch_s == 0 || now - static_cast<int64_t>(measured_epoch_s) >= REMEASURE_S;
}

/// Is there an automatic run left in the last 24 h? `starts`: when the latest
/// automatic runs started (0 = free slot).
inline bool run_left_today(const uint32_t (&starts)[MAX_RUNS_PER_DAY], int64_t now_epoch_s) {
  for (uint32_t s : starts)
    if (s == 0 || now_epoch_s - static_cast<int64_t>(s) >= 86400)
      return true;
  return false;
}

/// Record an automatic run's start in the oldest slot.
inline void note_run(uint32_t (&starts)[MAX_RUNS_PER_DAY], int64_t now_epoch_s) {
  size_t oldest = 0;
  for (size_t i = 1; i < MAX_RUNS_PER_DAY; i++)
    if (starts[i] < starts[oldest])
      oldest = i;
  starts[oldest] = static_cast<uint32_t>(now_epoch_s);
}

/// May an automatic run start now? `room_cold`: an enabled room is below its target
/// (the others close, so it would wait for heat); `idle_epoch_s`: when the heat source
/// was last seen not running (or Touch's start); `remeasure`: the picked loop already
/// has a curve; `last_remeasure_epoch_s`: start of the last automatic remeasurement
/// (0 = none); `run_left`: run_left_today.
inline bool may_start(bool hs_running, bool room_cold, int64_t now_epoch_s, int64_t idle_epoch_s, bool remeasure,
                      int64_t last_remeasure_epoch_s, bool run_left) {
  if (now_epoch_s <= 0 || !run_left)
    return false;
  if (hs_running && now_epoch_s - idle_epoch_s < RUNNING_FALLBACK_S)
    return false;
  // While the heat source runs the others hold, so a cold room keeps its flow.
  if (!hs_running && room_cold)
    return false;
  return !remeasure || last_remeasure_epoch_s <= 0 || now_epoch_s - last_remeasure_epoch_s >= REMEASURE_GAP_S;
}

enum class Stage : uint8_t { Off, Settling, Measuring };

/// One measurement in progress.
struct Run {
  Stage stage{Stage::Off};
  uint8_t node{0};
  uint8_t zone{0};  ///< 0..5
  uint8_t step{0};
  uint8_t n_steps{0};
  uint8_t refine_from{0};  ///< First refine step; 0 until the coarse pass is done
  uint8_t phase{0};  ///< Heat-source phase at the start (caller's code); a change aborts
  uint32_t stage_start_ms{0};
  uint32_t last_sample_ms{0};
  float flow_sum{0.0f};
  uint8_t flow_n{0};
  float head_sum{0.0f};  ///< Pump head over every measuring sample of the run
  uint16_t head_n{0};
  float plan[MAX_STEPS]{};
  Point pts[MAX_STEPS]{};
  uint8_t n_pts{0};
};

enum class Action : uint8_t {
  None,
  SendStep,  ///< send run.plan[run.step] to the test loop, hold the rest
  Finish,    ///< all steps measured: stop the hold, fit
  Abort,     ///< stop the hold, no result
};

struct TickInput {
  uint32_t now_ms{0};
  bool conditions_ok{false};  ///< pump fresh, V6 boards reachable, phase unchanged
  bool has_flow_sample{false};
  float flow_lmin{NAN};
  float head_m{NAN};  ///< Pump head with the flow sample (NaN: unknown)
};

/// Mean pump head over the run's measuring samples (NaN: none).
inline float mean_head(const Run &r) {
  return r.head_n > 0 ? r.head_sum / static_cast<float>(r.head_n) : NAN;
}

inline void start(Run &r, uint8_t node, uint8_t zone, uint32_t now_ms, uint8_t phase = 0) {
  r = Run{};
  r.phase = phase;
  r.stage = Stage::Settling;
  r.node = node;
  r.zone = zone;
  r.step = 0;
  for (size_t k = 0; k < STEPS; k++)
    r.plan[k] = STEP_PCT[k];
  r.n_steps = STEPS;
  r.stage_start_ms = now_ms;
  r.last_sample_ms = now_ms;
}

/// Advances the run. Returns SendStep right after start() and after every step.
inline Action tick(Run &r, const TickInput &in, bool just_started) {
  if (r.stage == Stage::Off)
    return Action::None;
  if (!in.conditions_ok) {
    r.stage = Stage::Off;
    return Action::Abort;
  }
  if (just_started)
    return Action::SendStep;
  if (in.has_flow_sample && std::isfinite(in.flow_lmin))
    r.last_sample_ms = in.now_ms;
  if (in.now_ms - r.last_sample_ms > SAMPLE_STALE_MS) {
    r.stage = Stage::Off;
    return Action::Abort;
  }
  const uint32_t elapsed = in.now_ms - r.stage_start_ms;
  if (r.stage == Stage::Settling) {
    const bool far = r.step == 0 || (r.refine_from != 0 && r.step == r.refine_from);
    const uint32_t settle = far ? SETTLE_FIRST_MS : SETTLE_MS;
    if (elapsed >= settle) {
      r.stage = Stage::Measuring;
      r.stage_start_ms = in.now_ms;
      r.flow_sum = 0.0f;
      r.flow_n = 0;
    }
    return Action::None;
  }
  // Measuring
  if (in.has_flow_sample && std::isfinite(in.flow_lmin) && r.flow_n < MAX_SAMPLES) {
    r.flow_sum += in.flow_lmin;
    r.flow_n++;
    if (std::isfinite(in.head_m)) {
      r.head_sum += in.head_m;
      r.head_n++;
    }
  }
  if (elapsed < MEASURE_MS)
    return Action::None;
  if (r.flow_n == 0) {
    r.stage = Stage::Off;
    return Action::Abort;
  }
  Point &p = r.pts[r.n_pts++];
  p.flow_lmin = r.flow_sum / static_cast<float>(r.flow_n);
  p.pos_pct = r.plan[r.step];  // commanded; V6 reports targets, not the motor
  if (r.step + 1 >= r.n_steps && r.refine_from == 0) {
    const size_t k = plan_refine(r.pts, r.n_pts, r.plan + r.n_steps, MAX_STEPS - r.n_steps);
    r.refine_from = r.n_steps;
    r.n_steps = static_cast<uint8_t>(r.n_steps + k);
  }
  if (r.step + 1 >= r.n_steps) {
    r.stage = Stage::Off;
    return Action::Finish;
  }
  r.step++;
  r.stage = Stage::Settling;
  r.stage_start_ms = in.now_ms;
  return Action::SendStep;
}

}  // namespace lune_touch_valve_probe
