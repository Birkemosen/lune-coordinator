// =============================================================================
// Energy price → Odin (pure C++, host-testable)
// =============================================================================
// Odin 2.0 plans the heat pump on hourly electricity prices. Its built-in Danish
// price ((spot + 0.10 €) × 1.25) predates 2026: elafgift is now 0.008 DKK/kWh and
// the time-of-use grid tariff dominates the daily shape. Touch computes the real
// all-in consumer price and pushes it to Odin (POST /api/data/prices, €/kWh,
// hourly, starting at local 00:00 today, 24 or 48 values).
//
//   all_in_dkk(h) = (spot(h) + grid(h) + energinet(h) + elafgift + markup) × (1 + vat/100)
//   pushed €/kWh  = all_in_dkk(h) / fx, rounded to 4 decimals
//
// Every component is configurable: spot from Energi Data Service (DK1/DK2),
// grid tariff from DataHub (GLN + charge code), a user schedule (Odin-style
// blocks {h, v}) or none, Energinet tariffs from DataHub or a fixed value.
//
// This header has no ESPHome / ArduinoJson dependency: date math, the tiny
// record scanners for the Energi Data Service JSON, schedule parsing, the
// formula and the push scheduler are all exercised by tests/energy_price.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lune_touch_price {

static constexpr size_t HOURS = 24;
static constexpr size_t MAX_BLOCKS = 24;
static constexpr size_t MAX_TARIFF_RECORDS = 8;
/// Two local days of 15-minute records (a 25 h day has 100) plus slack.
static constexpr size_t MAX_SPOT_SAMPLES = 216;
static constexpr float DEFAULT_FX = 7.46f;

static constexpr const char *DEFAULT_GRID_GLN = "5790000610976";  // Vores Elnet
static constexpr const char *DEFAULT_GRID_CODE = "TNT1009";        // Nettarif C
static constexpr const char *ENERGINET_GLN = "5790000432752";
static constexpr const char *ENERGINET_TRANSMISSION_CODE = "40000";
static constexpr const char *ENERGINET_SYSTEM_CODE = "41000";

enum class Area : uint8_t { OFF = 0, DK1 = 1, DK2 = 2 };
enum class GridSource : uint8_t { NONE = 0, DATAHUB = 1, SCHEDULE = 2 };
enum class EnerginetSource : uint8_t { DATAHUB = 0, FIXED = 1 };

struct Block {
  uint8_t h = 0;
  float v = 0.0f;  ///< DKK/kWh excl. VAT
};

struct Schedule {
  Block blocks[MAX_BLOCKS]{};
  uint8_t count = 0;
};

struct Config {
  bool enabled = false;
  Area area = Area::DK1;
  GridSource grid_source = GridSource::DATAHUB;
  char grid_gln[16] = "5790000610976";
  char grid_code[24] = "TNT1009";
  Schedule grid_schedule{};
  EnerginetSource energinet_source = EnerginetSource::DATAHUB;
  float energinet_fixed_dkk = 0.115f;
  float elafgift_dkk = 0.008f;
  float markup_dkk = 0.0f;
  float vat_pct = 25.0f;
};

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

