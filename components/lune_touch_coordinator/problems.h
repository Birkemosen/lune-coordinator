// =============================================================================
// Problem tracker — what Touch reports to Home Assistant as `esphome.lune_problem`
// (pure C++, host-testable). Contract: lune/shared/contracts/lune_notifications_contract_v1.md
// =============================================================================
// Each problem has a stable id (`connectivity.v6:lune-v6-a1b2c3`, `room.not_reached:stue`).
// The caller feeds every known problem once per cycle with two conditions:
//
//   raise_cond  the problem is there (checked while inactive)
//   clear_cond  the problem is gone (checked while active) — hysteresis: a value
//               between the two thresholds neither raises nor clears
//
// Rules:
//   raise   raise_cond held for hold_ms, and the id was not raised in the last hour
//           (RAISE_INTERVAL_MS); until then it waits — it is not active locally either
//   clear   clear_cond, or the id was not fed this cycle (room or board removed)
//   notify  a clear is sent only when its raise was sent (`sent`, set by the caller)
//   runtime state only: after a reboot a present problem is raised again after hold_ms
//
// Missing data never raises: the caller passes raise_cond = false when it does not know.
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lune_touch_problems {

constexpr uint32_t MIN_MS = 60UL * 1000UL;
constexpr uint32_t HOUR_MS = 60UL * MIN_MS;
constexpr uint32_t DEFAULT_HOLD_MS = 10UL * MIN_MS;
constexpr uint32_t RAISE_INTERVAL_MS = HOUR_MS;
constexpr size_t MAX_PROBLEMS = 32;

enum class Severity : uint8_t { Warning, Error };

inline const char *severity_name(Severity s) { return s == Severity::Error ? "error" : "warning"; }

enum class EdgeKind : uint8_t { None, Raise, Clear };

struct Edge {
  EdgeKind kind = EdgeKind::None;
  int slot = -1;
  bool notify = false;   ///< Clear: the raise was sent, so the clear must be sent too
};

struct Slot {
  bool used = false;
  bool active = false;       ///< raised and not cleared
  bool sent = false;         ///< the raise reached Home Assistant
  bool fed = false;          ///< updated in the current cycle
  bool recent_raise = false; ///< raised less than RAISE_INTERVAL_MS ago
  Severity severity = Severity::Warning;
  uint32_t since_ms = 0;     ///< raise_cond true since (0 = not met)
  uint32_t last_raise_ms = 0;
  uint32_t seq = 0;          ///< raise counter: a queued raise is stale when seq moved on
  char id[64]{};
  char category[16]{};
  char room[48]{};
  char title[80]{};
  char message[176]{};
};

inline void copy_text(char *dst, size_t cap, const char *src) {
  if (cap == 0)
    return;
  if (src == nullptr)
    src = "";
  std::strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

inline uint32_t nz(uint32_t ms) { return ms == 0 ? 1 : ms; }

class Tracker {
 public:
  /// Start a cycle: problems that are not fed before end_cycle() are cleared.
  void begin_cycle() {
    for (auto &s : slots_)
      s.fed = false;
  }

  Edge update(const char *id, const char *category, Severity severity, bool raise_cond, bool clear_cond,
              uint32_t now_ms, uint32_t hold_ms = DEFAULT_HOLD_MS) {
    Edge e;
    int i = find(id);
    if (i < 0) {
      if (!raise_cond)
        return e;
      i = allocate(id);
      if (i < 0)
        return e;   // full: the problem waits for a free slot
    }
    Slot &s = slots_[i];
    s.fed = true;
    s.severity = severity;
    copy_text(s.category, sizeof(s.category), category);
    if (s.recent_raise && now_ms - s.last_raise_ms >= RAISE_INTERVAL_MS)
      s.recent_raise = false;
    if (!s.active) {
      if (!raise_cond) {
        s.since_ms = 0;
        return e;
      }
      if (s.since_ms == 0)
        s.since_ms = nz(now_ms);
      if (now_ms - s.since_ms >= hold_ms && !s.recent_raise) {
        s.active = true;
        s.sent = false;
        s.recent_raise = true;
        s.last_raise_ms = now_ms;
        s.seq++;
        e.kind = EdgeKind::Raise;
        e.slot = i;
      }
      return e;
    }
    if (clear_cond) {
      e = clear_slot(i);
    }
    return e;
  }

  /// End a cycle: clears active problems that were not fed. Fills up to cap edges.
  size_t end_cycle(uint32_t now_ms, Edge *out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < MAX_PROBLEMS; i++) {
      Slot &s = slots_[i];
      if (!s.used)
        continue;
      if (s.recent_raise && now_ms - s.last_raise_ms >= RAISE_INTERVAL_MS)
        s.recent_raise = false;
      bool cleared_now = false;
      if (!s.fed) {
        s.since_ms = 0;
        if (s.active) {
          const Edge e = clear_slot(static_cast<int>(i));
          cleared_now = true;
          if (n < cap)
            out[n++] = e;
        }
      }
      // Free slots that hold nothing: inactive, not pending, no raise in the last hour.
      // A slot cleared in this pass is kept until the next, so the caller can read it.
      if (!cleared_now && !s.active && s.since_ms == 0 && !s.recent_raise)
        s = Slot{};
    }
    return n;
  }

  void set_text(int slot, const char *title, const char *message, const char *room) {
    if (slot < 0 || static_cast<size_t>(slot) >= MAX_PROBLEMS)
      return;
    copy_text(slots_[slot].title, sizeof(slots_[slot].title), title);
    copy_text(slots_[slot].message, sizeof(slots_[slot].message), message);
    copy_text(slots_[slot].room, sizeof(slots_[slot].room), room);
  }

  void mark_sent(int slot) {
    if (slot >= 0 && static_cast<size_t>(slot) < MAX_PROBLEMS)
      slots_[slot].sent = true;
  }

  const Slot &slot(int i) const { return slots_[i]; }
  Slot &slot_mut(int i) { return slots_[i]; }

  size_t active_count() const {
    size_t n = 0;
    for (const auto &s : slots_)
      n += (s.used && s.active) ? 1 : 0;
    return n;
  }

  bool is_active(const char *id) const {
    const int i = find(id);
    return i >= 0 && slots_[i].active;
  }

  /// Titles of the active problems, "; "-separated, cut at a UTF-8 boundary to fit cap - 1.
  size_t titles(char *out, size_t cap) const {
    if (cap == 0)
      return 0;
    out[0] = '\0';
    size_t len = 0;
    bool full = false;
    for (const auto &s : slots_) {
      if (!s.used || !s.active || full)
        continue;
      if (len > 0)
        len = append(out, cap, len, "; ", &full);
      if (!full)
        len = append(out, cap, len, s.title[0] != '\0' ? s.title : s.id, &full);
    }
    return len;
  }

 private:
  Slot slots_[MAX_PROBLEMS]{};

  int find(const char *id) const {
    for (size_t i = 0; i < MAX_PROBLEMS; i++)
      if (slots_[i].used && std::strcmp(slots_[i].id, id) == 0)
        return static_cast<int>(i);
    return -1;
  }

  int allocate(const char *id) {
    for (size_t i = 0; i < MAX_PROBLEMS; i++) {
      if (!slots_[i].used) {
        slots_[i] = Slot{};
        slots_[i].used = true;
        copy_text(slots_[i].id, sizeof(slots_[i].id), id);
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  Edge clear_slot(int i) {
    Slot &s = slots_[i];
    Edge e;
    e.kind = EdgeKind::Clear;
    e.slot = i;
    e.notify = s.sent;
    s.active = false;
    s.sent = false;
    s.since_ms = 0;
    return e;
  }

  static size_t append(char *out, size_t cap, size_t len, const char *text, bool *full) {
    size_t n = std::strlen(text);
    if (len + n >= cap) {
      *full = true;
      n = cap - 1 - len;
      // Do not cut a UTF-8 sequence: back up to a lead byte.
      while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
        n--;
    }
    std::memcpy(out + len, text, n);
    out[len + n] = '\0';
    return len + n;
  }
};

// ---------------------------------------------------------------------------
// Text helpers: catalogue templates use {name} placeholders (as the web UI).
// ---------------------------------------------------------------------------

/// Fills {key} placeholders from keys/values; unknown placeholders are kept as is.
inline size_t fill(char *out, size_t cap, const char *tpl, const char *const *keys, const char *const *values,
                   size_t n) {
  if (cap == 0)
    return 0;
  size_t len = 0;
  auto put = [&](const char *s, size_t k) {
    for (size_t i = 0; i < k && len + 1 < cap; i++)
      out[len++] = s[i];
  };
  const char *p = tpl != nullptr ? tpl : "";
  while (*p != '\0') {
    if (*p == '{') {
      const char *end = std::strchr(p, '}');
      bool done = false;
      if (end != nullptr) {
        const size_t klen = static_cast<size_t>(end - p - 1);
        for (size_t i = 0; i < n; i++) {
          if (std::strlen(keys[i]) == klen && std::strncmp(p + 1, keys[i], klen) == 0) {
            put(values[i], std::strlen(values[i]));
            p = end + 1;
            done = true;
            break;
          }
        }
      }
      if (done)
        continue;
    }
    put(p, 1);
    p++;
  }
  // Never end inside a UTF-8 sequence: drop an incomplete last character.
  if (len > 0) {
    size_t lead = len - 1;
    while (lead > 0 && (static_cast<unsigned char>(out[lead]) & 0xC0) == 0x80)
      lead--;
    const unsigned char c = static_cast<unsigned char>(out[lead]);
    const size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (len - lead < need)
      len = lead;
  }
  out[len] = '\0';
  return len;
}

/// A number with the language's decimal sign ("21,5" in Danish).
inline void format_number(char *out, size_t cap, float value, int decimals, char dec) {
  std::snprintf(out, cap, "%.*f", decimals, static_cast<double>(value));
  for (char *c = out; *c != '\0'; c++)
    if (*c == '.')
      *c = dec;
}

}  // namespace lune_touch_problems
