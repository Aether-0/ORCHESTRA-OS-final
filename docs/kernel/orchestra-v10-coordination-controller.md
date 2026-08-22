# ORCHESTRA-OS v10 Native Coordination and Feedback Controller

**Status:** Implemented in the canonical sched_ext prototype

**Maturity:** Kernel-prototyped and source/build validated. Verifier acceptance,
scheduler attachment, task ownership, and hardware behavior remain separate
runtime claims.

## Contract surface

The v10 contract is additive to the v8 kernel ABI:

- `kernel/sched_ext/include/orchestra_control_abi.h` defines the fixed-size
  records, versions, bounded domains, states, deficit classes, actuator IDs,
  flags, and map names.
- `kernel/sched_ext/include/orchestra_coord.h` implements bounded windows,
  native S1/S2/S3/S4/Q calculation, deficit classification, and the shared
  deficit-to-actuator matrix.
- `kernel/sched_ext/include/orchestra_controller.h` implements the staged
  controller, bounds, state machine, rollback, recovery, and telemetry.
- `orchestra_scx_stage7.bpf.c` records signal/action/execution observations,
  publishes v10 runtime state, and applies the controller view to the existing
  decision pipeline.
- `bridge/orchestra_bridge.c` and `bridge/orchestra_loader.c` validate and
  expose the exact v10 map schemas alongside the v8 maps.

The canonical action set remains exactly `RUN`, `SLEEP`, `MIGRATE`,
`THROTTLE`, and `YIELD`.

## Bounded coordination windows

`orch_coord_v10` is a two-bank `ARRAY` map. Its fixed capacity is:

```text
(1024 CPU-domain slots + 64 NUMA-domain slots + 1 global slot) * 2 banks
```

The active window is selected by `floor(ktime_ns / window_duration)` and the
bank is selected by generation parity. The default controller period is
100 ms, so the default coordination window is 10 ms. The duration is bounded
to 1 ms through 1 s before it can affect map selection. CPU, NUMA, and global
aggregates are updated together; `orch_coord_cpu` is a one-entry per-CPU
scratch map used for short transition-burst detection without scanning the
task population.

At a window boundary the previous global record is finalized under its map
lock before the next record is initialized. The finalized compact record is
then handed to the controller. Generation parity, schema checks, and locked
publication prevent a reader from treating a partially initialized record as
a completed window.

## Native coordination metrics

All scores use integer permille (`0..1000`) and are computed in the kernel.
No floating-point operation is required.

`S1` is the mean of four signal/prediction factors over signal observations:

1. remaining-frame freshness;
2. published prediction confidence;
3. prediction fidelity (`1000 - abs(observed - predicted)`);
4. signal-generation continuity.

Invalid and stale observations remain in the observation denominator but do
not contribute positive factor sums, so a window containing only invalid
signals does not appear healthy.

`S2` is policy compliance:

```text
S2 = policy-compliant executed observations / executed observations
```

The action record preserves policy-selected, controller-selected,
capability-adjusted, and actual-executed actions. Directive compliance is
tracked separately from policy compliance.

`S3` is state-conditioned action coherence. For each populated runtime-state
class, the largest actual-action share is calculated; `S3` is the mean of
those per-class maxima. Empty state classes do not contribute to the mean.

`S4` is temporal stability. The implementation converts the largest applicable
penalty into a score:

```text
penalty = max(
    action transitions / eligible observations,
    synchronized-mass-switch transitions / eligible observations,
    detected oscillation events / eligible observations
)
S4 = max(0, 1000 - penalty)
```

Mass-switch detection uses an eight-transition threshold inside a 2 ms local
burst window. Oscillation telemetry covers repeated MIGRATE, RUN/YIELD
alternation, THROTTLE cycles, and SLEEP cycles. This makes synchronized mass
switching visible to the corrected metric instead of allowing high instantaneous
agreement to look automatically healthy.

The aggregate is the fixed-point geometric mean:

```text
Q = floor((S1 * S2 * S3 * S4) ** 1/4)
```

The implementation obtains the fourth root with two bounded integer square-root
passes and clamps the result to permille. The finalized record includes every
component, Q, sample window, observation counts, transition counters, and
deficit metadata; Q is never exposed as a standalone unexplained value.

## Deficit classification and actuator matrix

The default deficit threshold is 700 permille. Each component below the
threshold is a deficit. With no deficits the class is `NONE`; one deficit uses
that component's class; two or more use `MIXED`, while `primary_deficit` and
`secondary_deficit` retain the lowest and next-lowest component classes.
Severity is `1000 - Q`.

