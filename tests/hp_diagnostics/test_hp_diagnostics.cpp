// Host tests for hp_diagnostics.h, with the morning of 21 Oct 2026 as the case:
// plan 3.9–6.3 kW, delivered 8–9.7 kW; secondary 15 l/min against primary 13 l/min.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "hp_diagnostics.h"

using namespace lune_touch_hp_diag;
static constexpr uint32_t MIN = 60UL * 1000UL;

static Input heating(float out_kw, float plan_kw, uint32_t t) {
  Input in;
  in.telemetry_fresh = true;
  in.telemetry_ms = t;
  in.operation_mode = 2;
  in.compressor_on = true;
  in.primary_pump_on = true;
  in.output_kw = out_kw;
  in.plan_known = true;
  in.plan_kw = plan_kw;
  in.primary_lmin = 13.0f;
  in.secondary_lmin = 12.0f;
  return in;
}

// Runs one sample a minute from t for n minutes; returns the time after.
template <typename F>
static uint32_t run(State &s, uint32_t t, uint32_t n, F make, Edges *acc = nullptr) {
  for (uint32_t i = 0; i < n; i++, t += MIN) {
    const Edges e = step(s, make(t), t);
    if (acc) {
      acc->over_raised |= e.over_raised; acc->over_cleared |= e.over_cleared;
      acc->under_raised |= e.under_raised; acc->under_cleared |= e.under_cleared;
      acc->mix_raised |= e.mix_raised; acc->mix_cleared |= e.mix_cleared;
    }
  }
  return t;
}

