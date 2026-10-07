// Host test for hp_history.h: stream parsing of Asgard's /dashboard/history
// rows into averaged flow/return buckets.
#include "hp_history.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace esphome::lune_touch_coordinator::hp_history;

static int failures = 0;
#define CHECK(name, cond)                                   \
  do {                                                      \
    if (cond) std::printf("PASS  %s\n", name);              \
    else { std::printf("FAIL  %s\n", name); failures++; }   \
  } while (0)

int main() {
  // Two rows in bucket 0, one in bucket 1, one before the range, one after.
  const char *body =
      "[[999,100,90,1],"
      "[1000,345,270,216,-32768,236],"
      "[1059,355,-32768,216],"
      "[1060,300,245,216],"
      "[1200,400,300]]";
  Bucketer b;
  b.begin(1000, 60, 2);
  // Feed in awkward slices to exercise chunk boundaries inside numbers.
  const size_t len = std::strlen(body);
  for (size_t i = 0; i < len; i += 3) b.feed(body + i, len - i < 3 ? len - i : 3);
  Series s{};
  const uint8_t filled = b.finish(&s);
  CHECK("rows counted", b.rows() == 5);
  CHECK("two buckets filled", filled == 2 && s.valid);
  CHECK("bucket 0 feed averages 34.5 and 35.5", s.feed_x10[0] == 350);
  CHECK("bucket 0 return skips -32768", s.return_x10[0] == 270);
  CHECK("bucket 1 feed", s.feed_x10[1] == 300 && s.return_x10[1] == 245);
  CHECK("series metadata", s.from_ts == 1000 && s.step_s == 60 && s.n == 2);

  Bucketer e;
  e.begin(5000, 900, 96);
  e.feed("[]", 2);
  Series se{};
  CHECK("empty body is not valid", e.finish(&se) == 0 && !se.valid && se.feed_x10[0] == NONE);

  Bucketer g;
  g.begin(0, 10, 3);
  const char *garbage = "[[5,-2000,9999],[15,-35,12]]";   // implausible values are dropped
  g.feed(garbage, std::strlen(garbage));
  Series sg{};
  g.finish(&sg);
  CHECK("implausible feed/return dropped", sg.feed_x10[0] == NONE && sg.return_x10[0] == NONE);
  CHECK("negative feed kept", sg.feed_x10[1] == -35 && sg.return_x10[1] == 12);

  Bucketer r;
  r.begin(1791324000, 900, 2);   // real Asgard timestamps have 10 digits
  const char *real = "[[1791324060,290,245,216],[1791324987,300,250]]";
  r.feed(real, std::strlen(real));
  Series sr{};
  r.finish(&sr);
  CHECK("10-digit unix timestamps", sr.feed_x10[0] == 290 && sr.feed_x10[1] == 300 && sr.return_x10[1] == 250);

  // Mode in flags (column 10, bit 6–9): 1 = hot water → bucket 0; 6 = legionella → bucket 1; 2 = heating → none.
  Bucketer m;
  m.begin(0, 60, 3);
  const char *modes = "[[10,300,250,0,0,0,0,0,0,0,64],[70,300,250,0,0,0,0,0,0,0,384],[130,300,250,0,0,0,0,0,0,0,128]]";
  m.feed(modes, std::strlen(modes));
  Series sm{};
  m.finish(&sm);
  CHECK("hot water marked", sm.marks[0] == MARK_DHW);
  CHECK("legionella marked", sm.marks[1] == MARK_LEGIONELLA);
  CHECK("heating not marked", sm.marks[2] == 0);

  CHECK("clock offset: UTC rows give 0", clock_offset_s(1000000, 1000000 + 30) == 0);
  CHECK("clock offset: rows 2 h ahead give 7200", clock_offset_s(1000000 + 7170, 1000000) == 7200);
  CHECK("clock offset: no rows give 0", clock_offset_s(0, 1000000) == 0);
  Bucketer t;
  t.begin(0, 60, 1);
  const char *ts = "[[1791400000,300,250],[1791407190,300,250]]";
  t.feed(ts, std::strlen(ts));
  CHECK("max_ts tracks newest row", t.max_ts() == 1791407190);

  if (failures) std::printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
