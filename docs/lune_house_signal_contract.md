# Lune House-Signal Contract

## Purpose

Lune Touch is the normal publisher of the house signal to Asgard. It must provide a
truthful physical temperature observation and a separately named comfort target. Demand,
comfort priority, forecast offsets, valve position, and heat-pump state must never alter
the physical temperature to make heating appear necessary. The current priority-weighted
Touch strategy is compatibility behavior that P2.2 must replace.

## Inputs and Eligibility

Touch aggregates logical rooms, not V6 zone slots or physical loops. Each room has a
stable `room_id`, `physical_weight_m2` (initially its total area), and
`include_in_house_temperature` (default true). A room can control one or more loops, but
it supplies no more than one selected room-temperature sample and one area weight.

A room contributes only if it is included, has finite positive physical weight, has a
finite fresh selected temperature, and has a consistent room/node/loop mapping. Its
designated room sensor supplies that selected temperature. Touch may report the selection
source but may not average copied loop values or substitute a setpoint for a missing
measurement.

An excluded room appears only in `excluded_area_m2`; it contributes to neither numerator
nor denominator. An included room with a stale, missing, or invalid measurement is omitted
from both sums and counted as missing area. This makes a partial-house result explicit.

## Aggregates

The physical house temperature is:

```text
physical_house_temperature_c =
  sum(room_temperature_c * room_physical_weight_m2) /
  sum(room_physical_weight_m2)
```

The sums include only contributing logical rooms. `priority`, target, schedule, demand
deficit, forecast offset, valve position, and heat-pump state are not terms in this
formula. Priority remains available for room-distribution decisions and diagnostics only.
If no room contributes, Touch has no physical temperature and must not publish a made-up
replacement.

The house comfort target is separate:

```text
house_comfort_target_c =
  sum(effective_room_target_c * room_physical_weight_m2) /
  sum(room_physical_weight_m2)
```

It uses the same included/contributing rooms and physical weights but each room's current
effective comfort target. A schedule may change this target without changing the physical
temperature. Temporary forecast or loop-distribution offsets do not change it unless a
future explicit contract classifies them as comfort intent.

## Quality

Every aggregate and Asgard publication includes:

```json
{
  "contributing_room_count": 2,
  "contributing_area_m2": 68.0,
  "eligible_area_m2": 78.0,
  "missing_area_m2": 10.0,
  "excluded_area_m2": 12.0,
  "oldest_contributing_sensor_age_s": 42,
  "coverage_ratio": 0.8718,
  "expected_manifolds": 2,
  "contributing_manifolds": 2,
  "status": "healthy"
}
```

`status` is `healthy` when configured coverage and expected-manifold conditions pass,
`degraded` when a usable value exists with declared quality loss, or `insufficient` when
there is no publishable observation or the quality gate fails after its bounded grace
period. P2.4 defines the initial coverage threshold and grace behavior. Missing area is
included non-contributing area; excluded area is never missing area.

## Asgard Boundary

The normal Touch adapter receives a versioned snapshot:

```json
{
  "installation_id": "01j5n8r5fppv6x7bfzp53h2w6v",
  "physical_house_temperature_c": 20.71,
  "house_comfort_target_c": 21.21,
  "quality": {"status": "healthy", "contributing_room_count": 2,
              "contributing_area_m2": 68.0, "missing_area_m2": 10.0,
              "oldest_contributing_sensor_age_s": 42}
}
```

Only `physical_house_temperature_c` is written to Asgard's virtual-thermostat input. The
comfort target uses a separately named target path or diagnostic field; it is not encoded
by modifying the physical value. Authority permission and Asgard transport confirmation
are reported separately from data quality.

## Examples

`room-living` is 48 m² with one fresh 21.0 °C sensor and controls loops 03 and 04.
`room-bedroom` is 20 m² at 20.0 °C. Living contributes once, so the physical value is:

```text
(21.0 * 48 + 20.0 * 20) / (48 + 20) = 20.71 °C
```

It is incorrect to count the two living loops as 96 m². If their effective targets are
21.5 °C and 20.5 °C respectively, the house target is
`(21.5 * 48 + 20.5 * 20) / 68 = 21.21 °C`.

If a 12 m² guest room at 19.0 °C is excluded, it changes neither numerator nor denominator:
the physical result remains 20.71 °C and `excluded_area_m2` rises by 12.0. Changing
living-room priority from 1 to 3 may change room-distribution order, but it must leave the
20.71 °C physical result unchanged.

## Compatibility and Tests

The existing `weighted_temperature` and `priority_weighted_house_temp` labels are not this
contract and must be removed or clearly deprecated when P2.2 changes the calculation. New
APIs expose physical temperature, house target, and quality separately. Tests must cover
the examples above, stale and excluded rooms, no valid rooms, and priority invariance.
