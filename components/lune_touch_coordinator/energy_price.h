// =============================================================================
// Energy price → Odin (pure C++, host-testable)
// =============================================================================
// Odin 2.0 plans the heat pump on hourly electricity prices. It can fetch them
// itself (Energy-Charts / ENTSO-E spot + a per-zone energy tax and VAT, or a
// fixed price), or take pushed prices (price_source = "api"). Its tax table is
// coarse — for Denmark it predates 2026 (0.10 €/kWh instead of elafgift 0.008
// DKK) and no zone has a grid tariff — so Touch offers two models:
//
//   Model::ODIN   Touch writes Odin's own price settings and shows them.
//   Model::TOUCH  Touch computes the all-in consumer price and pushes it
//                 (POST /api/data/prices, €/kWh, hourly from local 00:00 today,
//                 24 or 48 values):
//
//   all_in(h)  = (spot(h) + grid(h) + system(h) + energy_tax + markup) × (1 + vat/100)
//   pushed €   = all_in(h) / fx, rounded to 4 decimals
//
// All components are in one calculation currency (EUR or a local one with fx =
// local per EUR). Spot: Energi Data Service (DK1/DK2, DKK + EUR), Energy-Charts
// (EUR/MWh, any zone), ENTSO-E (EUR/MWh, needs a token) or a fixed € price.
// Grid tariff: DataHub (DK), a user schedule (Odin-style blocks {h, v}) or none.
// System/transmission tariff: DataHub (Energinet, DK) or a fixed value.
//
// No ESPHome / ArduinoJson dependency: date math, the zone table, the small
// JSON/XML scanners, schedule parsing, the formula and the push scheduler are
// all exercised by tests/energy_price.
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

enum class Model : uint8_t { ODIN = 0, TOUCH = 1 };
enum class SpotSource : uint8_t { EDS = 0, ENERGY_CHARTS = 1, ENTSOE = 2, FIXED = 3 };
enum class GridSource : uint8_t { NONE = 0, DATAHUB = 1, SCHEDULE = 2 };
enum class SystemSource : uint8_t { DATAHUB = 0, FIXED = 1 };
enum class OdinMode : uint8_t { DYNAMIC = 0, FIXED = 1 };
enum class OdinSource : uint8_t { ENERGY_CHARTS = 0, ENTSOE = 1 };

struct Block {
  uint8_t h = 0;
  float v = 0.0f;  ///< calculation currency per kWh, excl. VAT
};

struct Schedule {
  Block blocks[MAX_BLOCKS]{};
  uint8_t count = 0;
};

struct Config {
  bool enabled = false;
  Model model = Model::TOUCH;
  char zone[12] = "DK1";  ///< Energy-Charts / Odin ec_bzn code
  // Touch model
  SpotSource spot_source = SpotSource::EDS;
  float spot_fixed_eur = 0.10f;  ///< €/kWh, SpotSource::FIXED
  char currency[4] = "DKK";      ///< calculation currency of every component below
  float fx = DEFAULT_FX;         ///< currency per EUR (EUR → 1)
  GridSource grid_source = GridSource::DATAHUB;
  char grid_gln[16] = "5790000610976";
  char grid_code[24] = "TNT1009";
  Schedule grid_schedule{};
  SystemSource system_source = SystemSource::DATAHUB;
  float system_fixed = 0.115f;
  float energy_tax = 0.008f;
  float markup = 0.0f;
  float vat_pct = 25.0f;
  // Odin model (written to Odin's own settings)
  OdinMode odin_mode = OdinMode::DYNAMIC;
  OdinSource odin_source = OdinSource::ENERGY_CHARTS;
  float odin_fixed_price = 0.25f;  ///< €/kWh
};

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

template <typename E, size_t N>
inline bool parse_enum_(const char *s, const char *const (&names)[N], E *out) {
  if (s == nullptr || out == nullptr) return false;
  for (size_t i = 0; i < N; i++)
    if (names[i] != nullptr && std::strcmp(s, names[i]) == 0) { *out = static_cast<E>(i); return true; }
  return false;
}
static constexpr const char *const MODEL_NAMES[] = {"odin", "touch"};
static constexpr const char *const SPOT_SOURCE_NAMES[] = {"eds", "energy_charts", "entsoe", "fixed"};
static constexpr const char *const GRID_SOURCE_NAMES[] = {"none", "datahub", "schedule"};
static constexpr const char *const SYSTEM_SOURCE_NAMES[] = {"datahub", "fixed"};
static constexpr const char *const ODIN_MODE_NAMES[] = {"dynamic", "fixed"};
static constexpr const char *const ODIN_SOURCE_NAMES[] = {"energy_charts", "entsoe"};

