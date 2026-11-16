#pragma once

// Room expectation for the shadow planner: the house's two-mass model (house_model.h)
// per room, on the hours Touch keeps for each room (7 days: hour-mean temperature and
// valve opening).
//
// A room's heat is its share of the house's space heating in the hour, split by valve
// opening × loop area × (zone feed − room). Ahead, a room gets the plan's heat by its
// share over the last few days: valves follow the rooms, not the plan, so the share is
// what the floors have been giving it lately. Each room is refitted once a day from a
// prior scaled to its floor area.
//
// What is shown blends the model with "the room stays as it is", per horizon, by how
// each did out of sample over the last days (yesterday's model on today's hours, at 3,
// 6, 12 and 24 h). Some rooms are driven more by doors, people and sun on the sensor
// than by anything the model sees, and a day later most rooms are back where they are
// now, so the model earns its weight in hours where it beats that.
// Pure C++, host-testable.

#include <cmath>
#include <cstddef>

#include "house_model.h"

namespace lune_planner {

static constexpr size_t ROOM_WINDOW = 7 * 24;
static constexpr int ROOM_SCORE_N = 4;
static constexpr int ROOM_SCORE_H[ROOM_SCORE_N] = {3, 6, 12, 24};

// A room's weight in an hour's floor heat. Without a zone feed (the pump was off, or no
// probe) the temperature difference counts as 5 K for every room alike.
inline float room_heat_weight(float valve_pct, float area_m2, float feed_c, float room_c) {
  if (!(valve_pct > 0.0f) || !(area_m2 > 0.0f)) return 0.0f;
  const float dt = std::isfinite(feed_c) && std::isfinite(room_c) ? std::fmax(0.0f, feed_c - room_c) : 5.0f;
  return valve_pct / 100.0f * area_m2 * dt;
}

struct RoomModel {
  HouseParams p{};
  float size{0};             // the room's share of the heated floor area
  bool fitted{false};
  float rmse_k{NAN};         // the last fit's 1–24 h forecast RMSE
  // Out-of-sample errors per horizon, decayed by day: model², persistence², their product.
  float se_m[ROOM_SCORE_N]{}, se_p[ROOM_SCORE_N]{}, se_mp[ROOM_SCORE_N]{};
  int scored{0};             // forecasts scored
  // The model's weight at horizon h hours (1…): minimum-variance blend of the two
  // forecasts, between the scored horizons linearly; 1 until anything is scored.
  float weight(int h) const {
    if (!fitted) return 0.0f;
    if (scored < 24) return 1.0f;
    float w[ROOM_SCORE_N];
    for (int j = 0; j < ROOM_SCORE_N; j++) {
      const float den = se_m[j] + se_p[j] - 2.0f * se_mp[j];
      const float v = den > 1e-6f ? (se_p[j] - se_mp[j]) / den : 0.5f;
      w[j] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    if (h <= ROOM_SCORE_H[0]) return w[0];
    for (int j = 1; j < ROOM_SCORE_N; j++)
      if (h <= ROOM_SCORE_H[j])
        return w[j - 1] + (w[j] - w[j - 1]) * static_cast<float>(h - ROOM_SCORE_H[j - 1]) /
                              static_cast<float>(ROOM_SCORE_H[j] - ROOM_SCORE_H[j - 1]);
    return w[ROOM_SCORE_N - 1];
  }
};

// The house's defaults scaled to the room's share of the floor area.
inline HouseParams room_prior(float size) {
  HouseParams p;
  p.ua_kw_per_k *= size; p.ca_kwh_per_k *= size; p.cs_kwh_per_k *= size; p.h_kw_per_k *= size;
  p.g_kw_per_100wm2 *= size; p.p_int_kw *= size;
  return p;
}

// Floors through the window from a start 2 K above the first measured hour; returns the
// floor at the start of hour n (the hour now).
inline float room_floor_now(const HouseParams &p, const float *t, const HourIn *in, size_t n, float *track) {
  size_t first = 0;
  while (first < n && !std::isfinite(t[first])) first++;
  if (first + 24 > n) return NAN;
  track_floor(p, &t[first], &in[first], n - first, t[first] + 2.0f, track);
  float s = track[n - first - 1];
  const float tl = std::isfinite(t[n - 1]) ? t[n - 1] : t[n];
  if (std::isfinite(tl))
    s += (in[n - 1].q_kw + p.sun_floor_frac * p.g_kw_per_100wm2 * in[n - 1].sun_wm2 / 100.0f - p.h_kw_per_k * (s - tl)) /
         p.cs_kwh_per_k;
  return s;
}

// How the current model did on hours [from, n): a forecast from each hour, at each
// scored horizon, against the room staying as it was. Earlier days count less (×0.95 a day).
// The forecasts run as forecast_room's do: the floors tracked on the room's own heat, the
// steps ahead on the house's heat (house_q) × `share`. (A room's hour-by-hour heat is a
// poor guide ahead: valves open because the room is cold, so it follows the room; on
// 2 Oct–16 Nov it forecast the rooms worse than staying as they are.)
inline void score_room(RoomModel *m, const float *t, const HourIn *in, const float *house_q, float share, size_t n,
                       size_t from, float *track) {
  if (!m->fitted) return;
  const float decay = std::pow(0.95f, static_cast<float>(n - from) / 24.0f);
  for (int j = 0; j < ROOM_SCORE_N; j++) { m->se_m[j] *= decay; m->se_p[j] *= decay; m->se_mp[j] *= decay; }
  track_floor(m->p, t, in, n, std::isfinite(t[0]) ? t[0] + 2.0f : 22.0f, track);
  const int hmax = ROOM_SCORE_H[ROOM_SCORE_N - 1];
  for (size_t s = from > static_cast<size_t>(hmax) ? from - hmax : 0; s + 1 <= n; s++) {
    if (!std::isfinite(t[s]) || !std::isfinite(track[s])) continue;
    HouseState st{t[s] - (s ? m->p.sensor_sun_k * in[s - 1].sun_wm2 / 100.0f : 0.0f), track[s]};
    for (int k = 1, j = 0; k <= hmax && s + k <= n && j < ROOM_SCORE_N; k++) {
      HourIn x = in[s + k - 1];
      x.q_kw = house_q[s + k - 1] * share;
      st = house_step(m->p, st, x);
      if (k != ROOM_SCORE_H[j]) continue;
      const float meas = t[s + k];
      // Only forecasts that end in [from, n]: the hours since the last score.
      if (s + k > from && std::isfinite(meas)) {
        const float em = sensor_reading(m->p, st.t_c, in[s + k - 1]) - meas, ep = t[s] - meas;
        m->se_m[j] += em * em;
        m->se_p[j] += ep * ep;
        m->se_mp[j] += em * ep;
        if (j == 1) m->scored++;
      }
      j++;
    }
  }
}

// Refit a room on its window (t: n + 1 hour-start temperatures, in: n hours with the
// room's heat in q_kw) from the prior for its size.
inline bool fit_room(RoomModel *m, HouseFitter *f, const float *t, const HourIn *in, size_t n, float size) {
  if (n < 4 * HouseFitter::HORIZON || n > HouseFitter::MAX_HOURS) return false;
  m->size = size;
  const HouseParams prior = room_prior(size);
  f->reset(prior, 0.01f, size);
  HouseParams p = prior;
  if (!f->fit(t, in, n, &p) && !std::isfinite(f->rmse())) return false;
  m->p = p;
  m->fitted = true;
  m->rmse_k = f->rmse();
  return true;
}

// The room's expected temperature at the end of each future hour: the model from the
// floors tracked through the window and the temperature now (t[n]), through `fut` (the
// room's planned heat in q_kw), blended with the temperature now by weight(). Returns
// false when only the temperature now could be shown.
inline bool forecast_room(const RoomModel &m, const float *t, const HourIn *in, size_t n, const HourIn *fut, int nf,
                          float *track, float *out) {
  float now = t[n];
  for (size_t i = n; !std::isfinite(now) && i > 0; i--) now = t[i - 1];
  if (!std::isfinite(now)) return false;
  const float floor = m.fitted ? room_floor_now(m.p, t, in, n, track) : NAN;
  if (!std::isfinite(floor)) {
    for (int k = 0; k < nf; k++) out[k] = now;
    return false;
  }
  HouseState st{now - m.p.sensor_sun_k * in[n - 1].sun_wm2 / 100.0f, floor};
  for (int k = 0; k < nf; k++) {
    st = house_step(m.p, st, fut[k]);
    const float w = m.weight(k + 1);
    out[k] = w * sensor_reading(m.p, st.t_c, fut[k]) + (1.0f - w) * now;
  }
  return true;
}

}  // namespace lune_planner