inline const char *area_name(Area a) {
  switch (a) {
    case Area::DK1: return "DK1";
    case Area::DK2: return "DK2";
    default: return "off";
  }
}
inline bool parse_area(const char *s, Area *out) {
  if (s == nullptr || out == nullptr) return false;
  if (std::strcmp(s, "DK1") == 0 || std::strcmp(s, "dk1") == 0) { *out = Area::DK1; return true; }
  if (std::strcmp(s, "DK2") == 0 || std::strcmp(s, "dk2") == 0) { *out = Area::DK2; return true; }
  if (std::strcmp(s, "off") == 0) { *out = Area::OFF; return true; }
  return false;
}
inline const char *grid_source_name(GridSource g) {
  switch (g) {
    case GridSource::DATAHUB: return "datahub";
    case GridSource::SCHEDULE: return "schedule";
    default: return "none";
  }
}
inline bool parse_grid_source(const char *s, GridSource *out) {
  if (s == nullptr || out == nullptr) return false;
  if (std::strcmp(s, "datahub") == 0) { *out = GridSource::DATAHUB; return true; }
  if (std::strcmp(s, "schedule") == 0) { *out = GridSource::SCHEDULE; return true; }
  if (std::strcmp(s, "none") == 0) { *out = GridSource::NONE; return true; }
  return false;
}
inline const char *energinet_source_name(EnerginetSource e) {
  return e == EnerginetSource::FIXED ? "fixed" : "datahub";
}
inline bool parse_energinet_source(const char *s, EnerginetSource *out) {
  if (s == nullptr || out == nullptr) return false;
  if (std::strcmp(s, "datahub") == 0) { *out = EnerginetSource::DATAHUB; return true; }
  if (std::strcmp(s, "fixed") == 0) { *out = EnerginetSource::FIXED; return true; }
  return false;
}

/// GLN numbers are 13 digits; charge codes are short alphanumerics. Both go
/// into a URL filter, so anything else is rejected rather than escaped.
inline bool valid_gln(const char *s) {
  if (s == nullptr) return false;
  size_t n = 0;
  for (; s[n] != '\0'; n++)
    if (s[n] < '0' || s[n] > '9') return false;
  return n == 13;
}
inline bool valid_charge_code(const char *s) {
  if (s == nullptr || s[0] == '\0') return false;
  size_t n = 0;
  for (; s[n] != '\0'; n++) {
    const char c = s[n];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == '-' || c == '_' || c == '.';
    if (!ok) return false;
  }
  return n < 24;
}

// ---------------------------------------------------------------------------
// Civil dates (proleptic Gregorian). ymd = YYYYMMDD; hour key = YYYYMMDDHH.
// ---------------------------------------------------------------------------

inline int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int>(yy + (*m <= 2));
}

inline int make_ymd(int y, int m, int d) { return y * 10000 + m * 100 + d; }

inline int add_days(int ymd, int days) {
  const int y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
  int ny;
  unsigned nm, nd;
  civil_from_days(days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) + days, &ny, &nm,
                  &nd);
  return make_ymd(ny, static_cast<int>(nm), static_cast<int>(nd));
}

inline int64_t hour_key(int ymd, int hour) { return static_cast<int64_t>(ymd) * 100 + hour; }

/// "YYYY-MM-DDTHH:MM[:SS]" (local, no zone) → ymd + hour + minute.
inline bool parse_iso_local(const char *s, int *ymd, int *hour, int *minute) {
  if (s == nullptr) return false;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  char sep = 0;
  if (std::sscanf(s, "%4d-%2d-%2d%c%2d:%2d", &y, &mo, &d, &sep, &h, &mi) < 4) return false;
  if (sep != 'T' && sep != ' ') return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return false;
  if (ymd) *ymd = make_ymd(y, mo, d);
  if (hour) *hour = h;
  if (minute) *minute = mi;
  return true;
}

inline void format_ymd(int ymd, char *out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  std::snprintf(out, cap, "%04d-%02d-%02d", ymd / 10000, (ymd / 100) % 100, ymd % 100);
}

// ---------------------------------------------------------------------------
// Schedule: Odin-style blocks [{"h":0,"v":0.077},{"h":6,"v":0.231},...]
// ---------------------------------------------------------------------------

namespace detail {
inline const char *skip_ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  return p;
}

/// Finds `"key":` inside [obj, end) and returns a pointer to the value.
inline const char *find_value(const char *obj, const char *end, const char *key) {
  char pattern[40];
  const int plen = std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if (plen <= 0 || static_cast<size_t>(plen) >= sizeof(pattern)) return nullptr;
  for (const char *p = obj; p + plen <= end; ++p) {
    if (std::memcmp(p, pattern, static_cast<size_t>(plen)) != 0) continue;
    const char *q = skip_ws(p + plen, end);
    if (q >= end || *q != ':') continue;
    return skip_ws(q + 1, end);
  }
  return nullptr;
}

