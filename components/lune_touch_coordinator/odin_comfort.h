// =============================================================================
// Odin comfort lift — temporary raise of Odin 2.0's comfort schedule
// (pure C++, host-testable)
// =============================================================================
// When Odin 2.0 drives the heat pump (Asgard in "Heat Flow Temperature" mode),
// Asgard's virtual-thermostat target is not what decides heating: Odin plans
// from its own comfort schedule (`sched_ui`: a repeating 24 h profile of blocks
// {h, sp, min, max}, band = [sp+min, sp+max]). The MPC-correct way for Touch to
// ask for extra heat (slab charge before a storm, a room that lags) is to shift
// that band up for the hours concerned. Odin then decides *when* to heat within
// its price/COP plan — the room temperature it learns from stays honest.
//
// Odin has no dated exceptions, so Touch:
//   1. keeps the user's own profile,
//   2. writes a lifted copy while a lift is wanted,
//   3. writes the user's profile back when it is not,
//   4. steps back (never overwrites) when someone edits the schedule in Odin's UI.
//
// See docs/house_balancing_and_weather.md §7.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lune_touch_odin {

static constexpr size_t MAX_BLOCKS = 24;

struct Block {
  uint8_t h = 0;
  float sp = 21.0f;
  float min = -0.5f;
  float max = 1.5f;
};

struct Profile {
  Block blocks[MAX_BLOCKS]{};
  uint8_t count = 0;
};

/// A lift of the comfort band for a run of wall-clock hours (wraps midnight).
struct Lift {
  uint8_t start_hour = 0;  ///< Local hour the lift begins (0–23)
  uint8_t hours = 0;       ///< Number of hours lifted (0 = none, max 23)
  float lift_c = 0.0f;     ///< Band shift (°C)
  bool active() const { return hours > 0 && lift_c > 0.0f; }
};

// ---------------------------------------------------------------------------
// Parse / format
// ---------------------------------------------------------------------------

