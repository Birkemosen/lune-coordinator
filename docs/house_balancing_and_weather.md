# House balancing, absorb and weather (Touch ↔ V6 ↔ Asgard/Odin)

This document records **who decides what** when one Lune Touch coordinates several
Lune V6 manifolds and talks to an Asgard/Odin heat pump stack — and the physical
reasons behind those decisions. It exists so later changes keep these factors in view.

The motivating house: a **wind-exposed timber-frame upper floor** (thin screed, leaky
envelope, large facade) on one V6, and a **concrete/masonry ground floor** (heavy slab,
little wind exposure) on another. In winter the upper floor can be short of heat while
the ground floor is satisfied.

Related: [`forecast_preload.md`](forecast_preload.md) (per-zone wind preload model),
[`lune_whole_house_flow_temperature.md`](lune_whole_house_flow_temperature.md) (flow
trim), [`odin_plan_ingestion.md`](odin_plan_ingestion.md) (Odin plan use),
[`lune_asgard_authority_state_machine.md`](lune_asgard_authority_state_machine.md).

---

## 1. Responsibilities

| Layer | Owns | Does not do |
|---|---|---|
| **Odin** (via Asgard) | Heat pump timing, prices, compressor, DHW, whole-house weather/solar response (implicit in its model) | Room-level routing; it has no notion of which facade the wind hits |
| **Touch** | Logical rooms, house temperature + target, room schedules/comfort, **per-room wind/cold preload and slab charge**, **whole-house balancing between manifolds**, house demand to the heat source (Asgard, Odin or generic) | Valve positions (never) |
| **V6** | Local, safe per-zone control (valves, motors, hydraulic balance, heat-pump-mode allocation), and full standalone fallback | House-wide decisions while a Touch lease is active |

Touch never sets valve positions. Its levers on a V6 are: base setpoint, an expiring
setpoint **offset** (clamped by V6), per-zone **absorb arm**, the heating **mode** (via
lease), and physics parameters (UA, thermal lead).

## 2. Authority: every V6 holds the lease

When V6s are paired with Touch, **every trusted V6 holds the Touch lease continuously**
(renewed every ~90 s; same `lease_id` and sequence to all boards). Earlier only the
"leader" V6 got it, so the other board neither received the heating mode nor knew a
house-level coordinator was in charge. Each node's lease state is reported in Touch
`/nodes` (`lease: granted | refused | none`).

The lease `sequence` is Unix time (falling back to a counter before SNTP). V6 rejects any
sequence at or below the last one it accepted and keeps that number across a Touch
reboot; a RAM counter that restarted at 1 locked Touch out (`replayed_sequence`) until
every V6 rebooted (seen 2026-10-04).

While a V6 holds the lease:
- Its own learned preheat is off (Touch does weather preload).
- **Reactive absorb auto-detection is off** (section 3). Only Touch arms absorb.
- The heating mode follows Touch.

When the lease lapses (Touch offline, unpaired):
- V6 returns to fully local control: local mode, local preheat, reactive absorb.
- **Touch's temporary commands are cleared at once** — coordinator setpoint offsets and
  all absorb arms (house and per-zone). They no longer linger up to their own TTL.

## 3. Absorb and multiple manifolds

*Absorb* keeps a satisfied zone's valve open above setpoint (a widened overheat band) so
the slab stores surplus heat.

**Standalone V6** (one manifold, no Touch): reactive detection — supply temperature more
than `detect_delta` (8 °C) above the room average and no zone calling → absorb. With one
manifold, hot supply really is surplus.

**With several manifolds this is wrong.** If the upper floor calls, the heat pump raises
supply temperature; the ground-floor V6 sees hot supply and no local demand and would
start absorbing. It then keeps its loops open, overheats the concrete, steals flow from
the floor that needs it, and pulls the weighted house temperature up so the heat pump
stops early. A single V6 cannot tell "surplus" from "another manifold's demand".

Therefore, under a Touch lease, **only Touch arms absorb**, because only Touch sees all
manifolds and the Odin plan:
- **Odin heat window** (cheap/planned surplus): all zones, house-wide.
- **Slab charge** (section 5): only the rooms that need stored heat, per zone
  (`POST /api/v1/zones/{z}/absorb-arm` / `absorb-disarm` on V6).

## 4. Balancing between manifolds

The weighted house temperature (area- or UA-weighted) is what Asgard's virtual
thermostat compares with the house target. A warm concrete floor can bring the average
to target while the timber floor still lags, so the heat pump stops when that floor
needs it.

