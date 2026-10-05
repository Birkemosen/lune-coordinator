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
  assert(!lift_changed(w, Lift{15, 3, 0.6f}));
  assert(lift_changed(w, Lift{16, 3, 0.5f}));
  assert(lift_changed(w, Lift{}));

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

  std::puts("odin_comfort: all assertions passed");
  return 0;
}
