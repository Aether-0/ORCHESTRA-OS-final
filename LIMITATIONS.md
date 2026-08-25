# ORCHESTRA-OS Limits and Constraint Rationale

This inventory distinguishes a limit that protects correctness or resources
from a historical implementation restriction. A retained limit must have a
documented failure mode. Limits are not silently increased merely to make a
benchmark pass.

## Kernel/ABI limits retained intentionally

| Limit | Location | Classification | Rationale and failure behavior |
| --- | --- | --- | --- |
| `BRIDGE_MAX_TASKS = 4096` | `kernel/sched_ext/include/orchestra_bridge_v1.h` | BPF map/resource protection | Bounds per-task hash maps and memory. Admission/map insertion reports capacity failure and falls back to RUN/conventional scheduling. A future ABI may negotiate a larger map set; changing it requires matching loader schemas and target validation. |
| `ORCHESTRA_KERNEL_MAX_POLICY_STATES = 256` | `orchestra_kernel_v8.h` | ABI/verifier/map architecture | Matches the fixed two-bank policy ARRAY and 8-bit state projection. The userspace loader now accepts the full 256 states; the former 64-entry loader restriction was removed. |
| `ORCHESTRA_KERNEL_POLICY_BANK_COUNT = 2` | `orchestra_kernel_v8.h` | Atomic publication architecture | One active and one inactive bank permit bounded staging/flip/rollback. Increasing it changes ABI/map size and is not a safe local tuning knob. |
| `ORCHESTRA_COORD_MAX_CPU_DOMAINS = 1024` | `orchestra_control_abi.h` | Bounded map/resource protection | Prevents topology-proportional unbounded BPF map allocation. CPUs above the representable range use the bounded aggregate slot. |
| `ORCHESTRA_COORD_MAX_NUMA_DOMAINS = 64` | `orchestra_control_abi.h` | Bounded map/resource protection | Prevents unbounded NUMA state. Invalid/unknown NUMA IDs use the safe aggregate behavior. A larger target needs an ABI/map revision. |
| Two coordination window banks | `orchestra_control_abi.h` | Atomic window lifecycle | Keeps current/previous finalized windows bounded and avoids reading a partially published window. |
| `BRIDGE_DEFER_SCAN_MAX = 64` per timer tick | `orchestra_bridge_v1.h`, BPF timer | Verifier/latency/DoS protection | Limits work in one timer callback. The timer re-arms and makes progress over subsequent ticks; CPU-affinity failure promotes expired work to the global RUN queue rather than leaving it stuck. |
| `BRIDGE_DEFER_TICK_NS = 250000` | bridge ABI | Scheduling overhead bound | Controls timer cadence and bounds callback frequency. It is not a security secret or policy input. |
| Slice floor `100us`, default `5ms`, maximum `100ms` | bridge ABI/BPF | Kernel progress and fairness bound | Prevents zero-length dispatch loops and extreme unbounded slices. Invalid values are rejected or clamped to safe RUN behavior. |
| Throttle period maximum `1s` | bridge ABI/BPF | Resource/fairness protection | Prevents stale budgets from suppressing a task indefinitely. Invalid period/budget combinations fall back to RUN. |
| Sleep defer maximum `5s` | bridge ABI/BPF | Stale-state/fairness protection | Prevents a malicious or stale sleep directive from creating a long local disappearance. Expired/replaced deferred directives are revalidated and released as RUN. |
| Directive expiry and lease maximum `60s` | bridge ABI/BPF | Freshness/DoS protection | Limits how long a publisher or directive can control scheduling without renewal. Stale lease/directive causes conventional fallback. |
| Signal maximum age `5s` | bridge ABI/BPF | Freshness protection | Prevents old predictive state from influencing decisions. The signal path rejects future, expired, over-age, or schema-incoherent frames. |
| Five actions | `orchestra_abi.h` | Research/API architecture | The canonical set is exactly RUN, SLEEP, MIGRATE, THROTTLE, YIELD. Unsupported semantics are capability-adjusted; no sixth action is hidden in compatibility code. |
| Eleven controller actuators | `orchestra_control_abi.h` | Research/API architecture | Fixed actuator matrix is part of the v10 controller ABI and causal deficit mapping. |
| Non-wrapping generation publication | bridge/controller/BPF | Replay/ABA protection | `UINT64_MAX` exhaustion disables or rejects publication. Reusing a generation is not an allowed optimization. |

## Userspace policy/control limits

