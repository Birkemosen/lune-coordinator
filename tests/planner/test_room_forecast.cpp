// Host tests for planner/room_forecast.h.
#include "room_forecast.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <new>
#include <vector>

using namespace lune_planner;

int main() {
  // Heat weights: valve × area × (feed − room); no feed → 5 K for every room.
  assert(std::fabs(room_heat_weight(50.0f, 20.0f, 30.0f, 22.0f) - 80.0f) < 1e-3f);
  assert(std::fabs(room_heat_weight(50.0f, 20.0f, NAN, 22.0f) - 50.0f) < 1e-3f);
  assert(room_heat_weight(0.0f, 20.0f, 30.0f, 22.0f) == 0.0f && room_heat_weight(NAN, 20.0f, 30.0f, 22.0f) == 0.0f);

  // A room (a tenth of the house) under a thermostat for two weeks, with a sunny
  // afternoon each day and a few hours without a reading.
  const float size = 0.1f;
  HouseParams truth = room_prior(size);
  truth.ua_kw_per_k = 0.02f; truth.g_kw_per_100wm2 = 0.12f; truth.p_int_kw = 0.03f;
  const size_t H = 14 * 24 + ROOM_WINDOW;
  std::vector<float> t(H + 1);
  std::vector<HourIn> in(H);
  std::vector<float> hq(H);
  HouseState s{21.5f, 23.0f};
  bool on = false;
  for (size_t h = 0; h < H; h++) {
    HourIn &x = in[h];
    x.hour_local = static_cast<float>(h % 24);
    x.out_c = 4.0f + 3.0f * std::sin((h % 24 - 9) * 6.2831853f / 24) + 2.0f * std::sin(h / 60.0f);
    x.wind_ms = 3.0f;
    const float day = std::sin((h % 24 - 6) * 3.14159265f / 12);
    x.sun_wm2 = day > 0 ? 400.0f * day * (0.6f + 0.4f * std::sin(h / 40.0f)) : 0.0f;
    if (s.t_c < 21.6f) on = true;
    if (s.t_c > 22.1f) on = false;
    hq[h] = on ? 5.0f : 0.0f;
    x.q_kw = 0.1f * hq[h];
    t[h] = (h % 37 == 5) ? NAN : s.t_c;
    s = house_step(truth, s, x);
  }
  t[H] = s.t_c;

  // A fitter in memory that was never cleared (a device's heap): the fit must not read
  // slots its windows never wrote, and a window without a start is skipped.
  void *mem = ::operator new(sizeof(HouseFitter));
  float *junk = static_cast<float *>(mem);
  for (size_t i = 0; i < sizeof(HouseFitter) / sizeof(float); i++) junk[i] = static_cast<float>((i * 7919) % 1000) * 0.01f;
  HouseFitter *f = new (mem) HouseFitter();
  RoomModel m;
  const size_t W = ROOM_WINDOW;
  size_t now = W;
  assert(fit_room(&m, f, &t[now - W], &in[now - W], W, size));
  std::printf("  room fit: rmse %.3f K, UA %.4f (truth %.4f, prior %.4f)\n", m.rmse_k, m.p.ua_kw_per_k, truth.ua_kw_per_k,
              room_prior(size).ua_kw_per_k);
  assert(m.fitted && m.rmse_k < 0.1f);
  assert(m.weight(6) == 1.0f);   // nothing scored yet

  // Daily: score yesterday's model on today, refit; forecast 12 h from each hour.
  std::vector<float> track(W + 1), out(24);
  std::vector<HourIn> fut(24);
  double se = 0, sp = 0;
  int cnt = 0;
  for (now = W + 24; now + 24 <= H; now += 24) {
    score_room(&m, &t[now - W], &in[now - W], &hq[now - W], 0.1f, W, W - 24, track.data());
    assert(fit_room(&m, f, &t[now - W], &in[now - W], W, size));
    for (int k = 0; k < 24; k++) { fut[k] = in[now + k]; fut[k].q_kw = hq[now + k] * 0.1f; }
    if (!std::isfinite(t[now])) continue;
    assert(forecast_room(m, &t[now - W], &in[now - W], W, fut.data(), 24, track.data(), out.data()));
    if (std::isfinite(t[now + 12])) {
      se += (out[11] - t[now + 12]) * (out[11] - t[now + 12]);
      sp += (t[now] - t[now + 12]) * (t[now] - t[now + 12]);
      cnt++;
    }
  }
  std::printf("  room forecast 12 h: %.3f K (stays as it is %.3f K), weight at 6 h %.2f, scored %d\n",
              std::sqrt(se / cnt), std::sqrt(sp / cnt), m.weight(6), m.scored);
  assert(m.scored > 24 && m.weight(6) > 0.8f);
  assert(se < sp);

  // A room with no model shows its temperature now; no temperature at all is no forecast.
  RoomModel none;
  assert(!forecast_room(none, &t[now - W], &in[now - W], W, fut.data(), 24, track.data(), out.data()));
  assert(out[23] == t[now] || (!std::isfinite(t[now]) && std::isfinite(out[23])));
  std::vector<float> blank(W + 1, NAN);
  assert(!forecast_room(m, blank.data(), &in[0], W, fut.data(), 24, track.data(), out.data()));

  f->~HouseFitter();
  ::operator delete(mem);
  std::printf("planner room_forecast: all assertions passed\n");
  return 0;
}
