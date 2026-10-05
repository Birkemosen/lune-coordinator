// Host tests for slab_charge.h (capacity-aware wind/cold pre-charging).
#include "slab_charge.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_touch_charge;

// Light timber-frame upper-floor room, west + north facades, quite exposed.
static Room timber_room() {
  Room r{};
  r.area_m2 = 20.0f;
  r.ua_w_per_k = 30.0f;            // leaky envelope: fine when calm, short in a storm
  r.floor_r_m2k_per_w = 0.10f;     // parquet over screed
  r.c_slab_kwh_per_k = 0.20f;      // thin screed — little storage
  r.exterior_walls = 1 | 8;        // N + W
  r.wind_exposure = 0.9f;
  r.solar_gain = 0.4f;
  r.setpoint_c = 22.0f;
  return r;
}

// Same room in heavy masonry/concrete, sheltered.
static Room concrete_room() {
  Room r = timber_room();
  r.ua_w_per_k = 25.0f;
  r.floor_r_m2k_per_w = 0.02f;     // tiles on concrete
  r.c_slab_kwh_per_k = 1.2f;
  r.wind_exposure = 0.2f;
  return r;
}

// 24 h: sunny calm day (h0–h9), then a cold westerly storm (h10–h17), then calm.
static void storm_day(Hour h[24]) {
  for (int i = 0; i < 24; i++) {
    h[i] = Hour{};
    if (i < 10) {
      h[i].temp_c = 2.0f;
      h[i].wind_ms = 2.0f;
      h[i].wind_dir_deg = 270.0f;
      h[i].shortwave_wm2 = (i >= 2 && i <= 8) ? 450.0f : 0.0f;
    } else if (i < 18) {
      h[i].temp_c = -8.0f;
      h[i].wind_ms = 14.0f;
      h[i].wind_dir_deg = 270.0f;  // straight onto the west wall
    } else {
      h[i].temp_c = 0.0f;
      h[i].wind_ms = 3.0f;
      h[i].wind_dir_deg = 270.0f;
    }
  }
}

int main() {
  Params p{};

  // Wind alignment: head-on west wall = 1, wind from the east on a west/north room = 0.
  assert(std::fabs(wind_alignment(8, 270.0f) - 1.0f) < 0.01f);
  assert(wind_alignment(1 | 8, 90.0f) < 0.01f);
  assert(wind_alignment(0, 270.0f) == 0.0f);

  // Wind raises loss; sun lowers it.
  {
    Room r = timber_room();
    Hour calm{-8.0f, 0.0f, 270.0f, 0.0f};
    Hour storm{-8.0f, 14.0f, 270.0f, 0.0f};
    Hour sunny{-8.0f, 0.0f, 270.0f, 600.0f};
    assert(hour_loss_w(storm, r, p) > hour_loss_w(calm, r, p) * 1.5f);
    assert(hour_loss_w(sunny, r, p) < hour_loss_w(calm, r, p));
  }

  // Timber room: the storm exceeds floor capacity → an episode with charging
  // planned before it, starting while the sun still keeps the room warm.
  {
    Hour h[24];
    storm_day(h);
    Room r = timber_room();
    const Decision d = plan(h, 24, 0, r, p);
    assert(d.episode);
    assert(d.peak_loss_w > d.floor_capacity_w);
    assert(d.episode_in_h == 10);
    assert(d.episode_end_in_h == 18);
    assert(d.store_c > 0.0f && d.store_c <= p.max_store_c + 1e-4f);
    assert(d.start_in_h < d.episode_in_h);  // charge before the storm
    // At hour 0 it is too early; a few hours before the storm it is time.
    const Decision later = plan(h, 24, static_cast<size_t>(std::max<int>(0, d.start_in_h)), r, p);
    assert(later.charge_now);
    std::printf("timber: cap %.0f W, peak %.0f W, deficit %.2f kWh, store %.2f C, "
                "charge %.0f h, start in %d h, insufficient %d\n",
                d.floor_capacity_w, d.peak_loss_w, d.deficit_kwh, d.store_c, d.charge_hours,
                d.start_in_h, d.insufficient ? 1 : 0);
  }

  // Concrete, sheltered room: the same storm stays within floor capacity.
  {
    Hour h[24];
    storm_day(h);
    const Decision d = plan(h, 24, 0, concrete_room(), p);
    assert(!d.episode);
    assert(!d.charge_now);
  }

  // Wind from the east (onto no exterior wall) → no wind boost → no episode.
  {
    Hour h[24];
    storm_day(h);
    for (int i = 0; i < 24; i++) h[i].wind_dir_deg = 90.0f;
    Room r = timber_room();
    r.ua_w_per_k = 25.0f;
    const Decision d = plan(h, 24, 0, r, p);
    assert(!d.episode);
  }

  // Inside the episode itself charging stays on until it ends.
  {
    Hour h[24];
    storm_day(h);
    const Decision d = plan(h, 24, 12, timber_room(), p);
    assert(d.episode && d.charge_now);
    assert(d.episode_in_h == 0 && d.episode_end_in_h == 6);
  }

  // Missing data → no decision.
  {
    Hour h[24];
    storm_day(h);
    Room r = timber_room();
    r.ua_w_per_k = 0.0f;
    assert(!plan(h, 24, 0, r, p).episode);
    assert(!plan(nullptr, 24, 0, timber_room(), p).episode);
  }

  std::puts("slab_charge: all assertions passed");
  return 0;
}
