# Upstream issue draft: versioned ODIN plan API

Copy the title and body below into a new GitHub issue:

**Repo:** https://github.com/gekkekoe/esphome-ecodan-hp/issues/new

Touch does **not** auto-create this issue. Until the contract lands, Touch stays
fail-closed on the approved `GET /dashboard/odin` snapshot.

---

**Title:** `Add versioned GET /api/plan for ODIN schedule consumers`

**Body:**

## Summary

Lune Touch (and similar room-distribution coordinators) currently poll the
approved Asgard JSON snapshot at `GET /dashboard/odin`. The payload is useful,
but it has no API version, plan revision, generation/expiry timestamps, or
explicit availability. Consumers must fail closed on shape errors while the
route can change without notice.

Please expose a stable, machine-oriented plan endpoint (suggested:
`GET /api/plan`) that ODIN/Asgard can evolve under an explicit contract.

## Motivation

- Touch uses the plan **read-only** for advisory timing (e.g. bias weather
  preload toward hours ODIN already plans to heat). It must **not** become a
  second optimizer and must not invent demand from a stale or malformed plan.
- Without version/revision/expiry, fail-closed logic cannot distinguish
  “unsupported schema” from “transient garbage,” and silent field renames
  break field installations.
- `/dashboard/odin` is fine as a debug/dashboard source; integrators need an
  API-shaped route with documented auth and freshness.

## Proposed contract (minimum)

### Endpoint

- `GET /api/plan` (path bikeshed OK; please keep it out of HTML dashboard space)
- Response: `application/json`
- Auth: document the model (none / basic / token / local-only). Same rules as
  other Asgard API surfaces are fine if stated explicitly.
- Rate limits: document expected poll cadence (Touch today: ≤ every 5 minutes).

### Top-level metadata (required)

| Field | Type | Meaning |
| --- | --- | --- |
| `api_version` | string or int | Contract version; unknown → consumer ignores plan |
| `revision` | string or int | Monotonic or unique id for this solved plan |
| `generated_at` | ISO-8601 or unix | When this plan was produced |
| `expires_at` | ISO-8601 or unix | After this, consumers must treat plan as unavailable |
| `timezone` | string | IANA or explicit offset used for hour indexing |
| `availability` | enum/string | e.g. `available` / `unavailable` / `solving` / `fault` — **explicit**, not inferred from NaNs |

Optional but valuable:

- `odin_status` / operating context: space heat, DHW, defrost, legionella, fault
  (today these cannot be inferred safely from raw `operation_mode` alone)
- Link or echo of solve identity for support

### Horizon payload (compatible with current `/dashboard/odin`)

Keep a day-aligned horizon (Touch validates **72** hourly points today) including
at least:

- schedule bounds: min / base / max target °C per hour
- `prices` per hour
- `heat_production` (planned kW) per hour
- expected temperatures if already computed
- raw `operation_mode` codes **plus** a documented mapping (or stable string enums)
- `today_start_index` + `current_hour` (or equivalent) so consumers can resolve
  “now” without guessing local clock skew

Unknown fields must be ignorable (forward compatible).

### Failure / freshness semantics

- While solving: prefer HTTP **503** *or* `availability=solving` with empty/ignored
  horizon — document which.
- Malformed / unsupported `api_version`: consumer fails closed (no valve commands,
  no manufactured room demand).
- Expired (`expires_at` in the past): ignore plan.
- Please do **not** require scraping HTML.

### Optional companion (not for steady-state control)

Read access to `/api/debug` (latest_request + latest_response) is useful for
commissioning. Note if it is mutex-gated and returns 503 during solve — fine for
debugging, not for control loops.

## Out of scope for this ask

- Touch writing setpoints or compressor commands to Asgard/ODIN
- ODIN learning room UA / wind routing (that stays on the room coordinator)

## References

- Current consumer docs: Lune Touch `odin_plan_ingestion.md` (fail-closed on
  `/dashboard/odin`)
- Current route observed in Asgard dashboard: `GET /dashboard/odin`
  (`components/asgard_dashboard/asgard_dashboard.cpp`)

## Acceptance

- [ ] `GET /api/plan` returns JSON with required metadata fields above
- [ ] Documented auth + poll guidance
- [ ] Explicit `availability` (or equivalent) so consumers need not infer from NaN
- [ ] Horizon arrays length and semantics documented; forward-compatible unknown fields
- [ ] `/dashboard/odin` may remain; API route is the supported integration surface
