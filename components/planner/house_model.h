#pragma once

// House model for the shadow planner: two thermal masses.
//
//   floor (screed + floor circuit):  Cs · dTs/dt = Q − H · (Ts − T)
//   rooms (air, furniture, walls):   Ca · dT/dt  = H · (Ts − T) + g · sun + P(hour) − UA · (1 + kw · wind) · (T − Tout)
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

namespace lune_planner {

struct HouseParams {
  float ua_kw_per_k{0.18f};       // loss in calm weather
  float ca_kwh_per_k{10.0f};      // rooms
  float cs_kwh_per_k{35.0f};      // floors
  float h_kw_per_k{1.5f};         // floor → rooms
  float g_kw_per_100wm2{0.5f};    // sun, per 100 W/m² global irradiance
  float p_int_kw{0.4f};           // internal gain
  float kw_per_ms{0.02f};         // extra loss per m/s of wind, fraction of UA
  float p_cos_kw{0.0f};           // daily rhythm of the internal gain: cos/sin of the local hour
  float p_sin_kw{0.0f};
  bool valid{false};
};

struct HourIn {
  float q_kw{0};       // space-heating heat in the hour (kWh = mean kW)
  float out_c{NAN};
  float sun_wm2{0};
  float wind_ms{0};
  float hour_local{0};   // 0–23, for the daily rhythm
};

struct HouseState {
  float t_c{NAN};      // rooms
  float ts_c{NAN};     // floors
};

inline HouseState house_step(const HouseParams &p, HouseState s, const HourIn &in) {
  const float flow = p.h_kw_per_k * (s.ts_c - s.t_c);
  const float loss = p.ua_kw_per_k * (1.0f + p.kw_per_ms * in.wind_ms) * (s.t_c - in.out_c);
  HouseState n;
  n.ts_c = s.ts_c + (in.q_kw - flow) / p.cs_kwh_per_k;
  const float ph = in.hour_local * (6.2831853f / 24.0f);
  const float gain = p.p_int_kw + p.p_cos_kw * std::cos(ph) + p.p_sin_kw * std::sin(ph);
  n.t_c = s.t_c + (flow + p.g_kw_per_100wm2 * in.sun_wm2 / 100.0f + gain - loss) / p.ca_kwh_per_k;
  return n;
}

// Track the floor temperature through measured hours: the floor equation runs on the
// measured room temperature. ts[i] is the floor at the start of hour i.
inline void track_floor(const HouseParams &p, const float *t_meas, const HourIn *in, size_t n, float ts0,
                        float *ts) {
  float s = ts0;
  for (size_t i = 0; i < n; i++) {
    ts[i] = s;
    const float t = t_meas[i];
    if (std::isfinite(t)) s += (in[i].q_kw - p.h_kw_per_k * (s - t)) / p.cs_kwh_per_k;
  }
}

// Heat the floors need over an hour to hold the rooms on a path t0 → t1 (planning
// inverse, one hour at a time; the floor lag is the planner's to respect).
inline float house_room_heat_needed(const HouseParams &p, float t0_c, float t1_c, const HourIn &in) {
  const float tm = 0.5f * (t0_c + t1_c);
  const float loss = p.ua_kw_per_k * (1.0f + p.kw_per_ms * in.wind_ms) * (tm - in.out_c);
  return p.ca_kwh_per_k * (t1_c - t0_c) + loss - p.g_kw_per_100wm2 * in.sun_wm2 / 100.0f - p.p_int_kw;
}

// Batch fit over a window of hours. t_meas[i] is the room temperature at the start of
// hour i (n + 1 values), in[i] the hour's inputs. Residuals are the hourly errors of 24 h
// forecasts started every `stride` hours, plus a weak pull toward the prior so poorly
// observed parameters stay physical. Levenberg–Marquardt with a forward-difference
// Jacobian accumulated window by window (no residual vector is stored).
class HouseFitter {
 public:
  static constexpr int NP = 9;
  static constexpr int HORIZON = 24;
  static constexpr size_t MAX_HOURS = 24 * 21;

  explicit HouseFitter(const HouseParams &prior = HouseParams{}, float prior_weight = 0.05f)
      : prior_(prior), prior_w_(prior_weight) {}

