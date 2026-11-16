#pragma once

// Planner history: one fixed 64-byte record per hour, the layout Odin uses for its
// hourly export (u32 epoch + 30 × int16, INT16_MIN = no value), with three changes:
//   - the epoch is the START of the hour (Odin stamps the end);
//   - a 64-byte header names the file kind and which quantity each column holds, so
//     a reader never guesses a column and new columns do not break old readers;
//   - every column has a fixed scale, listed below.
// House records hold the whole house per hour; room records one room per hour.
// The same records are the replay input on the host and the planner's flash log on
// Touch (docs/planner_history_format.md). Pure C++, host-testable.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lune_planner {

static constexpr int16_t NONE = INT16_MIN;
static constexpr size_t RECORD_SIZE = 64;
static constexpr size_t HEADER_SIZE = 64;
static constexpr uint8_t MAX_COLS = 30;
static constexpr uint16_t FORMAT_VERSION = 1;
static constexpr char MAGIC[4] = {'L', 'U', 'N', 'H'};

enum class Kind : uint8_t { HOUSE = 1, ROOM = 2 };

// House columns. The id is what the header stores, so ids are never reused.
enum HouseCol : uint8_t {
  H_HEAT_SH_KWH = 0,      // space-heating heat, kWh × 100
  H_HEAT_DHW_KWH,         // hot-water heat, kWh × 100
  H_ELEC_KWH,             // electricity, all modes, kWh × 100
  H_ROOM_C,               // house temperature as fed to the planner, °C × 100
  H_OUTDOOR_C,            // °C × 100
  H_WIND_MS,              // m/s × 10
  H_WIND_DIR_DEG,         // direction the wind comes from, degrees
  H_SOLAR_WM2,            // global irradiance, W/m²
  H_PRICE_EUR_KWH,        // EUR/kWh incl. grid tariffs × 10000
  H_SCHED_SP_C,           // comfort schedule setpoint, °C × 100
  H_SCHED_MIN_C,          // comfort band low, °C × 100
  H_SCHED_MAX_C,          // comfort band high, °C × 100
  H_RUN_MIN,              // compressor minutes on space heating
  H_STARTS,               // compressor starts
  H_HP_FEED_C,            // primary feed (THW1) while running, °C × 10
  H_HP_RETURN_C,          // primary return (THW2) while running, °C × 10
  H_Z1_FEED_C,            // zone feed after the buffer (THW6 / probe) while running, °C × 10
  H_Z1_RETURN_C,          // zone return (THW7 / probe) while running, °C × 10
  H_FLOW_TARGET_C,        // commanded flow target, hour mean, °C × 10
  H_FLOW_TARGET_MAX_C,    // commanded flow target, hour max, °C × 10
  H_OUTPUT_MAX_KW,        // highest reported output in the hour, kW × 100
  H_DEFROST_MIN,          // minutes in defrost
  H_DHW_TANK_C,           // hot-water tank, hour end, °C × 10
  H_MODE_FLAGS,           // MODE_* bits: anything that happened in the hour
  H_ROOM_EXPECTED_C,      // the running planner's expected house temperature, °C × 100
  H_COLDEST_DEV_K,        // coldest room − its target, K × 100
  H_HUMIDITY_PCT,         // relative humidity, %
  H_COL_COUNT
};

enum RoomCol : uint8_t {
  R_ROOM_INDEX = 0,       // Touch room index (stable per install)
  R_TEMP_C,               // hour mean, °C × 100
  R_TARGET_C,             // °C × 100
  R_VALVE_PCT,            // mean valve opening, %
  R_FLOOR_LAG_H,          // V6 floor response time, h × 100
  R_COL_COUNT
};

static constexpr uint16_t MODE_SPACE_HEATING = 1u << 0;
static constexpr uint16_t MODE_HOT_WATER = 1u << 1;
static constexpr uint16_t MODE_LEGIONELLA = 1u << 2;
static constexpr uint16_t MODE_DEFROST = 1u << 3;
static constexpr uint16_t MODE_FORECAST_PRICE = 1u << 4;   // price came from a forecast, not the market
static constexpr uint16_t MODE_FORECAST_WEATHER = 1u << 5; // weather is a forecast, not an observation

// Scale per column: stored = round(value × scale).
inline float house_scale(uint8_t col) {
  switch (col) {
    case H_HEAT_SH_KWH: case H_HEAT_DHW_KWH: case H_ELEC_KWH: case H_ROOM_C: case H_OUTDOOR_C:
    case H_SCHED_SP_C: case H_SCHED_MIN_C: case H_SCHED_MAX_C: case H_OUTPUT_MAX_KW:
    case H_ROOM_EXPECTED_C: case H_COLDEST_DEV_K:
      return 100.0f;
    case H_WIND_MS: case H_HP_FEED_C: case H_HP_RETURN_C: case H_Z1_FEED_C: case H_Z1_RETURN_C:
    case H_FLOW_TARGET_C: case H_FLOW_TARGET_MAX_C: case H_DHW_TANK_C:
      return 10.0f;
    case H_PRICE_EUR_KWH:
      return 10000.0f;
    default:
      return 1.0f;
  }
}