/// Number (or quoted number) after `"key":`. `null` → false.
inline bool read_number(const char *obj, const char *end, const char *key, double *out) {
  const char *q = find_value(obj, end, key);
  if (q == nullptr || q >= end) return false;
  if (*q == '"') ++q;
  if (q >= end || *q == 'n') return false;
  char buf[40];
  size_t n = 0;
  while (q + n < end && n + 1 < sizeof(buf) &&
         (std::strchr("+-.0123456789eE", q[n]) != nullptr))
    ++n;
  if (n == 0) return false;
  std::memcpy(buf, q, n);
  buf[n] = '\0';
  char *stop = nullptr;
  const double v = std::strtod(buf, &stop);
  if (stop == buf || !std::isfinite(v)) return false;
  *out = v;
  return true;
}

/// String value after `"key":` (no escapes expected). `null` → false.
inline bool read_string(const char *obj, const char *end, const char *key, char *out, size_t cap) {
  const char *q = find_value(obj, end, key);
  if (q == nullptr || q >= end || *q != '"' || cap == 0) return false;
  ++q;
  size_t n = 0;
  while (q + n < end && q[n] != '"') ++n;
  if (q + n >= end) return false;
  const size_t c = n < cap - 1 ? n : cap - 1;
  std::memcpy(out, q, c);
  out[c] = '\0';
  return true;
}

/// Calls fn(obj_begin, obj_end) for each flat {...} object inside the JSON
/// array that follows `"records":` (or the top-level array when key is null).
template <typename Fn> size_t for_each_object(const char *body, size_t len, const char *key, Fn fn) {
  if (body == nullptr) return 0;
  const char *end = body + len;
  const char *p = body;
  if (key != nullptr) {
    p = find_value(body, end, key);
    if (p == nullptr) return 0;
  } else {
    p = skip_ws(p, end);
  }
  if (p >= end || *p != '[') return 0;
  ++p;
  size_t count = 0;
  while (p < end) {
    p = skip_ws(p, end);
    if (p >= end || *p == ']') break;
    if (*p == ',') { ++p; continue; }
    if (*p != '{') return count;
    const char *obj = p;
    bool in_str = false;
    while (p < end && (in_str || *p != '}')) {
      if (*p == '"' && (p == obj || p[-1] != '\\')) in_str = !in_str;
      ++p;
    }
    if (p >= end) return count;
    ++p;  // past '}'
    if (!fn(obj, p)) return count;
    ++count;
  }
  return count;
}
}  // namespace detail

inline void sort_blocks(Schedule *s) {
  std::sort(s->blocks, s->blocks + s->count, [](const Block &a, const Block &b) { return a.h < b.h; });
}

/// Accepts unsorted blocks; rejects empty, duplicate hours, hours > 23 and
/// values outside [-5, 20] DKK/kWh.
inline bool parse_schedule(const char *json, Schedule *out) {
  if (json == nullptr || out == nullptr) return false;
  Schedule s{};
  bool ok = true;
  const size_t len = std::strlen(json);
  detail::for_each_object(json, len, nullptr, [&](const char *obj, const char *end) {
    double h = NAN, v = NAN;
    if (s.count >= MAX_BLOCKS || !detail::read_number(obj, end, "h", &h) ||
        !detail::read_number(obj, end, "v", &v) || h < 0 || h > 23 || h != std::floor(h) ||
        v < -5.0 || v > 20.0) {
      ok = false;
      return false;
    }
    for (uint8_t i = 0; i < s.count; i++)
      if (s.blocks[i].h == static_cast<uint8_t>(h)) { ok = false; return false; }
    s.blocks[s.count++] = {static_cast<uint8_t>(h), static_cast<float>(v)};
    return true;
  });
  if (!ok || s.count == 0) return false;
  sort_blocks(&s);
  *out = s;
  return true;
}

