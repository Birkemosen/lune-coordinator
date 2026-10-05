# Lune Touch Field Validation

This checklist captures the remaining work that cannot be honestly completed
without real hardware, network conditions, or a paired Lune V6. It is the bridge
from implemented plan to field debugging.

## Repository Gate

Run these from the repository root before flashing a candidate build:

```bash
make test-touch
make build-touch
make -C devices/lune-touch build-mini
```

The Touch build must pass the OTA slot-size check. Do not increase LVGL buffers,
dashboard bundle size, or HTTPS memory use after this point without re-running
the build and a display/WiFi soak.

## Phase 9 Staged Rollout Record

This is an operator-run field record. Do not mark a stage complete from a host test,
an HTTP response alone, or a dashboard screenshot. Capture the UTC interval, device
firmware hashes, room/loop IDs involved, diagnostics JSON, relevant V6/Touch logs, and
an explicit **go** or **no-go** decision for every stage. A no-go returns the system to
the previous safe stage; it must not be bypassed by changing a dashboard role toggle.

| Stage | Permitted behavior | Evidence to record | Go decision |
| --- | --- | --- | --- |
| 1. V6 local only | Touch off or disconnected; each V6 uses its own conservative local heating. No Asgard write from Touch or V6 fallback test. | Fresh local sensor status, valve movement/endstop faults, room temperature trend, and V6 diagnostics for every commissioned loop. | All local zones are safe, stable, and independently heat-capable; no stale sensor produces an unsafe command. |
| 2. Touch read-only comparison | Touch polls V6 and calculates house/room diagnostics, but sends no room commands and performs no Asgard write. | Side-by-side Touch calculated physical/target values and V6 observations, plus coverage/manifold quality. | Touch reports truthful values, degrades on incomplete coverage, and has no unexplained command/Asgard ledger entry. |
| 3. Shadow Asgard publishing | Generate and retain the exact candidate physical signal and readback expectation without sending it to Asgard. | Timestamped candidate value, source room coverage, encoded would-be URL, and simulated readback decision. | Candidate is physically derived, bounded, and agrees with independent operator calculation; any mismatch is no-go. |
| 4. Touch normal writer, learning/forecast off | Touch may hold the normal V6-A lease and write only the physical house signal. Forecast and learning modifiers remain disabled. | Lease generation/age, Touch write ledger, V6-A authority diagnostics, Asgard request/readback outcomes, and room temperatures. | One writer only; every successful write has a matching readback or declared bounded transport failure; no physical aggregation falsification. |
| 5. Intentional fallback/recovery | Under supervised maintenance, withdraw Touch renewal and verify the ordered V6-A fallback and Touch recovery. V6-B never writes. | Transition logs for expiry, 30 s guard, fallback, 120 s recovery stability, handover generation, and both writer ledgers. | No overlapping writers, V6-A is the only fallback writer, Touch is read-only until ordered handover. |
| 6. Learning shadow | Calculate learning/distribution suggestions but do not send their offsets. | Suggested versus baseline targets, confidence/sample counts, sensor freshness, and per-room expected effect. | Suggestions are bounded, attributable to stable loop IDs, and never alter physical house temperature aggregation. |
| 7. Bounded optimization room-by-room | Enable one logical room at a time, including every loop assigned to that room; retain rollback to Stage 4. | Requested/accepted/clamped V6 results, expiry, room temperature response, loop coverage, and impact on other rooms. | No partial multi-loop room success is hidden; each command is bounded, expires safely, and improves or at least does not degrade the stage baseline. |

Before each stage, run the repository gate above and archive the exact command output.
After a no-go, stop the rollout, preserve diagnostics, and return to the prior permitted
stage. ODIN remains advisory throughout: DHW, unavailable, and unknown/defrost context
must not manufacture a room command or alter physical temperature aggregation.

## Flash And Boot

1. On the first install of the dedicated Touch registry storage, connect USB and run
   `make install-registry-partition-touch PORT=/dev/cu.usbmodemXXXX`. This writes
   only the partition table and preserves the existing WiFi/default NVS data.
2. Flash `devices/lune-touch/configurations/lune-touch-7.yaml`.
3. Confirm the boot log reports `Using dedicated Touch registry NVS partition`.
4. Confirm the local display reaches the operating console and does not drift,
   smear, or redraw in a loop.
5. Confirm `/`, `/en/`, `/da/`, `/lune-ui.css`, `/ui.js`, and `/api/lune-touch/v1/diagnostics`
   are reachable from a browser on the local LAN.
6. Confirm `GET /api/lune-touch/v1/diagnostics` reports `ota.slot_size` and does
   not show `pending_verify` unexpectedly after a normal boot.

## Commissioning Flow

1. Open the web dashboard and check the Diagnostics view.
2. Follow `diagnostics.commissioning.next_action` rather than guessing the next
   step.
3. Scan the LAN for a V6 candidate, or add it by IP. mDNS is not used.
4. Confirm the candidate exposes a stable `pairing_fingerprint`.
5. Promote the node to `trusted` using the exact displayed confirmation token.
6. Confirm at least one configured V6 zone is imported automatically.
7. Wait for fresh zone telemetry. If no zone appears, inspect configuration on
   the respective V6 manifold; there is no separate Touch mapping step.
8. Confirm `ready_for_commands` becomes true before sending any command.

Expected blockers:

- `add_node` when no node is configured.
- `fix_node_poll` when the V6 cannot be reached.
- `verify_node_identity` when a node lacks a stored fingerprint.
- `trust_node` when a node is only paired.
- `review_v6_zones` when no configured V6 zone has been imported.
- `wait_for_fresh_zone_poll` when telemetry is stale.
- `set_forecast_location` when forecast location is missing.
- `ready` when commands and forecast are ready.

## Command Safety Flow

1. Send a small dashboard offset to a fresh, trusted V6 zone imported by Touch.
2. Confirm `GET /commands` records source, reason, requested offset, accepted
   offset, expiry, result, and clamp state.
3. Disconnect or block the V6 and confirm a new command is recorded as
   `blocked_unreachable` or `blocked_stale` rather than sent.
4. Demote a node to `paired` and confirm commands are `blocked_untrusted`.
5. Confirm V6 still clamps any accepted command locally.

## Forecast Flow

1. Save a non-zero forecast location through Settings or `POST /forecast/settings`.
2. Confirm `GET /forecast` changes to a queued/pending state and old cache is not
   presented as current for the new location.
3. Trigger `POST /forecast/fetch`.
4. Confirm `GET /forecast` exposes hourly weather, preload decisions, dispatch
   counts, and any fetch error.
5. Confirm stale, unreachable, and untrusted V6 nodes are skipped before send and
   recorded in the command ledger.

## UI Soak

Keep WiFi, dashboard polling, Touch display, and forecast fetch enabled together.
During the soak:

- Header and content must remain fixed on the display.
- The web dashboard must remain reachable.
- Diagnostics must keep updating without API crashes.
- Free heap and PSRAM must not trend downward after repeated dashboard refreshes.
- Forecast fetch failures must show as status/errors, not reboot loops.

## Production Decisions After Field Validation

Do not finalize these until the checklist above has been run on real hardware:

- stronger Touch-to-V6 authentication beyond identity fingerprint plus trust
  confirmation
- production display stack, enclosure, power, and service connector
- whether any richer UI runtime is worth the memory and OTA budget
- long-term weather/history storage beyond the current compact cache and learned
  coefficients
