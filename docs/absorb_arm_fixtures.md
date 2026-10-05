# Absorb-arm / timing-bias fixtures (shared with V6 A5)

Touch and V6 share these JSON shapes. Until A5 effect lands, V6 returns HTTP 501
with the `not_implemented` envelope; Touch still posts, ledger-logs, and parses.

## Request — absorb arm

`POST /api/v1/zones/{zone}/absorb-arm`

```json
{
  "ttl_s": 3600,
  "reason": "thermal_buffer",
  "decision_reason": "thermal_buffer",
  "source": "absorb_arm"
}
```

`decision_reason` is required for Odin 2.0 ledger/V6 U2. v1 clients may omit it; Touch
still sends `reason` for backward compatibility.

## Request — absorb disarm

`POST /api/v1/zones/{zone}/absorb-disarm`

```json
{
  "reason": "soft_stop",
  "decision_reason": "soft_stop",
  "source": "absorb_arm"
}
```

## Response — not implemented (P3)

HTTP 501

```json
{
  "result": "rejected",
  "error": "not_implemented",
  "path": "/api/v1/zones/0/absorb-arm",
  "api_version": 1
}
```

## Response — success (P5)

HTTP 200

```json
{
  "result": "accepted",
  "zone": 0,
  "ttl_s": 3600,
  "expires_at_ms": 1234567890,
  "clamped": false
}
```

## Ledger sources (Touch)

| source | Mechanism |
| --- | --- |
| `absorb_arm` | V6 absorb window arm (not room demand) |
| `odin_timing_bias` | Weather preload timing bias toward ODIN heat hours |
| `forecast` | Weather preload offset |

Keep these distinct in Commands UI and diagnostics.