**One house demand, never a faked temperature.** The measured house temperature
(`temperature_feedback`) is never altered — Odin learns house physics from it, and so does
any other controller that learns. Touch instead computes one *house demand*
(`house_demand.h`):
- the room that lags most (`driver_deficit_c`), passed through a **hold**
  (`DemandHold`): it engages above the comfort band (0.3 °C), stays engaged until the lag
  is below 0.1 °C **and** at least 30 min have passed, so the heat source is not cycled
  around the threshold;
- an open slab-charge window (section 5).

How that demand reaches the heat source depends on what the source can take — see
section 7, *Heat-source routes*. The chosen route is reported as
`GET /heat-source/control → demand.route`.

For Asgard **without** Odin the route is the **demand-led target**: the target sent to the
virtual thermostat is `max(house_target, house_temp + required)`, capped at
`house_target + 1.5 °C` (`target_sync.demand_uplift_c`). Requires "Sync comfort target".

Not done yet: actively **throttling** the satisfied manifold (e.g. negative offsets on
the ground floor while the upper floor catches up). In heat-pump mode a satisfied zone
holds its base opening and trims only above setpoint, so the ground floor may still run
somewhat warm. See section 8.

**Room spread is not a fault.** A wind/sun-exposed upper floor and a heavy ground floor
can differ by more than 4 °C on a sunny day (22.7 °C vs 27.3 °C, both correct, seen
2026-10-04). The old 4 °C spread gate blocked the whole house temperature, so Asgard and
Odin froze on the last value. Spread is now advisory (`quality.spread_warn`, `spread_c`);
only implausible readings are left out — outside 5–35 °C or more than 8 °C from the
median of the fresh rooms (a broken probe: 0 / −127 / 85 °C). Those count as missing
area (`quality.implausible_rooms`, `implausible_room`), so the coverage gate still decides.

## 5. Wind, cold and floor capacity — slab charge

Odin's model contains wind only implicitly; it cannot see that one facade is exposed.
A timber upper floor can lose more heat in a cold storm than its floor heating can
deliver. If it has not been pre-heated — typically because a sunny afternoon kept the
room warm and its valve closed — it lags and cannot catch up while the storm lasts.

A setpoint offset alone does not fix this: a sun-warmed room is already above
setpoint + offset, so V6 trims the valve and the slab never charges. Air temperature says
nothing about stored heat.

**Model** (`slab_charge.h`, per room, per forecast hour, 24 h horizon):

- Floor capacity (EN 1264 simplified):
  `P_floor = area × (T_water_mean − T_setpoint) / (R_floor + R_surface)`
  with `R_floor` from V6 floor data (covering + slab above pipes), `R_surface ≈ 0.093`,
  `T_water_mean = 30 °C`.
- Hourly loss at setpoint:
  `P_loss = UA × (T_set − T_out) × (1 + k_wind × exposure × align × v/10) − solar`
  - `align` = best cos(wind direction − wall normal) over the room's exterior walls
  - `k_wind = 0.6` (+60 % for a fully exposed facade at 10 m/s head-on)
  - `solar = solar_gain × irradiance × area × 0.10`
- **Episode**: the first contiguous hours where `P_loss > P_floor`; deficit energy
  `E = Σ (P_loss − P_floor)`.
- **Store**: `ΔT = E / C_slab`, capped at the house max boost (default 1.5 °C).
  `insufficient = true` when even the cap cannot cover `E`.
- **Start**: walking back from the episode, accumulate spare floor power
  `P_floor − P_loss` until the store is covered; start that many hours before, plus a 1 h
  margin.

**Action while the charge window is open** (from start to the end of the episode):
- the room's setpoint offset is raised to at least `ΔT`, and
- per-zone absorb is armed on that room's V6 loops (re-armed every 30 min, disarmed when
  the window closes), so the valve stays open even though the room is warm.

Reported per room in Touch `/forecast` → `decisions[].charge`
(`episode, now, insufficient, store_c, deficit_kwh, floor_capacity_w, peak_loss_w,
start_in_h, episode_in_h, end_in_h`).

**When `insufficient`**: the slab cannot store the whole deficit (thin screed, long
storm). Pre-charging helps, but the floor also needs a **higher supply temperature**
during the storm. The house demand (section 7 route) keeps the heat source on; raising the flow
temperature itself is the flow-trim path (section 7), which is in SHADOW by default.

**Calibration**: `k_wind`, `T_water_mean`, solar aperture and the default slab mass are
constants today. They should be learned per room from outcomes (undershoot during an
episode → raise `k_wind`/lead), the way preload gain and lead are already learned.

