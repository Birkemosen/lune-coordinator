// Host tests for problems.h (hold, once per hour, clear only after a sent raise,
// hysteresis, removed problems) and room_reached.h (24 h reached-target rule).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "problems.h"
#include "room_reached.h"

using namespace lune_touch_problems;
namespace rr = lune_touch_room_reached;
static constexpr uint32_t MIN = 60UL * 1000UL;
static constexpr uint32_t HOUR = 60UL * MIN;

// One cycle with a single problem; returns its edge.
static Edge cycle(Tracker &t, const char *id, bool raise, bool clear, uint32_t now, uint32_t hold = DEFAULT_HOLD_MS) {
  t.begin_cycle();
  const Edge e = t.update(id, "connectivity", Severity::Error, raise, clear, now, hold);
  Edge extra[4];
  const size_t n = t.end_cycle(now, extra, 4);
  assert(n == 0);
  return e;
}

static void test_tracker() {
  // Hold: raised only after 10 min of the condition; a blip does not raise.
  {
    Tracker t;
    uint32_t now = 1000;
    for (int i = 0; i < 9; i++, now += MIN)
      assert(cycle(t, "connectivity.asgard", true, false, now).kind == EdgeKind::None);
    assert(cycle(t, "connectivity.asgard", false, true, now).kind == EdgeKind::None);   // blip ends
    now += MIN;
    for (int i = 0; i < 10; i++, now += MIN)
      assert(cycle(t, "connectivity.asgard", true, false, now).kind == EdgeKind::None);
    const Edge e = cycle(t, "connectivity.asgard", true, false, now);
    assert(e.kind == EdgeKind::Raise && e.slot >= 0);
    assert(t.active_count() == 1 && t.is_active("connectivity.asgard"));
    // Stays active, no second raise.
    assert(cycle(t, "connectivity.asgard", true, false, now + MIN).kind == EdgeKind::None);
  }
  // Clear is notified only for a sent raise.
  {
    Tracker t;
    Edge e = cycle(t, "a", true, false, 0, 0);
    assert(e.kind == EdgeKind::Raise);
    e = cycle(t, "a", false, true, MIN, 0);
    assert(e.kind == EdgeKind::Clear && !e.notify);   // never sent → clear stays local
    Tracker t2;
    e = cycle(t2, "a", true, false, 0, 0);
    t2.mark_sent(e.slot);
    e = cycle(t2, "a", false, true, MIN, 0);
    assert(e.kind == EdgeKind::Clear && e.notify);
    assert(t2.active_count() == 0);
  }
  // At most one raise per id per hour: a re-raise waits for the hour, then comes.
  {
    Tracker t;
    uint32_t now = 5 * MIN;
    assert(cycle(t, "a", true, false, now, 0).kind == EdgeKind::Raise);
    now += 5 * MIN;
    assert(cycle(t, "a", false, true, now, 0).kind == EdgeKind::Clear);
    now += 5 * MIN;
    for (; now < 5 * MIN + HOUR; now += MIN)
      assert(cycle(t, "a", true, false, now, 0).kind == EdgeKind::None);
    assert(!t.is_active("a"));
    assert(cycle(t, "a", true, false, now, 0).kind == EdgeKind::Raise);
  }
  // Hysteresis: while active only clear_cond clears; while inactive only raise_cond raises.
  {
    Tracker t;
    assert(cycle(t, "h", true, false, 0, 0).kind == EdgeKind::Raise);
    assert(cycle(t, "h", false, false, MIN, 0).kind == EdgeKind::None);   // in the band
    assert(t.is_active("h"));
    assert(cycle(t, "h", false, true, 2 * MIN, 0).kind == EdgeKind::Clear);
  }
  // A problem that is no longer fed (room removed) is cleared in end_cycle.
  {
    Tracker t;
    t.begin_cycle();
    Edge e = t.update("room.not_reached:stue", "room_comfort", Severity::Warning, true, false, 0, 0);
    assert(e.kind == EdgeKind::Raise);
    t.mark_sent(e.slot);
    Edge out[4];
    assert(t.end_cycle(0, out, 4) == 0);
    t.begin_cycle();
    const size_t n = t.end_cycle(MIN, out, 4);
    assert(n == 1 && out[0].kind == EdgeKind::Clear && out[0].notify);
    assert(t.active_count() == 0);
    // Removed long after its raise: the cleared slot is still readable in this cycle.
    t.begin_cycle();
    e = t.update("connectivity.v6:a", "connectivity", Severity::Error, true, false, 2 * MIN, 0);
    assert(e.kind == EdgeKind::Raise);
    t.end_cycle(2 * MIN, out, 4);
    t.begin_cycle();
    assert(t.end_cycle(3 * HOUR, out, 4) == 1);
    assert(std::strcmp(t.slot(out[0].slot).id, "connectivity.v6:a") == 0);
  }
  // Titles: "; "-separated and cut to fit without splitting UTF-8.
  {
    Tracker t;
    t.begin_cycle();
    Edge a = t.update("x", "room_comfort", Severity::Warning, true, false, 0, 0);
    t.set_text(a.slot, "Stue når ikke sin temperatur", "m", "Stue");
    Edge b = t.update("y", "connectivity", Severity::Error, true, false, 0, 0);
    t.set_text(b.slot, "Asgard svarer ikke", "m", "");
    Edge out[2];
    t.end_cycle(0, out, 2);
    char buf[256];
    t.titles(buf, sizeof(buf));
    assert(std::strcmp(buf, "Stue når ikke sin temperatur; Asgard svarer ikke") == 0);
    char small[8];   // "Stue n" + 'å' (2 bytes) would need 9 bytes
    t.titles(small, sizeof(small));
    assert(std::strcmp(small, "Stue n") == 0);
  }
  // Slots are reused: a full tracker frees inactive ids after the hour.
  {
    Tracker t;
    char id[16];
    t.begin_cycle();
    for (size_t i = 0; i < MAX_PROBLEMS + 4; i++) {
      std::snprintf(id, sizeof(id), "p%zu", i);
      t.update(id, "sensor", Severity::Warning, true, false, 0, 0);
    }
    Edge out[MAX_PROBLEMS];
    t.end_cycle(0, out, MAX_PROBLEMS);
    assert(t.active_count() == MAX_PROBLEMS);
    t.begin_cycle();
    t.end_cycle(MIN, out, MAX_PROBLEMS);   // nothing fed → all cleared
    assert(t.active_count() == 0);
    t.begin_cycle();
    t.end_cycle(HOUR + MIN, out, MAX_PROBLEMS);   // hour passed → slots free
    t.begin_cycle();
    assert(t.update("new", "sensor", Severity::Warning, true, false, HOUR + 2 * MIN, 0).kind == EdgeKind::Raise);
  }
}

