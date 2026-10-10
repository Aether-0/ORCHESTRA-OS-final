# ORCHESTRA-OS sched_ext kernel integration

**Status:** Implemented  
**Maturity:** Kernel-prototyped; runtime evidence is kernel- and machine-specific

## Scope

The canonical source entry point is `bpf/orchestra_sched.bpf.c`, which selects
the single compatibility implementation body
`orchestra_scx_stage7.bpf.c`, with the privileged bridge under `bridge/`. It uses full-switch sched_ext operations,
which changes scheduling for every normal, batch, idle and ext task on the
host. Exact identity admission authorizes adaptive directives, rather than
isolating sched_ext ownership. Tasks without a directive enter the global RUN
queue. The prototype does not implement Linux nice/weight or cgroup CPU-control
semantics. Kernel activation requires a dedicated research host; per-task
telemetry is still required to prove adaptive execution. It provides:

- Exact-schema control, identity, directive, task, telemetry, deferred-timer,
  and fixed-point signal-frame maps
- Strong task lifetime identity (TGID, TID, and start-boottime cookie)
- The canonical actions RUN, SLEEP, MIGRATE, THROTTLE, and YIELD
- Bounded signal-frame publication with sequence, freshness, epoch, range, and
  controller-coherence validation
- A fail-closed `--require-signal` directive gate with signal accept/invalid/
  stale telemetry
- Bridge-side refusal to publish adaptive directives to `SCHED_FIFO`,
  `SCHED_RR`, or `SCHED_DEADLINE` targets
- Bounded deferred eligibility for SLEEP and THROTTLE
- Affinity/online-CPU validation for MIGRATE
- Safe RUN fallback with per-task and global diagnostics
- A pinned-map loader that pins the timer map before struct_ops attach
- Safe unload — opted-in tasks return to the normal Linux scheduler

## Files

| File | Purpose |
|------|---------|
| `include/orchestra_abi.h` | Canonical actions and policy ABI |
| `include/orchestra_bridge_v1.h` | Bridge map and telemetry contract |
| `include/orchestra_control_abi.h` | Additive v10 coordination/controller ABI |
| `include/orchestra_coord.h` | Native bounded-window metrics and deficit matrix |
| `include/orchestra_controller.h` | Multi-actuator feedback controller |
| `bpf/orchestra_sched.bpf.c` | Stable product BPF source entry point |
| `orchestra_scx_stage7.bpf.c` | Compatibility implementation body retained for evidence |
| `bridge/orchestra_bridge.c` | Privileged map bridge CLI |
| `bridge/orchestra_loader.c` | Exact-map pin/load/unload helper |
| `scripts/check_kernel_config.sh` | Kernel config validation |

## Prerequisites

- Linux with sched_ext; the running kernel's API family must match the
  target-matched build (sched_ext is upstream from Linux 6.12, but vendors
  may backport or change interfaces)
- `CONFIG_DEBUG_INFO_BTF=y`
- clang/LLVM 16+ for BPF compilation
- bpftool, libbpf >= 1.2.2, libelf, zlib, zstd, and pkg-config as required by
  the target build
- Root or `CAP_BPF`+`CAP_SYS_ADMIN` for scheduler load

## Build

Preferred: use the repository's target-matched out-of-tree builder. It keeps
generated headers and binaries outside the source tree and stops before a
build when the source does not match the running kernel.

```bash
ORCHESTRA_KERNEL_SRC=/path/to/exact/running-kernel-source \
ORCHESTRA_BUILD_DIR=/var/tmp/orchestra-os-build-$(id -u) \
  ./scripts/build.sh --kernel
```

The output directory contains `vmlinux.h`, `orchestra_scx_stage7.bpf.o`,
`orchestra_bridge`, `orchestra_loader`, and `build-manifest.txt`. A
`BLOCKED_*` result is a prerequisite failure, not a source-pass result.

Some distribution source/header exports omit `tools/include/uapi/linux/bpf.h`
or `scripts/bpf_doc.py`. In that case, `ORCHESTRA_BPF_UAPI` may point to the
exact running-kernel UAPI header and `ORCHESTRA_BPF_DOC` may point to a
reviewed helper-definition generator. Both paths are recorded; using an
override does not upgrade runtime validation until the loader is exercised.