inline const char *model_name(Model v) { return MODEL_NAMES[static_cast<size_t>(v) & 1]; }
inline const char *spot_source_name(SpotSource v) { return SPOT_SOURCE_NAMES[static_cast<size_t>(v) & 3]; }
inline const char *grid_source_name(GridSource v) { return static_cast<size_t>(v) < 3 ? GRID_SOURCE_NAMES[static_cast<size_t>(v)] : "none"; }
inline const char *system_source_name(SystemSource v) { return SYSTEM_SOURCE_NAMES[static_cast<size_t>(v) & 1]; }
inline const char *odin_mode_name(OdinMode v) { return ODIN_MODE_NAMES[static_cast<size_t>(v) & 1]; }
inline const char *odin_source_name(OdinSource v) { return ODIN_SOURCE_NAMES[static_cast<size_t>(v) & 1]; }
inline bool parse_model(const char *s, Model *o) { return parse_enum_(s, MODEL_NAMES, o); }
inline bool parse_spot_source(const char *s, SpotSource *o) { return parse_enum_(s, SPOT_SOURCE_NAMES, o); }
inline bool parse_grid_source(const char *s, GridSource *o) { return parse_enum_(s, GRID_SOURCE_NAMES, o); }
inline bool parse_system_source(const char *s, SystemSource *o) { return parse_enum_(s, SYSTEM_SOURCE_NAMES, o); }
inline bool parse_odin_mode(const char *s, OdinMode *o) { return parse_enum_(s, ODIN_MODE_NAMES, o); }
inline bool parse_odin_source(const char *s, OdinSource *o) { return parse_enum_(s, ODIN_SOURCE_NAMES, o); }

// ---------------------------------------------------------------------------
// Zones (Odin 2.0.0-33's list: Energy-Charts code, ENTSO-E EIC, energy tax and
// VAT). EIC codes checked against the ENTSO-E area list (entsoe-py mappings).
// Energy tax is €/kWh as in Odin; zone_defaults() converts it to the zone's
// currency. Denmark's row is Odin's (outdated) value; Touch's DK defaults use
// the 2026 elafgift instead.
// ---------------------------------------------------------------------------

enum class ZoneGroup : uint8_t { BENELUX, DACH, FR_IBERIA, BRITISH_ISLES, NORDIC, EASTERN, ITALY };

struct Zone {
  const char *id;
  const char *eic;
  ZoneGroup group;
  float tax_eur;
  float vat_pct;
  const char *currency;
};