static void test_text() {
  char out[96];
  const char *keys[] = {"room", "target", "max"};
  const char *vals[] = {"Stue", "21,5", "20,8"};
  fill(out, sizeof(out), "{room} har ikke nået {target} °C i 24 timer (højst {max} °C).", keys, vals, 3);
  assert(std::strcmp(out, "Stue har ikke nået 21,5 °C i 24 timer (højst 20,8 °C).") == 0);
  fill(out, sizeof(out), "{unknown} {room}", keys, vals, 3);
  assert(std::strcmp(out, "{unknown} Stue") == 0);
  char tiny[5];   // "Stue " + "når": never ends inside 'å'
  fill(tiny, sizeof(tiny), "nåå", keys, vals, 3);
  assert(std::strcmp(tiny, "nå") == 0);
  char num[16];
  format_number(num, sizeof(num), 21.5f, 1, ',');
  assert(std::strcmp(num, "21,5") == 0);
  format_number(num, sizeof(num), 9.14f, 1, '.');
  assert(std::strcmp(num, "9.1") == 0);
}

static rr::Input room(float temp, float target, bool heating = true) {
  rr::Input in;
  in.heating_active = heating;
  in.has_target = true;
  in.target_c = target;
  in.comfort_c = target;
  in.has_temp = std::isfinite(temp);
  in.temp_c = temp;
  return in;
}

static void test_room_reached() {
  // Never within 0.3 °C of 21.5 for 24 h → not reached; max is reported.
  {
    rr::State s;
    rr::Result r;
    uint32_t t = 1000;
    for (; t < 24 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(t < 12 * HOUR ? 20.8f : 20.5f, 21.5f), t);
    assert(!r.not_reached);   // less than 24 h observed
    r = rr::step(s, room(20.5f, 21.5f), 24 * HOUR + 1000);
    assert(r.not_reached && std::fabs(r.max_c - 20.8f) < 1e-4f);
    // Clears as soon as the room reaches target − 0.3.
    r = rr::step(s, room(21.2f, 21.5f), 24 * HOUR + 2000);
    assert(!r.not_reached);
  }
  // Reached once in the last 24 h → fine; 24 h after that it counts again.
  {
    rr::State s;
    rr::Result r;
    uint32_t t = 0;
    for (; t <= 30 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(t == 10 * HOUR ? 21.3f : 20.0f, 21.5f), t);
    assert(!r.not_reached);   // reached at 10 h, window until 34 h
    for (; t <= 35 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(20.0f, 21.5f), t);
    assert(r.not_reached);
  }
  // A night setback reached counts: target 18 at night, room at 18.
  {
    rr::State s;
    rr::Result r;
    for (uint32_t t = 0; t <= 25 * HOUR; t += 10 * MIN) {
      rr::Input in = room((t % (24 * HOUR)) < 6 * HOUR ? 18.0f : 20.0f, (t % (24 * HOUR)) < 6 * HOUR ? 18.0f : 21.5f);
      in.comfort_c = 21.5f;
      r = rr::step(s, in, t);
    }
    assert(!r.not_reached);
  }
  // Heating off resets the observation; a target change restarts it.
  {
    rr::State s;
    rr::Result r;
    uint32_t t = 0;
    for (; t <= 23 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(20.0f, 21.5f), t);
    r = rr::step(s, room(20.0f, 21.5f, false), t);
    assert(!r.observing);
    for (t += 10 * MIN; t <= 40 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(20.0f, 21.5f), t);
    assert(!r.not_reached);   // only 17 h since heating came back
    for (; t <= 48 * HOUR; t += 10 * MIN)
      r = rr::step(s, room(20.0f, 21.5f), t);
    assert(r.not_reached);
    r = rr::step(s, room(20.0f, 22.0f), t);   // new target: start over
    assert(!r.not_reached && r.observing);
  }
  // Missing data never raises: readings in fewer than 20 of the 24 hours.
  {
    rr::State s;
    rr::Result r;
    for (uint32_t t = 0; t <= 30 * HOUR; t += 10 * MIN) {
      const bool have = (t / HOUR) % 2 == 0;   // every other hour
      r = rr::step(s, room(have ? 20.0f : NAN, 21.5f), t);
    }
    assert(!r.not_reached && r.hours_with_data < rr::MIN_HOURS_WITH_DATA);
  }
}

int main() {
  test_tracker();
  test_text();
  test_room_reached();
  std::printf("problems: all tests passed\n");
  return 0;
}