For manual diagnosis, the equivalent low-level build is:

```bash
# From the Linux kernel source tree:
cd $ORCHESTRA_KERNEL_SRC
make -C tools/bpf/bpftool

# Generate the running-kernel BTF header and build the canonical scheduler:
sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > include/vmlinux.h
clang -O2 -target bpf -g \
    -nostdinc -D__BPF__ \
    -I include -I $ORCHESTRA_KERNEL_SRC/tools/lib \
    -I $ORCHESTRA_KERNEL_SRC/include \
    -I $ORCHESTRA_KERNEL_SRC/include/uapi \
    -I $ORCHESTRA_KERNEL_SRC/arch/x86/include \
    -I $ORCHESTRA_KERNEL_SRC/arch/x86/include/generated \
    -I $ORCHESTRA_KERNEL_SRC/tools/sched_ext/include \
    -I /usr/include/bpf \
    -Wno-missing-declarations -Wno-visibility \
    -Wno-address-of-packed-member \
    -c orchestra_scx_stage7.bpf.c -o orchestra_scx_stage7.bpf.o

# Build the bridge and exact-map loader:
cc -O2 -Wall -Wextra -Werror -I include \
    bridge/orchestra_bridge.c -o bridge/orchestra_bridge
cc -O2 -Wall -Wextra -Werror -I include \
    bridge/orchestra_loader.c -o bridge/orchestra_loader -lbpf -lelf -lz

# The build must use headers and BTF matching the running kernel.
```

## Load and Test

The normal product workflow is the foreground control plane. It verifies the
strict capability gate, root-safe artifact ownership, build-manifest hashes,
and exact scheduler ownership before it begins reporting:

```bash
sudo ./scripts/install.sh --with-kernel \
  --build-dir /var/tmp/orchestra-os-build-$(id -u)
sudo /usr/local/bin/orchestra run --interval 5
# Ctrl-C returns to conventional scheduling.
```

For a detached/manual lifecycle:

```bash
sudo /usr/local/bin/orchestra enable

# Verify sched_ext is active:
cat /sys/kernel/sched_ext/state
cat /sys/kernel/sched_ext/root/ops

# Should show "enabled"; normal tasks are now owned by full-switch sched_ext.
sudo /usr/local/bin/orchestra status
```

The direct loader remains an advanced diagnostic interface. It refuses to
unload unless the active ops name, pinned struct_ops link, pin directory, and
every expected map schema match ORCHESTRA. It never performs broad bpffs
cleanup.

## Telemetry

After scheduler unload, telemetry is printed to stdout:

- `load_count` / `unload_count` — scheduler attach/detach events
- `task_enable_count` / `task_disable_count` — sched_ext enable/disable events
- `enqueue_count` / `dispatch_count` — scheduling operations
- `run_count` / `yield_count` — action frequencies
- `fallback_count` / `invalid_action_count` — error diagnostics
- `signal_accepted_count` / `signal_invalid_count` /
  `signal_stale_count` — required-signal gate outcomes

## Signal frame

`orch_signal` is an `ARRAY[1]` map containing the 152-byte
`bridge_signal_frame`. It is a bounded native-endian transport for values
that have already been validated and quantized by the bridge. `--signal-publish`
updates it under `BPF_F_LOCK`; `--require-signal` makes a directive fail closed
unless BPF observes a current frame with the same scheduler epoch, controller
state, policy mode, and policy generation.

The frame can carry externally computed prediction inputs and S1/S2/S3/S4/Q
values, but it remains a local-trust transport and does not verify the
userspace HMAC. The additive v10 path independently computes bounded native
coordination windows and publishes its own S1/S2/S3/S4/Q record; it does not
turn the legacy frame into an authenticated signal bus or prove predictor
quality on hardware.

The RT policy check is an admission guard in the privileged bridge. It avoids
publishing adaptive directives to a target whose current Linux policy is
real-time, but it is not a kernel RT bypass/coexistence implementation.

