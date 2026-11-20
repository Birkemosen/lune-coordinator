// Host tests for planner/hour_log.h.
#include "hour_log.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace lune_planner;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static int utc_hour(uint32_t ts) { return static_cast<int>((ts / 3600u) % 24); }

int main() {
  const uint32_t T0 = 1794787200u;   // a whole UTC hour (local midnight treated as UTC here)

  // --- Accumulator: polls each minute over two hours, meters reset at the hour.
  {
    HourAccumulator acc;
    Record r;
    HourExtra x;
    int closed = 0;
    float sh = 50.0f, el = 12.0f;
    for (int m = 0; m <= 120; m++) {
      PollSample s;
      s.ts = T0 + 23 * 3600u + m * 60u;   // 23:00 → 01:00
      s.house_c = m < 15 ? 22.0f : 22.4f;
      s.out_c = 4.0f;
      s.compressor_on = m >= 10 && m < 70;
      s.output_kw = s.compressor_on ? 4.0f : 0.0f;
      s.hp_feed_c = 30.0f;
      s.zone_feed_c = 28.0f;
      s.plan_kw = 4.0f;
      s.expected_c = 22.3f;
      if (m == 60) { sh = 0.0f; el = 0.0f; }   // midnight reset
      else if (s.compressor_on) { sh += 4.0f / 60; el += 1.0f / 60; }
      s.sh_heat_kwh = sh;
      s.sh_elec_kwh = el;
      if (acc.add(s, &r, &x)) {
        closed++;
        if (closed == 1) {
          assert(r.hour_start == T0 + 23 * 3600u);
          assert(near(r.get(Kind::HOUSE, H_ROOM_C), 22.0f, 0.01f));     // the hour's start
          assert(near(r.get(Kind::HOUSE, H_HEAT_SH_KWH), 50 * 4.0f / 60, 0.05f));
          assert(near(r.get(Kind::HOUSE, H_RUN_MIN), 50.0f, 1.1f));
          assert(near(r.get(Kind::HOUSE, H_STARTS), 1.0f, 0.01f));
          assert(near(x.plan_kw, 4.0f, 1e-3f));
          assert(near(r.get(Kind::HOUSE, H_ROOM_EXPECTED_C), 22.3f, 0.01f));
        } else {
          // 00:00 hour: heat is the output over its 10 running polls; the electricity meter
          // counts from 0 at the reset poll, then 9 more running minutes.
          assert(near(r.get(Kind::HOUSE, H_HEAT_SH_KWH), 10 * 4.0f / 60, 0.05f));
          assert(near(r.get(Kind::HOUSE, H_ELEC_KWH), 9 * 1.0f / 60, 0.02f));
        }
      }
    }
    assert(closed == 2);
  }

  // --- A daily meter that ticks once a day (the Ecodan's): the hours it does not move get
  // the integrated output, and the hour it lumps the day into keeps the output too; the
  // electricity of that hour is dropped.
  {
    HourAccumulator acc;
    Record r;
    HourExtra x;
    int closed = 0;
    for (int m = 0; m <= 180; m++) {
      PollSample s;
      s.ts = T0 + 20 * 3600u + m * 60u;   // 20:00 → 23:00
      s.house_c = 22.0f;
      s.compressor_on = true;
      s.output_kw = 4.0f;
      s.sh_heat_kwh = m < 150 ? 40.0f : 59.8f;   // the day's total lands at 22:30
      s.sh_elec_kwh = m < 150 ? 9.0f : 13.4f;
      if (acc.add(s, &r, &x)) {
        closed++;
        assert(near(r.get(Kind::HOUSE, H_HEAT_SH_KWH), 4.0f, 0.1f));
        assert(heat_usable(r));
        assert(!r.has(H_ELEC_KWH));   // unmoved, or 4.4 kWh against 4 kWh of heat: no COP
      }
    }
    assert(closed == 3);
  }
  // --- The live all-mode meter (Asgard's daily_total_energy_consumption, 0.1 kWh steps)
  // gives each hour its electricity while the per-mode meters stand still all day.
  {
    HourAccumulator acc;
    Record r;
    HourExtra x;
    int closed = 0;
    float tot = 7.0f;
    for (int m = 0; m <= 120; m++) {
      PollSample s;
      s.ts = T0 + 10 * 3600u + m * 60u;   // 10:00 → 12:00
      s.house_c = 22.0f;
      s.compressor_on = true;
      s.output_kw = 6.0f;
      s.sh_heat_kwh = 40.0f;              // the daily meters: unmoved all day
      s.sh_elec_kwh = 9.0f;
      if (m > 0 && m % 4 == 0) tot += 0.1f;   // 1.5 kWh an hour
      s.total_elec_kwh = std::round(tot * 10.0f) / 10.0f;
      if (acc.add(s, &r, &x)) {
        closed++;
        assert(near(r.get(Kind::HOUSE, H_HEAT_SH_KWH), 6.0f, 0.15f));
        assert(near(r.get(Kind::HOUSE, H_ELEC_KWH), 1.5f, 0.11f));
      }
    }
    assert(closed == 2);
  }
  // --- Hours the learners skip: more heat than an hour holds, or none while running.
  {
    Record r;
    r.hour_start = T0;
    r.set(Kind::HOUSE, H_ROOM_C, 22.0f);
    r.set(Kind::HOUSE, H_HEAT_SH_KWH, 59.79f);
    assert(!heat_usable(r));
    r.set(Kind::HOUSE, H_HEAT_SH_KWH, 0.0f);
    r.set(Kind::HOUSE, H_RUN_MIN, 55.0f);
    r.set(Kind::HOUSE, H_MODE_FLAGS, static_cast<float>(MODE_SPACE_HEATING));
    assert(!heat_usable(r));
    r.set(Kind::HOUSE, H_HEAT_SH_KWH, 4.2f);
    assert(heat_usable(r));
    r.set(Kind::HOUSE, H_HEAT_SH_KWH, 0.0f);
    r.set(Kind::HOUSE, H_RUN_MIN, 0.0f);
    r.set(Kind::HOUSE, H_MODE_FLAGS, 0.0f);
    assert(heat_usable(r));   // off: no heat is right
  }

  // --- Store: live hours replace, seeds only fill.
  auto st = std::make_unique<HistoryStore<24 * 60>>();
  st->clear();
  {
    Record a;
    a.hour_start = T0;
    a.set(Kind::HOUSE, H_ROOM_C, 21.0f);
    a.set(Kind::HOUSE, H_ROOM_EXPECTED_C, 21.2f);
    assert(st->seed(a));
    assert(!st->at(T0)->has(H_ROOM_EXPECTED_C));
    Record b = a;
    b.set(Kind::HOUSE, H_ROOM_C, 21.5f);
    assert(!st->seed(b));                       // already there
    HourExtra x{3.7f, 29.0f};
    st->put(b, x);
    assert(near(st->at(T0)->get(Kind::HOUSE, H_ROOM_C), 21.5f, 1e-3f));
    assert(near(st->plan_kw(T0), 3.7f, 1e-3f) && near(st->shadow_flow(T0), 29.0f, 0.05f));
    assert(st->at(T0 + 3600u) == nullptr && std::isnan(st->plan_kw(T0 + 3600u)));
    // A slot is reused 60 days later.
    Record c = a;
    c.hour_start = T0 + 24u * 60u * 3600u;
    st->put(c, HourExtra{});
    assert(st->at(T0) == nullptr && st->at(c.hour_start) != nullptr);
  }

  // --- Learning: 40 days from a known house under a simple thermostat, then learn
  // from a seed that is off by a third.
  st->clear();
  HouseParams truth;
  truth.ua_kw_per_k = 0.16f; truth.ca_kwh_per_k = 12.0f; truth.cs_kwh_per_k = 25.0f; truth.h_kw_per_k = 0.8f;
  truth.g_kw_per_100wm2 = 0.6f; truth.p_int_kw = 0.5f; truth.kw_per_ms = 0.04f;
  HeatPumpParams pump;
  pump.f0 = 1.36f; pump.f1 = 0.44f; pump.b0 = 1.36f; pump.b1 = 0.41f; pump.c0 = 7.49f; pump.c1 = -0.107f; pump.c2 = 0.094f;
  const int DAYS = 40;
  HouseState s{22.0f, 24.0f};
  bool on = false;
  for (int h = 0; h < DAYS * 24; h++) {
    const uint32_t hour = T0 + static_cast<uint32_t>(h) * 3600u;
    HourIn in;
    in.hour_local = static_cast<float>(h % 24);
    in.out_c = 5.0f + 4.0f * std::sin((h % 24 - 9) * 6.2831853f / 24) + 3.0f * std::sin(h / 70.0f);
    in.wind_ms = 4.0f + 3.0f * std::sin(h / 23.0f);
    const float day = std::sin((h % 24 - 6) * 3.14159265f / 12);
    in.sun_wm2 = day > 0 ? 300.0f * day * (0.5f + 0.5f * std::sin(h / 50.0f)) : 0.0f;
    if (s.t_c < 21.8f) on = true;
    if (s.t_c > 22.3f) on = false;
    in.q_kw = on ? 4.0f + (h % 3) : 0.0f;
    Record r;
    r.hour_start = hour;
    r.set(Kind::HOUSE, H_ROOM_C, s.t_c);
    r.set(Kind::HOUSE, H_OUTDOOR_C, in.out_c);
    r.set(Kind::HOUSE, H_WIND_MS, in.wind_ms);
    r.set(Kind::HOUSE, H_SOLAR_WM2, in.sun_wm2);
    r.set(Kind::HOUSE, H_HEAT_SH_KWH, in.q_kw);
    if (on) {
      const float room = s.t_c;
      const float zf = zone_feed_for(pump, in.q_kw, room), hf = hp_feed_for(pump, in.q_kw, room);
      r.set(Kind::HOUSE, H_ELEC_KWH, in.q_kw / cop_at(pump, hf, in.out_c));
      r.set(Kind::HOUSE, H_HP_FEED_C, hf);
      r.set(Kind::HOUSE, H_Z1_FEED_C, zf);
      r.set(Kind::HOUSE, H_RUN_MIN, 60.0f);
      r.set(Kind::HOUSE, H_MODE_FLAGS, static_cast<float>(MODE_SPACE_HEATING));
    } else {
      r.set(Kind::HOUSE, H_RUN_MIN, 0.0f);
      r.set(Kind::HOUSE, H_MODE_FLAGS, 0.0f);
    }
    st->put(r, HourExtra{});
    s = house_step(truth, s, in);
  }
  const uint32_t now = T0 + DAYS * 24u * 3600u;
  {
    // The floor tracked through the store lands near the true floor.
    Record r;
    r.hour_start = now;
    r.set(Kind::HOUSE, H_ROOM_C, s.t_c);
    st->put(r, HourExtra{});
    auto w = std::make_unique<LearnScratch>(truth);
    const float fl = floor_now(*st, now, truth, utc_hour, w.get());
    assert(near(fl, s.ts_c, 0.4f));
  }
  HouseParams seed = truth;
  seed.ua_kw_per_k *= 1.35f; seed.p_int_kw *= 0.6f; seed.ca_kwh_per_k *= 0.7f; seed.h_kw_per_k *= 1.3f;
  auto w = std::make_unique<LearnScratch>(seed);
  HouseParams learned;
  LearnReport rep;
  assert(learn_house(*st, now, seed, utc_hour, w.get(), &learned, &rep));
  std::printf("  house: %u days, %u h, energy %d, dynamics %d, rmse %.3f K; UA %.3f (truth %.3f, seed %.3f)\n",
              rep.days, rep.hours, rep.energy, rep.dynamics, rep.rmse_k, learned.ua_kw_per_k, truth.ua_kw_per_k,
              seed.ua_kw_per_k);
  assert(rep.days >= 30 && rep.energy && rep.dynamics);
  assert(std::fabs(learned.ua_kw_per_k - truth.ua_kw_per_k) < std::fabs(seed.ua_kw_per_k - truth.ua_kw_per_k) / 2);
  assert(rep.rmse_k < 0.1f);

  HeatPumpParams pseed = pump;
  pseed.c0 = 6.5f; pseed.f1 = 0.6f;
  HeatPumpParams plearned;
  assert(learn_heat_pump(*st, now, pseed, &plearned, &rep));
  std::printf("  heat pump: %u hours, f1 %.3f (truth %.3f), c0 %.2f (truth %.2f)\n", rep.hp_hours, plearned.f1, pump.f1,
              plearned.c0, pump.c0);
  assert(rep.hp_hours >= 24 && near(plearned.f1, pump.f1, 0.05f));
  assert(near(cop_at(plearned, 32.0f, 5.0f), cop_at(pump, 32.0f, 5.0f), 0.15f));

  std::printf("planner hour_log: all assertions passed\n");
  return 0;
}
