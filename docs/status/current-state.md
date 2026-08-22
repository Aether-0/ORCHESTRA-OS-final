# ORCHESTRA-OS verified current state

- Audit date: 2026-08-23
- Repository commit: use `git rev-parse HEAD` for the exact handoff commit
- Branch: `codex/final-integrated-product` (publication branch is separate)
- Current milestone: ORCHESTRA-OS `1.0.0-rc1` product integration with a
  foreground sched_ext lifecycle, ownership-checked loader, target-matched
  build/install path, and explicit security boundary; no new bare-metal v8
  sched_ext acceptance has been run
- Active work-package boundary: WP1 kernel foundation plus a source/build-validated v8 policy/runtime path, with userspace precursors for WP2-WP6
- Overall claim class: mixed; see the component boundaries below

This file is a handoff index. The paper, ADRs, experiment contracts, raw
artifacts, and test protocols remain authoritative for their respective
decisions and evidence.

## 2026-08-23 product integration and security follow-up

The product control plane now provides `orchestra run` (foreground attach,
monitor, and bounded cleanup), `orchestra monitor`, explicit scheduler-owner
checks, and an opt-in systemd unit. `enable` and `disable` refuse a foreign or
ambiguous sched_ext owner. The loader verifies the expected struct_ops link,
pin directory, and every exact map schema before unlinking anything.

Kernel activation additionally requires root-owned, non-symlink,
non-group/world-writable installed artifacts and a build-manifest hash match;
the target-matched builder normalizes artifact permissions and the installer
does not install kernel files unless `--with-kernel` is requested. Generated
host binaries and `vmlinux.h` files are excluded from Git while raw research
evidence remains archived.

The security boundary is intentionally explicit: local schema, generation,
freshness, identity, fallback, and artifact-integrity checks are implemented;
cryptographic authentication of the kernel signal frame, signed release
provenance/SBOM, fuzzing/SCA review, and privileged runtime security gates are
not claimed by this release candidate. See
`docs/security/SECURITY.md` and `FINAL_PRODUCT_STATUS.md`.

## 2026-08-21 working-tree follow-up

The latest bare-metal continuation did not establish an ORCHESTRA performance
result. The controlled ownership retest proved that an explicitly opted-in
task can reach the prototype path, but the older comparison rows started work
before exact opt-in and therefore remain `INCONCLUSIVE_OWNERSHIP_NOT_PROVEN`.
The target-matched out-of-tree build now succeeds for the locally preserved
7.0.12 source export, producing the BPF object, bridge, and loader. It uses an
exact 7.0.12 UAPI header plus an explicitly supplied helper generator because
the source export omitted those files; their paths and hashes are recorded in
the build manifest. Verifier/attach testing remains blocked in this session
because non-interactive root/sudo authorization is unavailable.

The working tree now contains targeted remediations: benchmark and stress
runners use repository-relative paths, loader-scoped attach/unload, exact-TID
ownership barriers, bounded memory pressure, and interrupt cleanup; bridge
status resolves and filters the full live task identity (including start
time/epoch); SLEEP releases retire only the exact released generation to RUN;
and the integration signal-stop grace period covers the existing worker
teardown window. The fixed-iteration benchmark helper now reports
conventional success after completing its work, and the runners record
per-worker exit status, allowed CPU IDs, storage headroom after teardown, and
thermal samples. The new signal slice adds a bounded 152-byte fixed-point
`orch_signal` map, bridge readback/sequence/freshness checks,
signal accept/invalid/stale telemetry, and a fail-closed `--require-signal`
directive gate. The userspace kernel-bridge stream publishes one converted
frame before its per-task directives and requires that frame for each
directive. This is kernel-local transport/gating evidence: it does not make
the userspace HMAC verifiable in BPF and does not implement the kernel
predictor, S1-S4/Q computation, feedback controller, Hybrid Safety Layer,
NUMA tier, or distributed tier.
The bridge also refuses to publish adaptive directives to current
`SCHED_FIFO`, `SCHED_RR`, or `SCHED_DEADLINE` targets; this is only an admission
guard and not kernel RT coexistence validation.

## 2026-08-22 fixed-point signal bridge follow-up

The exact 7.0.12 target-matched out-of-tree build was rerun after the signal
stream extension. The BPF object, bridge, and loader compile successfully with
the running kernel's BTF and the recorded UAPI/helper-generator provenance.
The userspace and source-level gates pass; privileged verifier/attach testing
remains unavailable in this session because non-interactive root/sudo
authorization is not present.

