# Birkemosen Product Architecture

Birkemosen is the umbrella brand for intelligent, durable products for home,
garden, and everyday infrastructure. Products should feel calm, architectural, useful,
and technically robust: less gadget, more long-lived tool.

## Brand Structure

| Level | Names | Scope |
|---|---|---|
| Umbrella | Birkemosen | Shared product family and design language |
| Categories | Home, Garden, Objects | Home infrastructure, garden automation, physical design objects |
| Product lines | Lune, Spire, Brim | Short, warm, international names with a Nordic tone |
| Models | Lune V6, Lune Touch, Lune Mini | Specific products inside each line |

## Lune Product Line

Lune covers hydronic heating, indoor comfort, sensing, and whole-house heat coordination.

| Product | Role |
|---|---|
| Lune V6 | 6-zone manifold / valve controller for underfloor heating |
| Lune Touch | 7-inch house coordinator with local touch UI |
| Lune Mini | Headless house coordinator for installations without a screen |

The former public product name **HeatValve-6** is replaced by **Lune V6**. The V6
host dashboard HTTP API is `/api/v1` (no product segment; older `/api/lv6/v1` and
`/api/hv6/v1` paths are gone). Some internal identifiers (NVS namespace `"hv6"`,
legacy C++ type/log tags) may still say `hv6` to avoid wiping persisted config or a
large firmware rename; new user-facing text, docs, and product decisions should use
Lune V6 / `lv6`.

## Architecture Principles

Lune V6 is a robust local manifold node. It controls valve motors, endstops, motor
faults, local temperature inputs (probe, on-manifold BLE, HTTP EXTERNAL), minimum flow,
and conservative fail-safe regulation. Zone ↔ `sensor_id` binding for remote temps lives
only on V6. There is no MQTT room-temp path on V6, Touch, or Mini.

Lune Touch and Lune Mini are whole-house coordinators. They aggregate one or more Lune V6
nodes, learn house thermal response, and optimize comfort using history, weather, wind,
solar gain, flow temperature, and zone behavior. They send setpoints / authority only —
they must not ingest or forward room temperatures.

**Deferred:** Lune Sense and Lune Gateway may add dedicated sensing / BLE coverage later;
any mesh stays between those products and still terminates as HTTP EXTERNAL into V6.

Lune V6 must never depend on a coordinator for safe baseline operation. If the coordinator
is offline, local heating must continue conservatively.

The coordinator may override or bias zones through an explicit command path, but Lune V6
must validate, clamp, expire, and report every command locally.

Asgard / Odin integration should treat physical temperature and comfort demand as
separate signals. Asgard / Odin should receive a physical, strategically selected
temperature signal, while the Lune coordinator owns whole-house comfort, zone priority,
and preload decisions.

Forecast preload, wind exposure, solar gain, adaptive balancing, and learned house
physics should gradually move toward Lune Touch / Mini. Lune V6 should simplify toward
local I/O, motor control, zone safety, and a clear snapshot / command API.

## UI And Documentation Rules

Use **Birkemosen** for the umbrella brand and **Lune V6** for the current
manifold controller in user-facing surfaces.

Keep the UI dense, operational, and calm. Prioritize overview, reliability, zone balance,
diagnostics, and explicit Apply / Discard workflows.

Avoid large refactors that only serve branding. Functional safety, testability, and
verifiable control behavior have higher priority than perfect internal naming.