  bool fit(const float *t_meas, const HourIn *in, size_t n, HouseParams *p, int stride = 6, int iters = 30) {
    if (n < 4 * HORIZON || n > MAX_HOURS) return false;
    t_ = t_meas; in_ = in; n_ = n; stride_ = stride;
    float x[NP], x0[NP];
    to_vec_(*p, x);
    to_vec_(prior_, x0);
    for (int i = 0; i < NP; i++) x[i] = clamp_(i, x[i]);
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
        double dx[NP];
        if (!solve_(m, dx)) { lam *= 10; continue; }
        float xn[NP];
        for (int i = 0; i < NP; i++) xn[i] = clamp_(i, x[i] + static_cast<float>(dx[i]));
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
    rmse24_ = std::sqrt(sq_last_ / (count_ > 0 ? count_ : 1));
    return cost < cost0;
  }

  // RMS of the hourly forecast errors (1–24 h) of the last fit, K.
  float rmse() const { return static_cast<float>(rmse24_); }

 private:
  static void to_vec_(const HouseParams &p, float x[NP]) {
    x[0] = p.ua_kw_per_k; x[1] = p.ca_kwh_per_k; x[2] = p.cs_kwh_per_k; x[3] = p.h_kw_per_k;
    x[4] = p.g_kw_per_100wm2; x[5] = p.p_int_kw; x[6] = p.kw_per_ms; x[7] = p.p_cos_kw; x[8] = p.p_sin_kw;
  }
  static void from_vec_(const float x[NP], HouseParams *p) {
    p->ua_kw_per_k = x[0]; p->ca_kwh_per_k = x[1]; p->cs_kwh_per_k = x[2]; p->h_kw_per_k = x[3];
    p->g_kw_per_100wm2 = x[4]; p->p_int_kw = x[5]; p->kw_per_ms = x[6]; p->p_cos_kw = x[7]; p->p_sin_kw = x[8];
  }
  static float clamp_(int i, float v) {
    static constexpr float lo[NP] = {0.03f, 0.5f, 3.0f, 0.1f, 0.0f, 0.0f, 0.0f, -2.0f, -2.0f};
    static constexpr float hi[NP] = {1.0f, 60.0f, 400.0f, 20.0f, 5.0f, 3.0f, 0.15f, 2.0f, 2.0f};
    return v < lo[i] ? lo[i] : (v > hi[i] ? hi[i] : v);
  }
  // Typical size of each parameter: the prior's pull and the Jacobian step use it.
  static constexpr float scale_[NP] = {0.1f, 5.0f, 30.0f, 1.0f, 0.5f, 0.3f, 0.02f, 0.3f, 0.3f};

  // Forecast residuals for one parameter set, window by window, through a callback.
  template <typename F> void residuals_(const float x[NP], float *track, F &&each) {
    HouseParams p;
    from_vec_(x, &p);
    track_floor(p, t_, in_, n_, std::isfinite(t_[0]) ? t_[0] + 2.0f : 22.0f, track);
    int w = 0;
    for (size_t s = HORIZON; s + HORIZON < n_; s += stride_, w++) {
      if (!std::isfinite(t_[s])) continue;
      HouseState st{t_[s], track[s]};
      for (int h = 0; h < HORIZON; h++) {
        st = house_step(p, st, in_[s + h]);
        const float m = t_[s + h + 1];
        each(w * HORIZON + h, std::isfinite(m) ? st.t_c - m : NAN);
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
      const double pr = prior_w_ * (x[i] - x0[i]) / scale_[i];
      c += k * pr * pr / NP;
    }
    return c;
  }

  void accumulate_(const float x[NP], const float x0[NP], double jtj[NP][NP], double jtr[NP]) {
    // Residuals of the base and of each perturbed parameter set, all indexed alike.
    const int nres = static_cast<int>(((n_ - 2 * HORIZON) / stride_ + 1) * HORIZON);
    if (nres > MAX_RES) return;
    float dx[NP];
    residuals_(x, track_[0], [&](int i, float r) { base_[i] = r; });
    for (int a = 0; a < NP; a++) {
      float xp[NP];
      for (int k = 0; k < NP; k++) xp[k] = x[k];
      dx[a] = 1e-3f * scale_[a] * 10.0f;
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
      const double wa = std::sqrt(static_cast<double>(k) / NP) * prior_w_ / scale_[a];
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
  const float *t_{nullptr};
  const HourIn *in_{nullptr};
  size_t n_{0};
  int stride_{6};
  double sq_last_{0};
  long count_{0};
  double rmse24_{NAN};
  float track_[2][MAX_HOURS + 1];
  float base_[MAX_RES];
  float jac_[NP][MAX_RES];
};

}  // namespace lune_planner
