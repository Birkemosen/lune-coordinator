#pragma once

// Shadow planner, F3: the hourly history Touch keeps for itself and what it learns from it.
//
//   HourAccumulator  folds the telemetry polls of one hour into a history record (LUNH
//                    house columns, history_record.h): means, run minutes, starts, and the
//                    heat and electricity from the heat pump's own daily meters.
//   HistoryStore     one slot per hour, slot = hour % N, so a record lands in its place
//                    whatever order it arrives in (live hours, a seed from Odin's history).
//                    Next to each record: the plan's output for the hour and the shadow
//                    flow target, which the LUNH columns do not carry.
//   learn_house / learn_heat_pump
//                    the daily refit on the store: energy balance over every whole day,
//                    the hourly dynamics over the last 14 days, the heat pump's floor curve,
//                    buffer and COP over every full space-heating hour. Each starts from the
//                    seed again, so a refit never drifts on its own earlier result.
//   floor_now        the floors' temperature now, tracked through the stored hours.
// Pure C++, host-testable. The store is ~100 KB at 60 days and lives in PSRAM.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "heat_pump_model.h"
#include "history_record.h"
#include "house_model.h"

namespace lune_planner {

// One telemetry poll. NAN / negative = not known.
struct PollSample {
  uint32_t ts{0};
  float house_c{NAN};
  float out_c{NAN};
  float wind_ms{NAN};
  float sun_wm2{NAN};
  float price_eur_kwh{NAN};
  float band_sp_c{NAN}, band_min_c{NAN}, band_max_c{NAN};   // band as absolute temperatures
  bool compressor_on{false};
  bool hot_water{false};      // hot water or legionella now
  bool legionella{false};
  bool defrost{false};
  float output_kw{NAN};
  float hp_feed_c{NAN}, hp_return_c{NAN};
  float zone_feed_c{NAN}, zone_return_c{NAN};
  float flow_target_c{NAN};
  // The heat pump's daily meters (kWh since local midnight; they reset there).
  float sh_heat_kwh{NAN}, sh_elec_kwh{NAN}, dhw_heat_kwh{NAN}, dhw_elec_kwh{NAN};
  // Today's electricity so far over all modes from a meter that moves during the day
  // (Asgard's daily_total_energy_consumption, 0.1 kWh steps; resets at local midnight).
  float total_elec_kwh{NAN};
  float expected_c{NAN};      // the planner's forecast for this hour's end
  float plan_kw{NAN};         // the plan's output for this hour
  float shadow_flow_c{NAN};   // the shadow flow controller's target now
};

// What one hour can hold. More than this came from a meter that lumped several hours.
constexpr float HOUR_HEAT_MAX_KWH = 20.0f;
constexpr float HOUR_ELEC_MAX_KWH = 6.0f;

// Whether an hour's space heat can be trusted: not more than an hour can hold, and not
// (almost) nothing while the compressor ran on space heating for a quarter of an hour.
// Hours logged from the daily meter before 17 Nov 2026 fail this; the learners treat them
// as missing.
inline bool heat_usable(const Record &r) {
  const float q = r.get(Kind::HOUSE, H_HEAT_SH_KWH), run = r.get(Kind::HOUSE, H_RUN_MIN);
  const float flags = r.get(Kind::HOUSE, H_MODE_FLAGS);
  const bool space = std::isfinite(flags) && (static_cast<uint16_t>(flags) & MODE_SPACE_HEATING);
  if (std::isfinite(q) && q > HOUR_HEAT_MAX_KWH) return false;
  if (space && run >= 15.0f && !(q >= 0.5f)) return false;
  return true;
}

struct HourExtra {
  float plan_kw{NAN};
  float shadow_flow_c{NAN};
};

class HourAccumulator {
 public:
  // Adds a poll. When it falls in a later hour than the one being collected, that hour is
  // closed into *rec / *extra and true is returned; the poll then starts the new hour.
  bool add(const PollSample &s, Record *rec, HourExtra *extra) {
    const uint32_t hour = s.ts - s.ts % 3600;
    bool closed = false;
    if (hour_ != 0 && hour != hour_) {
      if (hour > hour_ && n_ > 0) {
        close_(rec, extra);
        closed = true;
      }
      reset_();
    }
    if (hour_ == 0) hour_ = hour;
    take_(s);
    return closed;
  }
  uint32_t hour() const { return hour_; }