// One row per line: web/touch-ui/build_ui.py reads this table for the zone select.
static constexpr Zone ZONES[] = {
    {"NL", "10YNL----------L", ZoneGroup::BENELUX, 0.109f, 21.0f, "EUR"},
    {"BE", "10YBE----------2", ZoneGroup::BENELUX, 0.050f, 6.0f, "EUR"},
    {"DE-LU", "10Y1001A1001A82H", ZoneGroup::DACH, 0.041f, 19.0f, "EUR"},
    {"DE-AT-LU", "10Y1001A1001A63L", ZoneGroup::DACH, 0.041f, 19.0f, "EUR"},
    {"AT", "10YAT-APG------L", ZoneGroup::DACH, 0.015f, 20.0f, "EUR"},
    {"CH", "10YCH-SWISSGRIDZ", ZoneGroup::DACH, 0.0f, 8.1f, "CHF"},
    {"FR", "10YFR-RTE------C", ZoneGroup::FR_IBERIA, 0.033f, 20.0f, "EUR"},
    {"ES", "10YES-REE------0", ZoneGroup::FR_IBERIA, 0.005f, 21.0f, "EUR"},
    {"PT", "10YPT-REN------W", ZoneGroup::FR_IBERIA, 0.001f, 23.0f, "EUR"},
    {"GB", "10YGB----------A", ZoneGroup::BRITISH_ISLES, 0.0f, 5.0f, "GBP"},
    {"IE", "10YIE-1001A00010", ZoneGroup::BRITISH_ISLES, 0.001f, 9.0f, "EUR"},
    {"DK1", "10YDK-1--------W", ZoneGroup::NORDIC, 0.100f, 25.0f, "DKK"},
    {"DK2", "10YDK-2--------M", ZoneGroup::NORDIC, 0.100f, 25.0f, "DKK"},
    {"FI", "10YFI-1--------U", ZoneGroup::NORDIC, 0.023f, 25.5f, "EUR"},
    {"SE1", "10Y1001A1001A44P", ZoneGroup::NORDIC, 0.0f, 25.0f, "SEK"},
    {"SE2", "10Y1001A1001A45N", ZoneGroup::NORDIC, 0.0f, 25.0f, "SEK"},
    {"SE3", "10Y1001A1001A46L", ZoneGroup::NORDIC, 0.0f, 25.0f, "SEK"},
    {"SE4", "10Y1001A1001A47J", ZoneGroup::NORDIC, 0.0f, 25.0f, "SEK"},
    {"NO1", "10YNO-1--------2", ZoneGroup::NORDIC, 0.0f, 25.0f, "NOK"},
    {"NO2", "10YNO-2--------T", ZoneGroup::NORDIC, 0.0f, 25.0f, "NOK"},
    {"NO3", "10YNO-3--------J", ZoneGroup::NORDIC, 0.0f, 25.0f, "NOK"},
    {"NO4", "10YNO-4--------9", ZoneGroup::NORDIC, 0.0f, 25.0f, "NOK"},
    {"NO5", "10Y1001A1001A48H", ZoneGroup::NORDIC, 0.0f, 25.0f, "NOK"},
    {"PL", "10YPL-AREA-----S", ZoneGroup::EASTERN, 0.010f, 23.0f, "PLN"},
    {"CZ", "10YCZ-CEPS-----N", ZoneGroup::EASTERN, 0.028f, 21.0f, "CZK"},
    {"SK", "10YSK-SEPS-----K", ZoneGroup::EASTERN, 0.013f, 20.0f, "EUR"},
    {"HU", "10YHU-MAVIR----U", ZoneGroup::EASTERN, 0.001f, 27.0f, "HUF"},
    {"RO", "10YRO-TEL------P", ZoneGroup::EASTERN, 0.003f, 19.0f, "RON"},
    {"BG", "10YCA-BULGARIA-R", ZoneGroup::EASTERN, 0.002f, 20.0f, "EUR"},
    {"SI", "10YSI-ELES-----O", ZoneGroup::EASTERN, 0.015f, 22.0f, "EUR"},
    {"HR", "10YHR-HEP------M", ZoneGroup::EASTERN, 0.005f, 25.0f, "EUR"},
    {"EE", "10Y1001A1001A39I", ZoneGroup::EASTERN, 0.005f, 22.0f, "EUR"},
    {"LT", "10YLT-1001A0008Q", ZoneGroup::EASTERN, 0.001f, 21.0f, "EUR"},
    {"LV", "10YLV-1001A00074", ZoneGroup::EASTERN, 0.001f, 21.0f, "EUR"},
    {"GR", "10YGR-HTSO-----Y", ZoneGroup::EASTERN, 0.003f, 24.0f, "EUR"},
    {"RS", "10YCS-SERBIATSOV", ZoneGroup::EASTERN, 0.0f, 20.0f, "RSD"},
    {"IT-North", "10Y1001A1001A73I", ZoneGroup::ITALY, 0.023f, 10.0f, "EUR"},
};
static constexpr size_t ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);

/// Case-insensitive (Odin writes IT-NORTH in its tax table, Energy-Charts IT-North).
inline const Zone *find_zone(const char *id) {
  if (id == nullptr) return nullptr;
  for (const Zone &z : ZONES) {
    size_t i = 0;
    for (; z.id[i] != '\0' && id[i] != '\0'; i++) {
      char a = z.id[i], b = id[i];
      if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 32);
      if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 32);
      if (a != b) break;
    }
    if (z.id[i] == '\0' && id[i] == '\0') return &z;
  }
  return nullptr;
}
inline const Zone *find_zone_by_eic(const char *eic) {
  if (eic == nullptr) return nullptr;
  for (const Zone &z : ZONES)
    if (std::strcmp(z.eic, eic) == 0) return &z;
  return nullptr;
}
inline bool zone_is_dk(const char *id) {
  const Zone *z = find_zone(id);
  return z != nullptr && (std::strcmp(z->id, "DK1") == 0 || std::strcmp(z->id, "DK2") == 0);
}

