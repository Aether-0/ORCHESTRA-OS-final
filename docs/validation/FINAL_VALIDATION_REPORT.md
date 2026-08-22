# Final validation report

This report records the strongest evidence available for product
`1.0.0-rc1`. States are explicit: `PASS`, `FAIL`, `BLOCKED`, or
`NOT APPLICABLE`. A compile is not an attach; a simulation is not a hardware
measurement; a request counter is not effective action evidence.

## Acceptance gates

| Gate | State | Evidence / command | Notes |
| --- | --- | --- | --- |
| Repository/build prerequisites | PASS | `make clean && make` | Existing userspace build completed in the campaign |
| Static/source checks | PASS | `make check`, `git diff --check` | Compiler, schemas, Python, and script checks |
| Unit/integration regression | PASS | `make test` | Existing campaign recorded 30/30 named unit tests and integration suites |
| Compatibility report | PASS | `./scripts/check-system.sh` | Observer capability report is non-mutating |
| Strict kernel capability on current host | BLOCKED | `./scripts/check-system.sh --strict` | Current host has sched_ext/BTF but lacks authorized runtime conditions and is not dedicated |
| Target-matched BPF build | PASS | `./scripts/build.sh --kernel` with recorded exact source/UAPI overrides | Artifact is target-specific; manifest records hashes |
| Verifier acceptance | BLOCKED | `orchestra enable` | No authorized non-interactive root session in current campaign |
| Attach and sysfs enabled state | BLOCKED | `cat /sys/kernel/sched_ext/state` after loader attach | Not run on the current host |
| Exact task ownership | BLOCKED | opt-in and enqueue/running telemetry gate | Existing VM evidence is archival; no current-host rerun |
| All five effective actions | BLOCKED | action-specific runtime matrix | Source semantics exist; live effectiveness requires ownership |
| RT/deadline bypass | BLOCKED | `SCHED_FIFO`, `SCHED_RR`, `SCHED_DEADLINE` coexistence | Admission guard exists; live coexistence is not claimed |
| Signal invalid/stale/replay handling | PASS | userspace/source contract tests and bridge checks | Kernel cryptographic authentication is not claimed |
| Prediction real-hardware behavior | BLOCKED | dynamic workload predictor matrix | Current kernel consumes bounded records; hardware convergence not tested |
| S1/S2/S3/S4/Q real-machine report | BLOCKED | coordination window ownership gate | Source/unit contract exists; no fabricated Q |
| Controller response/rollback | BLOCKED | controller telemetry under attached load | Source/unit contract exists; live causality pending |
| Clean unload/recovery | BLOCKED | loader detach and state verification | Requires privileged attach on an authorized host |
| Lifecycle ownership/foreign-owner refusal | PASS | source invariants; `orchestra` ownership gates; loader pin/link/schema checks | Live foreign-scheduler exercise remains part of the privileged gate |
| Privileged artifact integrity | PASS | installer/CLI source checks; build-manifest hash contract | Signed provenance and live install on the target matrix remain pending |
| Paper exact gate | BLOCKED | N=40, 4 exempt, 3000 ticks, 500 warm-up, seed 42, five seeds | No exact canonical runner currently established |
| Production soak/release | BLOCKED | 1 h/24 h, upgrade/rollback/recovery matrix | Release candidate only |

## Current-machine facts

The real-world campaign directory was
`/tmp/orchestra-realworld-20260823-015354-redshadow`. The machine is Kali
rolling on x86_64, kernel `7.0.12+kali-amd64`, Intel i5-10310U, 4 cores/8
logical CPUs, and approximately 30 GiB RAM. sched_ext, BTF, BPF JIT, and the
required kernel configuration were present. Root filesystem headroom was
approximately 5.7 GiB and non-interactive `sudo` was unavailable. The host
was not treated as a dedicated crash-risk scheduler test machine.

The campaign preserved pre-existing kernel warnings and did not perform a
kernel attach, reboot, package installation, or destructive bpffs cleanup.

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
