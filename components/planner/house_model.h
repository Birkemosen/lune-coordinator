#pragma once

// House model for the shadow planner: two thermal masses.
//
//   floor (screed + floor circuit):  Cs · dTs/dt = Q − H · (Ts − T)
//   rooms (air, furniture, walls):   Ca · dT/dt  = H · (Ts − T) + g · sun + P(hour) − UA · f · (T − Tout)
//                                                 + Knb · (T_neighbours − T)   (a room only)
//
// f = 1 + kw · wind + kc · max(0, COLD_REF_C − Tout): the loss grows with wind and, below
// COLD_REF_C, with cold (air leaks and ventilation; 21–24 Nov 2026 at 1–4 °C the house took
// 59–81 kWh a day where a straight UA fitted on the 4–15 °C days said 49–57).
//
// P(hour) = P + Pc·cos + Ps·sin of the local hour: people, cooking, appliances and any
// setback follow the clock, and without it a 24 h forecast is no better than persistence.
//
// Q is the space-heating heat. It goes into the floors first, which is why a day with a
// lot of heat can leave the measured house temperature almost unchanged and the next
// day need little: the heat is in the screed (2 Oct–16 Nov 2026: a one-mass model
// missed the daily heat by a median 22 %, whatever the window). Only T is measured; the
// floor temperature is tracked by running the floor equation with the measured T.
//
// The parameters are fitted in batch (Levenberg–Marquardt on 24 h forecasts started
// every few hours in a window of recent hours), which takes well under a second on
// Touch once a day. Pure C++, host-testable.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "heat_pump_model.h"   // EwLeastSquares