// ---------------------------------------------------------------------------
// Currencies (approximate default rates, local per EUR — editable in the UI)
// ---------------------------------------------------------------------------

struct Currency {
  const char *code;
  float default_fx;
};
static constexpr Currency CURRENCIES[] = {
    {"EUR", 1.0f},    {"DKK", 7.46f},  {"SEK", 11.0f},  {"NOK", 11.7f}, {"CHF", 0.94f}, {"PLN", 4.25f},
    {"CZK", 24.3f},   {"HUF", 395.0f}, {"RON", 5.08f},  {"BGN", 1.95583f}, {"GBP", 0.86f}, {"RSD", 117.2f},
};
inline const Currency *find_currency(const char *code) {
  if (code == nullptr) return nullptr;
  for (const Currency &c : CURRENCIES)
    if (std::strcmp(c.code, code) == 0) return &c;
  return nullptr;
}
inline bool is_eur(const char *code) { return code != nullptr && std::strcmp(code, "EUR") == 0; }

/// Defaults for "Apply zone defaults". Not a Config: only the fields the
/// action fills (spot source, currency, fx, tax, VAT, grid/system sources).
struct ZoneDefaults {
  bool known = false;
  SpotSource spot_source = SpotSource::ENERGY_CHARTS;
  char currency[4] = "EUR";
  float fx = 1.0f;
  float energy_tax = 0.0f;  ///< in `currency`
  float vat_pct = 21.0f;
  GridSource grid_source = GridSource::NONE;
  SystemSource system_source = SystemSource::FIXED;
  float system_fixed = 0.0f;
};

inline ZoneDefaults zone_defaults(const char *id) {
  ZoneDefaults d{};
  const Zone *z = find_zone(id);
  if (z == nullptr) return d;  // unknown → EUR, 0 tax, 21 % VAT (Odin's fallback)
  d.known = true;
  if (zone_is_dk(z->id)) {
    // 2026: elafgift 0.008 DKK/kWh; tariffs from DataHub (Vores Elnet / Energinet).
    d.spot_source = SpotSource::EDS;
    std::strcpy(d.currency, "DKK");
    d.fx = DEFAULT_FX;
    d.energy_tax = 0.008f;
    d.vat_pct = 25.0f;
    d.grid_source = GridSource::DATAHUB;
    d.system_source = SystemSource::DATAHUB;
    d.system_fixed = 0.115f;
    return d;
  }
  const Currency *c = find_currency(z->currency);
  std::strncpy(d.currency, c != nullptr ? c->code : "EUR", sizeof(d.currency) - 1);
  d.fx = c != nullptr ? c->default_fx : 1.0f;
  d.energy_tax = static_cast<float>(std::round(static_cast<double>(z->tax_eur) * d.fx * 10000.0) / 10000.0);
  d.vat_pct = z->vat_pct;
  return d;
}

/// € → calculation currency: EUR → 1; otherwise the configured rate.
inline float config_fx(const Config &c) { return is_eur(c.currency) ? 1.0f : c.fx; }

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
  float mwh = 0.0f;      ///< calculation currency per MWh (EDS: DKK)
  float eur_mwh = NAN;   ///< EUR per MWh when the source has it
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
    out[n].mwh = static_cast<float>(dkk);
    out[n].eur_mwh = static_cast<float>(eur);
    n++;
    return true;
  });
  return n;
}

struct SpotDay {
  bool complete = false;
  uint8_t hours_with_data = 0;
  float kwh[HOURS]{};  ///< calculation currency per kWh
};