 private:
  struct Mean {
    double sum{0};
    uint16_t n{0};
    void add(float v) { if (std::isfinite(v)) { sum += v; n++; } }
    float get() const { return n ? static_cast<float>(sum / n) : NAN; }
  };
  // A daily meter: increments add up; a drop is the midnight reset (counts from 0).
  struct Meter {
    float last{NAN};
    double sum{0};
    bool any{false};
    bool moved{false};
    void add(float v) {
      if (!std::isfinite(v)) return;
      if (std::isfinite(last)) {
        sum += v >= last ? v - last : v;
        any = true;
        moved = moved || v != last;
      }
      last = v;
    }
  };

  void reset_() {
    hour_ = 0;
    n_ = 0;
    house_ = house0_ = out_ = wind_ = sun_ = price_ = sp_ = bmin_ = bmax_ = Mean{};
    feed_ = ret_ = zfeed_ = zret_ = target_ = flow_ = Mean{};
    target_max_ = out_max_ = NAN;
    run_s_ = defrost_s_ = 0;
    starts_ = 0;
    flags_ = 0;
    output_kwh_ = 0;
    output_n_ = 0;
    for (Meter *m : {&shh_, &she_, &dwh_, &dwe_, &tot_}) { m->sum = 0; m->any = m->moved = false; }   // `last` carries over
    expected_ = plan_kw_ = NAN;
  }

  void take_(const PollSample &s) {
    // Seconds since the previous poll, capped so a gap is not counted as running time.
    const uint32_t dt = last_ts_ != 0 && s.ts > last_ts_ ? (s.ts - last_ts_ > 600 ? 600 : s.ts - last_ts_) : 0;
    last_ts_ = s.ts;
    n_++;
    house_.add(s.house_c);
    if (s.ts - hour_ < 900) house0_.add(s.house_c);
    out_.add(s.out_c); wind_.add(s.wind_ms); sun_.add(s.sun_wm2);
    price_.add(s.price_eur_kwh); sp_.add(s.band_sp_c); bmin_.add(s.band_min_c); bmax_.add(s.band_max_c);
    target_.add(s.flow_target_c);
    if (std::isfinite(s.flow_target_c) && !(s.flow_target_c <= target_max_)) target_max_ = s.flow_target_c;
    flow_.add(s.shadow_flow_c);
    const bool space = s.compressor_on && !s.hot_water;
    if (space) {
      feed_.add(s.hp_feed_c); ret_.add(s.hp_return_c); zfeed_.add(s.zone_feed_c); zret_.add(s.zone_return_c);
      run_s_ += dt;
      flags_ |= MODE_SPACE_HEATING;
      if (std::isfinite(s.output_kw)) { output_kwh_ += s.output_kw * dt / 3600.0; output_n_++; }
    }
    if (s.compressor_on && !was_on_) starts_++;
    was_on_ = s.compressor_on;
    if (s.hot_water) flags_ |= MODE_HOT_WATER;
    if (s.legionella) flags_ |= MODE_LEGIONELLA;
    if (s.defrost) { flags_ |= MODE_DEFROST; defrost_s_ += dt; }
    if (std::isfinite(s.output_kw) && !(s.output_kw <= out_max_)) out_max_ = s.output_kw;
    shh_.add(s.sh_heat_kwh); she_.add(s.sh_elec_kwh); dwh_.add(s.dhw_heat_kwh); dwe_.add(s.dhw_elec_kwh);
    tot_.add(s.total_elec_kwh);
    if (std::isfinite(s.expected_c)) expected_ = s.expected_c;
    if (std::isfinite(s.plan_kw)) plan_kw_ = s.plan_kw;
  }

