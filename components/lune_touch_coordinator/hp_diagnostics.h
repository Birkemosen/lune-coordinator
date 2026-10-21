// =============================================================================
// Heat pump diagnostics — delivered heat against Odin's plan, and mixing in the
// buffer tank (pure C++, host-testable)
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
// Mixing (both pumps running, space heating):
//   secondary (Alpha2, l/min) > primary (Asgard flow_rate, l/min) × 1.05 for 10 min
//   → MIXING; clears when secondary < primary for 10 min. Return water is drawn back
//   into the supply through the tank, and the heat pump overshoots.
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
};

enum class Dev : uint8_t { Unknown, Idle, Ok, Over, Under };
enum class Mix : uint8_t { Unknown, Idle, Ok, Mixing };

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
  uint32_t mix_judged_ms = 0;
  float primary_lmin = NAN, secondary_lmin = NAN;
};

/// What changed in one step: the caller logs an event per edge.
struct Edges {
  bool over_raised = false, over_cleared = false;
  bool under_raised = false, under_cleared = false;
  bool mix_raised = false, mix_cleared = false;
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

inline void step_mixing(State &s, const Input &in, uint32_t now_ms, const Params &p, Edges &e) {
  const bool was = s.mixing;
  const bool known = in.telemetry_fresh && std::isfinite(in.secondary_lmin);
  if (!known) {
    s.mixing = false;
    s.mix_since_ms = s.unmix_since_ms = 0;
    s.mix = Mix::Unknown;
    s.primary_lmin = s.secondary_lmin = NAN;
  } else if (!cp::mixing_sample_usable(in.primary_lmin, in.secondary_lmin, in.primary_pump_on,
                                        space_heating(in) ? ASGARD_SPACE_HEAT : -1)) {
    // A stopped pump says nothing about the ratio; a raised flag stays until the
    // pumps run again (it is a property of the pump settings), at most IDLE_CLEAR_MS.
    s.mix_since_ms = s.unmix_since_ms = 0;
    if (s.mix_judged_ms == 0 || now_ms - s.mix_judged_ms >= IDLE_CLEAR_MS)
      s.mixing = false;
    s.mix = s.mixing ? Mix::Mixing : Mix::Idle;
  } else {
    s.mix_judged_ms = nz(now_ms);
    s.primary_lmin = in.primary_lmin;
    s.secondary_lmin = in.secondary_lmin;
    if (in.secondary_lmin > in.primary_lmin * p.mix_ratio) {
      s.unmix_since_ms = 0;
      if (s.mix_since_ms == 0)
        s.mix_since_ms = nz(now_ms);
      if (now_ms - s.mix_since_ms >= MIX_HOLD_MS)
        s.mixing = true;
    } else {
      s.mix_since_ms = 0;
      if (in.secondary_lmin < in.primary_lmin) {
        if (s.unmix_since_ms == 0)
          s.unmix_since_ms = nz(now_ms);
        if (now_ms - s.unmix_since_ms >= MIX_HOLD_MS)
          s.mixing = false;
      } else {
        s.unmix_since_ms = 0;   // between 1.00 and 1.05: neither raise nor clear
      }
    }
    s.mix = s.mixing ? Mix::Mixing : Mix::Ok;
  }
  e.mix_raised = !was && s.mixing;
  e.mix_cleared = was && !s.mixing;
}

inline Edges step(State &s, const Input &in, uint32_t now_ms, const Params &p = Params{}) {
  Edges e;
  step_deviation(s, in, now_ms, p, e);
  step_mixing(s, in, now_ms, p, e);
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

inline const char *mix_name(Mix m) {
  switch (m) {
    case Mix::Idle: return "idle";
    case Mix::Ok: return "ok";
    case Mix::Mixing: return "mixing";
    default: return "unknown";
  }
}

}  // namespace lune_touch_hp_diag