/// Averages all samples that share a local clock hour (4 quarters; 8 for the
/// repeated hour on the 25 h autumn day). A day is complete with ≥ 23 hours
/// (the spring day has no 02:00); missing hours copy the previous hour.
inline SpotDay aggregate_spot_day(const SpotSample *samples, size_t n, int ymd) {
  SpotDay day{};
  double sum[HOURS]{};
  uint8_t cnt[HOURS]{};
  for (size_t i = 0; i < n; i++) {
    if (samples[i].ymd != ymd || samples[i].hour >= HOURS || !std::isfinite(samples[i].mwh)) continue;
    sum[samples[i].hour] += samples[i].mwh;
    cnt[samples[i].hour]++;
  }
  for (size_t h = 0; h < HOURS; h++) {
    if (cnt[h] > 0) {
      day.kwh[h] = static_cast<float>(sum[h] / cnt[h] / 1000.0);
      day.hours_with_data++;
    } else {
      day.kwh[h] = NAN;
    }
  }
  if (day.hours_with_data < HOURS - 1) return day;
  for (size_t h = 0; h < HOURS; h++) {
    if (std::isfinite(day.kwh[h])) continue;
    day.kwh[h] = h > 0 ? day.kwh[h - 1] : day.kwh[1];
  }
  day.complete = true;
  return day;
}

/// Re-prices samples from EUR: mwh = eur_mwh × fx (EDS with a non-DKK currency).
inline void reprice_from_eur(SpotSample *samples, size_t n, float fx) {
  for (size_t i = 0; i < n; i++) samples[i].mwh = samples[i].eur_mwh * fx;
}

/// DKK per EUR from the same records (mean of DKK/EUR where EUR is not ~0).
inline float spot_fx(const SpotSample *samples, size_t n) {
  double sum = 0.0;
  size_t cnt = 0;
  for (size_t i = 0; i < n; i++) {
    const float e = samples[i].eur_mwh;
    if (!std::isfinite(e) || std::fabs(e) < 1.0f) continue;
    const double r = samples[i].mwh / e;
    if (r < 5.0 || r > 10.0) continue;
    sum += r;
    cnt++;
  }
  return cnt > 0 ? static_cast<float>(sum / cnt) : DEFAULT_FX;
}

// ---------------------------------------------------------------------------
// Energy-Charts and ENTSO-E: EUR/MWh with UTC timestamps
// ---------------------------------------------------------------------------

inline int64_t epoch_from_utc(int y, int mo, int d, int h, int mi) {
  return days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400LL + h * 3600LL + mi * 60LL;
}

/// "2026-10-05T22:00Z" / "2026-10-05T22:00:00Z" → epoch seconds.
inline bool parse_iso_utc(const char *s, int64_t *out) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  if (s == nullptr || std::sscanf(s, "%4d-%2d-%2dT%2d:%2d", &y, &mo, &d, &h, &mi) != 5) return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 24 || mi < 0 || mi > 59) return false;
  *out = epoch_from_utc(y, mo, d, h, mi);
  return true;
}

/// "YYYYMMDDHHMM" in UTC (ENTSO-E periodStart/periodEnd).
inline void format_entsoe_time(int64_t epoch, char *out, size_t cap) {
  int y;
  unsigned m, d;
  const int64_t days = epoch >= 0 ? epoch / 86400 : (epoch - 86399) / 86400;
  civil_from_days(days, &y, &m, &d);
  const int64_t sec = epoch - days * 86400;
  std::snprintf(out, cap, "%04d%02u%02u%02d%02d", y, m, d, static_cast<int>(sec / 3600),
                static_cast<int>((sec % 3600) / 60));
}

/// {"unix_seconds":[…],"price":[…],"unit":"EUR / MWh"} → fn(epoch, eur_mwh).
/// Null prices are skipped. Returns the number of samples delivered.
template <typename Fn> size_t parse_energy_charts(const char *body, size_t len, Fn &&fn) {
  if (body == nullptr) return 0;
  const char *end = body + len;
  const char *ts = detail::find_value(body, end, "unix_seconds");
  const char *pr = detail::find_value(body, end, "price");
  if (ts == nullptr || pr == nullptr || *ts != '[' || *pr != '[') return 0;
  ++ts;
  ++pr;
  size_t n = 0;
  while (ts < end && pr < end) {
    ts = detail::skip_ws(ts, end);
    pr = detail::skip_ws(pr, end);
    if (ts >= end || pr >= end || *ts == ']' || *pr == ']') break;
    char *stop = nullptr;
    const long long t = std::strtoll(ts, &stop, 10);
    if (stop == ts) return n;
    ts = stop;
    double v = NAN;
    if (*pr == 'n') {
      pr += 4;  // null
    } else {
      v = std::strtod(pr, &stop);
      if (stop == pr) return n;
      pr = stop;
    }
    if (std::isfinite(v)) {
      fn(static_cast<int64_t>(t), v);
      n++;
    }
    ts = detail::skip_ws(ts, end);
    pr = detail::skip_ws(pr, end);
    if (ts < end && *ts == ',') ++ts;
    if (pr < end && *pr == ',') ++pr;
  }
  return n;
}

