# Lune whole-house flow temperature

How Lune Touch, Lune V6, Asgard, and Odin keep the heat-pump feed temperature as
low and constant as the house tolerates.

V6 stays the safe local manifold node: it never writes the heat source. Touch
owns room identity, house aggregation, and Asgard writes. Odin only answers
Asgard's pull.

## Goal

Keep as much floor area as possible accepting heat, and give the heat-source
side a clear signal when the hardest room cannot keep up versus when there is
headroom. That lets Auto-Adaptive settle on the lowest constant feed that still
meets comfort.

## Roles

| Actor | Owns | Does not own |
| --- | --- | --- |
| **Lune V6** | Loop physics (`area_m2`, `exterior_walls`, floor slab/covering, `ua_prior`/`ua_effective`, groups within a manifold, `include_in_house_temperature` per group); persists `ua_learned_*` written by Touch; house `u_base`/`u_wall`/`c_struct` after calibration | Heat-source writes, logical rooms across manifolds, weather exposure |
| **Lune Touch** | Logical room ↔ V6-group mapping across manifolds, `wind_exposure`/`solar_gain`, house physical temperature, comfort target, Setpoint Bias trim, UA learning write-back, Odin house calibration | Motor safety, local clamps, splitting V6 groups |
| **Asgard** | Ecodan interface, virtual thermostat, Auto-Adaptive flow, Odin pull | House temperature truth |
| **Odin** | Hourly plan (heat off / DHW / Legionella / load ratio) via `/api/solve`; house HL/τ via `/api/physics` | Asgard entity writes |

```text
V6 nodes ──zones, temps, heat_demand──► Touch
Touch ──physical temp → VT input──────► Asgard
Touch ──comfort target → VT climate───► Asgard
Touch ──trim → Setpoint Bias──────────► Asgard
Asgard ──house data, /api/solve───────► Odin
Odin ──hourly plan────────────────────► Asgard
Asgard ──relay + flow temp────────────► Ecodan
Touch ──lease, control_mode, setpoints► V6 nodes
```

## Signals (one writer each)

### Physical house temperature → Virtual Thermostat Input z1

Touch writes the area-weighted physical house temperature every 60 s, unchanged,
per [`lune_house_signal_contract.md`](lune_house_signal_contract.md). Demand,
priority, forecast offsets, and valve state must never alter this value to
manufacture heat call.

### Comfort target → virtual-thermostat climate target

Touch writes the house comfort target to the Asgard virtual-thermostat climate
(`POST /climate/<vt>/set?target_temperature=`, rounded to 0.1 °C, only on
change, read back). This replaces a manual step on Asgard.

The climate uses `heat_overrun: 0.5` and drives relay R1/IN1, so this target
sets heat-pump on/off.

### Valve-saturation trim → Auto-Adaptive Setpoint Bias

Touch writes the Setpoint Bias (range −2.5…+2.5 on Asgard; Lune caps trim at
±1.0 °C). Rate: at most 0.1 °C per 30 minutes.

Frozen during: domestic hot water, Legionella, heat-off plan hours, defrost,
degraded house-signal quality, and while a forecast offset is active on the
critical zone.

Auto-Adaptive computes flow as `return_temp + target_delta_t` with
`room_target += setpoint_bias` applied only in the flow calculation — not to
the relay. The trim therefore raises or lowers feed without changing when the
heat pump switches on or off.

Odin never writes Asgard entities, so the bias remains free for Touch.

### Timing → Odin (unchanged)

Asgard pushes house data and pulls `/api/solve`. The plan returns a per-hour
load ratio and mode. No change in this design.

## V6 heat_demand summary

Each V6 publishes (never acts on) a post-min-flow summary over enabled,
calibrated sync-group primaries:

| Field | Meaning |
| --- | --- |
| `critical_zone` | Zone calling for heat with the highest opening relative to its max |
| `critical_opening_ratio` | That opening ÷ `max_opening_pct` |
| `saturated_s` | How long any demanding zone has stayed ≥ 90 % of its max |
| `demanding_zones` | Count of zones in DEMAND |
| `headroom` | No zone calling for heat and highest opening below `hp_base_pct` |
| `recommendation` | `raise` after 20 min saturated; `lower` after 30 min headroom; else `hold` |

Touch aggregates rooms by taking the maximum heat demand across a room's
groups, then drives Setpoint Bias from that.

## Control modes on V6

