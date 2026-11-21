#pragma once

// Shadow planner, F2: the 48-hour heat plan and the flow target that delivers it.
//
// Plan: start with no heat, simulate the house (house_model.h), and add heat one step at
// a time where it removes the most comfort miss (K·h below the floor or above the top)
// per euro: price × electricity for the step at that hour's output (heat_pump_model.h;
// COP falls as output rises). An hour runs at 0 or between the pump's minimum and
// maximum output, so its first step jumps to the minimum. The floors delay the heat, so
// a step early can overshoot later; counting overshoot as a miss keeps that in check.
// The strategy decides where the hard floor is:
//   comfort first  the band's floor;
//   price first    the floor less `dip_k` (the house may dip a little to save money);
//   weighted       as price first, and between the two floors heat is added while a step
//                  costs less than the comfort it buys (penalty € per K·h).
// The plan is the house's; rooms come from it in F3.
// Sun ahead: an hour whose house would, without heat, climb back to the floor on forecast
// sun within the next few hours may sag below the floor by up to `sun_hold_k` first, so the
// plan does not charge the floors for a morning the sun will warm anyway (20 Nov 2026: a
// plan of 7.7 kW before a sunny morning, which still ended 1 K over the band).
//
// Flow target: the zone feed the floor curve says the planned output needs, trimmed by
// the measured output against the plan (both ways, bounded and rate-limited). Unlike a
// target anchored on the return it never rises because the return rose.
// Pure C++, host-testable; a 48 h plan is a few thousand house steps.

#include <cmath>
#include <cstdint>

#include "heat_pump_model.h"
#include "house_model.h"

