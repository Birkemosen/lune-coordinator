// Host tests for odin_comfort.h (temporary lift of Odin 2.0's comfort schedule).
#include "odin_comfort.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace lune_touch_odin;

static bool near(float a, float b) { return std::fabs(a - b) < 0.011f; }

// Captured from a live Odin 2.0.0-23 (`/api/config` → sched_ui).
static const char *kUserSchedule =
    "[{\"h\":0,\"sp\":21,\"min\":-1,\"max\":1.5},{\"h\":6,\"sp\":22.5,\"min\":-0.5,\"max\":1.5},"
    "{\"h\":9,\"sp\":22,\"min\":-0.5,\"max\":1.5},{\"h\":22,\"sp\":21,\"min\":-0.5,\"max\":1.5}]";

int main() {
  // --- parse / format round trip -------------------------------------------
  Profile user{};
  assert(parse_profile(kUserSchedule, &user));
  assert(user.count == 4);
  assert(user.blocks[1].h == 6 && near(user.blocks[1].sp, 22.5f) && near(user.blocks[1].min, -0.5f));
  char buf[512];
  assert(format_profile(user, buf, sizeof(buf)) > 0);
  assert(std::strcmp(buf, kUserSchedule) == 0);

  // Unsorted input with spaces is accepted and sorted.
  Profile unsorted{};
  assert(parse_profile(" [ {\"h\": 9, \"sp\": 22, \"min\": -0.5, \"max\": 1.5}, "
                       "{\"h\":0,\"sp\":21,\"min\":-1,\"max\":1.5} ]",
                       &unsorted));
  assert(unsorted.count == 2 && unsorted.blocks[0].h == 0);
  // Garbage / duplicates / out of range are rejected.
  Profile bad{};
  assert(!parse_profile("", &bad));
  assert(!parse_profile("[]", &bad));
  assert(!parse_profile("[{\"h\":0,\"sp\":21}]", &bad));
  assert(!parse_profile("[{\"h\":3,\"sp\":21,\"min\":-1,\"max\":1},{\"h\":3,\"sp\":20,\"min\":-1,\"max\":1}]", &bad));
  assert(!parse_profile("[{\"h\":30,\"sp\":21,\"min\":-1,\"max\":1}]", &bad));

  // block_at wraps the last block past midnight when the first is not h=0.
  assert(near(block_at(unsorted, 23).sp, 22.0f));
  Profile late{};
  assert(parse_profile("[{\"h\":6,\"sp\":22,\"min\":-1,\"max\":1},{\"h\":22,\"sp\":20,\"min\":-1,\"max\":1}]", &late));
  assert(near(block_at(late, 3).sp, 20.0f));

  // --- apply_lift ------------------------------------------------------------
  // Charge 15:00–18:00 (+0.5 °C): only those hours shift; the rest is unchanged.
  Lift lift{15, 3, 0.5f};
  const Profile lifted = apply_lift(user, lift);
  assert(near(block_at(lifted, 14).sp, 22.0f));
  assert(near(block_at(lifted, 15).sp, 22.5f));
  assert(near(block_at(lifted, 17).sp, 22.5f));
  assert(near(block_at(lifted, 18).sp, 22.0f));
  assert(near(block_at(lifted, 15).min, -0.5f) && near(block_at(lifted, 15).max, 1.5f));
  assert(lifted.blocks[0].h == 0);
  assert(!equivalent(lifted, user));
  assert(format_profile(lifted, buf, sizeof(buf)) > 0);
  std::printf("lifted: %s\n", buf);

  // A lift across midnight (22:00 + 4 h) wraps into the night block.
  const Profile night = apply_lift(user, Lift{22, 4, 1.0f});
  assert(near(block_at(night, 23).sp, 22.0f));
  assert(near(block_at(night, 1).sp, 22.0f));
  assert(near(block_at(night, 2).sp, 21.0f));
  assert(near(block_at(night, 21).sp, 22.0f));

  // No lift → equivalent to the user's profile.
  assert(equivalent(apply_lift(user, Lift{}), user));

  // --- amount and window -----------------------------------------------------
  LiftParams p{};
  // 6 kWh into Odin's 11.66 kWh/K house ≈ +0.5 °C.
  assert(near(lift_for_energy(6.0f, 11.66f, p), 6.0f / 11.66f));
  assert(near(lift_for_energy(60.0f, 11.66f, p), p.max_lift_c));
  assert(lift_for_energy(6.0f, NAN, p) == 0.0f);
  const Lift w = make_lift(13, 2, 5, 0.52f, p);  // now 13:00, charge in 2 h until 5 h
  assert(w.start_hour == 15 && w.hours == 3 && near(w.lift_c, 0.5f));
  assert(!make_lift(13, 2, 5, 0.1f, p).active());   // too small to bother Odin
  assert(make_lift(13, -3, 2, 0.5f, p).start_hour == 13);  // already started → from now
  assert(make_lift(13, 0, 40, 0.5f, p).hours == 23);       // never the whole day
  // A lagging room: from the next hour, for the floor lag + 2 h (3–6 h).
  {
    const Lift l = lag_lift(10, 0.6f, 2, p);   // 10:40, concrete/parquet ≈ 2 h
    assert(l.active() && l.start_hour == 11 && l.hours == 4);
    assert(lag_lift(10, 0.6f, 1, p).hours == 3);    // light timber floor: 3 h minimum
    assert(lag_lift(10, 0.6f, 9, p).hours == 6);    // lag capped at 4 h → 6 h
    assert(lag_lift(10, 0.6f, 0, p).hours == 4);    // unknown lag: 2 h
    assert(lag_lift(23, 0.6f, 2, p).start_hour == 0);  // wraps midnight
    assert(!lag_lift(10, 0.2f, 2, p).active());      // below the minimum lift
  }
  assert(!lift_changed(w, Lift{15, 3, 0.6f}));
  assert(lift_changed(w, Lift{16, 3, 0.5f}));
  assert(lift_changed(w, Lift{}));

  // --- one or two lagging rooms open only the band's top -------------------
  {
    assert(near(floor_share_for(1.0f / 12.0f), FLOOR_SHARE_MIN));  // Laura alone
    assert(near(floor_share_for(0.25f), 0.5f));
    assert(near(floor_share_for(0.6f), 1.0f));
    assert(near(floor_share_for(NAN), 1.0f));
    // 2026-11-12: Laura 1.6 K behind lifted the whole band 1.5 °C for 6 h.
    const Lift l = lag_lift(19, 1.5f, 2, p, floor_share_for(1.0f / 12.0f));
    assert(l.start_hour == 20 && l.hours == LAG_CHOICE_H && near(l.floor_share, 0.2f));
    assert(lag_lift(19, 1.5f, 2, p).hours == 4);  // whole house lagging: as before
    const Profile one = apply_lift(user, Lift{15, 3, 1.5f, 0.2f});
    assert(near(block_at(one, 15).sp, 22.3f));                       // floor up 0.3
    assert(near(block_at(one, 15).sp + block_at(one, 15).max, 22.0f + 1.5f + 1.5f));  // top up 1.5
    assert(near(block_at(one, 14).sp, 22.0f) && near(block_at(one, 14).max, 1.5f));
    assert(lift_changed(Lift{15, 3, 1.5f, 0.2f}, Lift{15, 3, 1.5f, 1.0f}));
  }

  // --- a dear hour is skipped only when heating earlier is cheaper -----------
  {
    // Tariff peak 17–21 with now = 13: window 14–21 (8 h), up to 2 h skipped: the
    // two with the largest saving over 14–16, heat loss included.
    float price[24];
    for (int k = 0; k < 24; k++) {
      const int h = (13 + k) % 24;
      price[k] = (h >= 17 && h < 21) ? 2.6f : 1.8f;
    }
    price[(18 + 24 - 13) % 24] = 2.9f;  // 18:00 dearest
    price[(19 + 24 - 13) % 24] = 2.8f;
    Lift l{14, 8, 1.0f};
    skip_dearest(l, 13, price, 24);
    assert(l.skip_mask == ((1UL << 18) | (1UL << 19)));
    assert(!hour_in_lift(l, 18) && hour_in_lift(l, 17) && hour_in_lift(l, 14));
    const Profile lifted2 = apply_lift(user, l);
    assert(near(block_at(lifted2, 18).sp, 22.0f) && near(block_at(lifted2, 17).sp, 23.0f));
    // Flat prices: nothing skipped.
    float flat[24];
    for (float &f : flat) f = 1.8f;
    Lift f2{14, 8, 1.0f};
    skip_dearest(f2, 13, flat, 24);
    assert(f2.skip_mask == 0);
    // An unknown price in the window: nothing skipped.
    price[3] = NAN;
    Lift u{14, 8, 1.0f};
    skip_dearest(u, 13, price, 24);
    assert(u.skip_mask == 0);
    // Short window: a third of 2 h is 0 h.
    Lift s2{18, 2, 1.0f};
    price[3] = 1.8f;
    skip_dearest(s2, 13, price, 24);
    assert(s2.skip_mask == 0);
    // Across midnight: 00:00 with now = 22 → bit 0.
    float night[24];
    for (int k = 0; k < 24; k++) night[k] = 1.0f;
    night[2] = 3.0f;
    Lift m{23, 3, 1.0f};
    skip_dearest(m, 22, night, 24);
    assert(m.skip_mask == 1UL);
    // The window's first hour has nothing earlier to heat in: it stays lifted.
    float first[24];
    for (int k = 0; k < 24; k++) first[k] = 1.0f;
    first[1] = 3.0f;
    Lift fl{23, 6, 1.0f};
    skip_dearest(fl, 22, first, 24);
    assert(fl.skip_mask == 0);
    // Heat loss against price: 5 % dearer 3 h later is worth storing for in a house
    // with a 167 h time constant (3/167 ≈ 2 % loss), not in one with 10 h (30 %).
    float small[24];
    for (int k = 0; k < 24; k++) small[k] = 1.0f;
    small[4] = 1.05f;
    Lift slow{14, 6, 1.0f};
    skip_dearest(slow, 13, small, 24, 167.0f);
    assert(slow.skip_mask == (1UL << 17));
    Lift fast{14, 6, 1.0f};
    skip_dearest(fast, 13, small, 24, 10.0f);
    assert(fast.skip_mask == 0);
    // Time constant from Odin's thermal mass and heat loss; a default without them.
    assert(std::fabs(house_tau_h(21.76f, 0.13f) - 167.4f) < 0.1f);
    assert(house_tau_h(NAN, 0.13f) == TAU_DEFAULT_H && house_tau_h(21.76f, 0.0f) == TAU_DEFAULT_H);
  }

  // --- ownership state machine --------------------------------------------
  {
    OwnerState s{};
    // Idle, nothing wanted: first contact adopts the remote as the user baseline.
    StepResult r = step(s, user, Lift{});
    assert(r.action == Action::NONE && s.has_user && equivalent(s.user, user));

    // Lift wanted → write lifted profile.
    r = step(s, user, w);
    assert(r.action == Action::WRITE_LIFT);
    commit(s, r, w);
    assert(s.applied);
    Profile remote = r.profile;

    // Same lift again → nothing to do.
    r = step(s, remote, Lift{15, 3, 0.6f});
    assert(r.action == Action::NONE);

    // Lift over → restore the user's profile.
    r = step(s, remote, Lift{});
    assert(r.action == Action::RESTORE && equivalent(r.profile, user));
    commit(s, r, Lift{});
    assert(!s.applied);
  }
  {
    // The user edits the schedule in Odin's UI while lifted → yield, never overwrite.
    OwnerState s{};
    step(s, user, Lift{});
    StepResult r = step(s, user, w);
    commit(s, r, w);
    Profile edited{};
    assert(parse_profile("[{\"h\":0,\"sp\":20,\"min\":-1,\"max\":1.5}]", &edited));
    r = step(s, edited, w);
    assert(r.action == Action::YIELD && !s.applied && s.yielded);
    assert(equivalent(s.user, edited));
    // Still wanted, but yielded → hands off.
    r = step(s, edited, w);
    assert(r.action == Action::NONE);
    // Need passes → back to normal; a later need lifts the *new* baseline.
    r = step(s, edited, Lift{});
    assert(r.action == Action::NONE && !s.yielded);
    r = step(s, edited, w);
    assert(r.action == Action::WRITE_LIFT);
    assert(near(block_at(r.profile, 15).sp, 20.5f));
  }
  {
    // Edited while idle → adopt as the new baseline (no write).
    OwnerState s{};
    step(s, user, Lift{});
    Profile edited{};
    assert(parse_profile("[{\"h\":0,\"sp\":21.5,\"min\":-1,\"max\":1.5}]", &edited));
    const StepResult r = step(s, edited, Lift{});
    assert(r.action == Action::ADOPT_USER && equivalent(s.user, edited));
  }
  {
    // After a Touch reboot with a persisted baseline and lift: restore works.
    OwnerState s{};
    s.user = user;
    s.has_user = true;
    s.written = apply_lift(user, w);
    s.applied = true;
    s.lift = w;
    const StepResult r = step(s, s.written, Lift{});
    assert(r.action == Action::RESTORE && equivalent(r.profile, user));
  }
  {
    // Reboot that lost the baseline while our lift is on Odin → yield.
    OwnerState s{};
    s.written = apply_lift(user, w);
    s.applied = true;
    const StepResult r = step(s, s.written, Lift{});
    assert(r.action == Action::YIELD);
  }

  // ---- Setpoint sync: Odin's setpoint follows the house target, band ±0.5 ----
  {
    Profile user{};
    assert(parse_profile("[{\"h\":0,\"sp\":22,\"min\":-1,\"max\":1},{\"h\":6,\"sp\":22,\"min\":-0.5,\"max\":1.5}]", &user));
    OwnerState s{};
    Sync sync{};
    sync.on = true;
    sync.sp = 21.63f;
    sync.below_c = 0.5f;
    sync.above_c = 0.5f;
    StepResult r = step(s, user, Lift{}, sync);
    assert(r.action == Action::WRITE_LIFT);
    assert(r.profile.count == 1 && std::fabs(r.profile.blocks[0].sp - 21.6f) < 0.01f);
    assert(std::fabs(r.profile.blocks[0].min + 0.5f) < 0.01f && std::fabs(r.profile.blocks[0].max - 0.5f) < 0.01f);
    commit(s, r, Lift{}, sync);
    // Same target (within 0.1): nothing to write.
    Sync near_same = sync;
    near_same.sp = 21.66f;
    assert(step(s, r.profile, Lift{}, near_same).action == Action::NONE);
    // Target moves 0.2: rewrite.
    Sync moved = sync;
    moved.sp = 21.83f;
    StepResult r2 = step(s, r.profile, Lift{}, moved);
    assert(r2.action == Action::WRITE_LIFT && std::fabs(r2.profile.blocks[0].sp - 21.8f) < 0.01f);
    commit(s, r2, Lift{}, moved);
    // A lift goes on top of the synced band.
    Lift lift{};
    lift.start_hour = 3;
    lift.hours = 2;
    lift.lift_c = 1.0f;
    StepResult r3 = step(s, r2.profile, lift, moved);
    assert(r3.action == Action::WRITE_LIFT);
    assert(std::fabs(block_at(r3.profile, 3).sp - 22.8f) < 0.01f && std::fabs(block_at(r3.profile, 10).sp - 21.8f) < 0.01f);
    commit(s, r3, lift, moved);
    // Edited in Odin's UI while synced: adopted and synced again (no yield).
    Profile edited = r3.profile;
    edited.blocks[0].sp = 23.0f;
    StepResult r4 = step(s, edited, lift, moved);
    assert(r4.action == Action::WRITE_LIFT && !s.yielded);
    commit(s, r4, lift, moved);
    // Sync and lift off: the baseline (the last user edit) is restored.
    StepResult r5 = step(s, r4.profile, Lift{}, Sync{});
    assert(r5.action == Action::RESTORE);
  }

  // ---- Lopsided band: 0.5 below, 1.0 above ----
  {
    Profile user{};
    assert(parse_profile("[{\"h\":0,\"sp\":22,\"min\":-1,\"max\":1}]", &user));
    OwnerState s{};
    Sync sync{};
    sync.on = true;
    sync.sp = 21.5f;
    sync.below_c = 0.5f;
    sync.above_c = 1.0f;
    StepResult r = step(s, user, Lift{}, sync);
    assert(r.action == Action::WRITE_LIFT && r.profile.count == 1);
    assert(std::fabs(r.profile.blocks[0].min + 0.5f) < 0.01f && std::fabs(r.profile.blocks[0].max - 1.0f) < 0.01f);
    commit(s, r, Lift{}, sync);
    // Only the upper side changes: rewrite.
    Sync wider = sync;
    wider.above_c = 1.5f;
    StepResult r2 = step(s, r.profile, Lift{}, wider);
    assert(r2.action == Action::WRITE_LIFT && std::fabs(r2.profile.blocks[0].max - 1.5f) < 0.01f &&
           std::fabs(r2.profile.blocks[0].min + 0.5f) < 0.01f);
  }

  // ---- Weather preload: now through the hour after the peak, at least 3 h ----
  {
    LiftParams p{};
    Lift l = preload_lift(14, 1.5f, 3, p, 0.2f);
    assert(l.active() && l.start_hour == 14 && l.hours == 5 && std::fabs(l.lift_c - 1.5f) < 0.01f);
    assert(std::fabs(l.floor_share - 0.2f) < 0.01f);
    Lift soon = preload_lift(23, 0.8f, 0, p);
    assert(soon.active() && soon.start_hour == 23 && soon.hours == 3 && std::fabs(soon.floor_share - 1.0f) < 0.01f);
    assert(!preload_lift(14, 1.5f, -1, p).active());   // no peak in the forecast
    assert(!preload_lift(14, 0.2f, 3, p).active());    // below the write threshold
  }

  // ---- Merge: union of windows, larger lift, larger floor share ----
  {
    LiftParams p{};
    Lift lag = lag_lift(14, 0.6f, 2, p, 0.2f);       // 15:00 for 8 h
    Lift pre = preload_lift(14, 1.2f, 3, p, 0.4f);   // 14:00 for 5 h
    Lift m = merge_lifts(14, lag, pre, p);
    assert(m.active() && m.start_hour == 14 && m.hours == 9);
    assert(std::fabs(m.lift_c - 1.2f) < 0.01f && std::fabs(m.floor_share - 0.4f) < 0.01f);
    Lift only = merge_lifts(14, Lift{}, pre, p);
    assert(only.start_hour == pre.start_hour && only.hours == pre.hours);
    assert(!merge_lifts(14, Lift{}, Lift{}, p).active());
    // Across midnight.
    Lift a = make_lift(22, 0, 3, 0.5f, p);   // 22–01
    Lift b = make_lift(22, 2, 6, 0.5f, p);   // 00–04
    Lift w = merge_lifts(22, a, b, p);
    assert(w.start_hour == 22 && w.hours == 6);
  }

  std::puts("odin_comfort: all assertions passed");
  return 0;
}