## 2026-08-22 metrics-v7 contract follow-up

The userspace binary's current 121-column output is now represented by the
append-only v6/v7 JSON schemas and bounded exploratory manifests. The benchmark
runner resolves those local append-only bases, enforces exact column counts,
validates policy-lifecycle fields, and applies the v7 conditioned-coordination
semantics. The first end-to-end v7 attempt is preserved as a failure artifact;
after correcting the runner's controller-causality mapping to match the current
C implementation, a fresh six-invocation run validated 6/6 CSVs and 6/6
independent integration-validator invocations. This remains userspace
pipeline validation, not kernel ownership or performance evidence.

## 2026-08-22 kernel-ABI-v8 implementation follow-up

The kernel path now includes an additive v8 runtime state record, two-bank
generation-checked policy publication, task hot state and diagnostics, global
telemetry, bounded state construction from observed/predicted/coordination
inputs, an explicit controller gate, and a single canonical policy-to-action
decision path for RUN, SLEEP, MIGRATE, THROTTLE, and YIELD. The legacy v2/v6/v7
bridge maps and publication paths remain compatible. Exact 7.0.12 target-
matched BPF, bridge, and loader builds plus source/unit/integration gates pass.
This is `KERNEL_PROTOTYPED` source/build evidence only: verifier acceptance,
attach, ownership, effective actions, unload, and performance remain untested
in the current unprivileged session.
The v8 contract also negotiates adaptive-slice/state-derived-CPU and
defer-compatible action backends, records bounded migration outcomes, enforces
EVALUATE lifecycle freeze/explicit transition rules, and exposes generation
change/defer/action telemetry. These are implementation properties, not live
kernel observations.

## Claim boundary

The repository is not deployment-ready and does not contain a complete
ORCHESTRA kernel architecture. It contains:

- a pre-kernel discrete-event simulation reported by the research paper;
- a real-process userspace prototype in which Linux CFS/EEVDF still performs
  final dispatch;
- a full-switch sched_ext BPF prototype with explicit identity admission and a
  privileged userspace map bridge;
- VirtualBox evidence for exact Linux 6.12.96 boot, BPF verification, attach,
  bounded stability, action paths, deferred SLEEP/THROTTLE release, legal
  MIGRATE placement, clean unload, and explicit SCHED_EXT task ownership;
- no bare-metal sched_ext execution evidence;
- no kernel implementation of authenticated signal frames, online predictor
  training, full S1-S4/Q computation, or distributed/NUMA policy learning. The
  v8 path consumes bounded externally supplied prediction/coordination records,
  publishes generation-safe policy banks, applies explicit controller-state
  gates, and records task/action telemetry; it does not establish online
  learning convergence or hardware runtime behavior.

Repository "Stage" numbers are historical development milestones. They are
not equivalent to the WP1-WP10 exit gates.

## Verified working components

### Userspace

- Canonical actions: RUN, SLEEP, MIGRATE, THROTTLE, YIELD.
- Canonical 128-byte, fixed-endian signal payload plus HMAC-SHA256 tag.
- Generation-stamped two-slot shared-memory publication with bounded readers.
- Field, identity, sequence, freshness, epoch, and constant-time tag checks.
- Observed-state fallback when a prediction lacks confidence.
- Fixed-gain predictor and robust observation-noise estimate.
- Tabular per-process learning, directive-aligned state thresholds, epsilon
  annealing, local/difference reward blending, and bounded consensus.
- S1-S4 and `Q = (S1*S2*S3*S4)^(1/4)`.
- Experimental burst-sensitive S4 diagnostics kept outside historical Q.
- Bounded, slower-timescale multi-actuator controller state machine.
- Process cleanup, signal teardown, strict CSV validation, and reproducible
  userspace benchmark/microbenchmark tooling through the committed v5
  experiment contracts.

On 2026-08-21, the captured `make test` gate passed:

- 30/30 named unit tests under GCC ASan/UBSan;
- the same 30/30 tests under the legacy transport and Clang passes;
- generation-stamped thread and MAP_SHARED process stress tests;
- 21 benchmark-runner validator tests;
- 4 signal-publication runner tests;
- 34 strict CSV validator tests;
- five bounded integration scenarios (baseline, ORCHESTRA, tamper,
  controller-tamper, and signal-stop).

