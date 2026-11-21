#pragma once

// Live scores of the shadow planner's forecasts at fixed horizons, against "it stays as it
// is now" (persistence). Three kinds:
//
//   house, plan    the plan's expected house temperature (HeatPlan::t_end_c), kept when the
//                  plan is made and scored when its hour comes. It assumes the plan's own
//                  heat, which nobody delivers in shadow mode, so it mixes the model's
//                  error with the plan's.
//   house, model   the house model re-run over the same hours on the heat that was
//                  actually delivered and the weather that was logged (score_house_hour):
//                  the model alone, without plan execution or forecast weather.
//   rooms          each room's shown forecast (RoomModel blend on the plan's heat), kept
//                  and scored like the plan's.
//
// Each score is exponentially weighted with a week's memory, so it follows the model as it
// is refitted. Scores are not kept across a restart. Pure C++, host-testable.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "house_model.h"

namespace lune_planner {

static constexpr int SCORE_N = 5;
static constexpr int SCORE_H[SCORE_N] = {1, 3, 6, 12, 24};
static constexpr float SCORE_DECAY = 0.99406f;   // exp(−1/168): a week's memory, per scored forecast

struct HorizonScore {
  float n{0};      // weight of the forecasts scored (decayed count)
  float se{0};     // squared error
  float e{0};      // error (forecast − measured)
  float sp{0};     // persistence's squared error
  uint32_t count{0};
  void add(float err, float perr) {
    if (!std::isfinite(err) || !std::isfinite(perr)) return;
    n = n * SCORE_DECAY + 1.0f;
    se = se * SCORE_DECAY + err * err;
    e = e * SCORE_DECAY + err;
    sp = sp * SCORE_DECAY + perr * perr;
    count++;
  }
  float rmse() const { return n > 0 ? std::sqrt(se / n) : NAN; }
  float bias() const { return n > 0 ? e / n : NAN; }
  float persistence_rmse() const { return n > 0 ? std::sqrt(sp / n) : NAN; }
};

struct ScoreSet {
  HorizonScore h[SCORE_N];
};

// Forecasts waiting for their hour: one slot per target hour and horizon (slot = target
// hour % SLOTS, SLOTS above the longest horizon). The first forecast for a target hour at a
// horizon is kept; a re-plan later in the same hour does not replace it.
template <int SLOTS> struct PendingForecasts {
  static_assert(SLOTS > 24, "a slot must outlive the longest horizon");
  struct Slot {
    uint32_t target{0};
    float value{NAN};
    float base{NAN};   // the measured value when the forecast was made (persistence)
  };
  Slot s[SCORE_N][SLOTS];

  static size_t slot(uint32_t target_hour) { return (target_hour / 3600u) % SLOTS; }
  void put(int j, uint32_t target_hour, float value, float base) {
    if (!std::isfinite(value) || !std::isfinite(base)) return;
    Slot &x = s[j][slot(target_hour)];
    if (x.target == target_hour) return;
    x = {target_hour, value, base};
  }
  // The plan's forecasts from a plan made in `hour0`: value[k] is the end of hour k, i.e.
  // hour0 + (k + 1) h.
  void put_plan(uint32_t hour0, const float *value, int n, float base) {
    for (int j = 0; j < SCORE_N; j++)
      if (SCORE_H[j] <= n) put(j, hour0 + static_cast<uint32_t>(SCORE_H[j]) * 3600u, value[SCORE_H[j] - 1], base);
  }
  // Scores every forecast for `target_hour` against `measured` and frees its slot.
  void score(uint32_t target_hour, float measured, ScoreSet *out) {
    for (int j = 0; j < SCORE_N; j++) {
      Slot &x = s[j][slot(target_hour)];
      if (x.target != target_hour) continue;
      if (std::isfinite(measured)) out->h[j].add(x.value - measured, x.base - measured);
      x = Slot{};
    }
  }
};

// The house model re-run on what happened: for each horizon H, from the measured house at
// hour n − H (floors tracked through the hours before) through the logged heat and weather
// to hour n, against the measured house at hour n. t: n + 1 hour-start temperatures,
// in: n hours, track: n + 1 floats of scratch. Needs n ≥ 48 so the floors have settled.
inline void score_house_hour(const HouseParams &p, const float *t, const HourIn *in, size_t n, float *track,
                             ScoreSet *out) {
  if (n < 48 || !std::isfinite(t[n])) return;
  size_t first = 0;
  while (first < n && !std::isfinite(t[first])) first++;
  if (first + 24 + static_cast<size_t>(SCORE_H[SCORE_N - 1]) > n) return;
  track_floor(p, &t[first], &in[first], n - first, t[first] + 2.0f, &track[first]);
  for (int j = 0; j < SCORE_N; j++) {
    const size_t s = n - static_cast<size_t>(SCORE_H[j]);
    if (s < first + 24 || !std::isfinite(t[s]) || !std::isfinite(track[s])) continue;
    HouseState st{t[s] - (s ? p.sensor_sun_k * in[s - 1].sun_wm2 / 100.0f : 0.0f), track[s]};
    for (size_t k = s; k < n; k++) st = house_step(p, st, in[k]);
    out->h[j].add(sensor_reading(p, st.t_c, in[n - 1]) - t[n], t[s] - t[n]);
  }
}

}  // namespace lune_planner