namespace lune_planner {

struct HouseParams {
  float ua_kw_per_k{0.18f};       // loss in calm weather
  float ca_kwh_per_k{10.0f};      // rooms
  float cs_kwh_per_k{10.0f};      // floors (screed ≈ 6–7 kWh/K per 5 cm × 280 m²; bounded to 30 so the
                                  // fit cannot let the house coast for days on its floors)
  float h_kw_per_k{1.5f};         // floor → rooms
  float g_kw_per_100wm2{0.5f};    // sun, per 100 W/m² global irradiance
  float p_int_kw{0.4f};           // internal gain
  float kw_per_ms{0.02f};         // extra loss per m/s of wind, fraction of UA
  float cold_per_k{0.0f};         // extra loss per K of outdoor temperature below COLD_REF_C, fraction of UA
  float p_cos_kw{0.0f};           // daily rhythm of the internal gain: cos/sin of the local hour
  float p_sin_kw{0.0f};
  float k_nb_kw_per_k{0.0f};      // exchange with neighbouring rooms (a room's model; 0 for the house)
  float sensor_sun_k{0.0f};       // the sensor reads this much high per 100 W/m² of sun now (sun on the sensor)
  float sun_floor_frac{0.0f};     // share of the sun that lands on the floor (sun patches) instead of the air
  float g_lag_kw_per_100wm2{0.0f};  // sun that arrives late, through a roof or wall that heats up first
  bool valid{false};
};

// Below this outdoor temperature the loss grows with the cold (cold_per_k).
constexpr float COLD_REF_C = 8.0f;

struct HourIn {
  float q_kw{0};       // space-heating heat in the hour (kWh = mean kW)
  float out_c{NAN};
  float sun_wm2{0};
  float wind_ms{0};
  float hour_local{0};   // 0–23, for the daily rhythm
  float nb_c{NAN};       // neighbours' temperature (the house average for a room); NAN = none
  float sun_lag_wm2{0};  // the same sun through a first-order lag (sun_lag_filter), for g_lag
};

// Sun through a roof or wall: a first-order lag of tau_h hours, one-hour steps.
inline float sun_lag_filter(float prev_wm2, float sun_wm2, float tau_h = 3.0f) {
  return prev_wm2 + (sun_wm2 - prev_wm2) * (1.0f - std::exp(-1.0f / tau_h));
}

// The loss per kelvin of indoor − outdoor in an hour's weather, as a multiple of UA.
inline float loss_factor(const HouseParams &p, float out_c, float wind_ms) {
  return 1.0f + p.kw_per_ms * wind_ms + p.cold_per_k * std::fmax(0.0f, COLD_REF_C - out_c);
}

struct HouseState {
  float t_c{NAN};      // rooms
  float ts_c{NAN};     // floors
};

inline HouseState house_step(const HouseParams &p, HouseState s, const HourIn &in) {
  const float flow = p.h_kw_per_k * (s.ts_c - s.t_c);
  const float loss = p.ua_kw_per_k * loss_factor(p, in.out_c, in.wind_ms) * (s.t_c - in.out_c);
  HouseState n;
  const float sun = p.g_kw_per_100wm2 * in.sun_wm2 / 100.0f;
  n.ts_c = s.ts_c + (in.q_kw + p.sun_floor_frac * sun - flow) / p.cs_kwh_per_k;
  const float ph = in.hour_local * (6.2831853f / 24.0f);
  const float gain = p.p_int_kw + p.p_cos_kw * std::cos(ph) + p.p_sin_kw * std::sin(ph);
  const float nb = std::isfinite(in.nb_c) ? p.k_nb_kw_per_k * (in.nb_c - s.t_c) : 0.0f;
  const float late = p.g_lag_kw_per_100wm2 * in.sun_lag_wm2 / 100.0f;
  n.t_c = s.t_c + (flow + (1.0f - p.sun_floor_frac) * sun + late + gain + nb - loss) / p.ca_kwh_per_k;
  return n;
}

// What the room sensor shows for a room at t_c: a sensor in direct sun reads high while
// the sun shines, without that heat being in the room.
inline float sensor_reading(const HouseParams &p, float t_c, const HourIn &in) {
  return t_c + p.sensor_sun_k * in.sun_wm2 / 100.0f;
}

// Track the floor temperature through measured hours: the floor equation runs on the
// measured room temperature (less the sun on the sensor). ts[i] is the floor at the start of hour i.
inline void track_floor(const HouseParams &p, const float *t_meas, const HourIn *in, size_t n, float ts0,
                        float *ts) {
  float s = ts0;
  for (size_t i = 0; i < n; i++) {
    ts[i] = s;
    const float t = t_meas[i] - (i ? p.sensor_sun_k * in[i - 1].sun_wm2 / 100.0f : 0.0f);
    if (std::isfinite(t))
      s += (in[i].q_kw + p.sun_floor_frac * p.g_kw_per_100wm2 * in[i].sun_wm2 / 100.0f - p.h_kw_per_k * (s - t)) /
           p.cs_kwh_per_k;
  }
}

// Heat the floors need over an hour to hold the rooms on a path t0 → t1 (planning
// inverse, one hour at a time; the floor lag is the planner's to respect).
inline float house_room_heat_needed(const HouseParams &p, float t0_c, float t1_c, const HourIn &in) {
  const float tm = 0.5f * (t0_c + t1_c);
  const float loss = p.ua_kw_per_k * loss_factor(p, in.out_c, in.wind_ms) * (tm - in.out_c);
  return p.ca_kwh_per_k * (t1_c - t0_c) + loss - p.g_kw_per_100wm2 * in.sun_wm2 / 100.0f - p.p_int_kw;
}

// The house's long-run energy balance from whole days: heat ≈ UA·Σ(T − Tout)·(1 + kw·wind
// + kc·cold) − g·Σsun − 24·P, cold = max(0, COLD_REF_C − Tout). Only days below COLD_REF_C
// say anything about kc, and the ridge keeps it at the prior's until they come. Fitted on daily sums (exponentially weighted over days, ridge toward
// the prior), it pins loss, internal gain and useful sun to the heat actually metered;
// the hourly fit then only shapes the dynamics. Without it, sun on a room sensor in a
// sunny room passes for heat and the model's house needs a quarter of the real heat.
class EnergyBalance {
 public:
  explicit EnergyBalance(const HouseParams &prior = HouseParams{}, float lambda = 0.97f) : prior_(prior), ls_(lambda) {}
  // At least this many of a day's hours must be logged for the day to count.
  static constexpr int MIN_HOURS = 20;
  // One day (24 hours from local midnight): t[0..24], in[0..23]. An hour without a log
  // (t[i] not finite: a restart, an update) is left out and the day's sums are scaled up
  // from the hours that are there (21–24 Nov 2026, the first cold days, each lacked 3–6
  // hours to updates and were all dropped when a day had to be complete).
  void add_day(const float *t, const HourIn *in) {
    double d = 0, dw = 0, dc = 0, sun = 0, q = 0;
    int n = 0;
    for (int i = 0; i < 24; i++) {
      if (!std::isfinite(t[i]) || !std::isfinite(in[i].out_c)) continue;
      n++;
      d += t[i] - in[i].out_c;
      dw += (t[i] - in[i].out_c) * in[i].wind_ms;
      dc += (t[i] - in[i].out_c) * std::fmax(0.0f, COLD_REF_C - in[i].out_c);
      sun += in[i].sun_wm2 / 100.0f;
      q += in[i].q_kw;
    }
    if (n < MIN_HOURS) return;
    const double k = 24.0 / n;
    d *= k; dw *= k; dc *= k; sun *= k; q *= k;
    if (q < 5) return;   // a day the pump hardly ran says little about the loss
    // Scaled so each coefficient is of order 1: UA·100, UA·kw·1000, UA·kc·1000, P·10, g·10.
    const float x[5] = {static_cast<float>(d / 100), static_cast<float>(dw / 1000), static_cast<float>(dc / 1000), -2.4f,
                        -static_cast<float>(sun / 10)};
    ls_.add(x, static_cast<float>(q));
  }
  uint32_t days() const { return ls_.samples(); }
  // Writes UA, wind and cold factors, P and sun into p when there are enough days; true if it did.
  bool apply(HouseParams *p) const {
    if (ls_.samples() < 7) return false;
    const float pr[5] = {prior_.ua_kw_per_k * 100, prior_.ua_kw_per_k * prior_.kw_per_ms * 1000,
                         prior_.ua_kw_per_k * prior_.cold_per_k * 1000, prior_.p_int_kw * 10,
                         prior_.g_kw_per_100wm2 * 10};
    float th[5];
    if (!ls_.solve(pr, 3.0f, th)) return false;
    // Less loss in the cold than in mild weather is not physical: then the days say nothing
    // about the cold, and the others are solved with it held at the prior's.
    if (th[2] / 1000 < prior_.ua_kw_per_k * prior_.cold_per_k) {
      const float ridge[5] = {3.0f, 3.0f, 1e9f, 3.0f, 3.0f};
      if (!ls_.solve(pr, ridge, th)) return false;
    }
    const float ua = th[0] / 100;
    if (!(ua > 0.03f && ua < 1.0f)) return false;
    p->ua_kw_per_k = ua;
    p->kw_per_ms = std::fmax(0.0f, std::fmin(0.15f, th[1] / 1000 / ua));
    p->cold_per_k = std::fmax(0.0f, std::fmin(0.1f, th[2] / 1000 / ua));
    p->p_int_kw = std::fmax(0.0f, std::fmin(3.0f, th[3] / 10));
    p->g_kw_per_100wm2 = std::fmax(0.0f, std::fmin(2.0f, th[4] / 10));
    return true;
  }