  void close_(Record *r, HourExtra *x) const {
    r->clear();
    r->hour_start = hour_;
    const Kind K = Kind::HOUSE;
    // Heat: the integrated output when the pump reports it; the meter only when it moved by
    // what an hour can hold. The Ecodan's daily meters may tick once a day, which would put
    // the whole day into one hour and leave the others at 0.
    float sh = 0.0f;
    if (output_n_ > 0) sh = static_cast<float>(output_kwh_);
    else if (shh_.moved && shh_.sum <= HOUR_HEAT_MAX_KWH) sh = static_cast<float>(shh_.sum);
    else if (run_s_ > 0) sh = NAN;
    r->set(K, H_HEAT_SH_KWH, sh);
    if (dwh_.moved && dwh_.sum <= HOUR_HEAT_MAX_KWH) r->set(K, H_HEAT_DHW_KWH, static_cast<float>(dwh_.sum));
    // Electricity has no live reading, only the meters: kept when the hour's COP is plausible.
    // The live all-mode meter first; the per-mode daily meters only when it is missing (on
    // the Ecodan they tick once a day, so no hour of theirs passed the check).
    const bool live = tot_.any;
    const float el = static_cast<float>(live ? tot_.sum : she_.sum + dwe_.sum);
    const float heat = (std::isfinite(sh) ? sh : 0.0f) + (dwh_.moved ? static_cast<float>(dwh_.sum) : 0.0f);
    if ((live || she_.moved || dwe_.moved) && el <= HOUR_ELEC_MAX_KWH && heat >= 1.5f * el && heat <= 8.0f * el)
      r->set(K, H_ELEC_KWH, el);
    // The house at the hour's start (its first 15 minutes), as the model reads t[i].
    r->set(K, H_ROOM_C, house0_.n ? house0_.get() : house_.get());
    r->set(K, H_OUTDOOR_C, out_.get());
    r->set(K, H_WIND_MS, wind_.get());
    r->set(K, H_SOLAR_WM2, sun_.get());
    r->set(K, H_PRICE_EUR_KWH, price_.get());
    r->set(K, H_SCHED_SP_C, sp_.get());
    r->set(K, H_SCHED_MIN_C, bmin_.get());
    r->set(K, H_SCHED_MAX_C, bmax_.get());
    r->set(K, H_RUN_MIN, run_s_ / 60.0f);
    r->set(K, H_STARTS, static_cast<float>(starts_));
    r->set(K, H_HP_FEED_C, feed_.get());
    r->set(K, H_HP_RETURN_C, ret_.get());
    r->set(K, H_Z1_FEED_C, zfeed_.get());
    r->set(K, H_Z1_RETURN_C, zret_.get());
    r->set(K, H_FLOW_TARGET_C, target_.get());
    r->set(K, H_FLOW_TARGET_MAX_C, target_max_);
    r->set(K, H_OUTPUT_MAX_KW, out_max_);
    r->set(K, H_DEFROST_MIN, defrost_s_ / 60.0f);
    r->set(K, H_MODE_FLAGS, static_cast<float>(flags_));
    r->set(K, H_ROOM_EXPECTED_C, expected_);
    x->plan_kw = plan_kw_;
    x->shadow_flow_c = flow_.get();
  }

