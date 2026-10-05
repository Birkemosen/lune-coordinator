# Lune Asgard Authority State Machine

## Purpose and Scope

This document defines the writer-authority protocol for the physical house-temperature
signal sent to Asgard's virtual thermostat. It is a safety and availability boundary,
not a room-control protocol: Lune V6 continues conservative local heating regardless of
this protocol's state.

The protocol is the target contract for the implementations introduced in subsequent
plan tasks. The current V6 `coordinator` dashboard toggle predates this contract and
must not be treated as a sufficient writer lease.

## Roles and Invariants

| Role | Authority |
| --- | --- |
| Lune Touch | Sole normal Asgard writer while it holds a current V6-A lease. |
| Lune V6-A | Fixed initial fallback leader and lease arbiter. It may write only in `V6_FALLBACK_ACTIVE`. |
| Lune V6-B | Local manifold controller and telemetry provider only. It never writes to Asgard in the initial design. |
| Asgard | Receives the physical house-temperature signal; it does not select the writer. |

The authority invariant is: a valid writer lease has one holder and one generation. A
writer must stop before its lease expires. V6-A must wait longer than Touch's maximum
lease lifetime before writing after it revokes or observes expiry of a Touch lease. An
ambiguous lease or conflicting observation is fail-closed: no device writes.

V6-A is deliberately the arbiter even while Touch is the normal writer. If V6-A cannot
be reached, Touch must eventually stop publishing rather than independently extending
its authority. This favors a truthful, single-writer signal over an unsafe partitioned
fallback; both V6 nodes still heat safely from their local control loops.

## Lease and Timing Contract

All durations are measured with a process-local monotonic clock. A lease is not
restored across boot: after a reboot, the rebooted participant is non-writer until a new
lease handshake completes. The later stable-ID and persistence work supplies the
installation, node, boot, and lease-generation identities used on the wire.

| Setting | Value | Meaning |
| --- | ---: | --- |
| Touch renewal cadence | 30 s | Touch renews before each third of its lease elapses. |
| Touch lease lifetime | 90 s | From Touch receiving V6-A's acknowledged grant. Touch stops at or before expiry. |
| V6-A fallback guard | 30 s | Extra time V6-A waits after its recorded Touch lease expiry. |
| Earliest automatic fallback | 120 s | Last acknowledged grant plus the 90 s lifetime and 30 s guard. |
| Touch recovery stability | 120 s | Continuous healthy communication and valid inputs before requesting handover. |
| Asgard request timeout | 3 s | Bound per request; a timeout never changes writer authority by itself. |

The grant contains `installation_id`, `writer_node_id`, `boot_id`, a monotonically
increasing `lease_generation`, the grant lifetime, and the V6-A authority endpoint
identity. Touch acknowledges the exact generation before it publishes. V6-A records the
acknowledgement before permitting `NORMAL_TOUCH`.

Touch stops writes immediately on explicit revocation, a generation mismatch, or a
detected conflict. It also stops when it cannot renew before the 90-second deadline.
V6-A never writes during a live Touch lease; it waits the full 30-second guard after its
recorded expiry before the fallback transition. This acknowledgement-plus-guard sequence
prevents a network partition from making both devices writers.

## States

| State | Writer permission | Meaning and behavior |
| --- | --- | --- |
| `NORMAL_TOUCH` | Touch only | Touch holds the current acknowledged lease and publishes its physical house signal. V6-A and V6-B do not write. |
| `TOUCH_DEGRADED` | Touch only | Touch still owns a live lease, but a required input, V6 poll, or Asgard request is degraded. It reports quality and retries bounded work; it does not fabricate temperature or hand authority to V6-A after one failed request. |
| `V6_FALLBACK_PENDING` | None | V6-A has no valid Touch renewal and waits through the Touch lease lifetime and guard. Touch must have stopped before this state can become active. |
| `V6_FALLBACK_ACTIVE` | V6-A only | V6-A publishes its conservative, real local/fresh-peer physical signal. It continues local control and uses bounded retry when Asgard is unavailable. |
| `TOUCH_RECOVERY_PENDING` | V6-A only | Touch has reconnected but has not yet demonstrated 120 seconds of stable operation. It is read-only to Asgard. |
| `NO_PUBLISHER` | None | No valid writer lease exists, V6-A is unavailable, or the installation is intentionally disabled. Local V6 heating remains active. |
| `AUTHORITY_CONFLICT` | None | Any two current writer claims, mismatched lease generation, or unexplained writer observation was detected. Both writers stop and retain diagnostic evidence until V6-A-mediated recovery or explicit operator resolution. |

## Transition Rules

