# Final validation report

This report records the strongest evidence available for product
`1.0.0-rc1`. States are explicit: `PASS`, `FAIL`, `BLOCKED`, or
`NOT APPLICABLE`. A compile is not an attach; a simulation is not a hardware
measurement; a request counter is not effective action evidence.

## 2026-08-25 bare-metal addendum

The latest campaign is recorded at
`/tmp/orchestra-realworld-20250825-042022-redshadow-complete`. On the
bare-metal `7.0.12+kali-amd64` host, the target-matched fix34 artifact passed
the strict capability check, BPF verifier, attach, exact-TID ownership gate,
and clean unload. Effective SLEEP/THROTTLE release, stale-signal rejection,
live S1/S2/S3/S4/Q telemetry, controller cadence/state publication, and
policy-driven YIELD were observed. CPU, I/O, and mixed repository stress
phases completed with ownership; memory remained explicitly blocked because
the existing `stress` tool is not installed.

The final state after every runtime phase was `sched_ext=disabled`; no broad
bpffs cleanup or reboot was performed. The verification rerun at
`/tmp/orchestra-realworld-20260825-verify-redshadow` repeated the repository
gate, strict capability check, P0 ownership/action gate, invalid-input checks,
and Stage 8 short comparison. Its preserved summary is
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825.md`.
The continuation also passed all six existing metrics-v7 userspace pipeline
invocations and reran the short CFS and ownership-gated ORCHESTRA stress
phases. Its command-level evidence is
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_CONTINUED.md`.
An extended matrix also passed owned 1/2/4/8-worker fixed-work CPU and mixed
comparisons, limited FIFO/RR/DEADLINE admission exclusion, owned loopback
network transfer, and a 30-second owned stress run. The preserved matrix is
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_MATRIX.md`.
The documented signal-publication microbenchmark completed 12/12 validated
userspace invocations. The exact paper gate remains `BLOCKED` because the
maintained runner does not implement the required nonzero-exemption,
3,000-tick, and 500-warm-up contract; details are preserved in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_PROTOCOLS.md`.
The v3–v6 manifests were also exercised and produced 24 preserved schema
failures because the current binary emits only the v7 ordered header; legacy
results were not silently pooled or promoted.
An unload-under-active-worker probe also completed successfully and is
preserved in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_RECOVERY.md`.
An isolated FIFO-versus-normal-worker contention probe recorded normal
forward progress; its harness cleanup defect and repair are documented in
`artifacts/real-world/20260825-042022-redshadow-complete/VERIFICATION_RERUN_20260825_RT_CONTENTION.md`.
This addendum supersedes only the older campaign's privilege/runtime
`BLOCKED` rows; it does not claim RT
coexistence, authenticated kernel signal frames, predictor convergence,
physical MIGRATE placement beyond the bounded probe, full multi-actuator/
rollback causality, NUMA or
distributed scheduling, or production soak readiness.

## Acceptance gates

| Gate | State | Evidence / command | Notes |
| --- | --- | --- | --- |
| Repository/build prerequisites | PASS | `make clean && make` | Existing userspace build completed in the campaign |
| Static/source checks | PASS | `make check`, `git diff --check` | Compiler, schemas, Python, and script checks |
| Unit/integration regression | PASS | `make test` | Existing campaign recorded 30/30 named unit tests and integration suites |
| Compatibility report | PASS | `./scripts/check-system.sh` | Observer capability report is non-mutating |
| Strict kernel capability on current host | PASS | `sudo orchestra check-system --strict` | All required capability/tool/config checks passed on the recorded host |
| Target-matched BPF build | PASS | `./scripts/build.sh --kernel` with recorded exact source/UAPI overrides | Artifact is target-specific; manifest records hashes |
| Verifier acceptance | PASS | `orchestra enable` / loader attach | Fix34 object loaded and passed the running kernel verifier |
| Attach and sysfs enabled state | PASS | `cat /sys/kernel/sched_ext/state` after loader attach | Enabled during each runtime phase; returned to disabled after unload |
| Exact task ownership | PASS | `p0_ownership_retest.sh` | Fix34 P0 gate completed with `failures=0` |
| All five effective actions | PASS | action-specific runtime matrix | RUN, SLEEP, THROTTLE, YIELD, and legal MIGRATE placement were observed on the current host; broad action matrices remain pending |
| RT/deadline bypass | BLOCKED | `SCHED_FIFO`, `SCHED_RR`, `SCHED_DEADLINE` coexistence | Admission guard exists; live coexistence is not claimed |
| Signal invalid/stale/replay handling | PASS | fix34 live signal probe plus userspace/source contract tests | Stale required-signal directives were rejected; kernel cryptographic authentication is not claimed |
| Prediction real-hardware behavior | BLOCKED | dynamic workload predictor matrix | Current kernel consumes bounded records; hardware convergence not tested |
| S1/S2/S3/S4/Q real-machine report | PASS | fix34 live telemetry/status captures | Limited owned-host windows exposed all four components and Q; broad population sampling remains pending |
| Controller response/rollback | BLOCKED | controller telemetry under attached load | Bounded actuator adaptation/recovery was observed; full multi-actuator behavior and rollback causality remain pending |
| Clean unload/recovery | PASS | loader detach and final state inventory | Every attached phase returned to disabled with no ORCHESTRA pins |
| Lifecycle ownership/foreign-owner refusal | PASS | source invariants; `orchestra` ownership gates; loader pin/link/schema checks | Live foreign-scheduler exercise remains part of the privileged gate |
| Privileged artifact integrity | PASS | installer/CLI source checks; build-manifest hash contract | Signed provenance and live install on the target matrix remain pending |
| Paper exact gate | BLOCKED | N=40, 4 exempt, 3000 ticks, 500 warm-up, seed 42, five seeds | No exact canonical runner currently established |
| Production soak/release | BLOCKED | 1 h/24 h, upgrade/rollback/recovery matrix | Release candidate only |

## Current-machine facts

The latest real-world campaign directory is
`/tmp/orchestra-realworld-20250825-042022-redshadow-complete`. The machine is
Kali rolling on x86_64, kernel `7.0.12+kali-amd64`, Intel i5-10310U, 4
cores/8 logical CPUs, and approximately 30 GiB RAM. sched_ext, BTF, BPF JIT,
and the required kernel configuration were present. Root filesystem
headroom was approximately 42 GiB and authorized `sudo` was available. The
host was used only for short, controlled, ownership-gated runs; no reboot or
destructive bpffs cleanup was performed.

The campaign preserved pre-existing kernel warnings and performed only
controlled loader-scoped attaches. It did not reboot, install packages, or
perform destructive bpffs cleanup. Every attached phase returned to
`sched_ext=disabled`.

## Evidence interpretation

- `PASS` means the named command or contract completed successfully under the
  stated environment; it does not upgrade a separate runtime gate.
- `BLOCKED` means the result requires privilege, exact kernel infrastructure,
  a dedicated host, or an implementation/test artifact not available here.
- Historical VM action/ownership evidence remains useful but is not silently
  substituted for current-host evidence.
- CFS comparisons are not assigned a fabricated `Q_CFS`; the paper does not
  define a valid mapping from CFS observations to ORCHESTRA's signal/action
  index.

## Reproduction sequence

```bash
./scripts/check-system.sh
make clean && make && make check && make test
./scripts/build.sh --userspace --bridge
./scripts/policy_load.py --bridge \
  /var/tmp/orchestra-os-build-"$(id -u)"/orchestra_bridge \
  --dry-run config/examples/safe.json
```

On a separately authorized compatible host, add the exact-kernel build,
strict check, loader attach, ownership gate, per-action matrix, telemetry,
fault/recovery, unload, and soak procedures from the installation,
troubleshooting, and archived real-machine checklist documents.

The security-specific source and documentation checks are:

```bash
scripts/security-scan.sh
python3 tests/unit/test_orchestra_scx_source.py
```

These checks cover source contracts and local artifact handling. They do not
replace cryptographic review, signed release verification, fuzzing, or a
privileged verifier/attach/ownership test.
