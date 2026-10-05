# Optional ODIN Plan Ingestion

## Current State

ODIN plan ingestion is an **optional Asgard-only** integration. Enable it from Heat
Source when type is Asgard (`odin_plan_enabled`). Host and port always follow the
Asgard heat-source connection; if Asgard is not the heat source (or publishing is
disabled), the plan is inactive even if the preference remains stored.

When active, Touch reads `http://{asgard-host}:{port}/dashboard/odin`. Although its
route name is `dashboard`, the observed response is `application/json`, not rendered
HTML or a browser scrape. It is an Asgard-published ODIN planning snapshot with a
72-point day-aligned window, schedule bounds, prices, weather/solar, expected
temperatures, heat production, and raw operation-mode codes.
`today_start_index + current_hour` identifies the current point.

Touch polls this source at most every five minutes and validates all 72 elements
of the schedule, expected-temperature, price, heat-production, and raw mode arrays before
exposing only the current plan point. A fetch or validation failure leaves core control
unchanged and becomes stale after 20 minutes.

Upstream issue draft for a versioned `GET /api/plan`:
[`upstream_odin_api_plan_issue.md`](upstream_odin_api_plan_issue.md).

## Policy extension — advisory use of the plan (B7)

Ownership stays: **Odin decides when and how much heat in total; Touch decides where.**

Touch may use a fresh, validated plan for exactly three mechanisms:

### 1. Weather preload timing bias (amount-capped)

Bias the *timing* of already weather-driven preload toward hours Odin plans to run
space heat. This is **not** quantity-neutral: raising room offsets in those hours can
open valves and increase delivered energy at the same flow temperature.

- Cap total weather+bias offset contribution per hour (`preload_bias_offset_cap_c`,
  default 0.5 °C).
- Log `bias_window_planned_kwh` (from plan `heat_production`) vs
  `bias_window_delivered_kwh` (from measured `thermal_kw` integration) per bias window.
- Ledger source: `odin_timing_bias` (distinct from weather preload and absorb-arm).

### 2. Absorb-arm (feature-flagged)

Arm V6’s absorptions window before planned heat hours via a bounded, expiring,
V6-clamped command. This is **not** room demand and must not issue valve setpoints
from the plan.

- Arm only when `heat_production[i] > 0` **and** `operation_mode[i] == 2` (space
  heat). **Never** arm for `operation_mode == 1` (DHW — FTC redirects to the tank).
- `idx_now = today_start_index + current_hour` (never hardcode 24). History
  arrays are only valid for indices `≤ idx_now`.
- Client runs when `absorb_arm_enabled` is true.
- Until V6 A5 effect: V6 route returns **501** with the normal v1 error envelope.
  Touch still exercises auth, ledger post (`source=absorb_arm`), TTL parsing, and the
  failure path. Shared JSON fixtures live with V6.
- Ledger source: `absorb_arm` — kept separate from `odin_timing_bias` in the GUI.
- Staleness: prefer `last_run` fingerprint (`execution_ms` + `evaluated_nodes` +
  `total_cost`) over array hash; unchanged across an hour boundary ⇒ plan stale.

### 3. Flow-trim freeze on heat-off

When the current plan hour resolves to `OFF` (`current_hour_is_heat_off`), Touch
freezes Auto-Adaptive Setpoint Bias trim (no step timer advance, no bias write).
An unavailable or stale plan does **not** freeze on this input alone; other freeze
gates (DHW, Legionella, defrost, house quality, forecast offset) still apply.
See [`lune_whole_house_flow_temperature.md`](lune_whole_house_flow_temperature.md).

### Explicit non-goals

- No valve commands manufactured from the plan.
- No change to physical temperature aggregation from plan data.
- No second optimizer: prices and compressor timing remain Odin’s.

## Future Read-Only Contract

