# Lune End-to-End Simulation

`make -C devices/lune-touch test-simulation` constructs two trusted six-loop V6
manifolds and uses the same host-testable Touch model, V6-A authority lease, ODIN
operation boundary, and forecast preload model used by firmware. Each scenario prints
the expected `authority`, `room_control`, and `diagnostic` triple and fails on any
mismatch.

The simulator is deliberately network-free. It does not contact Asgard, ODIN, weather
providers, or physical devices; it checks the intended local failure behavior before
the staged field validation in P9.3.

Covered scenarios are Touch reboot, V6-A reboot, V6-B disconnect, Asgard unavailable,
internet/weather unavailable, DHW/unknown-defrost, cold wind, solar relief, all rooms
satisfied, multi-loop partial failure, and conflicting authority.