inline size_t format_schedule(const Schedule &s, char *out, size_t cap) {
  if (out == nullptr || cap < 3) return 0;
  size_t off = 0;
  out[off++] = '[';
  for (uint8_t i = 0; i < s.count; i++) {
    const int n = std::snprintf(out + off, cap - off, "%s{\"h\":%u,\"v\":%.4g}", i ? "," : "",
                                static_cast<unsigned>(s.blocks[i].h), static_cast<double>(s.blocks[i].v));
    if (n < 0 || static_cast<size_t>(n) >= cap - off) { out[0] = '\0'; return 0; }
    off += static_cast<size_t>(n);
  }
  if (off + 2 > cap) { out[0] = '\0'; return 0; }
  out[off++] = ']';
  out[off] = '\0';
  return off;
}

/// Each block applies from its hour until the next block; the last block
/// wraps past midnight (so hours before the first block take the last value).
inline bool expand_schedule(const Schedule &s, float out[HOURS]) {
  if (s.count == 0) return false;
  for (size_t h = 0; h < HOURS; h++) {
    float v = s.blocks[s.count - 1].v;
    for (uint8_t i = 0; i < s.count; i++)
      if (s.blocks[i].h <= h) v = s.blocks[i].v;
    out[h] = v;
  }
  return true;
}

/// Vores Elnet "Nettarif C" from 2026-10-01 (excl. VAT) — the UI's starting point.
inline Schedule default_grid_schedule() {
  Schedule s{};
  s.blocks[0] = {0, 0.077f};
  s.blocks[1] = {6, 0.231f};
  s.blocks[2] = {17, 0.692f};
  s.blocks[3] = {21, 0.231f};
  s.count = 4;
  return s;
}

// ---------------------------------------------------------------------------
// Spot: 15-minute DayAheadPrices → hourly DKK/kWh per local day
// ---------------------------------------------------------------------------

struct SpotSample {
  int ymd = 0;
  uint8_t hour = 0;
  float dkk_mwh = 0.0f;
  float eur_mwh = 0.0f;
};

/// Parses {"records":[{"TimeDK":"…","DayAheadPriceDKK":…,"DayAheadPriceEUR":…},…]}.
inline size_t parse_spot_records(const char *body, size_t len, SpotSample *out, size_t cap) {
  size_t n = 0;
  detail::for_each_object(body, len, "records", [&](const char *obj, const char *end) {
    if (n >= cap) return false;
    char ts[24];
    double dkk = NAN, eur = NAN;
    int ymd = 0, hour = 0, minute = 0;
    if (!detail::read_string(obj, end, "TimeDK", ts, sizeof(ts)) || !parse_iso_local(ts, &ymd, &hour, &minute) ||
        !detail::read_number(obj, end, "DayAheadPriceDKK", &dkk))
      return true;  // skip incomplete records
    if (!detail::read_number(obj, end, "DayAheadPriceEUR", &eur)) eur = NAN;
    out[n].ymd = ymd;
    out[n].hour = static_cast<uint8_t>(hour);
    out[n].dkk_mwh = static_cast<float>(dkk);
    out[n].eur_mwh = static_cast<float>(eur);
    n++;
    return true;
  });
  return n;
}

struct SpotDay {
  bool complete = false;
  uint8_t hours_with_data = 0;
  float dkk_kwh[HOURS]{};
};