The currently authorized response does not contain a version, generation timestamp,
or a self-describing raw operation-mode mapping. The installed optimizer source currently
maps raw `1` to DHW, `2` to heat, `3` to cooling, and `255` to unavailable; all other
values (including NaN) become off. Its frost-protect and legionella alternatives are
commented out in that mapper, even though the upstream enum declares their values. Upstream
tracks defrost through a separate `status_defrost` binary sensor, which is not present in
the authorized debug response. Consequently this response cannot identify defrost, an
active legionella cycle, or a fault; unavailable is not a fault diagnosis. The integration
therefore fails closed on shape or numeric errors and must not use `operation_mode` to
infer those contexts until ODIN supplies them explicitly. Any future expansion needs:

- API version and plan revision;
- generation and expiry timestamps with declared timezone;
- explicit freshness / availability status;
- declared operating context (for example space heating, DHW, defrost, fault);
- optional timing intent as advisory data, never an actuator command;
- a documented authentication and rate-limit model.

An unavailable, stale, malformed, unsupported-version, or expired snapshot is ignored. It
must not manufacture room demand, trigger learning, alter V6 fallback behavior, or block
normal Touch operation.

## Commissioning Decision

The approved endpoint may be read only. ODIN remains the heat-pump optimizer; Touch may
distribute advisory timing and absorb-arm under the policy above; V6 provides safe local
heating plus explicit fallback. This explicit project decision permits this direct JSON
route; it does not permit HTML/dashboard scraping or undisclosed endpoints.

## Fail-closed drill (U0 — run before Asgard/Odin upgrade)

This is exactly what happens the moment Asgard upgrades away from `/dashboard/odin`.
Prove it on the current install **before** upgrading.

1. Enable ODIN plan and absorb-arm in Heat Source settings (`odin_plan_enabled=1`,
   `absorb_arm_enabled=1`). Confirm `/forecast` shows `odin_plan.available=true` and
   that absorb-arm ledger posts appear when a heat window is active.
2. Block plan fetch without touching z1 push: set heat-source host to a non-routable
   address (or a host that does not serve `/dashboard/odin`) and save. Keep publishing
   enabled if you want to prove z1 still works independently.
3. Within the documented freshness window (`ODIN_PLAN_STALE_MS` = **20 minutes**):
   - `odin_plan.fresh` becomes false / `available` falls after failed fetches
   - timing-bias stops (`apply_odin_timing_bias_` requires `available`)
   - absorb-arm stops issuing new arms; existing V6 TTLs expire → V6 local auto-detect
4. Expected GUI: Heat Source / Forecast ODIN block shows invalid/unreachable; Commands
   ledger stops gaining new `absorb_arm` / `odin_timing_bias` rows after the window.
5. Restore the real Asgard host when the drill is done.

Reference fixtures (schema-faithful baselines for U3 migration comparison):

- `tests/fixtures/odin_dashboard_v1.json` — `GET /dashboard/odin`
- `tests/fixtures/odin_debug_v1.json` — Odin 1.x `/api/debug` learned physics

Replace with live captures from the site when available; keep the filenames stable.

## Odin 2.0 dual-source (U1–U5)

`plan_source` config: `auto` | `v1` | `v2`. Runtime v2 is gated by
`LUNE_ODIN_FORECAST_V2_ENABLED` (default **0**) until a real `/api/forecast` capture
passes the indexing fixture test. Until then only `asgard_dashboard_v1` arms with the
legacy `heat_production > 0 && operation_mode == 2` rule.

v2 arms from `decision_reason` (see plan_source.h). DHW never arms. MQTT is read-only
on Odin’s broker; password is NVS-only and never returned by GET endpoints.

### U3 / U4 activation checklist

1. Replace `tests/fixtures/odin_forecast_v2.json` (+ MQTT/physics placeholders) with live
   captures after Odin/Asgard upgrade.
2. Write/enable the indexing fixture test; set `LUNE_ODIN_FORECAST_V2_ENABLED=1`.
3. Verify arm matrix: `thermal_buffer*` arms; `comfort_*` and `unknown` do not.
4. Parallel day: keep v1 fetch for comparison logging while only v2 steers; watch events
   for `plan_source_disagree` if added during field validation.
5. After ≥1 week stable v2: remove v1 `/dashboard/odin` adapter (keep fixtures).

