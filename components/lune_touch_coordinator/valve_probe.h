// =============================================================================
// Valve curve measurement (pure C++, host-testable)
// =============================================================================
// V6's dynamic balancing turns each loop's relative flow into an opening through
// a valve curve: the opening where flow starts and where it is full. Manifold
// valve inserts open quickly and differ per loop, so Touch measures the curve from
// the circulation pump's flow, which only Touch sees:
//
//   1. every loop on every V6 holds its position (V6 /valve-test, zone 0);
//   2. the test loop steps through STEP_PCT; after each step settles, the pump's
//      flow is averaged (MEASURE_MS);
//   3. fit flow = base + delta · ramp(opening; start, full) by least squares;
//   4. a good fit (delta and R² large enough) is written to V6 with its time.
//
// With a constant-pressure pump the change in total flow is the loop's own flow.
// Runs at night while the heat pump is idle, a few loops per night, each loop
// again after REMEASURE_S. Aborts (no result) when the heat pump wakes, the pump
// reading goes stale or a V6 does not answer; V6 releases the hold by its TTL.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_valve_probe {

constexpr size_t STEPS = 10;
constexpr float STEP_PCT[STEPS] = {0, 5, 10, 15, 20, 25, 30, 40, 60, 100};
constexpr uint32_t SETTLE_FIRST_MS = 150000;  ///< First step: the valve may travel far
constexpr uint32_t SETTLE_MS = 75000;
constexpr uint32_t MEASURE_MS = 60000;
constexpr uint32_t SAMPLE_STALE_MS = 90000;   ///< No pump reading this long → abort
constexpr uint32_t HOLD_TTL_S = 300;          ///< V6 hold TTL, refreshed every step
constexpr uint16_t CIRCULATION_POLL_S = 15;   ///< Pump poll interval while measuring
constexpr float MIN_DELTA_LMIN = 0.8f;        ///< Smallest loop flow worth a curve
constexpr float MIN_R2 = 0.8f;
constexpr uint8_t WINDOW_START_H = 1;         ///< Local hours [start, end)
constexpr uint8_t WINDOW_END_H = 5;
constexpr uint8_t MAX_PER_NIGHT = 3;
constexpr uint32_t REMEASURE_S = 30UL * 86400UL;
constexpr uint32_t RETRY_FAILED_S = 86400UL;
constexpr size_t MAX_SAMPLES = 16;

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
/// start (0..40 %, 0.5 steps) and full (start + 10 .. 100 %, 1 % steps).
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
  for (float s = 0.0f; s <= 40.0f; s += 0.5f) {
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

/// Inside the night window (local hour).
inline bool in_window(int hour) { return hour >= WINDOW_START_H && hour < WINDOW_END_H; }

/// Is a loop due? `measured_epoch_s` from V6 (0 = never), `failed_epoch_s` from
/// Touch's own memory of a failed try (0 = none).
inline bool loop_due(uint32_t measured_epoch_s, uint32_t failed_epoch_s, int64_t now_epoch_s) {
  if (now_epoch_s <= 0)
    return false;
  const int64_t now = now_epoch_s;
  if (failed_epoch_s != 0 && now - static_cast<int64_t>(failed_epoch_s) < RETRY_FAILED_S)
    return false;
  return measured_epoch_s == 0 || now - static_cast<int64_t>(measured_epoch_s) >= REMEASURE_S;
}

enum class Stage : uint8_t { Off, Settling, Measuring };

/// One measurement in progress.
struct Run {
  Stage stage{Stage::Off};
  uint8_t node{0};
  uint8_t zone{0};  ///< 0..5
  uint8_t step{0};
  uint32_t stage_start_ms{0};
  uint32_t last_sample_ms{0};
  float flow_sum{0.0f};
  uint8_t flow_n{0};
  Point pts[STEPS]{};
  uint8_t n_pts{0};
};

enum class Action : uint8_t {
  None,
  SendStep,  ///< send STEP_PCT[run.step] to the test loop, hold the rest
  Finish,    ///< all steps measured: stop the hold, fit
  Abort,     ///< stop the hold, no result
};

struct TickInput {
  uint32_t now_ms{0};
  bool conditions_ok{false};  ///< heat pump idle, pump fresh, V6 boards reachable
  bool has_flow_sample{false};
  float flow_lmin{NAN};
};

inline void start(Run &r, uint8_t node, uint8_t zone, uint32_t now_ms) {
  r = Run{};
  r.stage = Stage::Settling;
  r.node = node;
  r.zone = zone;
  r.step = 0;
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
    const uint32_t settle = r.step == 0 ? SETTLE_FIRST_MS : SETTLE_MS;
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
  }
  if (elapsed < MEASURE_MS)
    return Action::None;
  if (r.flow_n == 0) {
    r.stage = Stage::Off;
    return Action::Abort;
  }
  Point &p = r.pts[r.n_pts++];
  p.flow_lmin = r.flow_sum / static_cast<float>(r.flow_n);
  p.pos_pct = STEP_PCT[r.step];  // commanded; V6 reports targets, not the motor
  if (r.step + 1 >= STEPS) {
    r.stage = Stage::Off;
    return Action::Finish;
  }
  r.step++;
  r.stage = Stage::Settling;
  r.stage_start_ms = in.now_ms;
  return Action::SendStep;
}

}  // namespace lune_touch_valve_probe