| Limit | Location | Classification | Rationale and failure behavior |
| --- | --- | --- | --- |
| Policy file `1 MiB` | `scripts/policy_load.py` | Input/resource protection | Prevents a root control-plane request from consuming unbounded parser memory. Oversize input is rejected before JSON parse. |
| Policy `256` entries | `scripts/policy_load.py` | ABI capacity | Aligns with the kernel policy bank; it is no longer the old arbitrary `64`. |
| Numeric fields `[0, 2^64-1]` | policy loader/bridge | Serialization/arithmetic safety | Rejects negative, boolean, oversized, and overflow-prone values before command construction. |
| Duplicate JSON keys rejected | policy loader | Ambiguity protection | Prevents parser-dependent “last key wins” policy interpretation. |
| Per-bridge command timeout `10s` | policy loader | Control-plane liveness | A hung map operation cannot hold the publisher forever. The command is reported as failed and abort/recovery is attempted. |
| Full policy publication timeout `300s` | policy loader | Control-plane resource protection | Bounds a 256-entry sequential publication campaign. It is deliberately far above normal map operation latency but finite. |
| Policy abort copies active bank to inactive | bridge `--policy-abort` | Recovery boundary | Failed staging cannot accidentally become the next active bank. The operation is bounded by the fixed policy bank. |
| Installer/uninstaller manifest | install/uninstall scripts | Ownership/integrity boundary | Recursive removal is refused if any untracked or symlinked path appears. Configuration is preserved unless explicit safe removal is requested. |

## Build and test-only limits

These do not constrain the production scheduler ABI:

| Limit | Location | Classification | Rationale |
| --- | --- | --- | --- |
| `ORCHESTRA_MAX_DISPATCH_LOOPS = 4` | historical Stage 6 header | Verifier/progress bound in compatibility code | Prevents a legacy callback loop from spinning indefinitely. The canonical product path uses bounded callback logic and the file remains compatibility evidence. |
| Historical Stage 6 table size `30` | `orchestra_scx.h` | Compatibility/test artifact | Not the v8/v10 policy-bank capacity. It is retained for historical source compatibility and is not the canonical policy path. |
| Integration worker/attempt/time limits | `tests/integration` | Test containment | Prevent runaway test processes and make failures reproducible; they do not claim production capacity. |
| CSV validator worker/sleep/oscillation bounds | `tests/integration/validate_paper_cpu_csv.py` | Test/schema integrity | Reject fabricated or physically implausible research results. These are evidence-contract bounds, not scheduler limits. |
| Stress/ownership forward timeouts | `benchmarks`, `kernel/sched_ext/scripts` | Test safety | Prevent a failed runtime experiment from running indefinitely. No test timeout is converted into a PASS. |
| Fuzz/mutation iterations default `1000` (maximum `100000`) | `tests/security/fuzz_policy_loader.py` | Test resource bound | Makes CI finite while allowing an isolated campaign to increase coverage. |
| ABI/state mutation iterations `50000` (maximum `200000`) | `tests/security/fuzz_abi_state.c`, `tests/security/run.sh` | Test resource bound | Covers fixed-width bridge, signal, policy-meta, controller, and parser validators under sanitizers without making CI unbounded. |

## Limits deliberately not removed

The following would be unsafe to remove merely to increase apparent
capability:

- BPF verifier-bounded loops and fixed-width map records;
- generation non-wrap and freshness/expiry checks;
- RT/deadline admission bypass;
- controller rate limits, hysteresis, saturation, rollback, and disabled
  states;
- exact map/schema/capability negotiation;
- root ownership and symlink/path checks for privileged operations;
- loader ownership and detach-before-unpin teardown;
- map-capacity and malformed-input fail-closed paths.

## Remaining constraints requiring future design

1. **Cryptographic signal authentication.** The kernel-local signal map does
   not currently verify an HMAC. Adding it requires a key-management and
   bounded verifier design, not simply removing a field check.
2. **Dynamic topology sizing.** CPU/NUMA bounds are fixed in the v10 map ABI.
   A capability-negotiated map family or per-topology object build is needed
   before supporting larger topology claims.
3. **Signed release provenance.** Hashes in a local build manifest do not
   establish who built or approved the artifact.
4. **Privileged runtime coverage.** The recorded host has passed a
   target-matched verifier/attach, exact-TID ownership, bounded action, and
   scoped-unload gate. Broader recovery, hotplug, cross-kernel, and soak
   results still require dedicated authorized targets.
5. **Telemetry saturation semantics.** Lifetime counters wrap. This is safe
   for control because they are observational; a future telemetry ABI may
   add saturating totals or explicit wrap epochs.

No other hard-coded limit identified in the audited active paths is currently
classified as an unexplained arbitrary restriction. Any limit added later
must be entered here with its safety rationale and regression coverage.
