// =============================================================================
// System curve — the circulation pump's flow at full load, learned from operation
// (pure C++, host-testable)
// =============================================================================
// The pump head needed for the recommended secondary flow depends on how far the
// valves are open, which changes with the rooms' load. Scaling the live operating
// point (H ∝ Q²) answers for the valves as they are now: with warm rooms nearly
// closed it asked for 7.3 m where 5 m was enough at full load.
//
// Instead Touch learns how the house's flow follows the valves:
//
//   x = Σ loop flow (V6's flow_pct, % of full) × the loop's measured full flow, at
//       REF_HEAD_M (valve_probe.h): what the loops would pass each on its own.
//   y = the pump's flow scaled to REF_HEAD_M (flow ∝ √head).
//
// y is not x: valves near the start of their curve pass more than the curve says,
// and with many loops open the shared pipes and the tank limit the total (author's
// house, a week at 3.4 and 4.5 m: y ≈ 4.5 at x ≈ 1.5, 15 at 13, 22 at 25). So y(x) is
// kept as a table of time-weighted means per x bin, decaying over a few days as the
// valves' seats drift and are relearned.
//
// Full load is every room calling: each loop at its design share (V6's measured
// shares: heat demand per litre of full flow, the board's largest = 1) × Touch's house
// balance, capped at full flow (dynamic_balance.h with every load factor 1). The head
// for a target flow at full load is then REF_HEAD_M × (target / y(x_full))².
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lune_touch_system_curve {

constexpr float REF_HEAD_M = 3.0f;      ///< Same reference head as the valve curves
constexpr float BIN_LMIN = 2.0f;        ///< x bin width (l/min at REF_HEAD_M)
constexpr size_t BINS = 20;             ///< x from 0 to 40 l/min
constexpr float TAU_H = 72.0f;          ///< Old samples fade with this time constant
constexpr float MAX_SAMPLE_H = 10.0f / 60.0f;  ///< A longer gap counts as this much
constexpr float MIN_WEIGHT_H = 1.0f;    ///< A bin is used with at least this much data
constexpr float MIN_HEAD_M = 0.5f;
constexpr float MIN_FLOW_LMIN = 1.0f;

struct Curve {
  float sum[BINS]{};     ///< Σ weight × y
  float weight[BINS]{};  ///< Σ weight (hours, decayed)
};

struct Loop {
  bool enabled{false};
  float full_lmin{0.0f};  ///< Measured full flow at REF_HEAD_M; 0 = not measured
  float ua_w_k{0.0f};     ///< Heat demand (V6's UA_effective)
  float balance{1.0f};    ///< Touch's house balance as applied on the board
  float flow_frac{NAN};   ///< Live relative flow (V6 flow_pct / 100)
};

inline bool usable(const Loop &l) { return l.enabled && l.full_lmin > 0.0f; }

/// Live x: Σ relative flow × full flow. NAN unless every enabled loop has a full flow
/// and a live flow (a loop without one would be counted as closed).
inline float live_x(const Loop *loops, size_t n) {
  float x = 0.0f;
  size_t used = 0;
  for (size_t k = 0; k < n; k++) {
    const Loop &l = loops[k];
    if (!l.enabled)
      continue;
    if (!usable(l) || !std::isfinite(l.flow_frac))
      return NAN;
    x += std::clamp(l.flow_frac, 0.0f, 1.0f) * l.full_lmin;
    used++;
  }
  return used > 0 ? x : NAN;
}

/// Full-load x for `boards` boards of `per_board` loops (loops[b * per_board + z]):
/// each loop at min(1, share × balance), share = (UA / full flow) / the board's largest.
/// NAN unless every enabled loop has a full flow and a heat demand.
inline float full_load_x(const Loop *loops, size_t boards, size_t per_board) {
  float x = 0.0f;
  size_t used = 0;
  for (size_t b = 0; b < boards; b++) {
    const Loop *board = loops + b * per_board;
    float max_need = 0.0f;
    for (size_t z = 0; z < per_board; z++) {
      const Loop &l = board[z];
      if (!l.enabled)
        continue;
      if (!usable(l) || !(l.ua_w_k > 0.0f))
        return NAN;
      max_need = std::max(max_need, l.ua_w_k / l.full_lmin);
    }
    if (!(max_need > 0.0f))
      continue;
    for (size_t z = 0; z < per_board; z++) {
      const Loop &l = board[z];
      if (!l.enabled)
        continue;
      const float share = l.ua_w_k / l.full_lmin / max_need;
      x += std::min(1.0f, share * std::clamp(l.balance, 0.0f, 1.0f)) * l.full_lmin;
      used++;
    }
  }
  return used > 0 ? x : NAN;
}

/// Adds a sample: pump flow and head now, x now, `dt_h` since the previous sample
/// (capped at MAX_SAMPLE_H). Every bin fades by exp(−dt / TAU_H) first. Returns false
/// when the sample is unusable (nothing is changed then).
inline bool add(Curve &c, float x, float flow_lmin, float head_m, float dt_h) {
  if (!std::isfinite(x) || x < 0.0f || !std::isfinite(flow_lmin) || flow_lmin < MIN_FLOW_LMIN ||
      !std::isfinite(head_m) || head_m < MIN_HEAD_M || !std::isfinite(dt_h) || !(dt_h > 0.0f))
    return false;
  const size_t bin = std::min(BINS - 1, static_cast<size_t>(x / BIN_LMIN));
  const float w = std::min(dt_h, MAX_SAMPLE_H);
  const float fade = std::exp(-w / TAU_H);
  for (size_t k = 0; k < BINS; k++) {
    c.sum[k] *= fade;
    c.weight[k] *= fade;
  }
  c.sum[bin] += w * flow_lmin * std::sqrt(REF_HEAD_M / head_m);
  c.weight[bin] += w;
  return true;
}

/// y(x) at REF_HEAD_M: linear between the nearest populated bin centres on either side
/// (at most two bins apart), or the nearest one when x is within a bin of it. NAN where
/// the house has not run.
inline float flow_at(const Curve &c, float x) {
  if (!std::isfinite(x) || x < 0.0f)
    return NAN;
  auto centre = [](size_t k) { return (static_cast<float>(k) + 0.5f) * BIN_LMIN; };
  auto mean = [&c](size_t k) { return c.sum[k] / c.weight[k]; };
  int left = -1, right = -1;
  for (size_t k = 0; k < BINS; k++) {
    if (c.weight[k] < MIN_WEIGHT_H)
      continue;
    if (centre(k) <= x)
      left = static_cast<int>(k);
    else if (right < 0)
      right = static_cast<int>(k);
  }
  const bool l_near = left >= 0 && x - centre(left) <= 2.0f * BIN_LMIN;
  const bool r_near = right >= 0 && centre(right) - x <= 2.0f * BIN_LMIN;
  if (l_near && r_near) {
    const float t = (x - centre(left)) / (centre(right) - centre(left));
    return mean(left) + t * (mean(right) - mean(left));
  }
  if (l_near && x - centre(left) <= BIN_LMIN)
    return mean(left);
  if (r_near && centre(right) - x <= BIN_LMIN)
    return mean(right);
  return NAN;
}

/// Flow at `head_m` from a flow at REF_HEAD_M.
inline float flow_at_head(float ref_lmin, float head_m) {
  if (!std::isfinite(ref_lmin) || !std::isfinite(head_m) || head_m <= 0.0f)
    return NAN;
  return ref_lmin * std::sqrt(head_m / REF_HEAD_M);
}

/// Head for `target_lmin` where the house passes `ref_lmin` at REF_HEAD_M, to 0.1 m.
inline float head_for(float ref_lmin, float target_lmin) {
  if (!std::isfinite(ref_lmin) || ref_lmin < MIN_FLOW_LMIN || !std::isfinite(target_lmin) ||
      target_lmin <= 0.0f)
    return NAN;
  const float r = target_lmin / ref_lmin;
  return std::round(REF_HEAD_M * r * r * 10.0f) / 10.0f;
}

}  // namespace lune_touch_system_curve