 private:
  HouseParams prior_;
  EwLeastSquares<5> ls_;
};

// Batch fit over a window of hours. t_meas[i] is the room temperature at the start of
// hour i (n + 1 values), in[i] the hour's inputs. Residuals are the hourly errors of 24 h
// forecasts started every `stride` hours, plus a weak pull toward the prior so poorly
// observed parameters stay physical. Levenberg–Marquardt with a forward-difference
// Jacobian accumulated window by window (no residual vector is stored).
class HouseFitter {
 public:
  static constexpr int NP = 13;
  static constexpr int HORIZON = 24;
  static constexpr size_t MAX_HOURS = 24 * 21;

  // size: 1 for the whole house; a room uses its share (e.g. 0.1) so the bounds on the
  // extensive parameters (loss, masses, coupling, gains) scale with it.
  explicit HouseFitter(const HouseParams &prior = HouseParams{}, float prior_weight = 0.05f, float size = 1.0f)
      : prior_(prior), prior_w_(prior_weight), size_(size) {}

  // Parameter indices for `freeze`: what the fit must leave as it is.
  enum Param { UA = 0, CA, CS, H, SUN, PINT, WIND, PCOS, PSIN, NB, SENSOR, SUNFLOOR, SUNLATE };

  bool fit(const float *t_meas, const HourIn *in, size_t n, HouseParams *p, int stride = 6, int iters = 30,
           uint32_t freeze = 0) {
    freeze_ = freeze;
    if (n < 4 * HORIZON || n > MAX_HOURS) return false;
    t_ = t_meas; in_ = in; n_ = n; stride_ = stride;
    float x[NP], x0[NP];
    to_vec_(*p, x);
    to_vec_(prior_, x0);
    for (int i = 0; i < NP; i++) x[i] = clamp_(i, x[i], size_);
    double lam = 1e-2;
    double cost = cost_(x, x0);
    if (!std::isfinite(cost)) return false;
    const double cost0 = cost;
    for (int it = 0; it < iters; it++) {
      double jtj[NP][NP] = {}, jtr[NP] = {};
      accumulate_(x, x0, jtj, jtr);
      bool stepped = false;
      for (int tries = 0; tries < 8 && !stepped; tries++) {
        double m[NP][NP + 1];
        for (int i = 0; i < NP; i++) {
          for (int j = 0; j < NP; j++) m[i][j] = jtj[i][j] + (i == j ? lam * (jtj[i][i] + 1e-9) : 0.0);
          m[i][NP] = -jtr[i];
        }
        for (int i = 0; i < NP; i++)
          if (freeze_ & (1u << i)) {
            for (int j = 0; j <= NP; j++) m[i][j] = 0;
            for (int j = 0; j < NP; j++) m[j][i] = 0;
            m[i][i] = 1;
          }
        double dx[NP];
        if (!solve_(m, dx)) { lam *= 10; continue; }
        float xn[NP];
        for (int i = 0; i < NP; i++) xn[i] = clamp_(i, x[i] + static_cast<float>(dx[i]), size_);
        const double c = cost_(xn, x0);
        if (std::isfinite(c) && c < cost) {
          const double gain = cost - c;
          for (int i = 0; i < NP; i++) x[i] = xn[i];
          cost = c;
          lam = lam * 0.3 > 1e-7 ? lam * 0.3 : 1e-7;
          stepped = true;
          if (gain < 1e-6 * cost) it = iters;   // converged
        } else {
          lam *= 10;
        }
      }
      if (!stepped) break;
    }
    from_vec_(x, p);
    p->valid = true;
    cost_(x, x0);   // the score of the kept parameters, not of the last rejected trial
    rmse24_ = std::sqrt(sq_last_ / (count_ > 0 ? count_ : 1));
    return cost < cost0;
  }

