// =============================================================================
// Heat pump diagnostics — delivered heat against Odin's plan, the buffer tank's
// balance, and the circulation pump's flow against the open loops (pure C++,
// host-testable)
// =============================================================================
// Odin plans heat per hour (kW) but commands only a flow temperature:
//   flow target = return + planned kW / (flow × 4.18 / 60)
// It never checks what the heat pump actually delivers. Measured delivery has been
// 0.5–2.5× the plan (21 Oct 08–11: plan 3.9–6.3 kW, actual 8–9.7 kW). Touch watches
// it and says so; it does not act on it.
//
// Plan deviation (judged only while Asgard reports space heating, the compressor
// runs, and neither hot water, legionella nor defrost is active):
//   mean = running mean of Asgard computed_output_power over the last 30 minutes
//   OVER   mean > 1.3 × plan and mean > plan + 1 kW, held 30 min
//          clears when mean < 1.15 × plan (or < plan + 0.5 kW, so a 0 kW plan can clear)
//   UNDER  plan > 1 kW and mean < 0.7 × plan, held 30 min; clears when mean > 0.85 × plan
//   A flag survives short pauses (hot water, defrost, compressor off) and clears
//   after IDLE_CLEAR_MS without a judgeable sample. Missing data (stale telemetry,
//   no plan): no flag, state "unknown" — fail quiet.
//
// Balance across the buffer tank (both pumps running, space heating; circulation_pump.h):
//   secondary (Alpha2, l/min) < primary (Asgard flow_rate) × 0.97 for 10 min → SHORT;
//   clears when secondary ≥ primary for 10 min. The heat pump's surplus runs through
//   the tank straight back to it and its return warms up.
//   secondary > primary × 1.25 for 10 min → MIXING; clears when < primary × 1.15 for
//   10 min. Floor return is drawn into the supply, and the heat pump runs hotter.
//   Which of the two apply follows the tank's piping (circulation_pump.h BufferTank):
//   SHORT only with a four-port tank, MIXING with a three- or four-port tank.
//   Both say only that the two circuits are out of step. The secondary flow follows the
//   valves (few loops open at night, many in a cold spell), so neither is a pump fault.
//
// Flow against the open loops (any tank): whether the circulation pump moves what the
// open loops normally take. Measured 14–24 Nov 2026 (2-min samples, valves steady 10 min,
// 12 loops): secondary ≈ 3.9 l/min + 0.090 l/min per m² of open loop, r² 0.93, rmse
// 1.5 l/min. V6 holds an open loop at 19 % or more, and the pin's quick opening gives most
// of the flow there, so a loop counts as open (valve > OPEN_VALVE_PCT) with its whole area;
// weighting by opening fitted worse (r² 0.75–0.81).
//   Touch learns a + b × open area per house: least squares, exponentially weighted with a
//   half-life of FLOW_HALF_LIFE samples (one a minute while learning), from minutes with the
//   compressor on in space heating, both pumps running, the open area unchanged (±1 m²)
//   for 10 min, no valve flush and no flow flag raised.
//   Judged once FLOW_MIN_SAMPLES minutes are learned over a spread of open areas
//   (σ ≥ FLOW_MIN_X_STD_M2), while the expected flow is at least FLOW_MIN_EXPECTED_LMIN
//   (below it the pump's minimum and bypass dominate):
//   LOW   secondary < 0.7 × expected and ≥ 3 l/min below, 30 min; clears at ≥ 0.8 ×
//         (filter clogging, air, pump fault or setting changed)
//   HIGH  secondary > 1.4 × expected and ≥ 3 l/min above, 30 min; clears at ≤ 1.25 ×
//         (a valve that does not close, pump setting changed)
// =============================================================================

#pragma once

#include "circulation_pump.h"

#include <cmath>
#include <cstdint>