## Unload

```bash
# Use the ownership-checked product control plane:
sudo /usr/local/bin/orchestra disable
```

All full-switch tasks return to CFS/EEVDF on scheduler detach.

## ABI v8 kernel-resident adaptive core

`orchestra_scx_stage7.bpf.c` now contains an additive v8 execution path. The
legacy bridge ABI remains unchanged; v8 is defined in
`include/orchestra_kernel_v8.h` and is exposed through six additional maps:

- `orch_runtime_v8`: fixed-point CPU/queue/memory/thermal state, prediction
  record, S1/S2/S3/S4/Q coordination fields, and controller/policy generations
- `orch_meta_v8` plus `orch_entry_v8`: two bounded policy banks with inactive
  bank staging and generation-checked active-bank publication
- `orch_task_v8` plus `orch_diag_v8`: hot per-task action/deadline state and
  separate diagnostic counters
- `orch_tel_v8`: policy lookup, generation, controller override,
  unsupported-action, fallback, lifecycle, action, and prediction telemetry

For each enqueue/select path the kernel performs the single decision sequence:

```text
read runtime state -> build bounded state index -> policy lookup
-> controller gate -> action/capability validation -> action execution
-> task/result telemetry
```

The policy lookup has a deterministic RUN fallback. Controller semantics are
explicit for NORMAL, DEGRADED, SATURATED, DISABLED, ROLLBACK, and RECOVERY;
MIGRATE selects a bounded affinity/online CPU when the policy target is ANY
and records selected/already-local/affinity/offline/no-target/fallback
outcomes, YIELD has a repeated-progress guard, and the existing
deadline-ordered SLEEP/THROTTLE compatibility path remains work-preserving.
Backend capabilities explicitly negotiate adaptive slices, state-derived CPU
selection, and the SLEEP/THROTTLE defer-compatible implementations; an
unsupported selection falls back to RUN without changing the active policy.

TRAIN stages only into the inactive bank, ADAPT permits generation-safe
activation, and EVALUATE freezes active-policy mutation until an explicit
TRAIN/ADAPT transition is requested. A rollback commit switches back to the
last committed bank/generation and marks the controller state ROLLBACK; the
controller gate remains RUN-only until recovery is explicitly selected.

Hot task state and diagnostics distinguish requested, deferred, executed, and
running observations. The v8 telemetry also records state, policy, and signal
generation changes, prediction fallback, unsupported actions, and policy
lookup outcomes. Legacy cache counters remain in the ABI; the decision path
performs a fresh coherent policy snapshot instead of reusing cached actions.

THROTTLE accounting follows continuous THROTTLE action periods. Renewing its
generation preserves charged runtime; changing into THROTTLE starts a fresh
period. Queue insertion is dispatch evidence, not effective execution. The
registered `.enqueue` calls `orchestra_decide` and `orchestra_execute_action`,
including native controller and capability gates. Task state and telemetry
remain correlated by publication generation.

A valid per-task bridge directive supplies identity admission and a bounded
lease. With policy generation zero, its action is used directly. Once a
native policy bank is committed, a coherent policy entry selects the action;
missing or invalid entries fall back to RUN. Epoch, policy generation, and
mode must agree across the policy snapshot and the admission snapshot.
Deferred policy actions retain that provenance and are revalidated against
the current admission and policy metadata before release.
The existing periodic timer finalizes coordination windows and updates the
native feedback controller before draining deferred work, including while
the global RUN queue remains busy. These stages use separate bounded stack
frames; decision snapshots use internal per-CPU working maps.

THROTTLE accounting is generation-scoped. A newly published generation, or a
change into THROTTLE, starts a fresh period with zero charged runtime. Requeues
for that same generation preserve charged runtime until the budget is exhausted
or the period rolls over. Runtime accumulated under an earlier RUN generation
is therefore not inherited by a newly published THROTTLE generation.
Targeted bridge status reports the corresponding `task_state` generation,
action, period start, charged runtime, eligibility deadline, and state flags so
the scheduler/telemetry correlation can be checked directly.