inline float room_scale(uint8_t col) {
  switch (col) {
    case R_TEMP_C: case R_TARGET_C: case R_FLOOR_LAG_H: return 100.0f;
    default: return 1.0f;
  }
}

inline float scale_for(Kind kind, uint8_t col) {
  return kind == Kind::ROOM ? room_scale(col) : house_scale(col);
}

inline int16_t encode(float value, float scale) {
  if (!std::isfinite(value)) return NONE;
  const float v = std::round(value * scale);
  if (v <= static_cast<float>(INT16_MIN) || v > static_cast<float>(INT16_MAX)) return NONE;
  return static_cast<int16_t>(v);
}

inline float decode(int16_t raw, float scale) {
  return raw == NONE ? NAN : static_cast<float>(raw) / scale;
}

struct Record {
  uint32_t hour_start{0};   // unix seconds, a whole UTC hour
  int16_t v[MAX_COLS];

  Record() { clear(); }
  void clear() {
    hour_start = 0;
    for (auto &x : v) x = NONE;
  }
  void set(Kind kind, uint8_t col, float value) {
    if (col < MAX_COLS) v[col] = encode(value, scale_for(kind, col));
  }
  float get(Kind kind, uint8_t col) const {
    return col < MAX_COLS ? decode(v[col], scale_for(kind, col)) : NAN;
  }
  bool has(uint8_t col) const { return col < MAX_COLS && v[col] != NONE; }
};

struct Header {
  Kind kind{Kind::HOUSE};
  uint8_t ncols{0};
  uint8_t cols[MAX_COLS]{};   // column id stored in each record slot
};

inline Header default_header(Kind kind) {
  Header h;
  h.kind = kind;
  h.ncols = kind == Kind::ROOM ? R_COL_COUNT : H_COL_COUNT;
  for (uint8_t i = 0; i < h.ncols; i++) h.cols[i] = i;
  return h;
}

// Little-endian on every target Lune builds for (ESP32-S3, x86-64, arm64).
inline void put_u16_(uint8_t *p, uint16_t x) { p[0] = x & 0xff; p[1] = x >> 8; }
inline void put_u32_(uint8_t *p, uint32_t x) { for (int i = 0; i < 4; i++) p[i] = (x >> (8 * i)) & 0xff; }
inline uint16_t get_u16_(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get_u32_(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Header: magic(4) version(2) record_size(2) kind(1) ncols(1) reserved(2) cols(30) pad.
inline void write_header(const Header &h, uint8_t out[HEADER_SIZE]) {
  std::memset(out, 0, HEADER_SIZE);
  std::memcpy(out, MAGIC, 4);
  put_u16_(out + 4, FORMAT_VERSION);
  put_u16_(out + 6, RECORD_SIZE);
  out[8] = static_cast<uint8_t>(h.kind);
  out[9] = h.ncols;
  for (uint8_t i = 0; i < h.ncols && i < MAX_COLS; i++) out[12 + i] = h.cols[i];
}

inline bool read_header(const uint8_t in[HEADER_SIZE], Header *h) {
  if (std::memcmp(in, MAGIC, 4) != 0) return false;
  if (get_u16_(in + 4) != FORMAT_VERSION || get_u16_(in + 6) != RECORD_SIZE) return false;
  if (in[8] != static_cast<uint8_t>(Kind::HOUSE) && in[8] != static_cast<uint8_t>(Kind::ROOM)) return false;
  if (in[9] > MAX_COLS) return false;
  h->kind = static_cast<Kind>(in[8]);
  h->ncols = in[9];
  for (uint8_t i = 0; i < MAX_COLS; i++) h->cols[i] = i < h->ncols ? in[12 + i] : 0;
  return true;
}

// Records are stored in header column order; in memory they are indexed by column id.
inline void write_record(const Header &h, const Record &r, uint8_t out[RECORD_SIZE]) {
  put_u32_(out, r.hour_start);
  for (uint8_t i = 0; i < MAX_COLS; i++) {
    const int16_t x = i < h.ncols && h.cols[i] < MAX_COLS ? r.v[h.cols[i]] : NONE;
    put_u16_(out + 4 + 2 * i, static_cast<uint16_t>(x));
  }
}

inline void read_record(const Header &h, const uint8_t in[RECORD_SIZE], Record *r) {
  r->clear();
  r->hour_start = get_u32_(in);
  for (uint8_t i = 0; i < h.ncols && i < MAX_COLS; i++) {
    if (h.cols[i] < MAX_COLS) r->v[h.cols[i]] = static_cast<int16_t>(get_u16_(in + 4 + 2 * i));
  }
}

}  // namespace lune_planner
