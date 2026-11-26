// Host tests for planner/heat_pump_model.h.
#include "heat_pump_model.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_planner;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
  // A known pump and floor: the learner finds them from clean hours.
  HeatPumpParams truth;
  truth.f0 = 1.2f; truth.f1 = 0.5f; truth.b0 = 1.5f; truth.b1 = 0.4f;
  truth.c0 = 7.2f; truth.c1 = -0.1f; truth.c2 = 0.1f;
  HeatPumpLearner l;
  for (int i = 0; i < 400; i++) {
    const float kw = 3.0f + (i % 7);           // 3–9 kW
    const float out = -2.0f + (i % 13);        // −2–10 °C
    const float room = 22.0f;
    const float zf = zone_feed_for(truth, kw, room), hf = hp_feed_for(truth, kw, room);
    l.learn(kw, kw / cop_at(truth, hf, out), hf, zf, room, out);
  }
  const HeatPumpParams &p = l.params();
  assert(p.valid);
  assert(near(p.f1, 0.5f, 0.02f) && near(p.b1, 0.4f, 0.03f) && near(p.c1, -0.1f, 0.01f));

  // Part load: the same heat costs less electricity at lower output (lower feed).
  const float e_low = 2 * elec_for(p, 4.0f, 22.0f, 5.0f), e_high = elec_for(p, 8.0f, 22.0f, 5.0f);
  assert(e_low < e_high);

  // Hours that are not full heating hours are ignored.
  HeatPumpLearner l2;
  l2.learn(0.2f, 0.1f, 35, 32, 22, 5);
  l2.learn(5.0f, NAN, 35, 32, 22, 5);
  assert(l2.samples() == 0);

  // Defrost: no minutes at or above defrost_below_c, more the colder it is, and a defrost
  // hour costs more electricity for the same heat.
  HeatPumpParams d;
  assert(defrost_min_for(d, 7.0f) == 0.0f && defrost_min_for(d, 10.0f) == 0.0f);
  assert(defrost_min_for(d, 0.0f) > defrost_min_for(d, 4.0f) && defrost_min_for(d, 4.0f) > 0.0f);
  assert(elec_for(d, 5.0f, 22.0f, 1.0f) / elec_for(d, 5.0f, 22.0f, 7.5f) > 1.0f);
  assert(cop_at(d, 35.0f, 1.0f, 6.0f) < cop_at(d, 35.0f, 1.0f, 0.0f));

  // The learner finds the defrost cost in COP and the defrost minutes per running hour.
  HeatPumpParams dt = truth;
  dt.c3 = -0.05f;
  dt.d0 = 8.0f; dt.d1 = -1.0f;
  HeatPumpLearner l3;
  for (int i = 0; i < 600; i++) {
    const float kw = 3.0f + (i % 7), out = -3.0f + (i % 11), room = 22.0f;
    const float zf = zone_feed_for(dt, kw, room), hf = hp_feed_for(dt, kw, room);
    const float dm = (i % 3) ? defrost_min_for(dt, out) : 0.0f;   // some hours without a defrost
    l3.learn(kw, kw / cop_at(dt, hf, out, dm), hf, zf, room, out, dm);
    l3.learn_defrost(defrost_min_for(dt, out) * 0.75f, 45.0f, out);   // 45 minutes running
  }
  const HeatPumpParams &q3 = l3.params();
  std::printf("defrost: c3 %.3f, d0 %.2f, d1 %.2f\n", q3.c3, q3.d0, q3.d1);
  assert(near(q3.c3, -0.05f, 0.01f) && near(q3.d0, 8.0f, 0.3f) && near(q3.d1, -1.0f, 0.06f));
  assert(near(q3.f1, 0.5f, 0.02f));   // floor curve from the hours without defrost
  // Unknown defrost minutes: the hour is not learned.
  HeatPumpLearner l4;
  l4.learn(5.0f, 1.2f, 35, 32, 22, 2, NAN);
  l4.learn_defrost(NAN, 60, 2);
  l4.learn_defrost(5, 20, 2);     // too little running time
  l4.learn_defrost(5, 60, 9);     // too warm for defrost
  assert(l4.samples() == 0 && l4.defrost_samples() == 0);

  // Efficient output: within the pump's range, higher when more COP loss is accepted.
  {
    HeatPumpParams q;
    const float strict = efficient_kw(q, 3.0f, 22.0f, 0.8f), loose = efficient_kw(q, 3.0f, 22.0f, 0.5f);
    std::printf("efficient output: %.1f kW at 0.8, %.1f kW at 0.5\n", strict, loose);
    assert(strict >= q.min_kw - 1e-3f && loose <= q.max_kw + 1e-3f && loose > strict);
  }

  std::printf("planner heat_pump_model: all assertions passed\n");
  return 0;
}