/// Averages all samples that share a local clock hour (4 quarters; 8 for the
/// repeated hour on the 25 h autumn day). A day is complete with ≥ 23 hours
/// (the spring day has no 02:00); missing hours copy the previous hour.
inline SpotDay aggregate_spot_day(const SpotSample *samples, size_t n, int ymd) {
  SpotDay day{};
  double sum[HOURS]{};
  uint8_t cnt[HOURS]{};
  for (size_t i = 0; i < n; i++) {
    if (samples[i].ymd != ymd || samples[i].hour >= HOURS || !std::isfinite(samples[i].dkk_mwh)) continue;
    sum[samples[i].hour] += samples[i].dkk_mwh;
    cnt[samples[i].hour]++;
  }
  for (size_t h = 0; h < HOURS; h++) {
    if (cnt[h] > 0) {
      day.dkk_kwh[h] = static_cast<float>(sum[h] / cnt[h] / 1000.0);
      day.hours_with_data++;
    } else {
      day.dkk_kwh[h] = NAN;
    }
  }
  if (day.hours_with_data < HOURS - 1) return day;
  for (size_t h = 0; h < HOURS; h++) {
    if (std::isfinite(day.dkk_kwh[h])) continue;
    day.dkk_kwh[h] = h > 0 ? day.dkk_kwh[h - 1] : day.dkk_kwh[1];
  }
  day.complete = true;
  return day;
}

/// DKK per EUR from the same records (mean of DKK/EUR where EUR is not ~0).
inline float spot_fx(const SpotSample *samples, size_t n) {
  double sum = 0.0;
  size_t cnt = 0;
  for (size_t i = 0; i < n; i++) {
    const float e = samples[i].eur_mwh;
    if (!std::isfinite(e) || std::fabs(e) < 1.0f) continue;
    const double r = samples[i].dkk_mwh / e;
    if (r < 5.0 || r > 10.0) continue;
    sum += r;
    cnt++;
  }
  return cnt > 0 ? static_cast<float>(sum / cnt) : DEFAULT_FX;
}

// ---------------------------------------------------------------------------
// DataHub price lists (ValidFrom/ValidTo + Price1..Price24)
// ---------------------------------------------------------------------------

struct TariffRecord {
  int64_t from_key = 0;  ///< YYYYMMDDHH, inclusive
  int64_t to_key = 0;    ///< YYYYMMDDHH, exclusive; 0 = open-ended
  float price[HOURS]{};  ///< DKK/kWh excl. VAT; Price1 = 00–01
};

struct TariffSeries {
  TariffRecord records[MAX_TARIFF_RECORDS]{};
  uint8_t count = 0;
};

/// Parses DatahubPricelist records in API order (sort=ValidFrom desc). When
/// `code` is set, only records whose ChargeTypeCode matches are kept.
/// Price2..24 null → Price1 for every hour (flat tariffs).
inline size_t parse_tariff_records(const char *body, size_t len, const char *code, TariffSeries *out) {
  if (out == nullptr) return 0;
  TariffSeries s{};
  detail::for_each_object(body, len, "records", [&](const char *obj, const char *end) {
    if (s.count >= MAX_TARIFF_RECORDS) return false;
    if (code != nullptr && code[0] != '\0') {
      char c[24];
      if (!detail::read_string(obj, end, "ChargeTypeCode", c, sizeof(c)) || std::strcmp(c, code) != 0) return true;
    }
    char from[24], to[24];
    int fy = 0, fh = 0, ty = 0, th = 0;
    if (!detail::read_string(obj, end, "ValidFrom", from, sizeof(from)) || !parse_iso_local(from, &fy, &fh, nullptr))
      return true;
    TariffRecord r{};
    r.from_key = hour_key(fy, fh);
    if (detail::read_string(obj, end, "ValidTo", to, sizeof(to)) && parse_iso_local(to, &ty, &th, nullptr))
      r.to_key = hour_key(ty, th);
    double p1 = NAN;
    if (!detail::read_number(obj, end, "Price1", &p1)) return true;
    for (size_t h = 0; h < HOURS; h++) {
      char key[10];
      std::snprintf(key, sizeof(key), "Price%u", static_cast<unsigned>(h + 1));
      double p = NAN;
      r.price[h] = static_cast<float>(h > 0 && detail::read_number(obj, end, key, &p) ? p : p1);
    }
    s.records[s.count++] = r;
    return true;
  });
  *out = s;
  return s.count;
}

