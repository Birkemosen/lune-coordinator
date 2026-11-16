// Host tests for planner/history_record.h (64-byte hourly records).
#include "history_record.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_planner;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
  // --- encode / decode ---------------------------------------------------------
  assert(encode(NAN, 100.0f) == NONE);
  assert(encode(400.0f, 100.0f) == NONE);           // 40000 does not fit an int16
  assert(encode(-327.68f, 100.0f) == NONE);         // would collide with NONE
  assert(encode(22.345f, 100.0f) == 2235 || encode(22.345f, 100.0f) == 2234);
  assert(std::isnan(decode(NONE, 10.0f)));
  assert(near(decode(355, 10.0f), 35.5f, 1e-6f));

  // --- a house hour round-trips through header + record ---------------------------
  Record r;
  r.hour_start = 1794816000;   // a whole UTC hour
  r.set(Kind::HOUSE, H_HEAT_SH_KWH, 10.28f);
  r.set(Kind::HOUSE, H_ELEC_KWH, 2.61f);
  r.set(Kind::HOUSE, H_PRICE_EUR_KWH, 0.3135f);
  r.set(Kind::HOUSE, H_FLOW_TARGET_MAX_C, 35.0f);
  r.set(Kind::HOUSE, H_OUTDOOR_C, -4.25f);
  r.v[H_MODE_FLAGS] = MODE_SPACE_HEATING | MODE_DEFROST;

  const Header h = default_header(Kind::HOUSE);
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  write_header(h, hb);
  write_record(h, r, rb);

  Header h2;
  assert(read_header(hb, &h2));
  assert(h2.kind == Kind::HOUSE && h2.ncols == H_COL_COUNT);
  Record r2;
  read_record(h2, rb, &r2);
  assert(r2.hour_start == r.hour_start);
  assert(near(r2.get(Kind::HOUSE, H_HEAT_SH_KWH), 10.28f, 0.006f));
  assert(near(r2.get(Kind::HOUSE, H_PRICE_EUR_KWH), 0.3135f, 0.00006f));
  assert(near(r2.get(Kind::HOUSE, H_FLOW_TARGET_MAX_C), 35.0f, 0.06f));
  assert(near(r2.get(Kind::HOUSE, H_OUTDOOR_C), -4.25f, 0.006f));
  assert(r2.v[H_MODE_FLAGS] == (MODE_SPACE_HEATING | MODE_DEFROST));
  assert(!r2.has(H_WIND_MS) && std::isnan(r2.get(Kind::HOUSE, H_WIND_MS)));

  // --- a reader maps columns by the header, not by position ----------------------
  // A writer that only stores three columns, in its own order.
  Header few;
  few.kind = Kind::HOUSE;
  few.ncols = 3;
  few.cols[0] = H_PRICE_EUR_KWH;
  few.cols[1] = H_HEAT_SH_KWH;
  few.cols[2] = H_OUTDOOR_C;
  write_header(few, hb);
  write_record(few, r, rb);
  Header few2;
  assert(read_header(hb, &few2) && few2.ncols == 3 && few2.cols[0] == H_PRICE_EUR_KWH);
  Record r3;
  read_record(few2, rb, &r3);
  assert(near(r3.get(Kind::HOUSE, H_PRICE_EUR_KWH), 0.3135f, 0.00006f));
  assert(near(r3.get(Kind::HOUSE, H_HEAT_SH_KWH), 10.28f, 0.006f));
  assert(!r3.has(H_ELEC_KWH));   // not stored by that writer

  // --- room records use their own scales -------------------------------------------
  Record room;
  room.hour_start = r.hour_start;
  room.set(Kind::ROOM, R_ROOM_INDEX, 7);
  room.set(Kind::ROOM, R_TEMP_C, 19.87f);
  room.set(Kind::ROOM, R_FLOOR_LAG_H, 1.4f);
  assert(near(room.get(Kind::ROOM, R_TEMP_C), 19.87f, 0.006f));
  assert(near(room.get(Kind::ROOM, R_ROOM_INDEX), 7.0f, 0.0f));

  // --- bad headers are refused ------------------------------------------------------
  write_header(default_header(Kind::ROOM), hb);
  hb[0] = 'X';
  assert(!read_header(hb, &h2));
  write_header(default_header(Kind::ROOM), hb);
  hb[4] = 9;   // unknown version
  assert(!read_header(hb, &h2));

  std::printf("planner history_record: all assertions passed\n");
  return 0;
}