On the target-matched kernel build, the verifier-safe compact
`orchestra_enqueue_bridge()` path is the authoritative live `.enqueue`
executor. The retained `orchestra_execute_action()` helper is not called from
that live path; compact-path dispatch telemetry is recorded by the dedicated
legacy recorder and mapped to the actual RUN/YIELD/MIGRATE action.

The bridge can stage and atomically activate a policy bank without changing
the legacy per-task publication interface:

```bash
./orchestra_bridge --policy-entry --policy-state-index 0 \
  --action RUN
./orchestra_bridge --policy-commit --policy-mode 1 \
  --controller-state NORMAL
```

The v8 implementation is source/build validated here. It is not, by itself,
proof of verifier acceptance, scheduler attachment, task ownership, or
real-machine performance. Those remain separate runtime claims.

## ABI v10 native coordination and feedback controller

The additive v10 path closes the kernel-side measurement/control loop without
changing the v8 bridge records. `include/orchestra_control_abi.h` defines exact
v10 schemas and bounded map capacities; `include/orchestra_coord.h` computes
native fixed-point coordination windows; and
`include/orchestra_controller.h` implements the staged multi-actuator
controller. The detailed contract is documented in
[`docs/kernel/orchestra-v10-coordination-controller.md`](../../docs/kernel/orchestra-v10-coordination-controller.md).

The window path records signal freshness/confidence/fidelity/continuity,
policy compliance, actual-action coherence conditioned on runtime state, and
temporal stability including transition, synchronized-mass-switch, and
oscillation penalties. It publishes all four components and the fixed-point
geometric-mean Q, then classifies deficits and selects bounded actuators from a
shared matrix. Controller updates are slower than the default 10 ms windows,
are limited by minimum hold/cooldown/persistence hysteresis and per-actuator
steps, and publish active/staging/previous-known-good banks by generation.

The scheduler records policy-selected, controller-adjusted,
capability-adjusted, and actual-executed actions in `orch_task_coord`. A
disabled, rolled-back, invalid, or threshold-ineligible action falls back to
RUN. `orch_runtime10` and `orch_ctrl_tel_v10` expose the finalized metric,
controller state, generations, and update/saturation/rollback/recovery
telemetry. This is kernel-prototype and build evidence; it is not verifier,
attachment, ownership, or performance evidence.

## Limitations

- SLEEP is a one-shot deferred eligibility transition; a successful release
  retires that generation to RUN, rather than creating a persistent sleep state
- The bridge signal frame is local-trust map IPC, not kernel HMAC verification
- RT protection currently stops at bridge admission; full kernel RT/deadline
  coexistence and starvation validation remain unimplemented
- The kernel consumes bounded prediction records and does not train a
  predictor or verify the userspace HMAC. The v10 path computes native bounded
  S1/S2/S3/S4/Q metrics, but it does not claim userspace-quality predictor
  calibration or distributed policy learning; NUMA results are bounded
  single-host aggregates
- The v8 TRAIN/ADAPT/EVALUATE lifecycle is a generation-safe policy-bank
  publication contract; it is not evidence that an online learning algorithm
  has converged on hardware
- Runtime acceptance requires a matching kernel source/BTF toolchain and
  remains separate from userspace/simulation validation

## Native coordination metrics

Kernel ABI v10 records use bounded event windows. S1 averages the four permille
scores for signal freshness, prediction confidence, prediction accuracy and
sequence continuity; invalid observations contribute zero. S2 is effective
policy compliance. S3 is the mean dominant-action share within each populated runtime-state class.
S4 penalizes transitions, synchronized bursts and oscillations. Q is the integer
fourth root of the product of all four permille scores. These event-window
metrics are distinct from the userspace paper's entropy-based population
metrics and must not be pooled with them.

Queue insertion increments dispatch counters. Only `.running` records an
execution. THROTTLE renewal preserves service already charged in its period;
changing its parameters never refunds service already consumed. Deferred work
is revalidated against current publication and controller state and released
as RUN when revoked or when its local CPU destination is unavailable.
