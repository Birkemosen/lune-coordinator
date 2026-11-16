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

  std::printf("planner heat_pump_model: all assertions passed\n");
  return 0;
}