namespace lune_touch_hp_diag {

namespace cp = esphome::lune_touch_coordinator::circulation_pump;

constexpr uint32_t MIN_MS = 60UL * 1000UL;
constexpr uint8_t MEAN_MINUTES = 30;
constexpr uint32_t DEV_HOLD_MS = 30UL * MIN_MS;
constexpr uint32_t IDLE_CLEAR_MS = 30UL * MIN_MS;
constexpr uint32_t MIX_HOLD_MS = 10UL * MIN_MS;
constexpr uint32_t FLOW_HOLD_MS = 30UL * MIN_MS;
constexpr uint32_t FLOW_STEADY_MS = 10UL * MIN_MS;
constexpr float OPEN_VALVE_PCT = 5.0f;
constexpr float FLOW_STEADY_M2 = 1.0f;
constexpr float FLOW_HALF_LIFE = 3.0f * 24.0f * 60.0f;  ///< samples (minutes of learning)
constexpr float FLOW_MIN_SAMPLES = 360.0f;
constexpr float FLOW_MIN_X_STD_M2 = 15.0f;
constexpr float FLOW_MIN_EXPECTED_LMIN = 8.0f;
constexpr float FLOW_LOW_RATIO = 0.7f, FLOW_LOW_CLEAR_RATIO = 0.8f;
constexpr float FLOW_HIGH_RATIO = 1.4f, FLOW_HIGH_CLEAR_RATIO = 1.25f;
constexpr float FLOW_MIN_GAP_LMIN = 3.0f;
/// Asgard operation_mode for space heating (1 = hot water, 6 = legionella).
constexpr int ASGARD_SPACE_HEAT = 2;

struct Params {
  float over_ratio = 1.3f;
  float over_margin_kw = 1.0f;
  float over_clear_ratio = 1.15f;
  float over_clear_margin_kw = 0.5f;
  float under_min_plan_kw = 1.0f;
  float under_ratio = 0.7f;
  float under_clear_ratio = 0.85f;
  float mix_ratio = cp::MIXING_RISK_RATIO;
  float mix_clear_ratio = 1.15f;
  float short_ratio = cp::SHORT_FLOW_RATIO;
  float short_clear_ratio = 1.0f;
  bool judge_short = true;  ///< cp::judges_short(tank)
  bool judge_mix = true;    ///< cp::judges_mixing(tank)
};

enum class Dev : uint8_t { Unknown, Idle, Ok, Over, Under };
enum class Mix : uint8_t { Unknown, Idle, Ok, Short, Mixing };
enum class FlowCheck : uint8_t { Unknown, Learning, Idle, Ok, Low, High };

/// Exponentially weighted least squares of secondary flow on open loop area. Persisted.
struct FlowFit {
  double w = 0.0, x = 0.0, y = 0.0, xx = 0.0, xy = 0.0;   ///< weighted sums
  float n = 0.0f;                                         ///< samples, decayed like w