- **Normal** — proportional open below setpoint; close at setpoint; hysteresis
  latch until `setpoint − comfort_band`. Flow allocator unused.
- **Heat pump** — allocator on DEMAND; hold `hp_base_pct` when satisfied; soft
  trim down to `hp_trim_floor_pct` between setpoint and
  `setpoint + hp_overheat_margin_c`; close only past the margin (+ absorb band
  while pre-buffering).
- **Touch lease** — optional `control_mode` (`normal` / `heat_pump`) overrides
  the local setting while the lease is active. Absent field keeps local mode.
  Local clamps, min-flow, and endstops are unchanged.

## Touch room-to-zone mapping

V6 keeps local grouping (`sync_to_zone`) so baseline control works without
Touch. Touch owns logical rooms across nodes.

### What V6 exposes (`GET /api/v1/zones`)

- `node_id` (stable, MAC-based)
- `zone` (1–6), `name`, `enabled`
- `group_primary` (sync root) and `group_members`
- merged group temperature and freshness
- the zone's heat-demand state and opening ratio

### Room table (Touch-owned)

```json
{
  "room_id": "room-living",
  "name": "Living",
  "area_m2": 48,
  "include_in_house": true,
  "groups": [{"node_id": "lune-v6-a1b2c3", "zone": 3}],
  "sensor": {"node_id": "lune-v6-a1b2c3", "zone": 3}
}
```

Rules:

1. A V6 group belongs to exactly one room. Map by `group_primary`, never by
   secondary zones.
2. A room may span groups on several nodes; Touch sends the same room setpoint
   to each.
3. Room temperature is the designated sensor group's merged temperature — one
   sample per room.
4. Room heat demand is the maximum over its groups.
5. Unmapped groups show as "unassigned" and are excluded from the house
   temperature.

Onboarding: Touch auto-proposes one room per discovered group, named after the
V6 zone. The user merges groups into rooms in the Touch UI.

Weather exposure is a property of the Touch room. It stays in Touch and reaches
V6 only as clamped, expiring setpoint offsets.

## Verified Asgard behaviour

From [esphome-ecodan-hp](https://github.com/gekkekoe/esphome-ecodan-hp):

- **Room temperature input** (`Virtual Thermostat Input z1` /
  `temperature_feedback_z1`) is a template `number` with `restore_value: true`
  and `initial_value: 20.0`. There is **no staleness timeout**. A value that
  stops updating is held indefinitely, including across Asgard reboots. The
  source falls back to the Ecodan room sensor only when the value is NaN, which
  never happens with a restored number.
- **Virtual thermostat** — ESPHome `thermostat` with `heat_overrun: 0.5`;
  drives relay R1/IN1.
- **Auto-Adaptive** — flow = `return_temp + target_delta_t`; Setpoint Bias
  applied only in the flow calculation.
- **Odin** — pull model; never writes Asgard entities.

## Stale input: V6-A fallback remains required

Asgard holds a stale VT input forever. A frozen "warm" value leaves the relay
off (house under-heats). A frozen "cold" value keeps the heat pump running;
only V6 valve cutoffs limit overshoot.

Keep the existing V6-A fallback writer and its lease guard. Slim what V6-A does
in fallback:

1. Publish the area-weighted average of its own fresh groups plus any fresh
   peer groups.
2. Write Setpoint Bias = 0 once when it takes over, so a frozen trim cannot
   linger.
3. Never write the comfort target.

**V6 firmware dependency (out of scope for Touch):** the behaviour above lives in
V6-A firmware. Touch only reseeds bias from Asgard read-back on lease recovery
and surfaces `last_fallback_value_c` / authority state in the dashboard.
Upstream Asgard VT staleness issue draft:
[`upstream_asgard_vt_staleness_issue.md`](upstream_asgard_vt_staleness_issue.md).

Recommend an upstream Asgard change (issue/PR): a staleness timeout on
`Virtual Thermostat Input` that falls back to the DS18x20 or MRC source. Once
that exists, the V6-A writer path can be removed.

## Upgrade note (V6)

Before OTA to a firmware that re-versions `ControlConfig` / `ZoneConfig` /
`SystemConfig` / `BalancingConfig` (and drops Forecast/Helios NVS sections):

1. Export a settings backup from the V6 dashboard.
2. Flash / OTA.
3. Import the backup. Key-based JSON survives layout changes; removed fields
   are ignored.

Heating mode lives under Settings → Hydraulics. Default is Heat pump.