| Deficit | Primary actuator mapping | Direction |
|---|---|---|
| `SIGNAL` | prediction confidence, prediction horizon, signal cadence | confidence up; horizon/cadence down |
| `COMPLIANCE` | consensus blend, migration threshold, yield threshold | up |
| `COHERENCE` | consensus blend, switching penalty, migration threshold, coordination threshold | blend/penalty/migration up; coordination threshold down |
| `STABILITY` | jitter, switching penalty, migration threshold, throttle duration, sleep defer | up |
| `MIXED` | bounded union of the selected primary/secondary responses | per-component |

Every actuator has an explicit minimum, maximum, default, and maximum step.
The matrix is shared by userspace contract tests and the BPF controller, so a
diagnostic interpretation cannot silently select an actuator outside the
kernel policy.

## Controller state machine and publication

The controller has two timescales: measurement windows are short (10 ms by
default), while actuator updates are rate-limited to the 100 ms controller
period. The state machine is:

| State | Behavior |
|---|---|
| `NORMAL` | default actuator values and normal action admission |
| `DEGRADED` | persistent deficit; selected bounded actuators move by at most one step per update |
| `SATURATED` | a persistent deficit has no further actuator movement available |
| `DISABLED` | invalid controller/metric contract; safe RUN behavior |
| `ROLLBACK` | evaluation regression restored the previous-known-good bank |
| `RECOVERY` | deficits cleared; actuators return toward defaults before NORMAL |

The controller record contains active, staging, and previous-known-good banks.
Updates copy the active bank into staging, change at most two selected
actuators per update, and publish the whole staging bank while holding the
controller lock. Generations identify each publication. A 200 ms evaluation
period compares Q against the update baseline; a decrease greater than 50
permille triggers rollback and a 200 ms cooldown. A 30 ms minimum hold, the
controller period, bounded actuator steps, persistence hysteresis, and the
rollback cooldown are the anti-oscillation safeguards.

`orch_ctrl_tel_v10` records updates, no-ops, saturation, rollback, recovery,
overrides, state transitions, last deficits/direction, and per-actuator and
per-deficit counts. `orch_runtime10` publishes the latest finalized metric
record together with controller state, flags, generation, policy generation,
signal generation, and prediction generation.

## Scheduler integration and safety fallback

The v10 view is read before the existing v8 decision path. It gates prediction
acceptance using controller confidence and horizon thresholds, and applies
controller state/thresholds to action admission. Disabled, rollback, or
unsupported actions fall back to `RUN`. `YIELD` and `MIGRATE` additionally
require their bounded threshold conditions. THROTTLE duration and SLEEP defer
values are clamped to their actuator bounds.

For each observed task decision, `orch_task_coord` records:

```text
policy-selected -> controller-adjusted -> capability-adjusted
-> actual-executed
```

It also records controller/policy/signal/prediction generations, previous
action/state, fallback reason, and execution timestamps. This separates a
requested action from an accepted, dispatched, effective, or fallback action.

## Map schemas

| Map | Type | Value | Purpose |
|---|---|---|---|
| `orch_coord_v10` | ARRAY, 2178 entries | `orchestra_coordination_state_v10` | two-bank CPU/NUMA/global windows |
| `orch_coord_cpu` | PERCPU_ARRAY, 1 entry | `orchestra_coord_cpu_v10` | transition-burst scratch |
| `orch_ctrl_v10` | ARRAY, 1 entry | `orchestra_controller_state_v10` | active/staging/rollback controller |
| `orch_ctrl_tel_v10` | ARRAY, 1 entry | `orchestra_controller_telemetry_v10` | controller diagnostics |
| `orch_runtime10` | ARRAY, 1 entry | `orchestra_runtime_state_v10` | compact runtime publication |
| `orch_task_coord` | HASH | `orchestra_task_coord_v10` | per-task provenance and generations |

The bridge and loader require exact key/value sizes and map types. Existing v8
maps remain available for compatibility; v10 capability bits are advertised
separately and the scheduler requests the combined v8+v10 capability set.

## Validation status

The implementation has been validated by:

- GCC ASan/UBSan and Clang deterministic fixed-point, deficit-matrix, and
  controller-contract tests;
- existing 30/30 policy unit tests, bridge ABI tests, publication stress tests,
  benchmark validators, and source safety invariants;
- `make check`;
- target-matched out-of-tree BPF, bridge, and loader compilation against the
  recorded running-kernel source/BTF inputs;
- `bpftool gen skeleton` and BTF inspection confirming the six v10 maps and
  native v10 record types in the BPF object.

This does not claim verifier acceptance or runtime validation. Loading,
ownership proof, action-effect measurements, long-run behavior, and hardware
performance still require the repository's privileged sched_ext campaign.
