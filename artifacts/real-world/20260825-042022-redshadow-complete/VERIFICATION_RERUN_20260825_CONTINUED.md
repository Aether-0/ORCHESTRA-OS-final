# Verification Rerun — Continued — 2026-08-25

This continuation runs the existing modern userspace metrics pipeline and the
short existing stress suite against the installed fix34 artifacts. No source,
test, kernel, package, or persistent system configuration was changed.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Existing metrics-v7 pipeline | PASS | 6/6 invocations validated; strict 121-column schema accepted |
| Kernel sanity check | PASS | required tools/configuration/BTF/sched_ext checks returned `0` |
| Kernel configuration check | PASS | required configs and available optional diagnostics returned `0` |
| CFS CPU/I/O/mixed stress | PASS | all three phases completed with zero errors/warnings |
| CFS memory stress | BLOCKED | existing `stress` command is not installed |
| ORCHESTRA CPU stress | PASS | 8/8 exact-TID workers passed accepted/dispatched/running ownership gate |
| ORCHESTRA I/O stress | PASS | 4/4 exact-TID workers passed ownership gate |
| ORCHESTRA mixed stress | PASS | 8/8 exact-TID workers passed ownership gate |
| ORCHESTRA memory stress | BLOCKED | child-worker identities are not exposed by the existing suite |
| Unload and cleanup | PASS | loader unload returned `0`; final `sched_ext=disabled`; no ORCHESTRA bpffs pins |
| Final strict capability/status checks | PASS | both commands returned `0`; scheduler inactive after cleanup |

## Evidence

- Commands and return codes: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/COMMANDS.log`
- Metrics-v7 result: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/smoke-v7/benchmark_result.json`
- CFS results: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/stress-cfs/results.csv`
- ORCHESTRA results: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/stress-orchestra/results.csv`
- Final strict check: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/final-strict.stdout`
- Final status: `/tmp/orchestra-realworld-20250825-verify-redshadow/remaining/final-status.stdout`

The metrics-v7 run remains userspace pipeline evidence. Its S1/S2/S3/S4/Q,
predictor, policy, and controller fields do not prove kernel scheduling,
cryptographic signal authentication, or deployment readiness.

## Remaining Gates

- Kernel-side cryptographic signal authentication is not implemented.
- Online predictor convergence and full staged controller rollback remain unvalidated.
- Full RT coexistence, hotplug/fault matrices, and fairness under RT load remain untested.
- Memory pressure requires an existing approved tool that is currently absent.
- NUMA testing is unavailable on this single-NUMA-node host; distributed scheduling has no backend.
- `scx_simple` comparison is unavailable because the executable is not installed.
- Long-duration soak and production/deployment-readiness validation remain open.

## Final Host State

The scheduler is disabled, `/sys/fs/bpf` contains only its root mount, and no
reboot, kernel replacement, package installation, or broad bpffs cleanup was
performed.
