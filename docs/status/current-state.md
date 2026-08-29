# ORCHESTRA-OS verified current state

- Audit date: 2026-08-29
- Repository commit: release candidate commit recorded by the release tag
- Working tree: frozen before release-gate execution; generated campaign
  evidence remains outside the public source export.
- Branch: `main` in the public distribution repository
- Current milestone: ORCHESTRA-OS `1.0.0` research-stable product integration with a
  foreground sched_ext lifecycle, ownership-checked loader, target-matched
  build/install path, explicit security boundary, and controlled bare-metal
  v8 sched_ext acceptance evidence
- Active work-package boundary: WP1 kernel foundation plus a source/build-validated v8 policy/runtime path, with userspace precursors for WP2-WP6
- Overall claim class: mixed; research-stable userspace/package surface with
  target-specific experimental sched_ext evidence; see the component
  boundaries below

This file is a handoff index. The paper, ADRs, experiment contracts, raw
artifacts, and test protocols remain authoritative for their respective
decisions and evidence.

## 2026-08-25 bare-metal runtime completion campaign

The controlled bare-metal campaign at
`/tmp/orchestra-realworld-20250825-042022-redshadow-complete` completed on
Kali kernel `7.0.12+kali-amd64` with eight logical CPUs. The exact
target-matched object, bridge, and loader built from the preserved 7.0.12
source export, passed the verifier, attached, and unloaded cleanly. The final
P0 ownership gate recorded zero failures in
`sched_ext/p0-fix34/results.csv`.

The runtime evidence includes effective deferred `SLEEP` and `THROTTLE`
release, fail-closed stale-signal rejection, live S1/S2/S3/S4/Q publication,
controller cadence/state transitions, and a committed policy entry that drove
an owned task to effective `YIELD`. The repository benchmark and stress
scripts completed owned CPU, I/O, and mixed workload phases. The final machine
state is `disabled`, with no ORCHESTRA pins left in bpffs.

This supersedes the earlier session-specific statements that privileged
bare-metal attach and ownership testing had not been run. It does not upgrade
the product to deployment-ready: RT coexistence, broad migration placement,
multi-actuator breadth/rollback causality, predictor convergence, authenticated kernel
signals, NUMA, distributed scheduling, and long-duration soak evidence remain
open acceptance work.

## 2026-08-25 verification rerun

The installed fix34 artifacts were rerun from
`/tmp/orchestra-realworld-20260825-verify-redshadow`. `git diff --check`,
`make check`, `make test`, and `sudo orchestra check-system --strict` all
returned zero. The maintained P0 and Stage 8 entry points passed exact-TID
ownership, forward progress, all action probes, attach, and clean unload.
Invalid PID and migration-CPU requests were rejected. A task admitted while
normal was removed from the kernel identity map when changed to `SCHED_FIFO`,
so protected RT work was not handed to the adaptive path; this is limited RT
admission evidence, not full RT coexistence validation. The final state was
`sched_ext=disabled` with only `/sys/fs/bpf` remaining.

The preserved command-level record is
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825.md`.
The continued rerun additionally validated the existing metrics-v7 pipeline
six times and completed owned CPU, I/O, and mixed stress phases; its evidence
is recorded in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_CONTINUED.md`.
The extended matrix then covered fixed-work 1/2/4/8-worker CPU and mixed
comparisons, actual FIFO/RR/DEADLINE admission exclusion, owned loopback
network transfer, and a 30-second owned stress run. Evidence is recorded in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_MATRIX.md`.
The documented signal-publication microbenchmark also completed 12/12
validated userspace invocations. The exact paper N=40/4-exempt/3,000-tick/
500-warm-up gate remains blocked because the maintained runner has no matching
canonical protocol; this distinction is recorded in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_PROTOCOLS.md`.
The same run also confirmed that the maintained v3–v6 manifests are stale
against the current v7-only binary output; all 24 legacy attempts were
preserved as schema failures rather than relabeled as valid evidence.
An additional unload-under-active-worker probe passed with forward progress
and clean disabled state; its record is
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_RECOVERY.md`.
An isolated FIFO-versus-normal-worker contention probe also showed normal
forward progress; its temporary harness cleanup defect and immediate repair
are preserved in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_RT_CONTENTION.md`.