namespace detail {
/// Text between <tag> and </tag> inside [p, end); returns the position after
/// the closing tag, or nullptr.
inline const char *xml_text(const char *p, const char *end, const char *tag, const char **text,
                            size_t *text_len) {
  char open[32], close[34];
  const int ol = std::snprintf(open, sizeof(open), "<%s>", tag);
  const int cl = std::snprintf(close, sizeof(close), "</%s>", tag);
  for (const char *q = p; q + ol <= end; ++q) {
    if (std::memcmp(q, open, static_cast<size_t>(ol)) != 0) continue;
    const char *t = q + ol;
    for (const char *r = t; r + cl <= end; ++r) {
      if (std::memcmp(r, close, static_cast<size_t>(cl)) == 0) {
        *text = t;
        *text_len = static_cast<size_t>(r - t);
        return r + cl;
      }
    }
    return nullptr;
  }
  return nullptr;
}

inline int resolution_minutes(const char *t, size_t n) {
  char buf[12];
  if (n == 0 || n >= sizeof(buf)) return 0;
  std::memcpy(buf, t, n);
  buf[n] = '\0';
  int v = 0;
  if (std::sscanf(buf, "PT%dM", &v) == 1 && v > 0) return v;
  if (std::sscanf(buf, "PT%dH", &v) == 1 && v > 0) return v * 60;
  return 0;
}

inline double text_number(const char *t, size_t n) {
  char buf[32];
  if (n == 0 || n >= sizeof(buf)) return NAN;
  std::memcpy(buf, t, n);
  buf[n] = '\0';
  char *stop = nullptr;
  const double v = std::strtod(buf, &stop);
  return stop == buf ? NAN : v;
}
}  // namespace detail

/// ENTSO-E A44 Publication_MarketDocument → fn(epoch, eur_mwh) for each slot.
/// Periods at the finest resolution present win (a market may publish PT60M and
/// PT15M side by side). Positions left out repeat the previous price (curve
/// type A03), up to the end of the period's timeInterval. An
/// Acknowledgement_MarketDocument (no data / bad token) yields 0.
template <typename Fn> size_t parse_entsoe_xml(const char *body, size_t len, Fn &&fn) {
  if (body == nullptr || len == 0) return 0;
  const char *end = body + len;
  int finest = 0;
  for (const char *p = body;;) {
    const char *t = nullptr;
    size_t n = 0;
    const char *next = detail::xml_text(p, end, "Period", &t, &n);
    if (next == nullptr) break;
    const char *rt = nullptr;
    size_t rn = 0;
    if (detail::xml_text(t, t + n, "resolution", &rt, &rn) != nullptr) {
      const int r = detail::resolution_minutes(rt, rn);
      if (r > 0 && (finest == 0 || r < finest)) finest = r;
    }
    p = next;
  }
  if (finest == 0) return 0;
  size_t count = 0;
  for (const char *p = body;;) {
    const char *t = nullptr;
    size_t n = 0;
    const char *next = detail::xml_text(p, end, "Period", &t, &n);
    if (next == nullptr) break;
    p = next;
    const char *pend = t + n;
    const char *x = nullptr;
    size_t xn = 0;
    if (detail::xml_text(t, pend, "resolution", &x, &xn) == nullptr || detail::resolution_minutes(x, xn) != finest)
      continue;
    const char *iv = nullptr;
    size_t ivn = 0;
    if (detail::xml_text(t, pend, "timeInterval", &iv, &ivn) == nullptr) continue;
    char tbuf[24];
    int64_t start = 0, stop = 0;
    if (detail::xml_text(iv, iv + ivn, "start", &x, &xn) == nullptr || xn >= sizeof(tbuf)) continue;
    std::memcpy(tbuf, x, xn);
    tbuf[xn] = '\0';
    if (!parse_iso_utc(tbuf, &start)) continue;
    if (detail::xml_text(iv, iv + ivn, "end", &x, &xn) == nullptr || xn >= sizeof(tbuf)) continue;
    std::memcpy(tbuf, x, xn);
    tbuf[xn] = '\0';
    if (!parse_iso_utc(tbuf, &stop) || stop <= start) continue;
    const int64_t step = static_cast<int64_t>(finest) * 60;
    const long slots = static_cast<long>((stop - start) / step);
    long last_pos = 0;
    double last_price = NAN;
    for (const char *q = t;;) {
      const char *pt = nullptr;
      size_t pn = 0;
      const char *qn = detail::xml_text(q, pend, "Point", &pt, &pn);
      if (qn == nullptr) break;
      q = qn;
      const char *a = nullptr;
      size_t an = 0;
      if (detail::xml_text(pt, pt + pn, "position", &a, &an) == nullptr) continue;
      const double posd = detail::text_number(a, an);
      if (detail::xml_text(pt, pt + pn, "price.amount", &a, &an) == nullptr) continue;
      const double price = detail::text_number(a, an);
      if (!std::isfinite(posd) || !std::isfinite(price)) continue;
      const long pos = static_cast<long>(posd);
      if (pos < 1 || pos > slots || pos <= last_pos) continue;
      for (long g = last_pos + 1; g < pos && std::isfinite(last_price); g++, count++)
        fn(start + (g - 1) * step, last_price);
      fn(start + (pos - 1) * step, price);
      count++;
      last_pos = pos;
      last_price = price;
    }
    for (long g = last_pos + 1; g <= slots && std::isfinite(last_price); g++, count++)
      fn(start + (g - 1) * step, last_price);
  }
  return count;
}

