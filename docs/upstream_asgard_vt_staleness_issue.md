# Upstream issue draft: Virtual Thermostat Input staleness timeout

Copy the title and body below into a new GitHub issue:

**Repo:** https://github.com/gekkekoe/esphome-ecodan-hp/issues/new

Touch does **not** auto-create this issue. Until Asgard adds a staleness
timeout, Touch + V6-A keep the lease/fallback writer fence described in
[`lune_asgard_authority_state_machine.md`](lune_asgard_authority_state_machine.md)
and [`lune_whole_house_flow_temperature.md`](lune_whole_house_flow_temperature.md).

---

**Title:** `Add staleness timeout on Virtual Thermostat Input (temperature_feedback_z1)`

**Body:**

## Summary

`Virtual Thermostat Input z1` / `temperature_feedback_z1` is a template `number`
with `restore_value: true` and `initial_value: 20.0`. There is **no staleness
timeout**. A value that stops updating is held indefinitely, including across
Asgard reboots. The source falls back to the Ecodan room sensor only when the
value is NaN, which never happens with a restored number.

Please add a bounded staleness timeout that falls back to the DS18x20 or MRC
room sensor when the external writer stops publishing.

## Motivation

- Lune Touch is the normal writer of the physical house temperature. When Touch
  or the LAN fails, Asgard currently keeps the last written value forever.
- A frozen **warm** value leaves the heat-pump relay off (house under-heats).
- A frozen **cold** value keeps the heat pump running; only V6 valve cutoffs
  limit overshoot.
- Touch and V6-A already coordinate a writer lease so only one publisher is
  active, but Asgard itself should not trust an indefinitely held number.

## Proposed behaviour

| Setting | Suggested default | Meaning |
| --- | ---: | --- |
| Staleness timeout | 180–300 s | After last successful set, mark input stale |
| Fallback source | DS18x20 / MRC / Ecodan room | Same fallback path used for NaN today |
| Restore on boot | keep | Boot may restore last value, but the timeout still applies from boot |

Optional diagnostics:

- Expose `last_update_age_s` and `source` (`external` / `fallback`) as sensors.
- Log a transition when falling back or recovering.

## Compatibility

Existing installations that write every ≤60 s are unaffected. Writers that
stop (or never exist) get a safe local sensor instead of a forever-held value.

Once this lands, Lune V6-A’s fallback Asgard writer path can be slimmed or
removed.

## References

- Lune Touch design: whole-house flow temperature + authority state machine
- Current Touch publish cadence: 60 s physical temperature, lease lifetime 90 s