/// The record valid at `key`: ValidFrom ≤ key < ValidTo. Several can match
/// (re-issued price lists): the latest ValidFrom wins, ties → first in list
/// (the API's most recent entry).
inline const TariffRecord *select_record(const TariffSeries &s, int64_t key) {
  const TariffRecord *best = nullptr;
  for (uint8_t i = 0; i < s.count; i++) {
    const TariffRecord &r = s.records[i];
    if (r.from_key > key || (r.to_key != 0 && key >= r.to_key)) continue;
    if (best == nullptr || r.from_key > best->from_key) best = &r;
  }
  return best;
}

/// Hourly tariff for local day `ymd`; false if any hour has no valid record.
inline bool tariff_for_day(const TariffSeries &s, int ymd, float out[HOURS]) {
  for (size_t h = 0; h < HOURS; h++) {
    const TariffRecord *r = select_record(s, hour_key(ymd, static_cast<int>(h)));
    if (r == nullptr) return false;
    out[h] = r->price[h];
  }
  return true;
}

// ---------------------------------------------------------------------------
// Formula + Odin payload
// ---------------------------------------------------------------------------

inline float all_in_dkk(float spot_dkk_kwh, float grid_dkk, float energinet_dkk, const Config &cfg) {
  const double base = static_cast<double>(spot_dkk_kwh) + grid_dkk + energinet_dkk + cfg.elafgift_dkk +
                      cfg.markup_dkk;
  return static_cast<float>(base * (1.0 + cfg.vat_pct / 100.0));
}

inline float round4(double v) { return static_cast<float>(std::round(v * 10000.0) / 10000.0); }

inline float to_eur(float dkk, float fx) {
  if (!(fx > 0.0f) || !std::isfinite(dkk)) return NAN;
  return round4(static_cast<double>(dkk) / fx);
}

struct DayPrices {
  bool valid = false;
  int ymd = 0;
  float spot[HOURS]{};       ///< DKK/kWh excl. VAT
  float grid[HOURS]{};       ///< DKK/kWh excl. VAT
  float energinet[HOURS]{};  ///< DKK/kWh excl. VAT
  float total[HOURS]{};      ///< DKK/kWh incl. taxes + VAT
};

inline DayPrices build_day(int ymd, const float spot[HOURS], const float grid[HOURS],
                           const float energinet[HOURS], const Config &cfg) {
  DayPrices d{};
  d.ymd = ymd;
  for (size_t h = 0; h < HOURS; h++) {
    if (!std::isfinite(spot[h]) || !std::isfinite(grid[h]) || !std::isfinite(energinet[h])) return DayPrices{};
    d.spot[h] = spot[h];
    d.grid[h] = grid[h];
    d.energinet[h] = energinet[h];
    d.total[h] = all_in_dkk(spot[h], grid[h], energinet[h], cfg);
  }
  d.valid = true;
  return d;
}

/// Odin's array: today's 24 hours from local 00:00, plus tomorrow's 24 when
/// known. Returns the number of values (0 when today is not valid).
inline size_t build_odin_array(const DayPrices &today, const DayPrices &tomorrow, float fx, float *out,
                               size_t cap) {
  if (!today.valid || out == nullptr || cap < HOURS || !(fx > 0.0f)) return 0;
  size_t n = 0;
  for (size_t h = 0; h < HOURS; h++) out[n++] = to_eur(today.total[h], fx);
  if (tomorrow.valid && cap >= 2 * HOURS)
    for (size_t h = 0; h < HOURS; h++) out[n++] = to_eur(tomorrow.total[h], fx);
  return n;
}

