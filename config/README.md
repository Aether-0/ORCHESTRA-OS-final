# ORCHESTRA-OS configuration

The JSON examples in `config/examples/` use the versioned
`orchestra.policy.v1` document shape. They are intentionally small and
bounded: the bridge accepts at most 64 entries in one policy publication.

The current implementation publishes policy entries through the existing
bridge and validates the ABI, action, state index, controller state, and
bounded timing fields before attempting a map update. `--dry-run` prints the
exact bridge commands without requiring an attached scheduler.

Common fields:

| Field | Type / units | Range or default | Meaning |
| --- | --- | --- | --- |
| `policy.mode` | enum | `TRAIN`, `ADAPT`, `EVALUATE` | Publication mode. The safe runtime default is `ADAPT`. |
| `policy.controller_state` | enum | `NORMAL`, `DEGRADED`, `SATURATED`, `DISABLED`, `ROLLBACK`, `RECOVERY` | Controller gate requested for the policy bank. |
| `policy.entries[].state_index` | integer | 0–63 in examples; bridge limit 0–255 | Fixed-point state bucket. |
| `policy.entries[].action` | enum | `RUN`, `SLEEP`, `MIGRATE`, `THROTTLE`, `YIELD` | Canonical action. |
| `slice_ns` | integer / ns | 0–5,000,000,000 | Bounded execution slice where supported. |
| `target_cpu` | integer | online CPU or `4294967295` for any | Migration target; capability and affinity checks still apply. |
| `throttle_period_ns` | integer / ns | positive, bridge-bounded | Throttle period. |
| `throttle_budget_ns` | integer / ns | positive, bridge-bounded | Throttle budget within the period. |

The `signal`, `predictor`, `controller`, and `telemetry` sections are
documented configuration intent and are retained for reproducibility. The
current bridge policy loader publishes the `policy` section; unsupported
runtime knobs are rejected or ignored by their owning component rather than
silently changing scheduler semantics.

Use the examples as starting points, then verify the active state with
`orchestra policy show` and `orchestra telemetry`.

The policy files do not enable the scheduler. Kernel mode is an explicit
administrator action through `orchestra run`/`orchestra enable`, and the
optional `config/systemd/orchestra.service` is never enabled by the
installer. Keep policy files reviewed and root-owned on systems where a root
control process will publish them.
