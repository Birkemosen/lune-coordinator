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
  // --- a room more than twice the comfort band behind calls for heat now -----
  {
    ColdRoomCall call;
    assert(!call.update(0.9f, 0.5f));                 // within twice the band: top only
    assert(!call.update(1.0f, 0.5f));                 // exactly 2 × band: not yet
    assert(call.update(1.1f, 0.5f));                  // more than 2 × band: call on
    assert(call.update(0.6f, 0.5f));                  // holds while outside the band
    assert(!call.update(0.5f, 0.5f));                 // back within the band: off
    assert(call.update(2.3f, 0.5f));                  // 2026-11-14: Laura 2.3 °C behind
    assert(!call.update(NAN, 0.5f));                  // no reading: no call
    // The call moves the whole band by the full lift from this hour (floor lag 1 h).
    const Lift l = lag_lift(3, p.max_lift_c, 1, p, 1.0f, 0);
    assert(l.start_hour == 3 && l.hours == 1 + 1 + 2 && near(l.floor_share, 1.0f));
    assert(near(l.lift_c, p.max_lift_c));
    const Profile cold = apply_lift(user, l);
    assert(near(block_at(cold, 3).sp, block_at(user, 3).sp + p.max_lift_c));  // floor up by the full lift
    // The ordinary lag window still starts next hour.
    assert(lag_lift(3, 0.6f, 1, p).start_hour == 4);
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
    // A saving below the minimum (a tenth of a 1.85 normal price) leaves the hour lifted:
    // the 18–19 saving of about 1 is kept, a day with 0.1 between hours is left whole.
    Lift ms{14, 8, 1.0f};
    skip_dearest(ms, 13, price, 24, TAU_DEFAULT_H, 0, 0.185f);
    assert(ms.skip_mask == ((1UL << 18) | (1UL << 19)));
    float cheap[24];
    for (int k = 0; k < 24; k++)
      cheap[k] = ((13 + k) % 24 >= 17 && (13 + k) % 24 < 21) ? 1.0f : 0.9f;
    Lift c2{14, 8, 1.0f};
    skip_dearest(c2, 13, cheap, 24);
    assert(c2.skip_mask != 0);
    skip_dearest(c2, 13, cheap, 24, TAU_DEFAULT_H, 0, 0.185f);
    assert(c2.skip_mask == 0);
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
    // Deferring (defer_before): a window that starts in the day's dearest hours leaves
    // them to cheaper lifted hours after, up to a third of the window, never past the need.
    float eve[24];
    for (int k = 0; k < 24; k++) eve[k] = 0.30f;
    eve[0] = 0.43f; eve[1] = 0.39f;   // 20:00 and 21:00 dearest, now = 20
    Lift late{20, 6, 1.3f};
    skip_dearest(late, 20, eve, 24, 167.0f);
    assert(late.skip_mask == 0);                                   // nothing earlier: lifted as before
    Lift defer{20, 6, 1.3f};
    skip_dearest(defer, 20, eve, 24, 167.0f, 6);
    assert(defer.skip_mask == ((1UL << 20) | (1UL << 21)));        // the two dear hours wait
    Lift soon{20, 6, 1.3f};
    skip_dearest(soon, 20, eve, 24, 167.0f, 1);
    assert(soon.skip_mask == 0);                                   // needed now: no later hour counts
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
    // A room that starts charging at 12 for wind peaking at 18 (now 09): the band opens
    // at 12, so Odin can plan the charge, through 19.
    Lift ahead = preload_lift(9, 1.3f, 9, p, 0.2f, 3);
    assert(ahead.active() && ahead.start_hour == 12 && ahead.hours == 8);
    Lift late = preload_lift(9, 1.3f, 2, p, 1.0f, 5);   // a start past the peak opens at the peak
    assert(late.active() && late.start_hour == 11 && late.hours == 3);
  }

  // ---- Cold-room call: lift by need, start at the cheapest hour unless urgent ----
  {
    LiftParams p{};
    // Stue 1.1 K behind (band 0.5) in a house at 22.2 with its floor at 21.5: 0.7 + 0.6.
    assert(std::fabs(cold_call_lift_c(22.2f, 21.5f, 1.1f, 0.5f, p) - 1.3f) < 1e-3f);
    // Laura 2.3 K behind in a house 0.9 above its floor: capped at the maximum, as before.
    assert(std::fabs(cold_call_lift_c(22.4f, 21.5f, 2.3f, 0.5f, p) - p.max_lift_c) < 1e-3f);
    // A cold house: at least the minimum lift. Unknown temperatures: the maximum.
    assert(std::fabs(cold_call_lift_c(21.0f, 21.5f, 1.05f, 0.5f, p) - 0.55f) < 1e-3f);
    assert(std::fabs(cold_call_lift_c(NAN, 21.5f, 1.1f, 0.5f, p) - p.max_lift_c) < 1e-3f);
    // Lift by forecast and share (22 Nov 2026). Stue Repo 1.4 K behind its 23.0 at 09,
    // 44.5 % of the house's heat, in a house at 21.9 over a floor of 21.5. The forecast
    // with the day's sun reaches 24.4 by noon: nothing left to lift for.
    const float sunny[8] = {21.8f, 22.8f, 23.8f, 24.4f, 24.5f, 24.0f, 23.5f, 22.9f};
    assert(forecast_shortfall_c(sunny, 8, 6, 23.0f, 0.5f) == 0.0f);
    assert(share_lift_c(21.9f, 21.5f, 0.0f, 0.445f, p) == 0.0f);
    // A grey day: it only gets to 21.9, 0.6 K short of 22.5; the lift is the house's 0.4
    // above its floor plus 0.445 × 0.6, against the full 1.3 K of the rule without a forecast.
    const float grey[8] = {21.6f, 21.7f, 21.8f, 21.9f, 21.9f, 21.8f, 21.7f, 21.7f};
    assert(std::fabs(forecast_shortfall_c(grey, 8, 6, 23.0f, 0.5f) - 0.6f) < 1e-4f);
    assert(std::fabs(share_lift_c(21.9f, 21.5f, 0.6f, 0.445f, p) - (0.4f + 0.445f * 0.6f)) < 1e-4f);
    assert(std::fabs(cold_call_lift_c(21.9f, 21.5f, 1.4f, 0.5f, p) - 1.3f) < 1e-3f);
    // Only the window counts; no forecast or no share: unknown, or the whole shortfall.
    assert(forecast_shortfall_c(sunny, 8, 1, 23.0f, 0.5f) > 0.0f);
    assert(std::isnan(forecast_shortfall_c(nullptr, 8, 6, 23.0f, 0.5f)));
    assert(std::isnan(share_lift_c(21.9f, 21.5f, NAN, 0.445f, p)));
    assert(std::fabs(share_lift_c(21.4f, 21.5f, 0.6f, NAN, p) - 0.6f) < 1e-4f);
    // A small room a little short: at least the minimum lift, never above the maximum.
    assert(std::fabs(share_lift_c(21.4f, 21.5f, 0.2f, 0.05f, p) - p.min_lift_c) < 1e-4f);
    assert(std::fabs(share_lift_c(23.5f, 21.5f, 2.0f, 1.0f, p) - p.max_lift_c) < 1e-4f);
    float eve[24];
    for (int k = 0; k < 24; k++) eve[k] = 0.30f;
    eve[0] = 0.43f; eve[1] = 0.39f; eve[2] = 0.33f;
    assert(cold_call_start_in_h(eve, 24, 4) == 2 || cold_call_start_in_h(eve, 24, 4) == 3);
    float flat[24];
    for (float &f : flat) f = 0.3f;
    assert(cold_call_start_in_h(flat, 24, 4) == 0);   // nothing to win by waiting
    eve[3] = NAN;
    assert(cold_call_start_in_h(eve, 24, 4) == 0);    // an unknown price: start now
    // Merge with a preload that opens only the top: the floor moves by the call's 1.3 K.
    Lift call = make_lift(20, 0, 5, 1.3f, p);
    call.floor_share = 1.0f;
    Lift pre = preload_lift(20, 1.5f, 1, p, 0.2f);
    Lift m = merge_lifts(20, call, pre, p);
    assert(std::fabs(m.lift_c - 1.5f) < 1e-3f && std::fabs(m.lift_c * m.floor_share - 1.3f) < 0.01f);
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

  // ---- Sources per hour: preload alone vs shared with a lagging room ----
  {
    LiftParams p{};
    Lift lag = lag_lift(14, 0.6f, 2, p, 0.2f);       // 15–22
    Lift pre = preload_lift(14, 1.2f, 3, p, 0.4f);   // 14–18
    Lift m = merge_lifts(14, lag, pre, p);
    m.skip_mask = 1UL << 16;
    const uint32_t keep = lift_hours_mask(m);
    assert(keep == (((1UL << 23) - 1) & ~((1UL << 14) - 1) & ~(1UL << 16)));
    LiftSources s{lift_hours_mask(Lift{}) & keep, lift_hours_mask(lag) & keep, lift_hours_mask(pre) & keep};
    assert(s.at(14) == 4);   // preload only
    assert(s.at(15) == 6);   // lag + preload
    assert(s.at(16) == 0);   // skipped
    assert(s.at(20) == 2);   // lag only
    assert(s.at(23) == 0);
    Lift w = make_lift(22, 0, 4, 0.5f, p);           // 22–01 across midnight
    assert(lift_hours_mask(w) == ((1UL << 22) | (1UL << 23) | 1UL | 2UL));
  }

  // ---- Room warm-up by hand (25 Nov 2026: Soveværelse aired, 1.9 K behind) ----
  {
    LiftParams p{};
    Warmups ws{};
    const uint32_t t0 = 1795600000U;
    RoomWarmup &w = ws.start("v62-z6", false, t0);
    assert(ws.any() && ws.find("v62-z6") == &w && w.until_ts == t0 + WARMUP_MAX_S);
    assert(w.hours_left(t0) == 6 && w.hours_left(t0 + 1) == 6 && w.hours_left(t0 + 5 * 3600 + 1) == 1);
    // Ends back within the band or when the time is up; no reading keeps it running.
    assert(!w.done(1.9f, 0.5f, t0) && w.done(0.5f, 0.5f, t0) && w.done(1.9f, 0.5f, t0 + WARMUP_MAX_S));
    assert(!w.done(NAN, 0.5f, t0 + 3600));
    // «Heat now»: the house at 22.0 over a floor of 21.5, the room 1.9 K behind: 0.5 + 1.4,
    // capped at 1.5, from this hour over its lag window, whole band, whatever the price.
    float dear[24];
    for (int k = 0; k < 24; k++) dear[k] = k < 2 ? 0.40f : 0.30f;
    float d[WARMUP_N] = {1.9f, NAN, NAN, NAN};
    uint8_t lead[WARMUP_N] = {2, 0, 0, 0};
    bool now_mode = false;
    Lift l = warmup_lift(10, ws, d, lead, 22.0f, 21.5f, 0.5f, dear, 24, t0, p, &now_mode);
    assert(l.active() && now_mode && l.start_hour == 10 && l.floor_share == 1.0f);
    assert(std::fabs(l.lift_c - p.max_lift_c) < 1e-3f && l.hours == lag_end_in_h(2));
    // «Heat cheaply» waits for cheaper hours as far as its 6 h allow: a 5 h window can
    // start one hour on, past the first dear hour.
    ws.start("v62-z6", true, t0);
    assert(ws.find("v62-z6")->cheap);
    l = warmup_lift(10, ws, d, lead, 22.0f, 21.5f, 0.5f, dear, 24, t0, p, &now_mode);
    assert(l.active() && !now_mode && l.start_hour == 10 + 6 - lag_end_in_h(2));
    // Never past its time: with one hour left only that hour is lifted, and it starts now.
    l = warmup_lift(15, ws, d, lead, 22.0f, 21.5f, 0.5f, dear, 24, t0 + 5 * 3600 + 60, p, &now_mode);
    assert(l.active() && l.hours == 1 && l.start_hour == 15);
    // Time up: nothing.
    assert(!warmup_lift(16, ws, d, lead, 22.0f, 21.5f, 0.5f, dear, 24, t0 + WARMUP_MAX_S, p).active());
    // Stop, and a full set drops the oldest.
    assert(ws.stop("v62-z6") && !ws.any() && !ws.stop("v62-z6"));
    const char *ids[5] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) ws.start(ids[i], false, t0 + static_cast<uint32_t>(i));
    assert(ws.find("a") == nullptr && ws.find("e") != nullptr && ws.find("b") != nullptr);
  }

  std::puts("odin_comfort: all assertions passed");
  return 0;
}