/// {"prices":[0.1234,...]} — 4 decimals, as Odin stores them.
inline size_t format_odin_payload(const float *eur, size_t n, char *out, size_t cap) {
  if (out == nullptr || cap < 16 || eur == nullptr || n == 0) return 0;
  size_t off = static_cast<size_t>(std::snprintf(out, cap, "{\"prices\":["));
  for (size_t i = 0; i < n; i++) {
    const int w = std::snprintf(out + off, cap - off, "%s%.4f", i ? "," : "", static_cast<double>(eur[i]));
    if (w < 0 || static_cast<size_t>(w) >= cap - off) { out[0] = '\0'; return 0; }
    off += static_cast<size_t>(w);
  }
  if (off + 3 > cap) { out[0] = '\0'; return 0; }
  out[off++] = ']';
  out[off++] = '}';
  out[off] = '\0';
  return off;
}

// ---------------------------------------------------------------------------
// Push scheduler
// ---------------------------------------------------------------------------
// After boot (once the clock is valid), then after local 00:05 (the array is
// anchored at today 00:00), then from 14:15 every 30 min until tomorrow's spot
// is in the push (stop retrying at 20:00). A failed push retries after 30 min.

static constexpr uint16_t MIDNIGHT_MIN = 5;              // 00:05
static constexpr uint16_t DAY_AHEAD_MIN = 14 * 60 + 15;  // 14:15
static constexpr uint16_t DAY_AHEAD_END_MIN = 20 * 60;   // 20:00
static constexpr uint16_t RETRY_MIN = 30;

enum class Due : uint8_t { NONE = 0, REQUEST, INITIAL, NEW_DAY, DAY_AHEAD, RETRY };

inline const char *due_name(Due d) {
  switch (d) {
    case Due::REQUEST: return "request";
    case Due::INITIAL: return "boot";
    case Due::NEW_DAY: return "midnight";
    case Due::DAY_AHEAD: return "day_ahead";
    case Due::RETRY: return "retry";
    default: return "none";
  }
}

struct SchedulerState {
  int pushed_ymd = 0;            ///< Day the last successful push was anchored at
  bool pushed_tomorrow = false;  ///< That push carried tomorrow's 24 hours
  int attempt_ymd = 0;           ///< Last attempt (any outcome), local
  uint16_t attempt_min = 0;
  bool attempt_ok = false;
};

inline int minutes_since_attempt(const SchedulerState &s, int ymd, uint16_t minute) {
  if (s.attempt_ymd == 0) return 1 << 20;
  if (s.attempt_ymd == ymd) return static_cast<int>(minute) - static_cast<int>(s.attempt_min);
  if (add_days(s.attempt_ymd, 1) == ymd) return static_cast<int>(minute) + 1440 - s.attempt_min;
  return 1 << 20;
}

inline Due push_due(bool enabled, bool requested, bool time_valid, int ymd, uint16_t minute,
                    const SchedulerState &s) {
  if (!enabled) return Due::NONE;
  if (requested) return Due::REQUEST;
  if (!time_valid || ymd == 0) return Due::NONE;
  const int since = minutes_since_attempt(s, ymd, minute);
  if (s.pushed_ymd != ymd) {
    if (s.attempt_ymd == 0) return Due::INITIAL;
    if (minute < MIDNIGHT_MIN) return Due::NONE;
    // First try of a new day, or a retry after a failure.
    if (s.attempt_ymd != ymd || (s.attempt_min < MIDNIGHT_MIN && s.attempt_ymd == ymd))
      return Due::NEW_DAY;
    return since >= RETRY_MIN ? Due::RETRY : Due::NONE;
  }
  if (s.pushed_tomorrow || minute < DAY_AHEAD_MIN || minute >= DAY_AHEAD_END_MIN) return Due::NONE;
  if (s.attempt_ymd == ymd && s.attempt_min < DAY_AHEAD_MIN) return Due::DAY_AHEAD;
  return since >= RETRY_MIN ? Due::DAY_AHEAD : Due::NONE;
}

}  // namespace lune_touch_price
