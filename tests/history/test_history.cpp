// Host test for room_history.h (hourly room buckets, 7 days) and
// house_history.h (Odin's hourly ledger, stream-parsed).
#include "house_history.h"
#include "room_history.h"

#include <cstdio>
#include <cstring>
#include <memory>

namespace rh = esphome::lune_touch_coordinator::room_history;
namespace hh = esphome::lune_touch_coordinator::house_history;

static int failures = 0;
#define CHECK(name, cond)                                   \
  do {                                                      \
    if (cond) std::printf("PASS  %s\n", name);              \
    else { std::printf("FAIL  %s\n", name); failures++; }   \
  } while (0)

static void test_room_history() {
  auto store = std::make_unique<rh::Store>();
  std::memset(store.get(), 0xAB, sizeof(rh::Store));   // garbage → attach clears it
  rh::Recorder r;
  r.attach(store.get());
  CHECK("attach clears a store without magic", store->magic == rh::MAGIC && store->newest_hour == 0);

  const uint32_t h0 = 500000;   // hour id
  const uint32_t t0 = h0 * 3600;
  // Hour h0: two samples for Laura, one with no temperature.
  r.sample("laura", 21.4f, true, 22.0f, true, 40.0f, true, t0 + 60);
  r.sample("laura", 21.6f, true, 22.0f, true, 60.0f, true, t0 + 120);
  r.sample("laura", 0.0f, false, 22.0f, true, 0.0f, false, t0 + 180);
  r.sample("kontor", 22.0f, true, 22.0f, true, 0.0f, true, t0 + 200);
  int16_t t[3], sp[3];
  uint8_t v[3];
  r.series("laura", t0 + 300, 3, t, sp, v);
  CHECK("hour in progress is the running mean", t[2] == 215 && sp[2] == 220 && v[2] == 50);
  CHECK("hours before the first sample are empty", t[0] == rh::NONE && v[1] == rh::NO_VALVE);

  // Next hour closes h0.
  const bool closed = r.sample("laura", 22.0f, true, 22.5f, true, 100.0f, true, t0 + 3600 + 10);
  CHECK("crossing an hour closes it", closed && store->newest_hour == h0);
  r.series("laura", t0 + 3600 + 20, 3, t, sp, v);
  CHECK("closed hour is stored", t[1] == 215 && sp[1] == 220 && v[1] == 50);
  CHECK("new hour runs", t[2] == 220 && sp[2] == 225 && v[2] == 100);

  // Touch off for 5 hours: the gap stays empty, the old hour survives.
  r.sample("laura", 22.2f, true, 22.5f, true, 0.0f, true, t0 + 7 * 3600 + 10);
  int16_t t8[8], sp8[8];
  uint8_t v8[8];
  r.series("laura", t0 + 7 * 3600 + 20, 8, t8, sp8, v8);
  CHECK("h0 still there after a gap", t8[0] == 215);
  CHECK("h0+1 closed when the clock jumped", t8[1] == 220);
  CHECK("missed hours are empty", t8[2] == rh::NONE && t8[6] == rh::NONE && v8[4] == rh::NO_VALVE);
  CHECK("current hour after the gap", t8[7] == 222);

  // A store copied to "flash" and back keeps the week.
  auto copy = std::make_unique<rh::Store>();
  std::memcpy(copy.get(), store.get(), sizeof(rh::Store));
  rh::Recorder r2;
  r2.attach(copy.get());
  r2.series("laura", t0 + 7 * 3600 + 20, 8, t8, sp8, v8);
  CHECK("reloaded store keeps closed hours", t8[0] == 215 && t8[1] == 220);
  CHECK("reloaded store has no running hour yet", t8[7] == rh::NONE);

  // Older than 7 days drops out.
  r.sample("laura", 21.0f, true, 22.0f, true, 0.0f, true, t0 + (rh::HOURS + 3) * 3600);
  int16_t tw[rh::HOURS], spw[rh::HOURS];
  uint8_t vw[rh::HOURS];
  r.series("laura", t0 + (rh::HOURS + 3) * 3600, rh::HOURS, tw, spw, vw);
  bool h0_gone = true;
  for (size_t k = 0; k < rh::HOURS; k++)
    if (tw[k] == 215) h0_gone = false;
  CHECK("hours older than a week are gone", h0_gone);

  // Unknown room.
  CHECK("unknown room has no series", !r.series("nope", t0, 3, t, sp, v) && t[0] == rh::NONE);

  // Full house: a 25th room reuses a slot with no data.
  rh::Recorder r3;
  auto s3 = std::make_unique<rh::Store>();
  s3->magic = 0;
  r3.attach(s3.get());
  char id[16];
  for (int i = 0; i < 24; i++) {
    std::snprintf(id, sizeof(id), "room-%02d", i);
    r3.sample(id, i == 5 ? 0.0f : 21.0f, i != 5, 0.0f, false, 0.0f, false, t0 + 60);
  }
  r3.sample("room-new", 20.0f, true, 0.0f, false, 0.0f, false, t0 + 90);
  r3.series("room-new", t0 + 100, 1, t, sp, v);
  CHECK("25th room takes the empty slot", t[0] == 200 && std::strcmp(s3->room_id[5], "room-new") == 0);
}

static void test_house_history() {
  // Two hours in range (one with -32768s), one before, one after.
  const char *body =
      "[[1000,90,0,0],"
      "[3600,107,90,468,14,424,9,0,90,320,2250,2252,0,0,10,990,970,0,2,2230],"
      "[7200,110,140,525,13,424,19,0,85,0,2245,2230,69,179,0,2296,1120,0,1,2260],"
      "[10800,-32768,0,0,13,424,0,0,0,0,-32768,-32768,0,0,0,0,0,0,0,0],"
      "[90000,1,1,1]]";
  hh::Parser p;
  p.begin(3600, 3);
  const size_t len = std::strlen(body);
  for (size_t i = 0; i < len; i += 5) p.feed(body + i, len - i < 5 ? len - i : 5);
  auto s = std::make_unique<hh::Series>();
  const uint8_t filled = p.finish(s.get());
  CHECK("house rows counted", p.rows() == 5);
  CHECK("two hours with values", filled == 2 && s->valid);
  CHECK("hour 0 values", s->out_x10[0] == 107 && s->room_x100[0] == 2252 && s->exp_room_x100[0] == 2250 &&
                             s->prod_x100[0] == 468 && s->plan_x100[0] == 320 && s->mode[0] == 2);
  CHECK("hour 1 hot water", s->dhw_x100[1] == 179 && s->mode[1] == 1 && s->plan_x100[1] == 0);
  CHECK("missing values stay NONE", s->out_x10[2] == hh::NONE && s->room_x100[2] == hh::NONE);
  CHECK("prod zero is a value", s->prod_x100[2] == 0);
}

int main() {
  test_room_history();
  test_house_history();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all history tests passed\n");
  return 0;
}