The historical entries below retain earlier campaign context. Where they say
that verifier, attach, or privileged runtime testing was unavailable, those
statements describe the earlier session and are superseded by the fix34
bare-metal addenda above.

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
cryptographic authentication of the kernel signal frame, distro package
signatures, fuzzing/SCA review, and privileged runtime security gates are not
claimed by this research-stable release. See
`docs/security/SECURITY.md` and `FINAL_PRODUCT_STATUS.md`.

## 2026-08-21 working-tree follow-up

The latest bare-metal continuation established controlled ORCHESTRA runtime
evidence. The older comparison rows started work before exact opt-in and
therefore remain `INCONCLUSIVE_OWNERSHIP_NOT_PROVEN`; the new short comparison
rows are the only current exploratory timing evidence.
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
The 2026-08-25 campaign additionally verified verifier acceptance, attach,
explicit ownership, bounded effective actions, live v10 telemetry, and clean
unload on the recorded bare-metal kernel. The timing rows remain exploratory
and do not establish deployment readiness.
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
- controlled bare-metal sched_ext execution evidence is now present, but it is
  limited to the recorded host and short ownership-gated runs;
- no kernel implementation of authenticated signal frames, online predictor
  training, or distributed/NUMA policy learning. The
  v8 path consumes bounded externally supplied prediction/coordination records,
  publishes generation-safe policy banks, applies explicit controller-state
  gates, computes live bounded v10 coordination summaries, and records
  task/action telemetry; it does not establish online learning convergence or
  broad hardware behavior.

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
hot task state, diagnostics, and global telemetry maps. The earlier
source/build-only statement in this historical subsection is superseded by the
fix34 bare-metal verifier, attach, ownership, and unload evidence above.

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
- The current host passed the bare-metal BPF verifier, attach, ownership,
  bounded action, watchdog, and detach gates; broader kernel matrices remain
  pending.
- YIELD remains a bounded relinquish approximation; physical fairness and
  latency validation remain pending.
- THROTTLE and SLEEP use bounded deferred eligibility; physical timing and
  bandwidth validation remain pending.
- MIGRATE placement was observed in the VM and one legal current-host
  placement; hotplug and physical-contention behavior remain unvalidated.
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
  do not prove ORCHESTRA-owned scheduling. The new benchmark rows prove
  ownership for their short runs but remain exploratory performance evidence.

## Current machine

Readiness classification: **controlled bare-metal pilot passed; research-stable
userspace/package release; sched_ext remains experimental and not
deployment-ready**.

| Item | Verified value |
| --- | --- |
| OS | Kali GNU/Linux Rolling 2026.3 (Debian family) |
| Kernel | `7.0.12+kali-amd64` |
| Architecture | x86_64 |
| Virtualization | none detected (bare metal) |
| CPU | Intel Core i5-10310U, 1 socket, 4 cores, SMT2, 8 logical CPUs |
| NUMA | one node, CPUs 0-7 |
| sched_ext sysfs | present; state `disabled` after the controlled campaign |
| BTF | `/sys/kernel/btf/vmlinux`, present |
| Required config | BPF, BPF_SYSCALL, BPF_JIT, DEBUG_INFO_BTF, BPF_EVENTS, and SCHED_CLASS_EXT all `y` |
| clang | 21.1.8 |
| bpftool/libbpf runtime | bpftool 7.7.0; libbpf 1.7 |
| Rust | rustc/cargo 1.95.0; not required by the current C prototype |
| Missing build inputs | distro `libbpf-dev`/`libelf-dev`; preserved source export and recorded helper inputs were used |