## 6. Sun

Sun is relief only: it lowers the computed loss and the preload. There is **no solar
hold-off**: a room below setpoint in the morning is heated normally even if strong sun is
forecast, and nothing sends a negative offset. With slow floor heating this can give a
mid-morning overshoot in sun-facing rooms. A solar hold-off (small negative offset while
within the comfort band and strong sun is due within the room's lead) is a candidate
(section 8). Note the interaction: sun is also what can leave the timber floor uncharged
before an evening storm — slab charge (section 5) is the counter to that.

## 7. Heat-source routes, Asgard and Odin

### Heat-source routes

Touch is independent of the heat source. The same house demand is translated into what
the source accepts:

| Source | Route (`demand.route`) | What Touch writes |
|---|---|---|
| Asgard + Odin 2.0 | `odin_schedule` | Honest house temperature; temporary lift of **Odin's comfort schedule** (below). The VT target is written **unlifted**. |
| Asgard + Odin, control off | `none` | Honest temperature only. Odin follows its own schedule. |
| Asgard without Odin (Auto-Adaptive) | `virtual_thermostat` | Honest temperature + demand-led VT target (section 4) + flow trim. |
| Generic, room-thermostat input | `generic` | `target_url_template`, `{value}` = demand-led target (°C). |
| Generic, return-temperature / weather curve | `generic` | `curve_offset_url_template`, `{value}` = parallel curve shift (°C) = `curve_gain × need`, capped at `curve_max_offset_c` (defaults 2.0 °C/°C, 5 °C). `need` = max(held lag, charge store, house below target). |
| Generic, on/off input (relay, OpenTherm CH enable) | `generic` | `heat_request_url_template`, `{value}` = 1/0. On while demand is held or the house is > 0.2 °C below target. |

Most boilers and many heat pumps without a central room sensor regulate on measured
return temperature or a weather-compensation curve; they cannot use a room target, which
is why the curve shift and the heat request exist. Generic levers are re-sent on change
and every 10 min (`push_generic_levers_`).

### Why Odin needs its own route

Odin 2.0 is an MPC: every hour (hh:59) it builds a 48 h plan from prices, weather, the
current room temperature and **its own comfort schedule** (`sched_ui`, blocks
`{h, sp, min, max}` → band `[sp+min, sp+max]`). Asgard must be in *Heat Flow Temperature*
mode; Auto-Adaptive then follows Odin's kW. Asgard's virtual-thermostat target is **not**
an input to that plan. Lifting it (the old route) did nothing useful while Odin drove the
pump. Odin learns heat loss from heat produced vs. indoor–outdoor ΔT and the time constant
from how the room drifts while the pump is off — both from the room temperature Asgard
forwards. Heat that does not show up as room response (a buffer, a faked temperature)
corrupts that learning.

So Touch asks Odin for heat the MPC way: it **shifts Odin's comfort band up** for the
hours concerned, and Odin decides *when* to buy that heat.

### Odin comfort lift (`odin_comfort.h`, opt-in)

Enable with `POST /heat-source/control {"odin_enabled":1}` (Touch UI: Heat source › Asgard
› Odin 2.0 › *Lift Odin's comfort schedule*). Needs the Odin address (`odin_host`).

- **Amount.** Slab charge: `lift = Σ room shortfall (kWh) / Odin's thermal mass (kWh/K)`
  — Odin's own `used_thermal_mass` from `/api/debug`, because Odin translates the band
  shift into energy with that number. Example: 6 kWh / 11.7 kWh/K ≈ +0.5 °C. Room lag:
  the held lag, for the next 2 h (re-evaluated every cycle). Capped at `max_lift_c`
  (default 1.5 °C); below 0.3 °C nothing is written.
- **When.** Slab charge: from the charge start to the end of the episode (local wall-clock
  hours). Odin is free to heat earlier if that is cheaper.
- **How.** Odin has no dated exceptions. Touch keeps the user's profile, writes a lifted
  copy (`POST /dashboard/set {"key":"sched_ui","value":"<json string>"}`), triggers
  `POST /api/solve`, and writes the user's profile back when the lift ends. At most one
  lift rewrite per 20 min. Profiles are compared as hourly bands, so Odin's own
  re-formatting is not mistaken for an edit.
- **Ownership.** If the schedule on Odin differs from both the user's profile and what
  Touch wrote, the user edited it in Odin's UI: Touch adopts it as the new baseline and
  **yields** — it does not lift again until the current need has passed. User profile,
  written profile and lift are persisted (NVS), so a Touch reboot can still restore.
  Disabling the control restores the user's profile.
- **Caveat.** Because the profile repeats daily, the lifted hours also apply to the same
  hours tomorrow until Touch restores the profile; Odin may plan a little for that
  phantom block. It is gone at the next restore.

### Asgard ↔ Odin link health

Touch reads Asgard's `ODIN Forwarder Active` binary sensor and Odin's `/dashboard/state`
(`telemetry_ts`, `room_z1_current`) every 5 min and reports
`link.status`: `ok | forwarder_off | telemetry_stale (> 15 min) | no_room_temperature |
odin_unreachable`. After 10 min in a bad state it logs a warning ("Odin is not learning").
Motivating incident (2026-10-04): after an Asgard reboot its MQTT client kept timing out
(esp-tls 0x8006) for 13 h; Odin ran on frozen telemetry with `room=nan` and generic
physics defaults until Asgard was rebooted again.

With MQTT enabled (subscribe-only, Heat source › Odin 2.0 › MQTT), the age of the pump
telemetry on the broker is a second freshness signal; the fresher of the two is used.
MQTT is an accelerator, never a dependency — the incident above was itself an MQTT client
that stopped connecting.

**Touch ↔ V6 transport** stays HTTP: commands need confirmation, read-back and expiry,
and V6 must fall back to local control when the lease lapses. To cut polling cost Touch
asks `GET /api/v1/summary?since=<rev>` every 15 s and fetches `/overview` + `/zones` only
when V6's control fingerprint changed, or at least every 60 s. ESP-NOW was rejected (it
shares the radio with Wi-Fi and V6's BLE room sensors); an MQTT broker in Touch was
rejected (a Touch reboot must never interrupt the heat pump or the V6s).

`/api/physics` on Odin 2.0 returns `values{raw-hl-tm, raw-heat, raw-run, …}`; Touch only
feeds Odin's τ / heat loss into its own model once Odin has heating data
(`raw-heat > 0 && raw-run > 0`, reported as `odin.learned`).

### Asgard writes

Touch → Asgard:
- `temperature_feedback`: the measured weighted house temperature (gated on coverage;
  Odin learns from it — never manipulated). Weighting is **UA** by default (falls back to
  area for rooms without UA), so a leaky floor counts by its real heat loss. The
  definition does not change at runtime.
- Virtual thermostat target: the house target — demand-led only on the
  `virtual_thermostat` route. **Keep "Sync comfort target" on with Odin**: the target is
  then sent unlifted as Asgard's backup. While Asgard reports `ODIN Forwarder Takeover`
  = ON, Odin drives the pump and the target is idle; when Odin stops (takeover OFF),
  Auto-Adaptive regulates to it. With sync off, Asgard would fall back to its own
  setpoint. The UI states this role explicitly ("Comfort target role").
- Setpoint bias (flow trim): RAISE when any room saturates, LOWER after sustained
  headroom, ±1.0 °C in 0.1 °C / 30 min steps. Default mode is **SHADOW** (computed, not
  written) and it fails closed when the DHW/legionella/defrost entities are not configured.

Odin → Touch: plan ingestion (heat windows → Odin absorb arm, preload timing bias,
trim freeze during heat-off hours) and read-only physics for comparison.

**Double counting**: Odin plans whole-house heat from weather; Touch adds room-level wind
preload and slab charge. Because Odin does not model facade wind exposure, Touch's
wind/cold terms add what Odin cannot see. Touch's preload timing bias in Odin heat
windows (+0.5 °C cap) is the one place where both push the same direction without
reconciliation.

## 8. Open items

0. **Odin comfort lift in the field**: verify on a live Odin that a lift is planned as
   expected and that restore always happens (watch `/heat-source/control`).
1. **Active throttling of the satisfied manifold** while another catches up (negative
   offsets / lower base opening on the warm floor).
2. **Solar hold-off** within the comfort band.
3. **Learn** slab-charge constants (`k_wind`, water temperature, slab mass) per room.
4. **Flow trim** out of SHADOW once the freeze inputs are configured; use `insufficient`
   episodes as a RAISE trigger.
5. **Odin timing bias** reconciliation with Touch preload.
6. **Wind/solar ownership**: V6 stores `wind_exposure` / `solar_gain` per zone but does
   not use them; Touch's room values are the ones that act. Remove from V6 config or
   define them as standalone fallback.
7. **Notifications** when a room is `insufficient`, a lease is refused, or a charge
   window could not be armed.