| From | Trigger | To | Required action |
| --- | --- | --- | --- |
| `NO_PUBLISHER` | V6-A reachable; Touch requests and acknowledges a new grant | `NORMAL_TOUCH` | V6-A records the acknowledged generation; Touch begins publishing only after acknowledgement. |
| `NORMAL_TOUCH` | One or more degraded inputs or an Asgard timeout/non-2xx | `TOUCH_DEGRADED` | Touch retains authority until its lease ends, reports quality, and retries with bounded backoff. |
| `TOUCH_DEGRADED` | Healthy inputs and successful renewal | `NORMAL_TOUCH` | Continue with the same or a renewed generation; log recovery. |
| `NORMAL_TOUCH` or `TOUCH_DEGRADED` | V6-A has no timely renewal | `V6_FALLBACK_PENDING` | Stop granting Touch renewal; V6-A does not write yet. A single lost request is insufficient. |
| `V6_FALLBACK_PENDING` | Touch renews the still-current lease | `NORMAL_TOUCH` or `TOUCH_DEGRADED` | Cancel pending fallback and retain Touch as the only writer. |
| `V6_FALLBACK_PENDING` | 90-second lease plus 30-second guard expires | `V6_FALLBACK_ACTIVE` | V6-A logs the lease generation and begins fallback publishing. |
| `V6_FALLBACK_ACTIVE` | Touch is reachable and healthy for 120 s | `TOUCH_RECOVERY_PENDING` | Keep V6-A as sole writer while Touch proves stable; Touch remains read-only. |
| `TOUCH_RECOVERY_PENDING` | V6-A stops its writes, grants a new generation, and Touch acknowledges | `NORMAL_TOUCH` | The handover is ordered: V6-A stops first, then Touch publishes. |
| `TOUCH_RECOVERY_PENDING` | Instability, failed acknowledgement, or timeout | `V6_FALLBACK_ACTIVE` | Discard the recovery attempt and leave V6-A as writer. |
| Any | V6-A unavailable while Touch has a live lease | `NORMAL_TOUCH` or `TOUCH_DEGRADED` until expiry, then `NO_PUBLISHER` | Touch cannot renew and stops on expiry; V6-B does not take over. |
| Any | Conflicting writer/generation claim | `AUTHORITY_CONFLICT` | Both writers stop immediately; no automatic promotion occurs. |
| `AUTHORITY_CONFLICT` | Operator resolves cause and V6-A completes a new grant handshake | `NO_PUBLISHER`, then normal transition | Preserve conflict evidence; never silently resume an old lease. |

Asgard transport success is intentionally separate from writer authority. If Asgard is
unavailable, the currently authorised writer remains the only permitted sender but
records `unreachable` and retries with bounded backoff. It must not cause a second
writer to start. If the LAN is partitioned, the lease expiry and guard determine the
safe fail-closed outcome.

## Failure Behavior

| Failure | Required behavior |
| --- | --- |
| V6-B unavailable | Exclude its stale measurements according to the house-signal quality rules. V6-B never writes and its loss never promotes it. |
| V6-A unavailable | Touch may finish only its live lease, then stops. The state becomes `NO_PUBLISHER`; each V6 continues safe local heating. |
| Touch unavailable or rebooted | Its lease cannot be renewed. V6-A waits through `V6_FALLBACK_PENDING`, then may enter `V6_FALLBACK_ACTIVE`. A rebooted Touch starts read-only. |
| Asgard unavailable | The authorised writer records the failed HTTP outcome and retries boundedly. No authority handover is triggered by this failure alone. |
| Network unavailable or partitioned | No lease renewal can occur. Touch stops at expiry; V6-A waits its guard before fallback. If V6-A cannot reach Asgard, the state is still observable but no physical write succeeds. |
| Authority endpoint or record corrupt | Enter `AUTHORITY_CONFLICT` or `NO_PUBLISHER`, never infer ownership from a mutable array index, stale uptime, or a dashboard role toggle. |

## Observability and Operator Actions

Every transition must emit a structured log and diagnostics event containing the prior
and next state, timestamp, installation/node/boot IDs, lease generation, trigger,
writer permission, remaining lease age, and Asgard transport result where applicable.
The dashboard must show the current state, permitted writer, last transition, lease age,
last successful and failed Asgard requests, and any conflict reason.

Only an authenticated, explicit maintenance action may disable publishing, revoke a
lease, clear an `AUTHORITY_CONFLICT`, or start a manual fallback. Manual fallback still
uses the same V6-A fence and guard; it never authorizes V6-B. No dashboard action may
make two writers active or bypass the ordered recovery handover.

## Implementation Boundary

This document does not add a runtime protocol by itself. The existing V6 direct bridge
and Touch advisory strategy must be changed in later tasks to use the lease endpoint,
generation fencing, diagnostics, persistence-safe boot handling, and authenticated
operator actions described here.