/// Stores EUR/MWh samples as local-hour SpotSamples; `to_local(epoch, &ymd,
/// &hour)` is localtime on the device and a fixed offset in tests.
template <typename ToLocal> struct EurSampleSink {
  SpotSample *out;
  size_t cap;
  size_t n;
  float fx;  ///< calculation currency per EUR
  ToLocal to_local;
  void operator()(int64_t epoch, double eur_mwh) {
    if (n >= cap) return;
    int ymd = 0, hour = 0;
    if (!to_local(epoch, &ymd, &hour)) return;
    out[n].ymd = ymd;
    out[n].hour = static_cast<uint8_t>(hour);
    out[n].eur_mwh = static_cast<float>(eur_mwh);
    out[n].mwh = static_cast<float>(eur_mwh * fx);
    n++;
  }
};
template <typename ToLocal>
inline EurSampleSink<ToLocal> make_sink(SpotSample *out, size_t cap, float fx, ToLocal to_local) {
  return EurSampleSink<ToLocal>{out, cap, 0, fx, to_local};
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

inline float all_in(float spot_kwh, float grid, float system, const Config &cfg) {
  const double base = static_cast<double>(spot_kwh) + grid + system + cfg.energy_tax + cfg.markup;
  return static_cast<float>(base * (1.0 + cfg.vat_pct / 100.0));
}

inline float round4(double v) { return static_cast<float>(std::round(v * 10000.0) / 10000.0); }

inline float to_eur(float local, float fx) {
  if (!(fx > 0.0f) || !std::isfinite(local)) return NAN;
  return round4(static_cast<double>(local) / fx);
}

struct DayPrices {
  bool valid = false;
  int ymd = 0;
  float spot[HOURS]{};    ///< calculation currency/kWh excl. VAT
  float grid[HOURS]{};    ///< excl. VAT
  float system[HOURS]{};  ///< excl. VAT
  float total[HOURS]{};   ///< incl. taxes + VAT
};

inline DayPrices build_day(int ymd, const float spot[HOURS], const float grid[HOURS],
                           const float system[HOURS], const Config &cfg) {
  DayPrices d{};
  d.ymd = ymd;
  for (size_t h = 0; h < HOURS; h++) {
    if (!std::isfinite(spot[h]) || !std::isfinite(grid[h]) || !std::isfinite(system[h])) return DayPrices{};
    d.spot[h] = spot[h];
    d.grid[h] = grid[h];
    d.system[h] = system[h];
    d.total[h] = all_in(spot[h], grid[h], system[h], cfg);
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