namespace lune_planner {

static constexpr int PLAN_H = 48;

enum class Strategy : uint8_t { COMFORT = 0, WEIGHTED = 1, PRICE = 2 };

struct PlanHour {
  HourIn weather;          // outdoor, sun, wind, local hour (q_kw is ignored)
  float price_eur_kwh{NAN};
  float band_min_c{NAN};   // comfort floor for the house at the end of the hour
  float band_max_c{NAN};   // top of the band
};

struct SolverSettings {
  Strategy strategy{Strategy::COMFORT};
  float penalty_eur_per_kh{0.5f};   // weighted: what a degree below the floor for an hour may cost
  float dip_k{0.5f};                // price first / weighted: how far below the floor the house may go
  float step_kw{0.5f};
  float room_c{22.0f};              // for the floor curve (zone feed − room)
  float sun_hold_k{0.5f};           // how far below the floor the house may sag ahead of sun (0 = off)
  float sun_hold_wm2{150.0f};       // sun that counts as warming the house back
  int sun_hold_h{6};                // how far ahead the sun may come
  void (*yield)() {nullptr};        // called once per step on a device, so a long plan lets other tasks run
};

struct HeatPlan {
  int n{0};
  float kw[PLAN_H];
  float t_end_c[PLAN_H];       // expected house temperature at the end of each hour
  float zone_feed_c[PLAN_H];   // flow target to the floors for the planned output
  float elec_kwh[PLAN_H];
  float cost_eur{0};
  float heat_kwh{0};
  float below_kh{0};           // K·h below the comfort floor that remain
  float relief_k[PLAN_H];      // how far below the floor the plan let the house sag ahead of sun
};

inline float price_of_(const PlanHour &h) { return std::isfinite(h.price_eur_kwh) ? h.price_eur_kwh : 0.3f; }

// Simulate the plan's heat through the house; fills t_end and the shortfall.
inline void simulate_plan(const HouseParams &hp, const HouseState &start, const PlanHour *hours, HeatPlan *p) {
  HouseState s = start;
  p->below_kh = 0;
  for (int i = 0; i < p->n; i++) {
    HourIn in = hours[i].weather;
    in.q_kw = p->kw[i];
    s = house_step(hp, s, in);
    p->t_end_c[i] = s.t_c;
    if (std::isfinite(hours[i].band_min_c) && s.t_c < hours[i].band_min_c) p->below_kh += hours[i].band_min_c - s.t_c;
  }
}

inline void finish_plan(const HeatPumpParams &pump, const PlanHour *hours, float room_c, HeatPlan *p) {
  p->cost_eur = p->heat_kwh = 0;
  for (int i = 0; i < p->n; i++) {
    p->elec_kwh[i] = elec_for(pump, p->kw[i], room_c, hours[i].weather.out_c);
    p->zone_feed_c[i] = p->kw[i] > 0 ? zone_feed_for(pump, p->kw[i], room_c) : NAN;
    p->cost_eur += p->elec_kwh[i] * price_of_(hours[i]);
    p->heat_kwh += p->kw[i];
  }
}

// Sun relief per hour (see the header): with no heat, the house at hour i is below the
// floor but is back on it at a later hour within sun_hold_h whose sun is at least
// sun_hold_wm2. Relief is the gap, at most sun_hold_k. Fills p->relief_k.
inline void sun_relief_(const HouseParams &hp, const HouseState &start, const PlanHour *hours,
                        const SolverSettings &cfg, HeatPlan *p) {
  for (int i = 0; i < p->n; i++) { p->relief_k[i] = 0; p->kw[i] = 0; }
  if (!(cfg.sun_hold_k > 0)) return;
  simulate_plan(hp, start, hours, p);
  for (int i = 0; i < p->n; i++) {
    const float gap = hours[i].band_min_c - p->t_end_c[i];
    if (!(gap > 0)) continue;
    for (int j = i + 1; j < p->n && j <= i + cfg.sun_hold_h; j++) {
      if (hours[j].weather.sun_wm2 >= cfg.sun_hold_wm2 && p->t_end_c[j] >= hours[j].band_min_c) {
        p->relief_k[i] = gap < cfg.sun_hold_k ? gap : cfg.sun_hold_k;
        break;
      }
    }
  }
}

// Comfort miss of a plan: K·h below the floor (less `floor_off` and the sun relief) plus
// K·h above the top.
inline float comfort_miss_(const PlanHour *hours, const HeatPlan &q, float floor_off, float *below_out) {
  float below = 0, above = 0;
  for (int i = 0; i < q.n; i++) {
    const float f = hours[i].band_min_c - floor_off - q.relief_k[i];
    if (std::isfinite(f) && q.t_end_c[i] < f) below += f - q.t_end_c[i];
    if (std::isfinite(hours[i].band_max_c) && q.t_end_c[i] > hours[i].band_max_c) above += q.t_end_c[i] - hours[i].band_max_c;
  }
  if (below_out) *below_out = below;
  return below + above;
}

// Adds heat one step at a time, always the step that removes the most comfort miss per
// euro, anywhere in the horizon. Hard pass (penalty = ∞): until nothing is below the
// floor or no step helps. Soft pass: while a step costs less than penalty × the K·h it
// removes.
inline void fill_plan_(const HouseParams &hp, const HeatPumpParams &pump, const HouseState &start,
                       const PlanHour *hours, const SolverSettings &cfg, float floor_off, float penalty,
                       HeatPlan *p) {
  simulate_plan(hp, start, hours, p);
  float below = 0;
  float miss = comfort_miss_(hours, *p, floor_off, &below);
  const int max_steps = p->n * static_cast<int>(pump.max_kw / cfg.step_kw + 2);
  for (int step = 0; step < max_steps; step++) {
    if (cfg.yield) cfg.yield();
    if (penalty == INFINITY && below <= 0.01f) break;
    int best = -1;
    float best_ratio = INFINITY, best_kw = 0, best_cost = 0, best_gain = 0, best_miss = miss, best_below = below;
    for (int j = 0; j < p->n; j++) {
      const float now = p->kw[j];
      const float next = now <= 0 ? pump.min_kw : now + cfg.step_kw;
      if (next > pump.max_kw + 1e-3f) continue;
      const float c = (elec_for(pump, next, cfg.room_c, hours[j].weather.out_c) -
                       elec_for(pump, now, cfg.room_c, hours[j].weather.out_c)) * price_of_(hours[j]);
      p->kw[j] = next;
      simulate_plan(hp, start, hours, p);
      float b2 = 0;
      const float m2 = comfort_miss_(hours, *p, floor_off, &b2);
      p->kw[j] = now;
      const float gain = miss - m2;
      if (gain <= 1e-4f) continue;
      const float ratio = c / gain;
      if (ratio < best_ratio) { best = j; best_ratio = ratio; best_kw = next; best_cost = c; best_gain = gain; best_miss = m2; best_below = b2; }
    }
    if (best < 0) break;                                   // no step improves comfort
    if (best_cost > penalty * best_gain) break;            // not worth it (soft pass)
    p->kw[best] = best_kw;
    miss = best_miss;
    below = best_below;
  }
  simulate_plan(hp, start, hours, p);
}

inline void solve_plan(const HouseParams &hp, const HeatPumpParams &pump, const HouseState &start,
                       const PlanHour *hours, int n, const SolverSettings &cfg, HeatPlan *p) {
  p->n = n > PLAN_H ? PLAN_H : n;
  sun_relief_(hp, start, hours, cfg, p);
  const float hard_off = cfg.strategy == Strategy::COMFORT ? 0.0f : cfg.dip_k;
  fill_plan_(hp, pump, start, hours, cfg, hard_off, INFINITY, p);
  if (cfg.strategy == Strategy::WEIGHTED) fill_plan_(hp, pump, start, hours, cfg, 0.0f, cfg.penalty_eur_per_kh, p);
  simulate_plan(hp, start, hours, p);
  finish_plan(pump, hours, cfg.room_c, p);
}

// Flow target to the floors, every few minutes: the floor curve's feed for the planned
// output, trimmed by how far the measured output is from the plan. Trim is bounded and
// rate-limited, and decays when the pump is off.
class FlowController {
 public:
  struct Config {
    float trim_max_k{3.0f};
    float k_per_kw{0.5f};          // trim per kW of error, per update
    float rate_k{0.5f};            // largest change per update
    float min_c{24.0f}, max_c{35.0f};
  };
  FlowController() = default;
  explicit FlowController(const Config &c) : cfg_(c) {}

  float update(const HeatPumpParams &pump, float plan_kw, float measured_kw, float room_c) {
    if (!(plan_kw > 0)) {
      trim_ *= 0.5f;
      last_ = NAN;
      return NAN;
    }
    if (std::isfinite(measured_kw)) {
      const float err = plan_kw - measured_kw;          // >0: delivering too little
      float d = cfg_.k_per_kw * err;
      d = d > cfg_.rate_k ? cfg_.rate_k : (d < -cfg_.rate_k ? -cfg_.rate_k : d);
      trim_ += d;
      trim_ = trim_ > cfg_.trim_max_k ? cfg_.trim_max_k : (trim_ < -cfg_.trim_max_k ? -cfg_.trim_max_k : trim_);
    }
    float t = zone_feed_for(pump, plan_kw, room_c) + trim_;
    t = t < cfg_.min_c ? cfg_.min_c : (t > cfg_.max_c ? cfg_.max_c : t);
    last_ = t;
    return t;
  }
  float trim() const { return trim_; }
  void set_limits(float min_c, float max_c) { cfg_.min_c = min_c; cfg_.max_c = max_c; }

 private:
  Config cfg_{};
  float trim_{0};
  float last_{NAN};
};

}  // namespace lune_planner
