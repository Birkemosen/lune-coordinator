# Lune Touch API v1

The cross-product v1 envelope, compatibility rules, and fixtures are in
[`shared/contracts/lune_api_v1.md`](../../../shared/contracts/lune_api_v1.md).

<!-- toc -->
**[Reads](#reads)**

- [`GET /strategy`](#get-strategy)
- [`GET /rooms`](#get-rooms)
- [`GET /heat-source`](#get-heat-source)
- [`GET /events`](#get-events)
- [`GET /commands`](#get-commands)
- [`GET /zones`](#get-zones)

**[Writes](#writes)**

- [`POST /zones/{room_id}/room`](#post-zonesroom_idroom)
- [`POST /nodes`](#post-nodes)
- [`POST /nodes/{node_id}/trust` (compatibility)](#post-nodesnode_idtrust-compatibility)
- [`POST /nodes/{node_id}/profile`](#post-nodesnode_idprofile)
- [`POST /nodes/{node_id}/remove`](#post-nodesnode_idremove)
- [`POST /nodes/scan`](#post-nodesscan)
- [`POST /zones/{room_id}`](#post-zonesroom_id)
- [`POST /zones/{room_id}/comfort`](#post-zonesroom_idcomfort)
- [`POST /zones/{room_id}/schedule`](#post-zonesroom_idschedule)
- [`POST /zones/{room_id}/forecast-profile`](#post-zonesroom_idforecast-profile)
- [`POST /zones/{room_id}/setpoint-command`](#post-zonesroom_idsetpoint-command)
- [`POST /forecast/settings`](#post-forecastsettings)
- [`POST /forecast/estimate-location`](#post-forecastestimate-location)
- [`POST /weather/settings`](#post-weathersettings)
- [`POST /settings`](#post-settings)
- [`POST /display/wake`](#post-displaywake)
- [`POST /heat-source/settings`](#post-heat-sourcesettings)
- [`POST /rooms`](#post-rooms)
- [`POST /rooms/{room_id}/groups`](#post-roomsroom_idgroups)
- [`POST /rooms/{room_id}/sensor`](#post-roomsroom_idsensor)
- [`POST /rooms/{room_id}/remove`](#post-roomsroom_idremove)
- [`GET /circulation`](#get-circulation)
- [`POST /circulation/settings`](#post-circulationsettings)
- [`POST /circulation/refresh`](#post-circulationrefresh)
- [`GET /heat-source/control`](#get-heat-sourcecontrol)
- [`GET /plan`](#get-plan)
- [`POST /heat-source/control`](#post-heat-sourcecontrol)
- [`POST /heat-source/push`](#post-heat-sourcepush)
- [`POST /heat-source/test-read`](#post-heat-sourcetest-read)
- [`POST /heat-source/test-push`](#post-heat-sourcetest-push)
- [`GET /prices`](#get-prices)
- [`GET /prices/zone-defaults/{zone}`](#get-priceszone-defaultszone)
- [`POST /prices/settings`](#post-pricessettings)
- [`POST /prices/push`](#post-pricespush)
- [`POST /forecast/fetch`](#post-forecastfetch)
- [`POST /zones/{room_id}/motor-action`](#post-zonesroom_idmotor-action)
- [`POST /recovery/reset-registry`](#post-recoveryreset-registry)
<!-- /toc -->

The embedded browser dashboard and external commissioning tools use:

```text
/api/lune-touch/v1
```

The API returns CORS headers on success and error responses and answers
`OPTIONS` preflight requests with `GET, POST, OPTIONS` plus `Content-Type`, so
browser-based commissioning tools can call the Touch directly on the local LAN.

Read endpoints return the standard envelope:

```json
{
  "ok": true,
  "version": "v1",
  "ts_ms": 12345,
  "data": {}
}
```

## Reads

- `GET /overview`
- `GET /nodes`
- `GET /zones`
- `GET /rooms`
- `GET /strategy`
- `GET /forecast`
- `GET /commands`
- `GET /events`
- `GET /diagnostics`
- `GET /settings`
- `GET /heat-source`
- `GET /circulation`

`GET /overview` returns a compact house snapshot for the dashboard strip and climate
panel: `summary` plus `house_temp_c`, `house_target_c`, manifold coverage, calling
count, and authority. Full strategy detail remains on `GET /strategy`.

`GET /nodes` also reports `device_name` (the V6's own name or location, used when Touch has no name for the board) and `lease` (`granted` | `refused` | `none`): every trusted V6 holds the Touch lease — see [`house_balancing_and_weather.md`](house_balancing_and_weather.md). `poll` reports whether the V6 answers `GET /api/v1/summary` (`summary`: `yes` | `no` | `unknown`; older firmware is re-probed hourly) and how many full polls were `skipped` because its control fingerprint was unchanged.

`GET /nodes` reports both configured hostname/IP and runtime poll evidence:
`last_success_host` shows whether the latest successful poll used mDNS hostname
or fallback IP, while `last_failure` carries the latest poll failure reason.
Each node has a stable registry `id` plus a Touch-owned friendly `name`;
dashboards should display `name` first and keep `id` for confirmations,
automation, and diagnostics.
`trust` remains the compact enum value and `trust_label` is the stable human/tool
label: `paired` or `trusted`. `pairing_fingerprint` is the stored V6 identity
hint used to detect a different device answering on the same address.
Each node also includes a Touch-derived `health` block aggregated from the zones
automatically imported from that V6, so commissioning tools can show useful manifold status before the richer
V6 diagnostics poll is promoted:

`imported_zones` is the preferred field name. `mapped_zones` remains a
compatibility alias with the same value for existing clients.

```json
{
  "id": "v6-ground",
  "name": "Ground floor manifold",
  "reachable": true,
  "trust_label": "trusted",
  "health": {
    "imported_zones": 6,
    "mapped_zones": 6,
    "fresh_zones": 6,
    "stale_zones": 0,
    "calling_zones": 2,
    "avg_temp_c": 21.0,
    "avg_setpoint_c": 21.3
  },
  "runtime": {
    "active_zones": 6,
    "avg_valve_pct": 15.0,
    "flow_c": 33.8,
    "return_c": 30.6,
    "drivers_enabled": true,
    "motor_fault": false,
    "motor_current_ma": null
  }
}
```

### `GET /strategy`

Returns the read-only house strategy snapshot. `physical_house_temperature_c` is
the area-weighted (default) or UA-weighted physical room temperature that the heat
source receives; `weighted_temperature` remains a compatibility alias with the same
value. `house_comfort_target_c` / `house_target` is a separate comfort-intent advisory
written to the Asgard VT climate when target sync is enabled; it is never written to
the physical-temperature number entity. Temporary command offsets are not comfort intent.
`physical.quality` is `healthy`, `degraded`, or `no_coverage`. A value is healthy
only when fresh contributing weight meets `min_contributing_ua_ratio`
(default 75%), at least `min(min_contributing_rooms, expected_included_rooms)`
rooms contribute (so a one-room house can publish that room), room spread is
within the commissioning threshold, and every expected manifold contributes,
unless commissioning has explicitly enabled degraded-manifold operation.
`coverage_ratio`, `expected_manifolds`, `contributing_manifolds`,
`room_temp_spread_c`, `excluded_area_m2`, and `weighting` make that decision observable.

`weighting.house_tau_h` / `hl_tm_product_h` is the house time constant **τ in
hours** (Odin’s `hl_tm_product`). It is **not** thermal capacity in kWh/K.
Odin computes `TM = HL × τ`. Touch mirrors `τ = TM / HL` when both are known.
`thermal_mass_total_kwh_per_k` remains the capacity sum in kWh/K.
`weighting.basis` is `area` (default), `ua`, or `mixed`.

```json
{
  "physical": {
    "has_temperature": true,
    "temperature_c": 20.7,
    "contributing_rooms": 8,
    "coverage_ratio": 1.0,
    "quality": "healthy",
    "expected_manifolds": 2,
    "contributing_manifolds": 2,
    "room_temp_spread_c": 1.2,
    "excluded_area_m2": 12.0
  },
  "physical_house_temperature_c": 20.7,
  "house_comfort_target_c": 21.4,
  "weighting": {
    "basis": "area",
    "ua_total_w_per_k": 96.0,
    "thermal_mass_total_kwh_per_k": 22.7,
    "house_tau_h": 100.9,
    "hl_tm_product_h": 100.9,
    "weights_revision": 3,
    "weights_updated_at_ms": 123456
  },
  "comfort": {
    "average_c": 21.1,
    "demand_c": 0.6,
    "demand_zones": 3
  },
  "driver": {
    "room_id": "bath",
    "name": "Bath",
    "deficit_c": 1.4,
    "priority": 3
  },
  "schedule": {
    "time_valid": true,
    "active_zones": 2,
    "driver_room_id": "bath",
    "driver_name": "Bath",
    "driver_setpoint_c": 22.0,
    "driver_priority": 3
  },
  "house_target": {
    "available": true,
    "value_c": 21.4,
    "source": "mixed",
    "contributing_area_m2": 96.0
  },
  "weighted_temperature": {
    "available": true,
    "value_c": 20.7,
    "contributing_rooms": 8
  },
  "heat_source": { "enabled": true, "mode": "active" }
}
```

`physical` and `asgard_odin` remain temporary compatibility fields for older
clients. Prefer `physical_house_temperature_c`, `house_comfort_target_c`, and
`GET /heat-source`.

### `GET /rooms`

Returns the logical room table used for house-signal aggregation and onboarding.
Each room lists its V6 **group-primary** loops, the designated sensor group, and
aggregated heat-demand. Unassigned group primaries are excluded from the house
temperature until merged into a room.

```json
{
  "rooms": [
    {
      "room_id": "room-living",
      "name": "Living",
      "area_m2": 48,
      "include_in_house": true,
      "groups": [{ "node_id": "lune-v6-a1b2c3", "zone": 3 }],
      "sensor": { "node_id": "lune-v6-a1b2c3", "zone": 3 },
      "heat_demand": {
        "recommendation": "hold",
        "opening_ratio": 0.72,
        "saturated_s": 0,
        "any_fresh": true
      },
      "revision": 7
    }
  ],
  "unassigned": [
    { "node_id": "lune-v6-d4e5f6", "zone": 2, "name": "Hall" }
  ]
}
```

### `GET /heat-source`

Returns heat-source configuration, current physical house temperature, comfort-target
sync state, and the bias-trim block. The `type` field identifies the adapter family
(`asgard` or `generic_http`). By default Asgard uses the ESPHome number entity
format: `weighted_temperature_variable` is the entity name (new installs default to
`temperature_feedback_z1`); Touch URL-encodes it once for
`POST /number/<encoded-name>/set?value=<temperature>`,
then performs bounded readback attempts against `GET /number/<encoded-name>`.
`generic_http` uses explicit `write_url_template` / `read_url_template` instead.
Default push interval is 60 s (existing NVS values are kept).

Optional `write_url_template` and `read_url_template` override those paths when
non-empty. Supported placeholders are `{host}`, `{port}`, `{entity}` (URL-encoded
once), and `{value}` (write only, two decimal places). Leave both blank to keep
the ESPHome default. The `push`
object reports `status` as `sent`, `confirmed`, `mismatch`, `unreachable`, or `blocked`,
together with the write `http_status`, requested and confirmed values, confirmation age, and both cumulative and consecutive
failure counters. `blocked` means Touch held the publish locally (coverage degraded, disabled,
missing address, etc.) and never contacted Asgard — it does not increment the failure streak.
`unreachable` is reserved for an actual write/read transport failure. A `sent` status means the
POST succeeded but readback was unavailable; it is not treated as confirmation.

When `target_sync_enabled` is true and `climate_entity` is set, Touch writes the house
comfort target to `/climate/<entity>/set?target_temperature=` on change (0.1 °C
rounding) with read-back. Comfort target writes are **not** lease-gated; physical
temperature and bias writes are. The dashboard gates climate entity and declared-target
fields behind the comfort-target-sync toggle. Without Odin in control the written target is **demand-led**: it is lifted (max +1.5 °C, reported as `target_sync.demand_uplift_c`) while the room that lags most is behind (with a 30 min hold) or a slab-charge window is open, so a warm floor cannot stop the heat pump while another floor still needs heat. When Odin is in control (Odin comfort control enabled, or Asgard's `ODIN Forwarder Active` is ON) the target is written **unlifted** — Odin plans from its own comfort schedule; see `GET /heat-source/control`. `temperature_feedback` is never altered (Odin learns from it). See [`house_balancing_and_weather.md`](house_balancing_and_weather.md). `target_sync.write_age_s` is seconds
since `last_target_write_ms`; it is `0` when no comfort target has been written.

The `trim` block reports `mode` (`off` | `shadow` | `active`), current/target bias,
freeze state/reason, and last confirmed bias. Trim steps at most 0.1 °C per 30 min
within ±1.0 °C and freezes on DHW, Legionella, defrost, Odin heat-off, degraded
house quality, forecast offset on the critical zone, or unconfigured freeze inputs.
Bias writes are lease-gated and reseed from Asgard read-back after lease recovery.

`v6_control_mode` / `v6_control_mode_effective` mirror the optional lease
`control_mode` (`local` omits the field; `heat_pump` / `normal` are sent).

`send_preview` always exposes the current calculated value that would be sent when a
healthy physical temperature is available, or the simple average of fresh primary zone
sensors during commissioning before room geometry is configured. Its
`target_setpoint_c` uses the fresh V6-reported setpoint while available, then falls back
to the area-weighted house target or configured room-comfort average; it is the
heat-source setpoint corresponding to the preview temperature. It is diagnostic-only while coverage is degraded and does not
relax the safety gate for actual publishing.
Its `mode` is `active` when publishing is enabled and `disabled_preview` otherwise.

The `compatibility` object reports the adapter capabilities separately.
`physical_temperature` is READY when a host/entity is configured.
`target_sync` becomes READY when a climate entity is configured; otherwise it stays
`unsupported`. `operating_state` remains `unsupported`.

`GET /diagnostics` also exposes a compact `learning` summary derived from the
current-boot in-memory zone history, including zones with samples, total samples,
heat-call sample count, calling ratio, zones with temperature-rate estimates, and
warming / cooling counts. The history is intentionally runtime-only; learned thermal
coefficients are persisted separately in the zone registry. It also includes `ota` runtime data with the running partition
label/subtype, slot size, rollback state, and `pending_verify` flag so installers
can confirm OTA recovery assumptions.

Its `ownership` block is the operational boundary: ODIN owns heat-pump timing,
prices, whole-house weather/solar optimization, compressor behavior, and DHW; Touch
owns logical rooms, room distribution, schedules, house signals, and normal
heat-source integration; V6 owns safe local heating and explicit fallback only. Touch exposes
electricity prices as read-only and does not schedule the heat pump.

`GET /forecast` includes an `odin_plan` block from the optional Asgard ODIN plan
integration. It reports `enabled` (operator preference), `active` (preference plus
Asgard heat source ready), availability/freshness, the source's
`current_hour` and resolved `current_index`, validated current schedule bounds, current
price/planned heat, and the raw (not normalized) operation-mode code.
`applies_valve_commands` is always false in this prototype. The schema and safety boundary
are in [`odin_plan_ingestion.md`](odin_plan_ingestion.md).

Diagnostics also includes `command_results`, a compact command-ledger summary for
field troubleshooting:

```json
{
  "pending": 0,
  "accepted": 2,
  "rejected": 0,
  "failed": 0,
  "expired": 0,
  "blocked": 1,
  "blocked_stale": 0,
  "blocked_unreachable": 1,
  "blocked_untrusted": 0,
  "clamped": 1
}
```

`failed` means Touch attempted to send the command and did not get a successful
V6 response; `rejected` means V6 answered but declined the command. `blocked`
is the sum of the local stale, unreachable, and untrusted safety blocks.

The diagnostics `forecast` block mirrors the current weather-fetch state:

```json
{
  "status": "queued",
  "fetch_pending": true,
  "last_fetch_age_s": 420,
  "last_error": ""
}
```

`fetch_pending` is true after `POST /forecast/fetch` queues a manual background
fetch and false once the poll task has picked it up. The full `GET /forecast`
response exposes the same `fetch_pending` field alongside cache metadata, hourly
weather, preload decisions, and dispatch counts. Touch also auto-refreshes the
forecast when a valid location exists and no cache has been fetched, shortly
after boot when an NVS forecast cache was restored, and then roughly every hour
while WiFi is connected.

Diagnostics also includes a `network` block for the sidebar live-status
component:

```json
{
  "ip": "192.168.1.40",
  "ssid": "home-iot",
  "mac": "AA:BB:CC:DD:EE:10",
  "version": "v0.1.0-119",
  "esphome": "2026.9.1",
  "uptime_s": 69420
}
```

`ip` is the STA IPv4 address, or empty when Wi-Fi has no address yet. `mac` is
the device MAC, `version` is the Lune Touch project firmware version,
`esphome` is the ESPHome framework version, and `uptime_s` is seconds since
boot (`millis()/1000`).

Diagnostics includes a `commissioning` readiness block for field testing:

```json
{
  "paired_nodes": 1,
  "trusted_nodes": 1,
  "reachable_nodes": 1,
  "reachable_trusted_nodes": 1,
  "stale_nodes": 0,
  "trusted_stale_nodes": 0,
  "identity_missing_nodes": 0,
  "bound_zones": 6,
  "fresh_zones": 6,
  "stale_zones": 0,
  "ready_for_commands": true,
  "ready_for_forecast": true,
  "next_action": "ready",
  "blockers": []
}
```

`ready_for_commands` is only true when at least one trusted node is reachable
and fresh, trusted node identity is complete, and at least one automatically
imported V6 zone has fresh telemetry. Physical zone names, sensors, valve
outputs, and configuration remain owned by the respective V6 manifold.

`next_action` is one of `add_node`, `fix_node_poll`,
`verify_node_identity`, `trust_node`, `review_v6_zones`,
`wait_for_fresh_zone_poll`, `set_forecast_location`, or `ready`.
`blockers` is a compact, ordered list of the first concrete commissioning
reasons that keep commands or forecast from being fully ready. Each blocker has
`scope`, `target`, `reason`, and the recommended `action`, for example
`{"scope":"node","target":"v6-ground","reason":"not_trusted","action":"trust_node"}`.

### `GET /events`

Returns the volatile runtime event ring, newest first, for dashboard diagnostics
and field troubleshooting. The log is intentionally small and reboot-local; it
captures coordinator lifecycle, commissioning changes, V6 poll failures,
forecast fetch outcomes, command dispatch results, and recovery actions:

```json
{
  "events": [
    {
      "ts_ms": 12345,
      "level": "warn",
      "source": "poll",
      "message": "node 2 overview failed status=0"
    }
  ]
}
```

### `GET /commands`

Returns the persisted command ledger. `room_id`, `node_id`, and `loop_id` are
captured when the command is issued; `name` is the stable room-ID display alias.
Older records therefore retain their original target, timing, and outcome even
after the registry changes:

```json
{
  "commands": [
    {
      "request_id": "touch-12345",
      "source": "forecast",
      "reason": "wind preload",
      "room_id": "living",
      "name": "Living",
      "node_id": "v6-ground-a4d9",
      "loop_id": "loop-v6-ground-a4d9-02",
      "node_index": 0,
      "zone_index": 1,
      "requested_offset_c": 0.4,
      "accepted_offset_c": 0.3,
      "created_at_ms": 12000,
      "expires_at_ms": 2712000,
      "created_at_epoch_s": 1735734600,
      "expires_at_epoch_s": 1735737300,
      "boot_id": 1409833421,
      "result": "accepted",
      "clamp_applied": true
    }
  ]
}
```

Command `room_id`, `node_id`, and `loop_id` are immutable target identity captured at
issuance. Numeric indexes are non-authoritative execution-location metadata and are never
used to retarget history after a node removal, reorder, or room rebind.

`created_at_ms` and `expires_at_ms` remain same-boot diagnostic values. When the
wall clock is valid, Touch also records UTC `*_epoch_s` timestamps. Every transient
record has a random `boot_id`; on boot, prior-boot pending or accepted commands are
retained as history but changed to `expired` before they can affect command resolution.
If the wall clock is unavailable, the boot ID still provides the fail-closed boundary.

`GET /forecast` → `decisions[].charge` reports slab-charge planning per room: `episode`, `now`, `insufficient`, `store_c`, `deficit_kwh`, `floor_capacity_w`, `peak_loss_w`, `start_in_h`, `episode_in_h`, `end_in_h` (`house_balancing_and_weather.md` §5).

### `GET /zones`

Each zone includes Touch-owned room intent, the latest V6-applied live state, forecast
tuning, and a lightweight learned thermal model. Live temperature, valve state, and
sampling history are runtime-only and reset on reboot; learned coefficients survive
registry import/export:

```json
{
  "room_id": "living",
  "name": "Living",
  "name_source": "v6",
  "node_index": 0,
  "zone_index": 1,
  "temperature_c": 21.3,
  "setpoint_c": 21.0,
  "valve_pct": 47.0,
  "status": "heat",
  "fresh": true,
  "comfort": {
    "setpoint_c": 21.5,
    "bias_c": 0.2,
    "effective_setpoint_c": 21.7,
    "effective_source": "schedule",
    "schedule_active": true,
    "time_valid": true,
    "priority": 3
  },
  "resolver": {
    "base_setpoint_c": 21.7,
    "base_source": "schedule",
    "manual_offset_c": 0.5,
    "forecast_offset_c": 0.3,
    "learned_offset_c": 0.3,
    "command_offset_c": 0.5,
    "command_source": "manual",
    "target_setpoint_c": 22.5
  },
  "schedule": {
    "enabled": true,
    "day_mask": 127,
    "start_min": 360,
    "end_min": 1320,
    "setpoint_c": 21.0
  },
  "history": {
    "samples": 42,
    "calling_samples": 18,
    "avg_temp_c": 21.12,
    "min_temp_c": 20.58,
    "max_temp_c": 21.54,
    "last_delta_c_per_h": 0.36
  },
  "thermal_model": {
    "samples": 12,
    "heat_gain_c_per_h": 0.18,
    "cool_loss_c_per_h": 0.18,
    "confidence": 0.5
  },
  "forecast": {
    "thermal_lead_h": 4,
    "learned_thermal_lead_h": 6,
    "active_thermal_lead_h": 6,
    "preload_gain_scale": 1.0,
    "preload_lead_bias_h": 0
  }
}
```

`comfort_chart` is **not** embedded in `GET /zones` (payload size). Fetch one zone:

`GET /zones/{room_id}/comfort-chart` → `{ "room_id", "comfort_chart": { hours, expected_temp_c[], ... } }`

`forecast.preload_gain_scale` / `preload_lead_bias_h` are closed-loop learning
state (NVS `weather/preload_lrn`); lead bias only extends preload, never shortens
the configured thermal lead.

`name_source` is `generated`, `v6`, or the legacy value `touch`. During V6 polling,
Touch imports the V6 `/api/v1/zones` friendly `name` and physical zone identity.
Names and physical assignments are configured on V6 and refresh automatically;
the Touch dashboard does not expose rename or mapping controls.

`comfort.effective_setpoint_c` is resolved by Touch. When local time is valid and
the room schedule is active, `effective_source` is `schedule`; otherwise it is
`comfort`. The stored comfort bias is applied in both cases. `resolver` then
layers expiring manual/forecast command offsets and the small calculated
`learned_offset_c` into `target_setpoint_c` for diagnostics. The learned offset
is positive-only, requires fresh live temperature plus enough slow-zone thermal
samples, and does not create a persisted command by itself.

`thermal_model` contains Touch-learned, persisted coefficients derived from
fresh temperature history. `forecast.learned_thermal_lead_h` is derived from the
learned heat-gain rate after enough samples exist; `active_thermal_lead_h` is
the larger of the configured and learned lead. This can extend weather preload
for slow zones, but it never shortens the configured lead and still sends only
expiring V6-clamped
commands.

`resolver` is the read-only ordering view used by the dashboard for field
debugging. It starts with the persistent fallback base, uses Touch's current
comfort/schedule target while Touch is available, then chooses an active manual
dashboard offset over an active forecast distribution offset (never both).
`learned_offset_c` is added only for fresh, slow, under-heated zones with enough
thermal samples. `pre_v6_target_c` is clamped to Touch's dispatch envelope as
`target_setpoint_c`; V6 applies its independent final safety clamp. On a stale
Touch path, the resolver reports `base_source: "fallback"` and no temporary
command from a previous boot may remain active.

## Writes

Write endpoints accept `application/x-www-form-urlencoded` request bodies and retain
query-parameter compatibility for migration/debug tooling. This matches ESPHome's
ESP-IDF web-server POST parser, so request bodies reach the component handler without
an unsupported-content-type fallback. Invalid writes return HTTP `4xx` with the standard `ok:false`
envelope. Valid safety outcomes, such as a setpoint command blocked because its
V6 node is stale, are returned as successful write responses and recorded in the
command ledger.

The API handler still understands JSON when invoked by a transport that supplies a
parsed JSON document, but device and browser clients use URL-encoded forms. Query-
parameter compatibility remains available for diagnostic tooling.

### `POST /zones/{room_id}/room`

Atomically stores Touch-owned coordination metadata for an already imported V6
zone. Existing physical identity validation, geometry/inclusion, comfort, schedule, and weather profile must be
supplied with the current runtime `expected_revision`; an invalid field or stale
revision returns no partial change (`409 stale_revision` for a conflicting edit).
The response contains the saved room and its new revision. V6 local applied
state is reported separately and is never silently copied back into this record.

`total_area_m2` is geometry mirrored from V6 (SoT on the manifold). `physical_weight` is a dimensionless multiplier on
computed UA (default `1.0`), not a copy of area. Wind/solar floats are Touch's
truth per logical room after a one-shot seed; UI presets (`0.2`/`0.5`/`0.8` wind, etc.) are write shortcuts only.
`exterior_walls` is the V6-owned facade bitmask (`N=1|E=2|S=4|W=8`); Touch mirrors it and write-throughs edits via
`POST /api/v1/zones/{loop}/physics` when the node reports `physics_contract: 1`.
`north|west = 9`. Forecast-profile wind/solar are **not** pushed to V6.

```json
{
  "expected_revision": 4,
  "total_area_m2": 30,
  "physical_weight": 1.0,
  "include_in_house_temperature": true,
  "comfort_setpoint_c": 21.5,
  "comfort_bias_c": 0,
  "priority": 2,
  "schedule_enabled": true,
  "schedule_day_mask": 127,
  "schedule_start_min": 360,
  "schedule_end_min": 1320,
  "schedule_setpoint_c": 21,
  "exterior_walls": 9,
  "wind_exposure": 0.5,
  "solar_gain": 0.3,
  "thermal_lead_h": 4,
  "max_offset_c": 1.5
}
```

Physics ownership (normative: Room Physics Contract v1):

| Field | SoT | Notes |
| --- | --- | --- |
| `area_m2`, `exterior_walls`, floor, UA triad | V6 | Touch mirrors + write-through |
| `wind_exposure`, `solar_gain` | Touch logical room | Seed once from V6; never reimport |
| `u_base`, `u_wall`, `c_struct` | V6 defaults; Touch calibrates | `POST /api/v1/physics/house` |
| `ua_learned_*` | Touch learns, V6 stores | `POST …/ua-learned` |

### `POST /nodes`

```json
{
  "node_id": "v6-ground",
  "hostname": "lune-v6-ground.local",
  "ip": "192.168.1.51",
  "pairing_fingerprint": "hv6-aabbccddeeff"
}
```

New nodes are stored as `paired`. A paired node can be polled and commissioned,
but Touch will not dispatch setpoint/forecast commands until the V6 overview
reports that this installation and coordinator have been approved locally on
that manifold. Touch then mirrors the node as `trusted`; it does not grant itself
control. If a fingerprint was captured during probe/add, later polling must see
the same V6 fingerprint or Touch marks the node unreachable with
`overview identity_mismatch`.

### `POST /nodes/{node_id}/trust` (compatibility)

Legacy commissioning endpoint for promoting or demoting stored trust state.
The dashboard no longer exposes this action. A subsequent V6 overview poll
overwrites the local state from the V6-owned approval status.

```json
{
  "trust": "trusted",
  "confirm": "hv6-aabbccddeeff"
}
```

Accepted values are `paired` and `trusted`. Promotion to `trusted` requires a
stored `pairing_fingerprint` and `confirm` must exactly match that displayed
fingerprint. If the node was added without one, probe or re-add the candidate
after V6 identity is available. Use `POST /nodes/{node_id}/remove` to remove a
node from the registry instead of writing `unpaired`.

### `POST /nodes/{node_id}/profile`

Stores Touch-owned manifold display metadata without changing the stable node id,
hostname/IP, or pairing fingerprint:

```json
{
  "name": "Ground floor manifold"
}
```

### `POST /nodes/{node_id}/remove`

Removes a node from the coordinator registry and disables mapped rooms that point
at it. The request must confirm the exact node id:

```json
{
  "confirm": "v6-ground"
}
```

### `POST /nodes/scan`

Without a request body, queues a LAN HTTP probe of the local `/24` for
`/api/v1/overview` and returns immediately. mDNS is not used. The dashboard
waits for `poll_generation` to advance, then `GET /nodes/scan` for the result.
Registered nodes are included alongside new LAN candidates:

```json
{
  "scan": "lan",
  "discovery": "lan_probe",
  "poll_generation": 12,
  "poll_pending": true,
  "found": [
    {
      "id": "192-168-20-126",
      "hostname": "192.168.1.126",
      "ip": "192.168.1.126",
      "model": "lune-v6",
      "firmware": "v1.0.0-37",
      "pairing_fingerprint": "hv6-288485830cb4",
      "reachable": true,
      "stale": false,
      "source": "lan_probe"
    }
  ]
}
```

`GET /nodes/scan` returns the last scan document without starting a new sweep.

With `hostname` or `ip`, Touch probes that V6 candidate's
`/api/v1/overview` endpoint without storing it:

```json
{
  "hostname": "lune-v6-ground.local"
}
```

Response:

During migration from older V6 firmware, Touch first tries the resource-shaped
`/api/v1/zones` endpoint and then falls back to the legacy
`/api/v1/state` snapshot. Legacy state ingest automatically creates stable
provisional `v6N-zM` coordinator IDs and adopts V6 names when available. There is
no installer mapping step on Touch.

```json
{
  "scan": "probe",
  "discovery": "manual_probe",
  "found": [
    {
      "id": "lune-v6-ground-local",
      "hostname": "lune-v6-ground.local",
      "ip": "192.168.1.51",
      "model": "lune-v6",
      "firmware": "1.4.12",
      "reachable": true,
      "stale": false,
      "source": "manual_probe",
      "http_status": 200
    }
  ]
}
```

### `POST /zones/{room_id}`

Manual zone mapping is retired. The route returns HTTP `410` with
`zone_managed_on_v6`. Configure the zone name, sensor, and valve output on the
respective V6 manifold; Touch imports the result automatically.

### `POST /zones/{room_id}/comfort`

Writes the permanent comfort target to every V6 loop mapped to the logical room.
Touch stores the new intent only after every reachable, V6-approved loop accepts
the update. The response includes `synced_loops`; a failed multi-loop update also
reports `updated_loops` and `required_loops` so partial delivery is explicit:

```json
{
  "comfort_setpoint_c": 21.5,
  "comfort_bias_c": 0.2,
  "priority": 2
}
```

`comfort_bias_c` and `priority` remain Touch coordination metadata. V6 reports its
locally applied target separately on each poll; that telemetry never overwrites
the Touch-owned room intent.

### `POST /zones/{room_id}/schedule`

Stores the first Touch-owned schedule primitive: one daily comfort window per
room. The schedule is persisted and exposed in `GET /zones`; Touch uses it as
the effective-comfort base when local time is valid and the window is active.
Manual dashboard offsets and forecast preload offsets remain explicit command
paths layered on top of that base.

```json
{
  "enabled": 1,
  "day_mask": 127,
  "start_min": 360,
  "end_min": 1320,
  "setpoint_c": 21.0
}
```

`day_mask` uses bit 0 for Monday through bit 6 for Sunday. `start_min` and
`end_min` are local minutes after midnight, with `end_min` allowed to be `1440`.

### `POST /zones/{room_id}/forecast-profile`

Updates Touch-owned weather exposure (`wind_exposure`, `solar_gain`) on the
logical room and write-throughs `exterior_walls` to V6 when the node reports
`physics_contract: 1`. Wind/solar are **not** pushed to V6 (and are not
reimported on poll after the one-shot seed).

```json
{
  "exterior_walls": 5,
  "wind_exposure": 0.8,
  "solar_gain": 0.2,
  "thermal_lead_h": 8,
  "max_offset_c": 1.25
}
```

`exterior_walls` is a bitmask for north/east/south/west walls (`1|2|4|8`).
Example `5` = north|south; `9` = north|west. Same semantics as
`POST /zones/{room_id}/room` — never a wall count.
`wind_exposure` and `solar_gain` are clamped to `0..1`, and `thermal_lead_h` to
`1..24`. `max_offset_c` is retained as imported V6 legacy metadata and advanced
diagnostics context, but normal Touch forecast decisions use the house-level
`weather.max_boost_c` cap. Saving the profile recomputes current forecast
decisions and persists the mirrored zone registry.

### `POST /zones/{room_id}/setpoint-command`

A room command has one `command_id` and a child result for every required loop.
The room result is `accepted` only when every loop accepts. If a node is stale,
unreachable, or rejects its child, the result is `partial` (or `failed`) and
`partial_application` is true; successful child commands are retained rather
than rolled back unsafely across independent V6 nodes.

```json
{
  "offset_c": 0.5,
  "ttl_s": 2700,
  "reason": "dashboard quick boost"
}
```

Touch resolves the room mapping, sends an expiring V6 command, and stores the
accepted/clamped result in the command ledger. Dashboard commands are blocked
before send when the mapped V6 node is stale, unreachable, or not trusted; the
ledger result is `blocked_stale`, `blocked_unreachable`, or `blocked_untrusted`
in that case. If Touch attempts the send but cannot get a usable V6 response,
the ledger result is `failed`; if V6 responds and declines the command, the
ledger result is `rejected`.

### `POST /forecast/settings`

```json
{
  "latitude": 55.6761,
  "longitude": 12.5683,
  "source": "manual"
}
```

Saving a valid location clears the old cache and makes the next coordinator poll
eligible for an automatic forecast fetch. `0,0` is treated as unset and is not a
valid fetch location. `POST /forecast/fetch` can still be used to queue an
immediate manual refresh; it returns `queued` while the poll task performs the
HTTPS request and command dispatch in the background.

### `POST /forecast/estimate-location`

Asks Touch to estimate a coarse forecast location from its own public IP
(HTTPS geo lookup on-device). Used by the HTTP dashboard because browsers cannot
call HTTPS geo APIs from a plain `http://` LAN page (mixed content).

```json
{
  "result": "ok",
  "source": "network",
  "latitude": 55.676100,
  "longitude": 12.568300,
  "city": "Copenhagen",
  "country": "Denmark"
}
```

On failure the write result is rejected with an `error` such as
`network_offline`, `lookup_failed`, or `coordinates_unavailable`. The estimate
only fills the UI fields; the operator still saves via `POST /forecast/settings`.

### `POST /weather/settings`

Stores house-level weather preload settings owned by Touch:

```json
{
  "max_boost_c": 1.5
}
```

`max_boost_c` is clamped to `0..5` and caps forecast preload offsets before
Touch sends expiring commands to V6. If no Touch value has been stored yet,
first V6 import seeds this from legacy V6 zone `max_offset_c` values using a
conservative minimum aggregate. V6 still applies its local per-zone command
clamps, absolute setpoint limits, expiry, and safety validation.

### `POST /settings`

Stores coordinator-owned display and install profile fields. On first boot Touch automatically
creates and persists `install_id`, a unique coordinator ID, and a random authentication key;
the identity is mirrored to the dedicated `touchreg` NVS partition so reboot and OTA updates do
not rotate V6 trust. After a successful registry migration, Touch removes the obsolete registry
namespace from default NVS to prevent identity writes from failing due to NVS pressure. Normal
clients do not submit or display those values. Explicit identity fields remain accepted
for service migration and recovery:

```json
{
  "name": "Lune Touch",
  "install_id": "house-main",
  "site_label": "Birkemosen",
  "install_mode": "commissioning",
  "display_idle_timeout_s": 60
}
```

All fields are optional, but at least one must be present. `install_mode` is one
of `commissioning`, `active`, or `service`. `display_idle_timeout_s` is one of
`60`, `120`, `300`, or `0` (always on) and controls how long the local Touch
panel stays lit without a touch. `GET /settings` returns the same coordinator
block, the Touch-owned `weather.max_boost_c` cap, and
`display.idle_timeout_s`.

### `POST /display/wake`

Turns the local Touch panel backlight on and resets the display idle timer.
Routine hydronic updates never wake the panel; only a physical touch or this
explicit request does. On Lune Mini (no display) the call is accepted and is a
no-op.

Empty body. Response:

```json
{"result":"accepted"}
```

Home Assistant example (presence → wake):

```yaml
rest_command:
  lune_touch_wake:
    url: "http://lune-touch.local/api/lune-touch/v1/display/wake"
    method: POST

automation:
  - alias: Wake Lune Touch on room entry
    trigger:
      - platform: state
        entity_id: binary_sensor.office_occupancy
        to: "on"
    action:
      - service: rest_command.lune_touch_wake
```

### `POST /heat-source/settings`

Optional text fields (`odin_host`, `mqtt_host`, `mqtt_username`, `mqtt_password`,
`mqtt_topic_prefix`, `mqtt_hp_id`, `bias_entity`, `dhw_entity`, `legionella_entity`,
`defrost_entity`) are **unchanged when absent** and set (also to empty) when present.
`mqtt_password` is never returned; `GET /odin/mqtt` reports `password_set`.

Stores the direct heat-source connection. All configuration fields should be
sent together by management clients:

```json
{
  "type": "asgard",
  "enabled": 1,
  "host": "asgard.local",
  "port": 80,
  "weighted_temperature_variable": "temperature_feedback_z1",
  "push_interval_s": 60,
  "write_url_template": "",
  "read_url_template": "",
  "climate_entity": "virtual_thermostat",
  "target_sync_enabled": 1,
  "odin_plan_enabled": 1,
  "bias_entity": "setpoint_bias",
  "dhw_entity": "",
  "legionella_entity": "",
  "defrost_entity": "",
  "trim_mode": "shadow",
  "v6_control_mode": "heat_pump",
  "house_weighting": "area",
  "declared_target_c": 21.0
}
```

`type` selects the adapter family. Supported values are `asgard` (ESPHome number /
climate / bias entities) and `generic_http` (custom write/read URL templates).
Other values are rejected. Existing installs without a stored type load as `asgard`.
`generic_http` requires non-empty `write_url_template` (must include `{value}`) and
`read_url_template`; comfort-target sync, ODIN plan ingestion, and bias trim are
Asgard-only and are cleared when switching to generic HTTP.
`odin_plan_enabled` (0/1) turns optional ODIN plan polling on or off. It only takes
effect when heat-source type is `asgard`, the heat source is enabled, and a host is
configured; host/port for the plan fetch always follow the heat-source connection
for v1. Optional fields for the Odin 2.0 transition:

| Field | Meaning |
| --- | --- |
| `absorb_arm_enabled` | 0/1 — V6 absorb-arm client |
| `arm_energy_cost_modulation` | 0/1 — v2 arms on `energy_cost`/`modulation` (default 1) |
| `plan_source` | `auto` \| `v1` \| `v2` |
| `odin_host` / `odin_port` | Separate Odin 2.x HTTP host (forecast/physics) |
| `mqtt_enabled` / `mqtt_host` / `mqtt_port` / `mqtt_username` / `mqtt_password` / `mqtt_topic_prefix` / `mqtt_hp_id` | Read-only MQTT to Odin’s broker. Password is write-only (NVS); GET responses expose `password_set` only |

With an Odin address set, the plan is read from Odin 2.x itself (`GET /dashboard/odin`,
72 slots = yesterday/today/tomorrow, today at index 24, no `current_hour`; past slots may
be null and only now→end must be complete; `source: "odin2_dashboard"`). Asgard no
longer serves `/dashboard/odin` (404). Fixture:
`tests/odin_plan/fixtures/odin2_dashboard_odin_2.0.0-23.json`.

`GET /odin/mqtt` and `GET /odin/physics` expose MQTT stream ages and Odin physics (`odin.learned` is false while Odin 2.0 still plans with generic defaults; Touch then ignores its values)
beside Touch UA/τ estimates. Neither returns the MQTT password.

v2 runtime stays disabled until `LUNE_ODIN_FORECAST_V2_ENABLED` is compiled in after
a real `/api/forecast` fixture test passes.

`host` accepts a hostname or IPv4 address. Variable names accept printable ASCII.
Push interval is 5–3600 seconds. By default Touch builds ESPHome number-entity
URLs from host, port, and variable. Optional `write_url_template` /
`read_url_template` (max 191 characters) replace those when non-empty; send an
empty string to clear a previously stored template. Write templates must include
`{value}`. Placeholders: `{host}`, `{port}`, `{entity}`, `{value}`. For example,
an Asgard installation could use `asgard.local` with `temperature_feedback_z1`
as the entity name and leave the URL templates blank.

`trim_mode` is `off`, `shadow`, or `active` (default `shadow`). `v6_control_mode`
is `local` (omit from lease), `heat_pump`, or `normal`. `house_weighting` is
`area` (default) or `ua`. Unconfigured freeze entities keep trim frozen
(fail-safe).

### `POST /rooms`

Creates an empty logical room. Body: `{ "name": "Living" }`. Returns the new
`room_id` and `revision`.

### `POST /rooms/{room_id}/groups`

Moves a V6 group-primary loop into the room. Body:

```json
{ "node_id": "lune-v6-a1b2c3", "zone": 3, "revision": 7 }
```

The group leaves any previous room. Secondary group members cannot be moved.

### `POST /rooms/{room_id}/sensor`

Sets the room temperature sensor to one of the room's group-primary loops.
Body: `{ "node_id", "zone", "revision" }`.

### `POST /rooms/{room_id}/remove`

Deletes the room when it has no groups. Returns conflict if groups remain.

### `GET /circulation`

Returns the read-only circulation-pump telemetry polled from a satellite
ESPHome Alpha2 Go node. Touch never commands the pump. Default sensor
entities are `pump_flow`, `pump_head_pressure`, and `pump_power`. Poll
defaults to **30 s when any zone is calling** and **300 s when idle**
(configurable 10–3600 s), with exponential backoff on fetch failures.
`status` is `unconfigured`, `disabled`, `pending`, `available`, `stale`, or
`unreachable`. Values older than 15 minutes are `stale`. `flow_m3h` is
volumetric flow; it is not V6 `flow_c` (manifold flow temperature).

When pump flow and manifold ΔT are available, Touch publishes
`thermal_kw = flow_m3h × (flow_c − return_c) × 1.163` with
`thermal_kw_source` of `measured` (single manifold) or `estimated`
(multi-manifold average ΔT). `hydraulic_authority` is a 0–1 advisory from
head/flow. Delivered energy is integrated into per-room
`delivered_kwh_today` for UA learning.

```json
{
  "enabled": true,
  "host": "alpha2go.local",
  "port": 80,
  "refresh_interval_s": 300,
  "flow_entity": "pump_flow",
  "head_entity": "pump_head_pressure",
  "power_entity": "pump_power",
  "reachable": true,
  "fresh": true,
  "status": "available",
  "http_status": 200,
  "age_s": 42,
  "flow_m3h": 0.84,
  "head_m": 3.1,
  "power_w": 22.4,
  "thermal_kw": 2.15,
  "thermal_kw_source": "measured",
  "hydraulic_authority": 0.72,
  "last_error": ""
}
```

`GET /heat-source` and `GET /diagnostics` embed the same object as
`circulation`.

### `POST /circulation/settings`

Stores the satellite pump node. All configuration fields should be sent
together by management clients:

```json
{
  "enabled": 1,
  "host": "alpha2go.local",
  "port": 80,
  "flow_entity": "pump_flow",
  "head_entity": "pump_head_pressure",
  "power_entity": "pump_power"
}
```

`host` accepts a hostname or IPv4 address. A successful save queues an
immediate poll; later polls stay at the five-minute interval.

### `POST /circulation/refresh`

Queues an immediate circulation poll on the coordinator task.

### `GET /heat-source/control`

House-demand routing, Odin 2.0 comfort control, Asgard→Odin link health and generic
heat-source levers. See [`house_balancing_and_weather.md`](house_balancing_and_weather.md) §7.

```json
{
  "demand": {"route": "odin_schedule", "held_c": 0.6, "target_uplift_c": 0.0},
  "odin": {
    "enabled": true, "max_lift_c": 1.5, "forwarder_entity": "ODIN Forwarder Active",
    "status": "lifted", "reason": "slab_charge", "last_action": "write_lift",
    "user_schedule_known": true, "yielded": false,
    "wanted":  {"active": true, "start_hour": 15, "hours": 3, "lift_c": 0.5, "energy_kwh": 6.0},
    "applied": {"active": true, "start_hour": 15, "hours": 3, "lift_c": 0.5},
    "thermal_mass_kwh_per_k": 11.66, "heat_loss_kw_per_k": 0.15,
    "last_write_age_s": 420, "last_http_status": 200, "last_error": ""
  },
  "link": {"status": "ok", "alarm": false, "bad_for_s": 0, "odin_reachable": true,
           "forwarder_known": true, "forwarder_active": true,
           "telemetry_age_s": 40, "odin_room_c": 22.4},
  "generic": {"applies": false, "target_url_template": "", "heat_request_url_template": "",
              "curve_offset_url_template": "", "curve_gain": 2.0, "curve_max_offset_c": 5.0,
              "last_target_c": null, "last_heat_request": null, "last_curve_offset_c": null,
              "status": "idle"}
}
```

- `demand.route`: `virtual_thermostat` | `odin_schedule` | `generic` | `none`.
- `odin.status`: `disabled` | `idle` | `watching` | `lifted` | `yielded` (user edited the
  schedule in Odin; Touch waits until the need has passed) | `rate_limited` |
  `write_failed` | `odin_unreachable` | `schedule_unreadable` | `clock_invalid`.
- `odin.reason`: `none` | `slab_charge` | `room_lag` | `slab_charge+room_lag`.
- `link.status`: `ok` | `forwarder_off` | `telemetry_stale` | `no_room_temperature` |
  `odin_unreachable` | `unknown`. `alarm` turns true after 10 min in a bad state.
- `link.telemetry_age_s` is the fresher of Odin's own `telemetry_ts` and, when MQTT is
  enabled and connected, the age of the pump telemetry stream on the broker
  (`link.mqtt_telemetry_age_s`, `link.source: "mqtt+http"`). MQTT is subscribe-only and
  never required: without it the link is judged over HTTP alone.
- Hours are local wall-clock hours (Odin's schedule hours).

`link.takeover` mirrors Asgard's `ODIN Forwarder Takeover` (`true` = Odin's commands
drive the pump now, `false` = Asgard's Auto-Adaptive regulates itself and uses the
virtual-thermostat target, `null` = unknown).

Asgard entities are addressed by **name** in current ESPHome web servers (defaults:
`Virtual Thermostat Input z1`, climate `Virtual Thermostat z1`); the old object ids
(`temperature_feedback_z1`, `virtual_thermostat`) answer 404.

### `GET /plan`

Touch's heating plan for the next 24 h (dashboard graph). Index 0 is the current local
hour.

```json
{
  "available": true, "clock": true, "start_hour": 14, "hours": 24,
  "house": {"target_c": 22.0, "temp_c": 21.8},
  "odin": {"available": true, "control": true,
           "heat_kw": [0, 0, 4.81, …], "band_min_c": [21.5, …], "band_max_c": [23.5, …],
           "expected_c": [23.8, …], "mode": [0, 0, 2, …],
           "lift_applied": false, "lift_c": [0, 0, 0.5, …]},
  "rooms": [
    {"room_id": "v62-z1", "name": "Josephine",
     "preload": {"from": 0, "to": 4, "offset_c": 0.4},
     "charge": {"from": 2, "to": 9, "store_c": 1.5, "insufficient": true}}
  ]
}
```

`odin.*` comes from the ingested Odin plan (`/forecast → odin_plan`), `lift_c` from the
Odin comfort lift (applied, else wanted). `preload` / `charge` are `null` when none is
planned; `from`/`to` are hours from now.

### `POST /heat-source/control`

All fields optional; absent = unchanged; an empty template string clears that lever.

| Field | Meaning |
|---|---|
| `odin_enabled` | `1`/`0` — lift Odin's comfort schedule. Turning it off restores the user's schedule. |
| `odin_max_lift_c` | 0.3–3.0 °C (default 1.5) |
| `forwarder_entity` | Asgard binary sensor for the Odin link (default `ODIN Forwarder Active`) |
| `target_url_template` | Generic: `{value}` = demand-led room target (°C) |
| `heat_request_url_template` | Generic: `{value}` = `1`/`0` |
| `curve_offset_url_template` | Generic: `{value}` = curve shift (°C) |
| `curve_gain` | 0–10 °C flow per °C room need (default 2.0) |
| `curve_max_offset_c` | 0–15 °C (default 5.0) |

Templates take `{host}`, `{port}` (the heat source's) and `{value}`; they are sent as an
empty `POST`. Response: `{"result":"saved","control":{…GET body…}}`.

### `POST /heat-source/push`

Queues an immediate weighted-temperature push on the coordinator task. The
alias `POST /heat-source/test` has the same behavior.

### `POST /heat-source/test-read`

Runs a synchronous diagnostic GET against the configured read URL (ESPHome
default or `read_url_template`). Returns `action`, `url`, `http_status`,
`value_c`, and `error`. Does not require publishing to be enabled.

### `POST /heat-source/test-push`

Runs a synchronous push attempt (same safety gates as a normal publish) and
returns `write_url`, `read_url`, `http_status`, confirmation `status`,
requested/confirmed values, and `error` for commissioning logs.

### `GET /prices`

Electricity price for Odin: settings, Odin's own price settings, push status and the
hourly breakdown (calculation currency per kWh) for the current local day and, once
published, tomorrow. The ENTSO-E token is never returned (`entsoe_token_set`,
`odin.current.token_set`).

```json
{"available":true,"enabled":true,"model":"touch","zone":"DK1","dk":true,
 "spot":{"source":"eds","fixed_eur":0.1000},"currency":"DKK","fx":7.4600,
 "grid":{"source":"datahub","gln":"5790000610976","code":"TNT1009",
         "schedule":[{"h":0,"v":0.077},{"h":6,"v":0.231},{"h":17,"v":0.692},{"h":21,"v":0.231}]},
 "system":{"source":"datahub","fixed":0.1150},
 "energy_tax":0.0080,"markup":0.0000,"vat_pct":25.00,"entsoe_token_set":false,"odin_host_set":true,
 "odin":{"mode":"dynamic","source":"energy_charts","fixed_price":0.2500,
         "current":{"known":true,"price_mode":"dynamic","price_source":"api","ec_bzn":"DK1",
                    "fixed_price":0.250000,"token_set":true,"age_s":120}},
 "status":{"state":"ok","pushes":true,"reason":"day_ahead","odin_source":"api","odin_write_pending":false,
           "last_fetch_age_s":120,"last_push_epoch":1791296100,"last_attempt_epoch":1791296100,
           "hours_pushed":48,"fx":7.473600,"spot_used":"eds","spot_fallback":false,
           "grid_from_cache":false,"system_from_cache":false,"last_error":""},
 "today":{"date":"2026-10-06","spot":[…24],"grid":[…24],"system":[…24],"total":[…24]},
 "tomorrow":null}
```

`model`: `odin` (Touch writes Odin's own price settings) | `touch` (Touch computes and
pushes). `status.state`: `disabled` | `odin` (Odin model, settings written) | `waiting` |
`running` | `ok` (today's array is in Odin) | `error` (`last_error`, e.g.
`spot_ec_http_429`, `spot_entsoe_http_401`, `spot_entsoe_no_token`, `spot_incomplete`,
`grid_no_records`, `system_…`, `grid_datahub_dk_only`, `odin_http_400`, `no_odin_host`,
`clock_invalid`). `spot_used`: `eds` | `energy_charts` | `entsoe` | `fixed`
(`spot_fallback` when the other source answered). `fx` is the rate used (the data's own
DKK/EUR for Energi Data Service in DKK, 1 for EUR). `odin.current` is read from Odin's
`/dashboard/state` (every ~5 min while an Odin host is set). `today`/`tomorrow` are
only filled in the Touch model.

### `GET /prices/zone-defaults/{zone}`

What "Apply zone defaults" fills in (nothing is saved): Odin's energy tax (converted to the
zone's currency) and VAT, the default currency and rate, and sources. Denmark: elafgift
0.008 DKK, 25 %, Energi Data Service + DataHub. Unknown zone → EUR, 0 tax, 21 % VAT.

```json
{"zone":"NL","known":true,"spot_source":"energy_charts","currency":"EUR","fx":1.0000,
 "energy_tax":0.1090,"vat_pct":21.00,"grid_source":"none","system_source":"fixed",
 "system_fixed":0.0000,"odin_tax_eur":0.1090,"odin_vat_pct":21.00}
```

### `POST /prices/settings`

Form or JSON; absent or empty fields are unchanged. The v1 names `area`,
`energinet_source`, `energinet_fixed_dkk`, `elafgift_dkk`, `markup_dkk` are still accepted.

| Field | Meaning |
|---|---|
| `enabled` | `1`/`0`. Off from the Touch model → Odin `price_source=energy_charts` |
| `model` | `odin` / `touch` |
| `zone` | Energy-Charts / Odin zone code (`DK1`, `NL`, `DE-LU`, `SE3`, `IT-North`, …) |
| `spot_source` | `eds` (DK only) / `energy_charts` / `entsoe` / `fixed` |
| `spot_fixed_eur` | −1…5 €/kWh |
| `currency`, `fx` | Calculation currency (`EUR`, `DKK`, `SEK`, `NOK`, `CHF`, `PLN`, `CZK`, `HUF`, `RON`, `BGN`, `GBP`, `RSD`) and its rate per € (a new currency without `fx` gets its default rate) |
| `grid_source` | `datahub` (DK only) / `schedule` / `none` |
| `grid_gln`, `grid_code` | DataHub GLN (13 digits) and ChargeTypeCode |
| `grid_schedule` | JSON string (or JSON array) `[{"h":0,"v":0.077},…]`, hours 0–23 unique |
| `system_source`, `system_fixed` | `datahub` (DK only, Energinet 40000 + 41000) / `fixed` |
| `energy_tax`, `markup`, `vat_pct` | per kWh in the calculation currency; VAT 0…50 % |
| `odin_mode`, `odin_source`, `odin_fixed_price` | Odin model: `dynamic`/`fixed`, `energy_charts`/`entsoe`, €/kWh |
| `entsoe_token` | Write-only. Stored for Touch's ENTSO-E fetch; in the Odin model also written to Odin. `clear_token=1` forgets it |

Saving with `enabled=1` (re)writes Odin's settings for the model: `price_source=api` (Touch
model) or `price_mode`, `price_source` + `ec_bzn` / `fixed_price` (+ `entsoe_token` when
entered) (Odin model). In the Touch model, `spot_source=eds` / `datahub` sources outside
DK1/DK2 are rejected with `datahub_dk_only`. Other errors: `invalid_zone`,
`invalid_currency`, `invalid_token`, `invalid_schedule`, `invalid_value`, ….

### `POST /prices/push`

Touch model: queues an immediate fetch + push. Odin model: re-writes Odin's price
settings now. `{"result":"queued"}`; rejected with `disabled` or `no_odin_host`. Follow
the outcome in `GET /prices`.

### `POST /forecast/fetch`

Fetches Open-Meteo, recomputes per-zone preload decisions, dispatches active
forecast commands only to fresh reachable V6 nodes, and reports dispatch counts.
`blocked_stale`, `blocked_unreachable`, and `blocked_untrusted` are counted as
skipped before send and are also recorded in the command ledger with source
`forecast`, target node/zone, requested offset, immediate expiry, and the matching
blocked result. `failed` means Touch attempted a command and did not get an
accepted response.

`GET /forecast` includes the cached hourly weather window for graphing:

```json
{
  "weather": {
    "max_boost_c": 1.5
  },
  "hours": [
    {
      "h": 0,
      "timestamp_s": 1735736400,
      "temp_c": 14.1,
      "wind_ms": 5.2,
      "wind_dir_deg": 261,
      "solar_wm2": 0
    }
  ],
  "cache": {
    "hours": 72,
    "fetch_epoch_s": 1735734600,
    "provider_timezone": "Europe/Copenhagen",
    "decision_start_index": 1,
    "restored": false
  },
  "decisions": [
    {
      "room_id": "living",
      "offset_c": 0.4,
      "peak_in_h": 8,
      "configured_thermal_lead_h": 4,
      "learned_thermal_lead_h": 9,
      "active_thermal_lead_h": 9
    }
  ]
}
```

`timestamp_s` is the Open-Meteo Unix timestamp for that exact hourly sample. Touch
uses the first sample at or after its current wall-clock time as the decision-window
origin, so `peak_in_h: 0` means the current or next forecast hour rather than the
midnight array entry. `cache.decision_start_index` identifies that origin in `hours`.

The hourly forecast cache is persisted in Touch NVS after a successful fetch with the
fetch epoch, provider timezone, and every hourly timestamp. After reboot, Touch restores
only a fresh (at most two hours old), clock-aligned cache; an expired cache, a missing
timezone, or malformed hourly timestamps is discarded safely. A restored cache reports
`status: "cached"` and `cache.restored: true` until the next successful live fetch or
location change.

### `POST /zones/{room_id}/motor-action`

Requests a bounded V6-local motor recovery action for the mapped room:

```json
{
  "action": "relearn",
  "confirm": "relearn"
}
```

Accepted actions are `reset_fault`, `reset_learned`, and `relearn`. `reset_learned`
and `relearn` require `confirm` to exactly match the action name. Touch resolves the
room mapping and only sends the corresponding V6-local command when the target node
is trusted, reachable, and fresh. Blocked outcomes are returned as successful write
responses with `result` set to `blocked_untrusted`, `blocked_unreachable`, or
`blocked_stale`; invalid actions, missing confirmations, and unmapped rooms return
`4xx`.

### `POST /recovery/reset-registry`

Clears the Touch-owned node registry, zone mappings, current in-memory learning
history, and persisted command ledger. Learned thermal coefficients are removed with
the zone registry. Forecast location settings are intentionally kept.
The request must include the confirmation token:

```json
{
  "confirm": "reset-registry"
}
```

Successful response:

```json
{
  "result": "reset",
  "registry": "cleared",
  "ledger": "cleared",
  "forecast_location": "kept"
}
```

This is a recovery/install workflow, not a normal operating command.

Forecast decisions include Touch-owned comfort intent so callers can distinguish
physical V6 state from optimizer intent:

```json
{
  "room_id": "living",
  "name": "Living",
  "node_index": 0,
  "zone_index": 1,
  "comfort_setpoint_c": 21.5,
  "priority": 3,
  "offset_c": 0.4,
  "peak_load": 1.8,
  "peak_in_h": 3,
  "active": true
}
```