int main() {
  // --- OVER: 9.1 kW against a 6.3 kW plan, raised after 30 min, not before.
  {
    State s;
    uint32_t t = 1000;
    t = run(s, t, 29, [](uint32_t tt) { return heating(9.1f, 6.3f, tt); });
    assert(!s.over && s.dev == Dev::Ok);
    Edges e;
    t = run(s, t, 3, [](uint32_t tt) { return heating(9.1f, 6.3f, tt); }, &e);
    assert(s.over && e.over_raised && s.dev == Dev::Over);
    assert(std::fabs(s.mean_kw - 9.1f) < 0.01f);
    // Mean drops to 7.5 (1.19 ×): inside the hysteresis band, still over.
    t = run(s, t, 40, [](uint32_t tt) { return heating(7.5f, 6.3f, tt); });
    assert(s.over);
    // Below 1.15 × plan: clears.
    Edges c;
    t = run(s, t, 40, [](uint32_t tt) { return heating(6.5f, 6.3f, tt); }, &c);
    assert(!s.over && c.over_cleared && s.dev == Dev::Ok);
  }
  // Small plan: 1.3 × but not +1 kW → no flag (2.0 vs 1.5 kW).
  {
    State s;
    run(s, 1000, 60, [](uint32_t tt) { return heating(2.0f, 1.5f, tt); });
    assert(!s.over && s.dev == Dev::Ok);
  }
  // Plan 0 kW, heat pump at 5 kW: over; clears when the plan catches up.
  {
    State s;
    uint32_t t = run(s, 1000, 31, [](uint32_t tt) { return heating(5.0f, 0.0f, tt); });
    assert(s.over);
    run(s, t, 2, [](uint32_t tt) { return heating(5.0f, 4.8f, tt); });
    assert(!s.over);
  }
  // A short spike does not raise: 20 min over, then back on plan.
  {
    State s;
    uint32_t t = run(s, 1000, 20, [](uint32_t tt) { return heating(9.0f, 5.0f, tt); });
    t = run(s, t, 60, [](uint32_t tt) { return heating(5.0f, 5.0f, tt); });
    assert(!s.over);
  }
  // --- UNDER: 2.5 kW against 5 kW for 30 min; clears above 0.85 ×.
  {
    State s;
    Edges e;
    uint32_t t = run(s, 1000, 31, [](uint32_t tt) { return heating(2.5f, 5.0f, tt); }, &e);
    assert(s.under && e.under_raised && s.dev == Dev::Under);
    t = run(s, t, 40, [](uint32_t tt) { return heating(4.0f, 5.0f, tt); });   // 0.8 ×: holds
    assert(s.under);
    Edges c;
    run(s, t, 40, [](uint32_t tt) { return heating(4.6f, 5.0f, tt); }, &c);
    assert(!s.under && c.under_cleared);
  }
  // Plan ≤ 1 kW: never under.
  {
    State s;
    run(s, 1000, 60, [](uint32_t tt) { return heating(0.2f, 0.9f, tt); });
    assert(!s.under);
  }
  // --- Pauses: hot water / defrost / compressor off do not count and do not raise.
  {
    State s;
    run(s, 1000, 60, [](uint32_t tt) { Input in = heating(9.0f, 5.0f, tt); in.operation_mode = 1; return in; });
    assert(!s.over && s.dev == Dev::Idle);
    State d;
    run(d, 1000, 60, [](uint32_t tt) { Input in = heating(9.0f, 5.0f, tt); in.defrost_active = true; return in; });
    assert(!d.over && d.dev == Dev::Idle);
    State c;
    run(c, 1000, 60, [](uint32_t tt) { Input in = heating(0.0f, 5.0f, tt); in.compressor_on = false; return in; });
    assert(!c.under && c.dev == Dev::Idle);
  }
  // A raised flag survives a 10 min hot-water pause, and clears after 30 min idle.
  {
    State s;
    uint32_t t = run(s, 1000, 31, [](uint32_t tt) { return heating(9.0f, 5.0f, tt); });
    assert(s.over);
    t = run(s, t, 10, [](uint32_t tt) { Input in = heating(3.0f, 5.0f, tt); in.operation_mode = 1; return in; });
    assert(s.over && s.dev == Dev::Over);
    run(s, t, 25, [](uint32_t tt) { Input in = heating(0.0f, 5.0f, tt); in.compressor_on = false; return in; });
    assert(!s.over && s.dev == Dev::Idle);
  }
  // --- Unknown: no plan or stale telemetry → no flag, state unknown.
  {
    State s;
    uint32_t t = run(s, 1000, 31, [](uint32_t tt) { return heating(9.0f, 5.0f, tt); });
    assert(s.over);
    Edges e;
    run(s, t, 1, [](uint32_t tt) { Input in = heating(9.0f, 5.0f, tt); in.plan_known = false; return in; }, &e);
    assert(!s.over && s.dev == Dev::Unknown && e.over_cleared && std::isnan(s.mean_kw));
    State u;
    run(u, 1000, 60, [](uint32_t tt) { Input in = heating(9.0f, NAN, tt); return in; });
    assert(!u.over && u.dev == Dev::Unknown);
    State f;
    run(f, 1000, 60, [](uint32_t tt) { Input in = heating(9.0f, 5.0f, tt); in.telemetry_fresh = false; return in; });
    assert(!f.over && f.dev == Dev::Unknown && f.mix == Mix::Unknown);
  }
  // The same Asgard sample counted once, however often the loop runs.
  {
    State s;
    for (uint32_t i = 0; i < 50; i++)
      step(s, heating(9.0f, 5.0f, 1000), 1000 + i * 100);
    assert(s.count[(1000 / MIN + 1) % MEAN_MINUTES] == 1);
  }
  // --- Mixing: 15 l/min secondary over 13 primary for 10 min.
  {
    auto mixing = [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.secondary_lmin = 15.0f; return in; };
    State s;
    uint32_t t = run(s, 1000, 9, mixing);
    assert(!s.mixing && s.mix == Mix::Ok);
    Edges e;
    t = run(s, t, 2, mixing, &e);
    assert(s.mixing && e.mix_raised && s.mix == Mix::Mixing);
    assert(s.secondary_lmin == 15.0f && s.primary_lmin == 13.0f);
    // Primary pump stops: the flag stays (idle), no clear.
    t = run(s, t, 5, [](uint32_t tt) { Input in = heating(0.0f, 6.0f, tt); in.primary_pump_on = false; in.secondary_lmin = 15.0f; return in; });
    assert(s.mixing && s.mix == Mix::Mixing);
    // Between 1.00 and 1.05 ×: neither raise nor clear.
    t = run(s, t, 20, [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.secondary_lmin = 13.3f; return in; });
    assert(s.mixing);
    // Secondary below primary for 10 min: clears.
    Edges c;
    t = run(s, t, 9, [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.secondary_lmin = 12.0f; return in; }, &c);
    assert(s.mixing && !c.mix_cleared);
    run(s, t, 2, [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.secondary_lmin = 12.0f; return in; }, &c);
    assert(!s.mixing && c.mix_cleared && s.mix == Mix::Ok);
  }
  // Hot water: primary goes to the tank, mixing not judged.
  {
    State s;
    run(s, 1000, 30, [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.operation_mode = 1; in.secondary_lmin = 20.0f; return in; });
    assert(!s.mixing && s.mix == Mix::Idle);
  }
  // No Alpha2: unknown.
  {
    State s;
    run(s, 1000, 30, [](uint32_t tt) { Input in = heating(6.0f, 6.0f, tt); in.secondary_lmin = NAN; return in; });
    assert(!s.mixing && s.mix == Mix::Unknown);
  }
  // millis() wrap: hold timers keep working across 2^32.
  {
    State s;
    const uint32_t t0 = 0xFFFFFFFFu - 5 * MIN;
    run(s, t0, 32, [](uint32_t tt) { return heating(9.0f, 5.0f, tt); });
    assert(s.over);
  }
  std::puts("hp_diagnostics: ok");
  return 0;
}