### Kernel prototype

The current kernel path is `orchestra_scx_stage7.bpf.c`, not the Stage 6
loader skeleton. It uses exact-schema control, directive, identity, task,
telemetry, per-task telemetry, deferred-timer, and fixed-point signal maps.
It uses full-switch sched_ext operations, but a task must still pass the
explicit exact-identity admission path before ownership is claimed.
The additive v8 path also exposes runtime state, policy metadata and entries,
hot task state, diagnostics, and global telemetry maps. Its source/build gate
passes against the recorded running-kernel toolchain, but no v8 object has
been verifier-checked or attached in the current session.

The final VirtualBox runtime gate on 2026-08-14 established:

- a non-opted task did not increment enqueue/running ownership counters;
- an opted task incremented enable, enqueue, RUN, and running counters;
- MIGRATE requests reached the targeted `SCX_DSQ_LOCAL_ON` path and 10/10
  observed runs reached CPU 3;
- SLEEP and THROTTLE deferred and later released through the pinned timer map;
- concurrent per-task actions did not cross-mix identities;
- the scheduler detached cleanly through three lifecycle cycles plus a
  pre-pinned bridge-contract cycle;
- 24/24 bounded fallback tasks completed; this is a correctness gate, not a
  performance claim.

## Unverified or incomplete components

- The validated VM uses Linux 6.12.96. Newer or older kernels may require a
  sched_ext API compatibility build and separate verifier validation.
- The distro bridge development packages are not installed (`libbpf-dev` and
  `libelf-dev` are absent); the successful out-of-tree build used the exact
  source-tree public headers and the installed `libbpf.so.1` runtime.
- No new bare-metal BPF verifier, attach, ownership, action, watchdog, or
  detach test could run without non-interactive root/sudo authorization.
- YIELD remains a bounded relinquish approximation; physical fairness and
  latency validation remain pending.
- THROTTLE and SLEEP use bounded deferred eligibility; physical timing and
  bandwidth validation remain pending.
- MIGRATE placement was observed in the VM; hotplug and physical-contention
  behavior remain unvalidated.
- Unknown actions use RUN fallback, but an invalid action cannot be published
  through the current CLI and lacks a current owned-task runtime test.
- The bridge map ABI is local native-endian packed data and is not the
  authenticated canonical signal-frame protocol.
- Deterministic PID-reuse, map-capacity-exhaustion, and CPU-hotplug race
  campaigns remain pending despite the corrected lifetime identity path.
- The loader and bridge now validate exact map schemas, identity, generation,
  expiry, and timer-map pinning before attach; deterministic stress campaigns
  remain pending.
- The current userspace binary emits the 121-column metrics v7 contract. v6/v7
  schemas and manifests, exact-column append-only resolution, policy-lifecycle
  checks, and v7 conditioned-coordination validation are now present. The
  bounded v7 pipeline run validated 6/6 invocations; this is still userspace
  pipeline evidence and does not prove sched_ext ownership or performance.
- No kernel tests cover real-time/deadline non-interference, starvation,
  affinity/cpuset constraints, CPU hotplug, NUMA, cgroups, or security faults.
- Earlier Stage 8/9 benchmark interpretations that predate the P0 ownership fix
  do not prove ORCHESTRA-owned scheduling and must not be used as performance
  evidence.

## Current machine

Readiness classification: **VM gate passed; controlled physical pilot only**.

| Item | Verified value |
| --- | --- |
| OS | Kali GNU/Linux Rolling 2026.3 (Debian family) |
| Kernel | `7.0.12+kali-amd64` |
| Architecture | x86_64 |
| Virtualization | none detected (bare metal) |
| CPU | Intel Core i5-10310U, 1 socket, 4 cores, SMT2, 8 logical CPUs |
| NUMA | one node, CPUs 0-7 |
| sched_ext sysfs | present; state `disabled`; enable sequence 0 at audit |
| BTF | `/sys/kernel/btf/vmlinux`, present |
| Required config | BPF, BPF_SYSCALL, BPF_JIT, DEBUG_INFO_BTF, BPF_EVENTS, and SCHED_CLASS_EXT all `y` |
| clang | 21.1.8 |
| bpftool/libbpf runtime | bpftool 7.7.0; libbpf 1.7 |
| Rust | rustc/cargo 1.95.0; not required by the current C prototype |
| Missing build inputs | `libbpf-dev`, `libelf-dev`, exact 7.0.12 sched_ext tool headers/source |