  void add(float area_m2, float flow_lmin) {
    const double k = std::pow(0.5, 1.0 / static_cast<double>(FLOW_HALF_LIFE));
    w = w * k + 1.0;
    x = x * k + area_m2;
    y = y * k + flow_lmin;
    xx = xx * k + static_cast<double>(area_m2) * area_m2;
    xy = xy * k + static_cast<double>(area_m2) * flow_lmin;
    n = n * static_cast<float>(k) + 1.0f;
  }
  float x_std() const {
    if (w <= 0.0) return 0.0f;
    const double v = xx / w - (x / w) * (x / w);
    return v > 0.0 ? static_cast<float>(std::sqrt(v)) : 0.0f;
  }
  /// a + b × area; false until enough spread and a plausible slope.
  bool coeffs(float *a, float *b) const {
    if (n < FLOW_MIN_SAMPLES || x_std() < FLOW_MIN_X_STD_M2)
      return false;
    const double mx = x / w, my = y / w;
    const double var = xx / w - mx * mx, cov = xy / w - mx * my;
    if (var <= 0.0) return false;
    const double bb = cov / var, aa = my - bb * mx;
    if (!(bb > 0.01 && bb < 0.5 && aa > -5.0 && aa < 15.0))
      return false;
    *a = static_cast<float>(aa);
    *b = static_cast<float>(bb);
    return true;
  }
};

struct Input {
  bool telemetry_fresh = false;
  uint32_t telemetry_ms = 0;   ///< time of the Asgard sample (a sample counts once)
  int operation_mode = -1;     ///< Asgard
  bool compressor_on = false;
  bool primary_pump_on = false;
  bool dhw_active = false;     ///< from Asgard binary sensors / Odin telemetry, false when unknown
  bool legionella_active = false;
  bool defrost_active = false;
  float output_kw = NAN;       ///< Asgard computed_output_power
  bool plan_known = false;
  float plan_kw = NAN;         ///< Odin heat for the current hour
  float primary_lmin = NAN;    ///< Asgard flow_rate
  float secondary_lmin = NAN;  ///< Alpha2 flow; NAN when stale
  float open_area_m2 = NAN;    ///< area of loops with valve > OPEN_VALVE_PCT; NAN when a loop is stale
  bool valve_flush = false;    ///< Touch is flushing a loop (valves held)
};

struct State {
  // 30 one-minute buckets of output kW while space heating.
  float sum[MEAN_MINUTES]{};
  uint16_t count[MEAN_MINUTES]{};
  uint32_t minute[MEAN_MINUTES]{};   ///< minute index + 1 of the bucket (0 = empty)
  uint32_t last_sample_ms = 0;
  // Plan deviation.
  Dev dev = Dev::Unknown;
  bool over = false, under = false;
  uint32_t over_since_ms = 0, under_since_ms = 0;   ///< 0 = condition not met
  uint32_t last_judged_ms = 0;
  float mean_kw = NAN;
  float plan_kw = NAN;
  // Mixing.
  Mix mix = Mix::Unknown;
  bool mixing = false;
  uint32_t mix_since_ms = 0, unmix_since_ms = 0;
  bool short_flow = false;
  uint32_t short_since_ms = 0, unshort_since_ms = 0;
  uint32_t mix_judged_ms = 0;
  float primary_lmin = NAN, secondary_lmin = NAN;
  // Flow against the open loops.
  FlowFit fit;
  bool fit_dirty = false;
  FlowCheck flow = FlowCheck::Unknown;
  bool flow_low = false, flow_high = false;
  uint32_t low_since_ms = 0, unlow_since_ms = 0, high_since_ms = 0, unhigh_since_ms = 0;
  uint32_t flow_judged_ms = 0;
  float area_ref_m2 = NAN;          ///< open area the steady clock runs from
  uint32_t area_since_ms = 0;
  uint32_t learn_minute = 0;        ///< minute index + 1 of the last learned sample
  float open_area_m2 = NAN, expected_lmin = NAN;
};

/// What changed in one step: the caller logs an event per edge.
struct Edges {
  bool over_raised = false, over_cleared = false;
  bool under_raised = false, under_cleared = false;
  bool mix_raised = false, mix_cleared = false;
  bool short_raised = false, short_cleared = false;
  bool low_raised = false, low_cleared = false;
  bool high_raised = false, high_cleared = false;
};

inline uint32_t nz(uint32_t ms) { return ms == 0 ? 1 : ms; }

inline float mean_kw(const State &s, uint32_t now_ms) {
  const uint32_t now_min = now_ms / MIN_MS + 1;
  float sum = 0.0f;
  uint32_t n = 0;
  for (uint8_t i = 0; i < MEAN_MINUTES; i++) {
    if (s.minute[i] == 0 || now_min - s.minute[i] >= MEAN_MINUTES)
      continue;
    sum += s.sum[i];
    n += s.count[i];
  }
  return n ? sum / static_cast<float>(n) : NAN;
}

inline void add_sample(State &s, float kw, uint32_t now_ms) {
  const uint32_t m = now_ms / MIN_MS + 1;
  const uint8_t i = static_cast<uint8_t>(m % MEAN_MINUTES);
  if (s.minute[i] != m) {
    s.minute[i] = m;
    s.sum[i] = 0.0f;
    s.count[i] = 0;
  }
  s.sum[i] += kw;
  if (s.count[i] < 0xFFFF)
    s.count[i]++;
}

inline bool space_heating(const Input &in) {
  return in.operation_mode == ASGARD_SPACE_HEAT && !in.dhw_active && !in.legionella_active &&
         !in.defrost_active;
}

inline void step_deviation(State &s, const Input &in, uint32_t now_ms, const Params &p, Edges &e) {
  const bool was_over = s.over, was_under = s.under;
  auto clear_all = [&]() {
    s.over = s.under = false;
    s.over_since_ms = s.under_since_ms = 0;
  };
  const bool plan_ok = in.plan_known && std::isfinite(in.plan_kw) && in.plan_kw >= 0.0f;
  s.plan_kw = plan_ok ? in.plan_kw : NAN;
  if (!in.telemetry_fresh || !plan_ok) {
    // Fail quiet: nothing is known, nothing is flagged.
    clear_all();
    s.dev = Dev::Unknown;
    s.mean_kw = NAN;
  } else if (!space_heating(in) || !in.compressor_on || !std::isfinite(in.output_kw)) {
    // Pause: hot water, defrost or a stopped compressor say nothing about the plan.
    s.over_since_ms = s.under_since_ms = 0;
    if (s.last_judged_ms == 0 || now_ms - s.last_judged_ms >= IDLE_CLEAR_MS)
      clear_all();
    s.mean_kw = mean_kw(s, now_ms);
    s.dev = s.over ? Dev::Over : s.under ? Dev::Under : Dev::Idle;
  } else {
    if (in.telemetry_ms != s.last_sample_ms) {
      add_sample(s, in.output_kw < 0.0f ? 0.0f : in.output_kw, now_ms);
      s.last_sample_ms = in.telemetry_ms;
    }
    s.last_judged_ms = nz(now_ms);
    const float mean = mean_kw(s, now_ms);
    const float plan = in.plan_kw;
    s.mean_kw = mean;
    // Over.
    const bool over_cond = mean > p.over_ratio * plan && mean > plan + p.over_margin_kw;
    if (over_cond) {
      if (s.over_since_ms == 0)
        s.over_since_ms = nz(now_ms);
      if (!s.over && now_ms - s.over_since_ms >= DEV_HOLD_MS)
        s.over = true;
    } else {
      s.over_since_ms = 0;
      if (s.over && (mean < p.over_clear_ratio * plan || mean < plan + p.over_clear_margin_kw))
        s.over = false;
    }
    // Under.
    const bool under_cond = plan > p.under_min_plan_kw && mean < p.under_ratio * plan;
    if (under_cond) {
      if (s.under_since_ms == 0)
        s.under_since_ms = nz(now_ms);
      if (!s.under && now_ms - s.under_since_ms >= DEV_HOLD_MS)
        s.under = true;
    } else {
      s.under_since_ms = 0;
      if (s.under && (mean > p.under_clear_ratio * plan || plan <= p.under_min_plan_kw))
        s.under = false;
    }
    if (s.over)
      s.under = false;
    s.dev = s.over ? Dev::Over : s.under ? Dev::Under : Dev::Ok;
  }
  e.over_raised = !was_over && s.over;
  e.over_cleared = was_over && !s.over;
  e.under_raised = !was_under && s.under;
  e.under_cleared = was_under && !s.under;
}

// One side of the balance: raise after MIX_HOLD_MS with the condition met, clear after
// MIX_HOLD_MS with the clear condition met; between the two nothing changes.
inline void hold_flag(bool &flag, uint32_t &since, uint32_t &unsince, bool raise_cond, bool clear_cond,
                      uint32_t now_ms, uint32_t hold_ms = MIX_HOLD_MS) {
  if (raise_cond) {
    unsince = 0;
    if (since == 0)
      since = nz(now_ms);
    if (now_ms - since >= hold_ms)
      flag = true;
  } else {
    since = 0;
    if (clear_cond) {
      if (unsince == 0)
        unsince = nz(now_ms);
      if (now_ms - unsince >= hold_ms)
        flag = false;
    } else {
      unsince = 0;
    }
  }
}

inline Mix mix_of(const State &s) {
  return s.mixing ? Mix::Mixing : s.short_flow ? Mix::Short : Mix::Ok;
}

inline void step_mixing(State &s, const Input &in, uint32_t now_ms, const Params &p, Edges &e) {
  const bool was_mix = s.mixing, was_short = s.short_flow;
  const bool known = in.telemetry_fresh && std::isfinite(in.secondary_lmin);
  if (!known) {
    s.mixing = s.short_flow = false;
    s.mix_since_ms = s.unmix_since_ms = s.short_since_ms = s.unshort_since_ms = 0;
    s.mix = Mix::Unknown;
    s.primary_lmin = s.secondary_lmin = NAN;
  } else if (!cp::mixing_sample_usable(in.primary_lmin, in.secondary_lmin, in.primary_pump_on,
                                        space_heating(in) ? ASGARD_SPACE_HEAT : -1)) {
    // A stopped pump says nothing about the ratio; a raised flag stays until the
    // pumps run again (it is a property of the pump settings), at most IDLE_CLEAR_MS.
    s.mix_since_ms = s.unmix_since_ms = s.short_since_ms = s.unshort_since_ms = 0;
    if (s.mix_judged_ms == 0 || now_ms - s.mix_judged_ms >= IDLE_CLEAR_MS)
      s.mixing = s.short_flow = false;
    s.mix = s.mixing || s.short_flow ? mix_of(s) : Mix::Idle;
  } else {
    s.mix_judged_ms = nz(now_ms);
    s.primary_lmin = in.primary_lmin;
    s.secondary_lmin = in.secondary_lmin;
    const float sec = in.secondary_lmin, pri = in.primary_lmin;
    hold_flag(s.mixing, s.mix_since_ms, s.unmix_since_ms, p.judge_mix && sec > pri * p.mix_ratio,
              !p.judge_mix || sec < pri * p.mix_clear_ratio, now_ms);
    hold_flag(s.short_flow, s.short_since_ms, s.unshort_since_ms, p.judge_short && sec < pri * p.short_ratio,
              !p.judge_short || sec >= pri * p.short_clear_ratio, now_ms);
    if (!p.judge_mix)
      s.mixing = false;
    if (!p.judge_short)
      s.short_flow = false;
    s.mix = mix_of(s);
  }
  e.mix_raised = !was_mix && s.mixing;
  e.mix_cleared = was_mix && !s.mixing;
  e.short_raised = !was_short && s.short_flow;
  e.short_cleared = was_short && !s.short_flow;
}


inline void step_flow(State &s, const Input &in, uint32_t now_ms, Edges &e) {
  const bool was_low = s.flow_low, was_high = s.flow_high;
  const bool known = in.telemetry_fresh && std::isfinite(in.secondary_lmin) && std::isfinite(in.open_area_m2);
  s.open_area_m2 = known ? in.open_area_m2 : NAN;
  // The steady clock: restarts when the open area moves by more than FLOW_STEADY_M2.
  if (!known || !std::isfinite(s.area_ref_m2) || std::fabs(in.open_area_m2 - s.area_ref_m2) > FLOW_STEADY_M2) {
    s.area_ref_m2 = known ? in.open_area_m2 : NAN;
    s.area_since_ms = nz(now_ms);
  }
  float a = NAN, b = NAN;
  const bool fitted = s.fit.coeffs(&a, &b);
  s.expected_lmin = fitted && known ? a + b * in.open_area_m2 : NAN;
  const bool running = known && !in.valve_flush && in.compressor_on &&
                       cp::mixing_sample_usable(in.primary_lmin, in.secondary_lmin, in.primary_pump_on,
                                                space_heating(in) ? ASGARD_SPACE_HEAT : -1);
  if (!known) {
    s.flow_low = s.flow_high = false;
    s.low_since_ms = s.unlow_since_ms = s.high_since_ms = s.unhigh_since_ms = 0;
    s.flow = FlowCheck::Unknown;
  } else if (!running) {
    // Like the balance: a raised flag waits for the pumps to run again, at most IDLE_CLEAR_MS.
    s.low_since_ms = s.unlow_since_ms = s.high_since_ms = s.unhigh_since_ms = 0;
    if (s.flow_judged_ms == 0 || now_ms - s.flow_judged_ms >= IDLE_CLEAR_MS)
      s.flow_low = s.flow_high = false;
    s.flow = s.flow_low ? FlowCheck::Low : s.flow_high ? FlowCheck::High : fitted ? FlowCheck::Idle : FlowCheck::Learning;
  } else {
    const bool steady = now_ms - s.area_since_ms >= FLOW_STEADY_MS;
    const uint32_t minute = now_ms / MIN_MS + 1;
    if (steady && !s.flow_low && !s.flow_high && minute != s.learn_minute) {
      s.fit.add(in.open_area_m2, in.secondary_lmin);
      s.learn_minute = minute;
      s.fit_dirty = true;
    }
    const bool judge = fitted && steady && s.expected_lmin >= FLOW_MIN_EXPECTED_LMIN;
    if (judge) {
      s.flow_judged_ms = nz(now_ms);
      const float sec = in.secondary_lmin, ex = s.expected_lmin;
      hold_flag(s.flow_low, s.low_since_ms, s.unlow_since_ms,
                sec < ex * FLOW_LOW_RATIO && ex - sec >= FLOW_MIN_GAP_LMIN, sec >= ex * FLOW_LOW_CLEAR_RATIO,
                now_ms, FLOW_HOLD_MS);
      hold_flag(s.flow_high, s.high_since_ms, s.unhigh_since_ms,
                sec > ex * FLOW_HIGH_RATIO && sec - ex >= FLOW_MIN_GAP_LMIN, sec <= ex * FLOW_HIGH_CLEAR_RATIO,
                now_ms, FLOW_HOLD_MS);
    } else {
      s.low_since_ms = s.high_since_ms = 0;
    }
    s.flow = s.flow_low ? FlowCheck::Low : s.flow_high ? FlowCheck::High
             : !fitted ? FlowCheck::Learning : judge ? FlowCheck::Ok : FlowCheck::Idle;
  }
  e.low_raised = !was_low && s.flow_low;
  e.low_cleared = was_low && !s.flow_low;
  e.high_raised = !was_high && s.flow_high;
  e.high_cleared = was_high && !s.flow_high;
}

inline Edges step(State &s, const Input &in, uint32_t now_ms, const Params &p = Params{}) {
  Edges e;
  step_deviation(s, in, now_ms, p, e);
  step_mixing(s, in, now_ms, p, e);
  step_flow(s, in, now_ms, e);
  return e;
}

inline const char *dev_name(Dev d) {
  switch (d) {
    case Dev::Idle: return "idle";
    case Dev::Ok: return "ok";
    case Dev::Over: return "over";
    case Dev::Under: return "under";
    default: return "unknown";
  }
}

inline const char *flow_check_name(FlowCheck f) {
  switch (f) {
    case FlowCheck::Learning: return "learning";
    case FlowCheck::Idle: return "idle";
    case FlowCheck::Ok: return "ok";
    case FlowCheck::Low: return "low";
    case FlowCheck::High: return "high";
    default: return "unknown";
  }
}

inline const char *mix_name(Mix m) {
  switch (m) {
    case Mix::Idle: return "idle";
    case Mix::Ok: return "ok";
    case Mix::Short: return "short";
    case Mix::Mixing: return "mixing";
    default: return "unknown";
  }
}

}  // namespace lune_touch_hp_diag