  uint32_t hour_{0};
  uint32_t last_ts_{0};
  uint16_t n_{0};
  Mean house_, house0_, out_, wind_, sun_, price_, sp_, bmin_, bmax_;
  Mean feed_, ret_, zfeed_, zret_, target_, flow_;
  float target_max_{NAN}, out_max_{NAN};
  uint32_t run_s_{0}, defrost_s_{0};
  uint16_t starts_{0};
  uint16_t flags_{0};
  double output_kwh_{0};
  uint16_t output_n_{0};
  bool was_on_{false};
  Meter shh_, she_, dwh_, dwe_, tot_;
  float expected_{NAN}, plan_kw_{NAN};
};

template <size_t N> struct HistoryStore {
  static constexpr uint32_t MAGIC = 0x484e554cu;   // "LUNH"
  static constexpr uint16_t VERSION = 1;
  static constexpr size_t SLOTS = N;
  uint32_t magic{0};
  uint16_t version{0};
  uint16_t slots{0};
  uint32_t newest_hour{0};
  Record rec[N];
  uint8_t plan_kw_x10[N];        // 255 = none
  int16_t shadow_flow_x10[N];    // NONE = none

  void clear() {
    magic = MAGIC;
    version = VERSION;
    slots = static_cast<uint16_t>(N);
    newest_hour = 0;
    for (size_t i = 0; i < N; i++) {
      rec[i].clear();
      plan_kw_x10[i] = 255;
      shadow_flow_x10[i] = NONE;
    }
  }
  bool valid() const { return magic == MAGIC && version == VERSION && slots == N; }
  static size_t slot(uint32_t hour) { return (hour / 3600u) % N; }

  // A live hour replaces whatever the slot held.
  void put(const Record &r, const HourExtra &x) {
    const size_t i = slot(r.hour_start);
    rec[i] = r;
    plan_kw_x10[i] = std::isfinite(x.plan_kw) ? static_cast<uint8_t>(std::fmin(25.4f, std::fmax(0.0f, x.plan_kw)) * 10 + 0.5f) : 255;
    shadow_flow_x10[i] = encode(x.shadow_flow_c, 10.0f);
    if (r.hour_start > newest_hour) newest_hour = r.hour_start;
  }
  // A seed hour only fills an empty or outdated slot; true if it did.
  bool seed(const Record &r) {
    const size_t i = slot(r.hour_start);
    if (rec[i].hour_start >= r.hour_start) return false;
    rec[i] = r;
    rec[i].v[H_ROOM_EXPECTED_C] = NONE;   // another planner's forecast is not ours to score
    plan_kw_x10[i] = 255;
    shadow_flow_x10[i] = NONE;
    if (r.hour_start > newest_hour) newest_hour = r.hour_start;
    return true;
  }
  const Record *at(uint32_t hour) const {
    const Record &r = rec[slot(hour)];
    return r.hour_start == hour ? &r : nullptr;
  }
  float plan_kw(uint32_t hour) const {
    const size_t i = slot(hour);
    return rec[i].hour_start == hour && plan_kw_x10[i] != 255 ? plan_kw_x10[i] / 10.0f : NAN;
  }
  float shadow_flow(uint32_t hour) const {
    const size_t i = slot(hour);
    return rec[i].hour_start == hour ? decode(shadow_flow_x10[i], 10.0f) : NAN;
  }
  size_t count_since(uint32_t from_hour) const {
    size_t c = 0;
    for (size_t i = 0; i < N; i++) c += rec[i].hour_start != 0 && rec[i].hour_start >= from_hour;
    return c;
  }
};

// Hours from_hour … from_hour + n − 1 as the house model's inputs: t[i] the house
// temperature at the start of hour i (n + 1 values), in[i] the hour's heat and weather. Missing weather carries the last value; missing heat is 0.
// Returns the number of hours that had a record.
template <size_t N>
size_t history_window(const HistoryStore<N> &st, uint32_t from_hour, size_t n, int (*local_hour)(uint32_t),
                      float *t, HourIn *in) {
  size_t have = 0;
  float out = NAN, wind = 0, sun = 0, lag = 0;
  for (size_t i = 0; i <= n; i++) {
    const uint32_t h = from_hour + static_cast<uint32_t>(i) * 3600u;
    const Record *r = st.at(h);
    if (r && !heat_usable(*r)) r = nullptr;
    t[i] = r ? r->get(Kind::HOUSE, H_ROOM_C) : NAN;
    if (i == n) break;
    HourIn &x = in[i];
    x = HourIn{};
    if (r) {
      have++;
      const float q = r->get(Kind::HOUSE, H_HEAT_SH_KWH);
      x.q_kw = std::isfinite(q) ? q : 0.0f;
      const float o = r->get(Kind::HOUSE, H_OUTDOOR_C), w = r->get(Kind::HOUSE, H_WIND_MS), s = r->get(Kind::HOUSE, H_SOLAR_WM2);
      if (std::isfinite(o)) out = o;
      if (std::isfinite(w)) wind = w;
      sun = std::isfinite(s) ? s : 0.0f;
    } else {
      sun = 0.0f;
    }
    x.out_c = out;
    x.wind_ms = wind;
    x.sun_wm2 = sun;
    lag = sun_lag_filter(lag, sun);
    x.sun_lag_wm2 = lag;
    x.hour_local = static_cast<float>(local_hour ? local_hour(h) : static_cast<int>((h / 3600u) % 24));
  }
  // Outdoor before the first known value: the first known value.
  float first = NAN;
  for (size_t i = 0; i < n && !std::isfinite(first); i++) first = in[i].out_c;
  for (size_t i = 0; i < n && !std::isfinite(in[i].out_c); i++) in[i].out_c = first;
  return have;
}

struct LearnReport {
  uint32_t fitted_hour{0};     // newest hour the fit saw
  uint16_t days{0};            // whole days in the energy balance
  uint16_t hours{0};           // hours in the dynamics window that had a record
  bool energy{false};          // the energy balance pinned loss, gains and sun
  bool dynamics{false};        // the hourly fit ran and was kept
  float rmse_k{NAN};           // its 1–24 h forecast RMSE
  uint32_t hp_hours{0};        // full space-heating hours the heat pump learned from
  bool heat_pump{false};       // enough of them to use the learned heat pump
};

// Workspace for learn_house: big, so it lives where the caller puts it (PSRAM on Touch).
struct LearnScratch {
  static constexpr size_t WINDOW = 14 * 24;
  float t[WINDOW + 1];
  HourIn in[WINDOW];
  float day_t[25];
  HourIn day_in[24];
  float track[WINDOW + 1];
  HouseFitter fitter;
  explicit LearnScratch(const HouseParams &seed) : fitter(seed) {}
};

// The house model from the store, starting from `seed`: the energy balance over every
// whole local day, then the hourly fit over the last 14 days with loss, gains, sun and
// wind frozen when the balance had enough days. The fit is kept only when it forecasts
// better than the seed on the same window and stays under `max_rmse`.
template <size_t N>
bool learn_house(const HistoryStore<N> &st, uint32_t now_hour, const HouseParams &seed, int (*local_hour)(uint32_t),
                 LearnScratch *w, HouseParams *out, LearnReport *rep, float max_rmse = 0.6f) {
  HouseParams p = seed;
  EnergyBalance eb(seed);
  // Whole days, oldest first, ending before now.
  const uint32_t oldest = now_hour > N * 3600u ? now_hour - static_cast<uint32_t>(N) * 3600u : 0;
  uint16_t days = 0;
  for (uint32_t h = oldest; h + 24 * 3600u <= now_hour; h += 3600u) {
    if (local_hour && local_hour(h) != 0) continue;
    if (history_window(st, h, 24, local_hour, w->day_t, w->day_in) < 24) continue;
    const uint32_t before = eb.days();
    eb.add_day(w->day_t, w->day_in);
    days += eb.days() > before;
    h += 23 * 3600u;
  }
  rep->days = days;
  rep->energy = eb.apply(&p);
  const size_t n = LearnScratch::WINDOW;
  const uint32_t from = now_hour - static_cast<uint32_t>(n) * 3600u;
  rep->hours = static_cast<uint16_t>(history_window(st, from, n, local_hour, w->t, w->in));
  rep->fitted_hour = now_hour - 3600u;
  rep->dynamics = false;
  if (rep->hours < n * 3 / 4) {   // too little for the dynamics: keep the seed's, with the balance
    *out = p;
    return rep->energy;
  }
  const uint32_t freeze = rep->energy ? (1u << HouseFitter::UA) | (1u << HouseFitter::PINT) | (1u << HouseFitter::SUN) |
                                            (1u << HouseFitter::WIND)
                                      : 0u;
  // The seed's score on this window, then the fit's (the fitter may have fitted a room last).
  w->fitter.reset(seed, 0.05f, 1.0f);
  HouseParams base = p;
  w->fitter.fit(w->t, w->in, n, &base, 6, 0, ~0u);
  const float seed_rmse = w->fitter.rmse();
  HouseParams fit = p;
  w->fitter.fit(w->t, w->in, n, &fit, 6, 30, freeze);
  const float rmse = w->fitter.rmse();
  if (std::isfinite(rmse) && rmse < max_rmse && !(rmse > seed_rmse)) {
    p = fit;
    rep->dynamics = true;
    rep->rmse_k = rmse;
  } else {
    rep->rmse_k = seed_rmse;
  }
  p.valid = true;
  *out = p;
  return true;
}

// The heat pump from every full space-heating hour in the store (oldest first): a whole
// hour on space heating, no hot water or defrost, meters ticked.
template <size_t N>
bool learn_heat_pump(const HistoryStore<N> &st, uint32_t now_hour, const HeatPumpParams &seed, HeatPumpParams *out,
                     LearnReport *rep) {
  HeatPumpLearner l(seed);
  const uint32_t oldest = now_hour > N * 3600u ? now_hour - static_cast<uint32_t>(N) * 3600u : 0;
  for (uint32_t h = oldest; h < now_hour; h += 3600u) {
    const Record *r = st.at(h);
    if (!r || !heat_usable(*r)) continue;
    const auto g = [r](uint8_t c) { return r->get(Kind::HOUSE, c); };
    const float flags = g(H_MODE_FLAGS);
    const uint16_t f = std::isfinite(flags) ? static_cast<uint16_t>(flags) : 0;
    if (f & (MODE_HOT_WATER | MODE_LEGIONELLA | MODE_DEFROST)) continue;
    if (!(g(H_RUN_MIN) >= 55.0f)) continue;
    l.learn(g(H_HEAT_SH_KWH), g(H_ELEC_KWH), g(H_HP_FEED_C), g(H_Z1_FEED_C), g(H_ROOM_C), g(H_OUTDOOR_C));
  }
  rep->hp_hours = l.samples();
  HeatPumpParams p = l.params();
  p.min_kw = seed.min_kw;
  p.max_kw = seed.max_kw;
  rep->heat_pump = p.valid;
  *out = p.valid ? p : seed;
  return p.valid;
}

// The floors' temperature at the start of `now_hour`, tracked through the last `hours`
// stored hours from a start where the floors would hold the first hour's temperature.
// NAN when the store has too little.
template <size_t N>
float floor_now(const HistoryStore<N> &st, uint32_t now_hour, const HouseParams &p, int (*local_hour)(uint32_t),
                LearnScratch *w, size_t hours = 72) {
  if (hours > LearnScratch::WINDOW) hours = LearnScratch::WINDOW;
  const uint32_t from = now_hour - static_cast<uint32_t>(hours) * 3600u;
  if (history_window(st, from, hours, local_hour, w->t, w->in) < hours * 3 / 4) return NAN;
  size_t first = 0;
  while (first < hours && !std::isfinite(w->t[first])) first++;
  if (first + 24 > hours) return NAN;
  // Start: floors warm enough to cover the first hour's loss.
  const HourIn &x = w->in[first];
  const float ph = x.hour_local * (6.2831853f / 24.0f);
  const float need = p.ua_kw_per_k * (1.0f + p.kw_per_ms * x.wind_ms) * (w->t[first] - x.out_c) - p.p_int_kw -
                     p.p_cos_kw * std::cos(ph) - p.p_sin_kw * std::sin(ph) - p.g_kw_per_100wm2 * x.sun_wm2 / 100.0f;
  const float ts0 = w->t[first] + std::fmax(0.0f, std::fmin(8.0f, need / p.h_kw_per_k));
  float *track = w->track;
  track_floor(p, &w->t[first], &w->in[first], hours - first, ts0, track);
  // One more step through the last hour.
  const size_t k = hours - first - 1;
  const float t_last = w->t[hours - 1];
  const float t = std::isfinite(t_last) ? t_last : w->t[hours];
  float s = track[k];
  if (std::isfinite(t))
    s += (w->in[hours - 1].q_kw + p.sun_floor_frac * p.g_kw_per_100wm2 * w->in[hours - 1].sun_wm2 / 100.0f -
          p.h_kw_per_k * (s - t)) / p.cs_kwh_per_k;
  return s;
}

}  // namespace lune_planner