  // Start again from another prior (a device reuses one fitter for the house and each room).
  void reset(const HouseParams &prior, float prior_weight, float size) {
    prior_ = prior;
    prior_w_ = prior_weight;
    size_ = size;
  }

  // RMS of the hourly forecast errors (1–24 h) of the last fit, K.
  float rmse() const { return static_cast<float>(rmse24_); }
  // Called once per forecast window on a device, so a fit lets other tasks run.
  void set_yield(void (*y)()) { yield_ = y; }

 private:
  static void to_vec_(const HouseParams &p, float x[NP]) {
    x[0] = p.ua_kw_per_k; x[1] = p.ca_kwh_per_k; x[2] = p.cs_kwh_per_k; x[3] = p.h_kw_per_k;
    x[4] = p.g_kw_per_100wm2; x[5] = p.p_int_kw; x[6] = p.kw_per_ms; x[7] = p.p_cos_kw; x[8] = p.p_sin_kw; x[9] = p.k_nb_kw_per_k; x[10] = p.sensor_sun_k; x[11] = p.sun_floor_frac; x[12] = p.g_lag_kw_per_100wm2;
  }
  static void from_vec_(const float x[NP], HouseParams *p) {
    p->ua_kw_per_k = x[0]; p->ca_kwh_per_k = x[1]; p->cs_kwh_per_k = x[2]; p->h_kw_per_k = x[3];
    p->g_kw_per_100wm2 = x[4]; p->p_int_kw = x[5]; p->kw_per_ms = x[6]; p->p_cos_kw = x[7]; p->p_sin_kw = x[8]; p->k_nb_kw_per_k = x[9]; p->sensor_sun_k = x[10]; p->sun_floor_frac = x[11]; p->g_lag_kw_per_100wm2 = x[12];
  }
  static float clamp_(int i, float v, float size) {
    static constexpr float lo[NP] = {0.03f, 0.5f, 3.0f, 0.1f, 0.0f, 0.0f, 0.0f, -2.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static constexpr float hi[NP] = {1.0f, 60.0f, 30.0f, 20.0f, 5.0f, 3.0f, 0.15f, 2.0f, 2.0f, 5.0f, 3.0f, 1.0f, 5.0f};
    const float k = (i == 6 || i == 10 || i == 11) ? 1.0f : size;   // wind factor, sensor offset and sun split are not sizes
    const float l = lo[i] * k, h = hi[i] * k;
    return v < l ? l : (v > h ? h : v);
  }
  // Typical size of each parameter: the prior's pull and the Jacobian step use it.
  static constexpr float scale_[NP] = {0.1f, 5.0f, 30.0f, 1.0f, 0.5f, 0.3f, 0.02f, 0.3f, 0.3f, 0.5f, 0.3f, 0.3f, 0.5f};

  // Forecast residuals for one parameter set, window by window, through a callback.
  template <typename F> void residuals_(const float x[NP], float *track, F &&each) {
    HouseParams p;
    from_vec_(x, &p);
    track_floor(p, t_, in_, n_, std::isfinite(t_[0]) ? t_[0] + 2.0f : 22.0f, track);
    int w = 0;
    for (size_t s = HORIZON; s + HORIZON < n_; s += stride_, w++) {
      if (yield_) yield_();
      if (!std::isfinite(t_[s])) {
        // A window with no start is no residuals (not the last call's values in its slots).
        for (int h = 0; h < HORIZON; h++) each(w * HORIZON + h, NAN);
        continue;
      }
      const float t0 = t_[s] - (s ? p.sensor_sun_k * in_[s - 1].sun_wm2 / 100.0f : 0.0f);
      HouseState st{t0, track[s]};
      for (int h = 0; h < HORIZON; h++) {
        st = house_step(p, st, in_[s + h]);
        const float m = t_[s + h + 1];
        each(w * HORIZON + h, std::isfinite(m) ? sensor_reading(p, st.t_c, in_[s + h]) - m : NAN);
      }
    }
  }

  double cost_(const float x[NP], const float x0[NP]) {
    double c = 0;
    long k = 0;
    residuals_(x, track_[0], [&](int, float r) { if (std::isfinite(r)) { c += r * r; k++; } });
    sq_last_ = c;
    count_ = k;
    for (int i = 0; i < NP; i++) {
      const double pr = prior_w_ * (x[i] - x0[i]) / (scale_[i] * ((i == 6 || i == 10 || i == 11) ? 1.0f : size_));
      c += k * pr * pr / NP;
    }
    return c;
  }

  void accumulate_(const float x[NP], const float x0[NP], double jtj[NP][NP], double jtr[NP]) {
    // Residuals of the base and of each perturbed parameter set, all indexed alike.
    // Windows start at HORIZON while start + HORIZON < n (as residuals_ runs them): only
    // slots a window writes, never ones a fresh (uninitialised) fitter never filled.
    const int nres = static_cast<int>(((n_ - 2 * HORIZON - 1) / stride_ + 1) * HORIZON);
    if (nres > MAX_RES) return;
    float dx[NP];
    residuals_(x, track_[0], [&](int i, float r) { base_[i] = r; });
    for (int a = 0; a < NP; a++) {
      float xp[NP];
      for (int k = 0; k < NP; k++) xp[k] = x[k];
      dx[a] = 1e-2f * scale_[a] * ((a == 6 || a == 10 || a == 11) ? 1.0f : size_);
      xp[a] += dx[a];
      residuals_(xp, track_[1], [&](int i, float r) { jac_[a][i] = r; });
    }
    long k = 0;
    for (int i = 0; i < nres; i++) {
      if (!std::isfinite(base_[i])) continue;
      double j[NP];
      bool ok = true;
      for (int a = 0; a < NP; a++) {
        if (!std::isfinite(jac_[a][i])) { ok = false; break; }
        j[a] = (jac_[a][i] - base_[i]) / dx[a];
      }
      if (!ok) continue;
      k++;
      for (int a = 0; a < NP; a++) {
        jtr[a] += j[a] * base_[i];
        for (int b = 0; b < NP; b++) jtj[a][b] += j[a] * j[b];
      }
    }
    // The prior's pull: residual sqrt(k/NP)·w·(x − x0)/scale per parameter.
    for (int a = 0; a < NP; a++) {
      const double wa = std::sqrt(static_cast<double>(k) / NP) * prior_w_ / (scale_[a] * ((a == 6 || a == 10 || a == 11) ? 1.0f : size_));
      jtr[a] += wa * wa * (x[a] - x0[a]);
      jtj[a][a] += wa * wa;
    }
  }

  static bool solve_(double m[NP][NP + 1], double out[NP]) {
    for (int c = 0; c < NP; c++) {
      int piv = c;
      for (int r = c + 1; r < NP; r++)
        if (std::fabs(m[r][c]) > std::fabs(m[piv][c])) piv = r;
      if (std::fabs(m[piv][c]) < 1e-15) return false;
      if (piv != c)
        for (int k = 0; k <= NP; k++) { const double t = m[c][k]; m[c][k] = m[piv][k]; m[piv][k] = t; }
      for (int r = 0; r < NP; r++) {
        if (r == c) continue;
        const double f = m[r][c] / m[c][c];
        for (int k = c; k <= NP; k++) m[r][k] -= f * m[c][k];
      }
    }
    for (int i = 0; i < NP; i++) out[i] = m[i][NP] / m[i][i];
    return true;
  }

  static constexpr int MAX_RES = static_cast<int>((MAX_HOURS / 6 + 1) * HORIZON);
  HouseParams prior_;
  float prior_w_;
  float size_;
  uint32_t freeze_{0};
  const float *t_{nullptr};
  const HourIn *in_{nullptr};
  size_t n_{0};
  int stride_{6};
  double sq_last_{0};
  long count_{0};
  double rmse24_{NAN};
  void (*yield_)() = nullptr;
  float track_[2][MAX_HOURS + 1];
  float base_[MAX_RES];
  float jac_[NP][MAX_RES];
};

}  // namespace lune_planner