namespace detail {
inline const char *skip_ws(const char *p) {
  while (p != nullptr && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    ++p;
  return p;
}

/// Read the number after `"key":` inside [obj, end). Accepts quoted numbers.
inline bool read_field(const char *obj, const char *end, const char *key, float *out) {
  char pattern[16];
  std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const size_t plen = std::strlen(pattern);
  for (const char *p = obj; p + plen <= end; ++p) {
    if (std::strncmp(p, pattern, plen) != 0)
      continue;
    const char *q = skip_ws(p + plen);
    if (q >= end || *q != ':')
      continue;
    q = skip_ws(q + 1);
    if (q < end && *q == '"')
      ++q;
    char *num_end = nullptr;
    const float v = std::strtof(q, &num_end);
    if (num_end == q || num_end > end || !std::isfinite(v))
      return false;
    *out = v;
    return true;
  }
  return false;
}
}  // namespace detail

/// Parse Odin's `sched_ui` JSON array (as stored, i.e. the decoded string).
/// Blocks are sorted by hour; duplicate hours or out-of-range values fail.
inline bool parse_profile(const char *json, Profile *out) {
  if (json == nullptr || out == nullptr)
    return false;
  Profile p{};
  const char *c = detail::skip_ws(json);
  if (*c != '[')
    return false;
  ++c;
  while (true) {
    c = detail::skip_ws(c);
    if (*c == ']')
      break;
    if (*c == ',') {
      ++c;
      continue;
    }
    if (*c != '{')
      return false;
    const char *end = std::strchr(c, '}');
    if (end == nullptr || p.count >= MAX_BLOCKS)
      return false;
    float h = NAN, sp = NAN, mn = NAN, mx = NAN;
    if (!detail::read_field(c, end, "h", &h) || !detail::read_field(c, end, "sp", &sp) ||
        !detail::read_field(c, end, "min", &mn) || !detail::read_field(c, end, "max", &mx))
      return false;
    if (h < 0.0f || h > 23.0f || sp < 5.0f || sp > 35.0f)
      return false;
    Block &b = p.blocks[p.count++];
    b.h = static_cast<uint8_t>(std::lround(h));
    b.sp = sp;
    b.min = mn;
    b.max = mx;
    c = end + 1;
  }
  if (p.count == 0)
    return false;
  std::sort(p.blocks, p.blocks + p.count, [](const Block &a, const Block &b) { return a.h < b.h; });
  for (uint8_t i = 1; i < p.count; i++)
    if (p.blocks[i].h == p.blocks[i - 1].h)
      return false;
  *out = p;
  return true;
}

/// Format like Odin's UI does: first block at h=0, numbers without noise.
inline size_t format_profile(const Profile &p, char *out, size_t cap) {
  if (out == nullptr || cap < 3)
    return 0;
  size_t off = 0;
  auto put = [&](const char *s) {
    const size_t n = std::strlen(s);
    if (off + n >= cap)
      return false;
    std::memcpy(out + off, s, n);
    off += n;
    out[off] = '\0';
    return true;
  };
  auto num = [](float v, char *buf, size_t len) {
    const float r = std::round(v * 100.0f) / 100.0f;
    std::snprintf(buf, len, "%.2f", static_cast<double>(r));
    // Trim trailing zeros / dot: 21.50 → 21.5, 22.00 → 22, -0.00 → 0
    char *dot = std::strchr(buf, '.');
    if (dot != nullptr) {
      char *e = buf + std::strlen(buf) - 1;
      while (e > dot && *e == '0')
        *e-- = '\0';
      if (e == dot)
        *e = '\0';
    }
    if (std::strcmp(buf, "-0") == 0)
      std::strcpy(buf, "0");
  };
  out[0] = '\0';
  if (!put("["))
    return 0;
  for (uint8_t i = 0; i < p.count; i++) {
    char sp[16], mn[16], mx[16], item[96];
    num(p.blocks[i].sp, sp, sizeof(sp));
    num(p.blocks[i].min, mn, sizeof(mn));
    num(p.blocks[i].max, mx, sizeof(mx));
    std::snprintf(item, sizeof(item), "%s{\"h\":%u,\"sp\":%s,\"min\":%s,\"max\":%s}",
                  i == 0 ? "" : ",", static_cast<unsigned>(p.blocks[i].h), sp, mn, mx);
    if (!put(item))
      return 0;
  }
  if (!put("]"))
    return 0;
  return off;
}

inline bool same_block_values(const Block &a, const Block &b, float tol = 0.02f) {
  return std::fabs(a.sp - b.sp) <= tol && std::fabs(a.min - b.min) <= tol &&
         std::fabs(a.max - b.max) <= tol;
}

/// The block in effect at `hour` (the last block wraps past midnight).
inline Block block_at(const Profile &p, uint8_t hour) {
  if (p.count == 0)
    return Block{};
  Block found = p.blocks[p.count - 1];
  for (uint8_t i = 0; i < p.count; i++)
    if (p.blocks[i].h <= hour)
      found = p.blocks[i];
  return found;
}

/// Equal as hourly bands (block boundaries may differ).
inline bool equivalent(const Profile &a, const Profile &b, float tol = 0.02f) {
  if (a.count == 0 || b.count == 0)
    return a.count == b.count;
  for (uint8_t h = 0; h < 24; h++)
    if (!same_block_values(block_at(a, h), block_at(b, h), tol))
      return false;
  return true;
}

inline bool hour_in_lift(const Lift &lift, uint8_t hour) {
  if (!lift.active())
    return false;
  const uint8_t rel = static_cast<uint8_t>((hour + 24 - lift.start_hour) % 24);
  return rel < lift.hours;
}

/// The user's profile with the band shifted up by `lift.lift_c` in the lifted
/// hours. Expanded per hour and re-compacted, so it always has a block at h=0.
inline Profile apply_lift(const Profile &user, const Lift &lift) {
  Block hourly[24];
  for (uint8_t h = 0; h < 24; h++) {
    hourly[h] = block_at(user, h);
    hourly[h].h = h;
    if (hour_in_lift(lift, h))
      hourly[h].sp = std::round((hourly[h].sp + lift.lift_c) * 10.0f) / 10.0f;
  }
  Profile out{};
  for (uint8_t h = 0; h < 24; h++) {
    if (out.count > 0 && same_block_values(out.blocks[out.count - 1], hourly[h]))
      continue;
    out.blocks[out.count++] = hourly[h];
  }
  return out;
}

// ---------------------------------------------------------------------------
// How much and when
// ---------------------------------------------------------------------------

struct LiftParams {
  float max_lift_c = 1.5f;  ///< Never shift Odin's band more than this
  float min_lift_c = 0.3f;  ///< Below this a lift is not worth a schedule write
  uint8_t max_hours = 23;   ///< A lift must never cover the whole day
};

/// House-level band shift needed to store `deficit_kwh` in a house whose
/// thermal mass Odin estimates as `thermal_mass_kwh_per_k` (kWh/K).
inline float lift_for_energy(float deficit_kwh, float thermal_mass_kwh_per_k, const LiftParams &p) {
  if (!(deficit_kwh > 0.0f) || !(thermal_mass_kwh_per_k > 0.5f))
    return 0.0f;
  return std::min(p.max_lift_c, deficit_kwh / thermal_mass_kwh_per_k);
}

/// Merge two hour windows given relative to now (start_in_h, end_in_h) into
/// one lift anchored at wall-clock `now_hour`.
inline Lift make_lift(uint8_t now_hour, int start_in_h, int end_in_h, float lift_c,
                      const LiftParams &p) {
  Lift l{};
  if (!(lift_c >= p.min_lift_c) || end_in_h <= 0)
    return l;
  const int start = std::max(0, start_in_h);
  int hours = end_in_h - start;
  if (hours <= 0)
    return l;
  hours = std::min<int>(hours, p.max_hours);
  l.start_hour = static_cast<uint8_t>((now_hour + start) % 24);
  l.hours = static_cast<uint8_t>(hours);
  l.lift_c = std::round(std::min(lift_c, p.max_lift_c) * 10.0f) / 10.0f;
  return l;
}

/// True when two lifts differ enough to justify rewriting Odin's schedule.
inline bool lift_changed(const Lift &a, const Lift &b, float tol_c = 0.25f) {
  if (a.active() != b.active())
    return true;
  if (!a.active())
    return false;
  return a.start_hour != b.start_hour || a.hours != b.hours || std::fabs(a.lift_c - b.lift_c) > tol_c;
}

// ---------------------------------------------------------------------------
// Ownership state machine
// ---------------------------------------------------------------------------

enum class Action : uint8_t {
  NONE = 0,     ///< Remote matches what we want
  WRITE_LIFT,   ///< Write `profile` (lifted)
  RESTORE,      ///< Write `profile` (the user's own)
  ADOPT_USER,   ///< Remote changed by the user while we were idle → new baseline
  YIELD,        ///< Remote changed by the user while lifted → stop touching it
};

inline const char *action_name(Action a) {
  switch (a) {
    case Action::WRITE_LIFT: return "write_lift";
    case Action::RESTORE: return "restore";
    case Action::ADOPT_USER: return "adopt_user";
    case Action::YIELD: return "yield";
    case Action::NONE:
    default: return "none";
  }
}

struct OwnerState {
  Profile user{};          ///< The user's own schedule (baseline)
  bool has_user = false;
  Profile written{};       ///< What Touch last wrote (valid while applied)
  bool applied = false;    ///< A lifted profile is believed to be on Odin
  Lift lift{};             ///< The lift that `written` encodes
  /// The user edited the schedule during a lift: leave Odin alone until the
  /// current need has passed (no lift wanted), then resume normally.
  bool yielded = false;
};

struct StepResult {
  Action action = Action::NONE;
  Profile profile{};
};

/// One reconciliation step. `remote` is the schedule read from Odin now.
/// The caller performs the write and then calls `commit()`.
inline StepResult step(OwnerState &s, const Profile &remote, const Lift &want) {
  StepResult r{};
  if (!s.has_user) {
    // First contact: whatever is on Odin is the user's schedule, unless it is
    // the lift we wrote before a reboot (persisted `written`).
    if (s.applied && equivalent(remote, s.written)) {
      // Our lifted copy is still there; the persisted user baseline is missing.
      // Cannot restore safely → yield and leave it to the user.
      s.yielded = true;
      r.action = Action::YIELD;
      return r;
    }
    s.user = remote;
    s.has_user = true;
    s.applied = false;
  }

  if (s.yielded) {
    if (!equivalent(remote, s.user))
      s.user = remote;  // keep following the user's edits
    if (want.active())
      return r;
    s.yielded = false;
  }

  if (s.applied) {
    if (!equivalent(remote, s.written)) {
      if (equivalent(remote, s.user)) {
        // Someone (Odin factory reset / restore) put the user's profile back.
        s.applied = false;
      } else {
        // Edited in Odin's UI while lifted: the user owns it now.
        s.user = remote;
        s.applied = false;
        s.lift = Lift{};
        s.yielded = want.active();
        r.action = Action::YIELD;
        return r;
      }
    }
  } else if (!equivalent(remote, s.user)) {
    // Edited while we were idle: adopt as the new baseline.
    s.user = remote;
    r.action = Action::ADOPT_USER;
    if (!want.active())
      return r;
  }

  if (want.active()) {
    if (s.applied && !lift_changed(s.lift, want))
      return r;
    r.action = Action::WRITE_LIFT;
    r.profile = apply_lift(s.user, want);
    return r;
  }
  if (s.applied) {
    r.action = Action::RESTORE;
    r.profile = s.user;
  }
  return r;
}

/// Record a successful write performed for `r`.
inline void commit(OwnerState &s, const StepResult &r, const Lift &want) {
  if (r.action == Action::WRITE_LIFT) {
    s.written = r.profile;
    s.applied = true;
    s.lift = want;
  } else if (r.action == Action::RESTORE) {
    s.applied = false;
    s.lift = Lift{};
  }
}

}  // namespace lune_touch_odin