No kernel replacement or reboot is indicated. Root is required for BPF load,
map pinning, task opt-in, and detach. Unprivileged BPF is disabled.

## Next acceptance gate

The next milestone is **bare-metal Linux 7.0.12 build/verifier compatibility
and a non-destructive explicit-ownership acceptance run**. It is not another
simulator feature and not the SuperTuxKart demonstration.

The gate passes only when a source-hashed build can:

1. compile against the exact running-kernel API;
2. pass the BPF verifier and attach as the identified ORCHESTRA scheduler;
3. prove a non-opted task remains outside ORCHESTRA;
4. prove an opted bounded task reaches enable/enqueue/running callbacks;
5. exercise owned-task RUN and YIELD;
6. exercise an invalid/unknown directive through a controlled test path and
   observe RUN fallback;
7. inspect requested versus effective telemetry without conflating them;
8. detach only the link and pins created by the test;
9. verify sched_ext state returns to `disabled` and the task returns to the
   normal Linux scheduler;
10. exercise verifier/load failure and watchdog/exit recovery safely; and
11. retain the environment, commands, stdout/stderr, dmesg excerpt, map data,
    scheduler state transitions, hashes, anomalies, and pass/block result.

Use `ORCHESTRA_OS_Real_World_Machine_Test_Checklist_Expanded.md` as the broad
real-machine protocol. It is the newer, 36-phase expansion of the earlier
checklist; do not create a duplicate checklist.

## Continuation order

1. Run the target-matched build and preserve the explicit UAPI/generator
   provenance, or obtain a complete exact Kali `7.0.12-2kali1` source export.
2. Use `kernel/sched_ext/scripts/build_stage7_out_of_tree.sh`, which never
   writes generated headers or objects over source files and refuses a kernel
   source version mismatch.
   A partial exact-version source export can use explicit
   `ORCHESTRA_BPF_UAPI`/`ORCHESTRA_BPF_DOC` overrides, but the paths and hashes
   must remain in the build evidence and do not replace a complete source
   provenance requirement.
3. Port the Stage 7 insertion calls to the Linux 7.0 sched_ext API while
   preserving action/fallback semantics; document the compatibility decision
   if it changes a contract.
4. Add source-level tests for bridge CLI parsing, map schema/ownership checks,
   generation/cache invalidation, action bounds, and opt-in ABI portability.
5. Keep the maintained benchmark, P0, Stage 8, Stage 9, and reproduction
   runners on exact loader-scoped cleanup; legacy broad-cleanup paths have
   been replaced by safe wrappers.
6. Build and run verifier-only loading with complete logs; do not opt in a task
   until the verifier and attach/detach path is clean.
7. Run the negative non-opt-in ownership test, then a single short opted RUN
   test, then YIELD and controlled invalid-action fallback.
8. Run clean detach, scheduler-exit, and bounded watchdog/failure recovery;
   verify state and conventional scheduling after every case.
9. Capture a new immutable bare-metal artifact set and update the relevant ADR,
   kernel result, and this status index.
10. Only after correctness/safety acceptance, validate effective MIGRATE and
    define real SLEEP/THROTTLE contracts before any comparative workload demo.

## Commands for the next controlled session

Read-only recheck:

```bash
git status --short --branch
uname -r
cat /sys/kernel/sched_ext/state
bash kernel/sched_ext/scripts/check_kernel_config.sh
bpftool version
clang --version | head -n 1
```

Proposed dependency installation, not executed during this audit:

```bash
sudo apt update
sudo apt install --no-install-recommends \
  libbpf-dev libelf-dev zlib1g-dev linux-source-7.0
```

Before using those packages, verify that `linux-source-7.0` resolves to the
same `7.0.12-2kali1` source version as the running kernel package. Installing
these development packages does not replace the running kernel, but it changes
host package state and therefore requires approval.

The maintained `p0_ownership_retest.sh`, `stage8_validate.sh`,
`reproduce_stage7_runtime.sh`, and `benchmarks/stage9/benchmark_compare.sh`
now delegate to the out-of-tree builder, exact-TID ownership gate, and
loader-scoped benchmark path. The historical 30-minute/fault-injection
portion of Stage 8 is explicitly reported as untested by the safe wrapper;
it is not silently counted as a pass.