No kernel replacement or reboot is indicated. Root is required for BPF load,
map pinning, task opt-in, and detach. Unprivileged BPF is disabled.

## Next acceptance gate

The bare-metal Linux 7.0.12 build/verifier compatibility and
non-destructive explicit-ownership gate is complete for this host. The
2026-08-28 campaign also covered bounded FIFO/RR contention, same-node
MIGRATE placement, and a 30-minute CPU/I/O/mixed soak. The next acceptance
gate is the remaining full-scope pilot: hard-RT/deadline coexistence, broad
physical migration and NUMA behavior, multi-actuator response/rollback,
fault-recovery breadth, predictor convergence, distributed scheduling, and
release documentation. It is not another simulator feature and not the
SuperTuxKart demonstration.

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

## 2026-08-28 real-world campaign addendum

The bounded follow-up campaign is preserved in the local evidence workspace at
`artifacts/real-world/20260828-132302-redshadow-fix-implementation/`. The
generated campaign directory is intentionally excluded from the source
release. Its complete claim ledger is `REAL_WORLD_TEST_REPORT.md`, and its
evidence audit is `audit/CLAIM_AUDIT.md` within that local package.
It used the current source revision, the exact running kernel
(`7.0.12+kali-amd64`), and a target-matched BPF/bridge/loader build. The
userspace regression gate passed (30/30 named unit tests plus integration and
security validators), the fixed runtime matrix recorded 81/81 passing rows,
and the final scheduler state was `disabled` with no ORCHESTRA pins left in
bpffs.

The strongest bounded claims from that campaign are:

| Area | Classification | Bounded evidence and limit |
| --- | --- | --- |
| Fixed-work performance comparison | `EXPERIMENTALLY_VALIDATED` (exploratory) | Three CFS and three ownership-proven ORCHESTRA repetitions for CPU and mixed workloads. These are completion-time observations only; they do not establish causal superiority, production overhead, or a valid `Q` comparison. `scx_simple` and tool-dependent memory rows were blocked. |
| CPU/I/O/mixed stress | `EXPERIMENTALLY_VALIDATED` (bounded) | Existing stress suite completed short, medium, and one 30-minute ORCHESTRA phases with exact ownership, clean health scan, and unload. The missing `stress` utility blocked the memory variant. |
| RT coexistence | `EXPERIMENTALLY_VALIDATED` (bounded) | Normal-task ownership coexisted with FIFO/RR contention and the admission matrix rejected adaptive opt-in for FIFO/RR/DEADLINE. DEADLINE contention was environment-blocked (`EPERM`); no hard-RT latency, starvation, or priority-inversion guarantee is claimed. |
| Signal integrity | `EXPERIMENTALLY_VALIDATED` (userspace only) | Existing tamper/replay/stale integration checks and the documented publication microbenchmark passed (12/12 runs, 12,000 publications, 27,161 verified reads). The kernel bridge remains local-trust; no kernel-side HMAC/authentication claim is made. |
| NUMA | `EXPERIMENTALLY_VALIDATED` (single-node only) | The host has one NUMA node; same-node affinity/migration evidence was collected. Cross-node placement and NUMA-aware policy remain untestable here. |
| Distributed scheduling | `NOT_IMPLEMENTED` | No distributed backend, node identity, remote ordering, or partition protocol exists in the current implementation. Loopback transfer evidence is transport smoke testing, not distributed scheduler validation. |
| Long duration | `EXPERIMENTALLY_VALIDATED` (bounded soak) | A 30-minute scheduler-enabled CPU/I/O/mixed sequence completed with exact ownership, no new critical health lines, maximum observed package temperature 87°C, and clean unload. This is not a production soak or deployment-readiness result. |

These results update the evidence envelope only; they do not close the
remaining full-scope gates for kernel cryptographic authentication, cross-node
NUMA, distributed scheduling, hard-RT coexistence, predictor convergence,
multi-actuator causal control, or production deployment readiness.
