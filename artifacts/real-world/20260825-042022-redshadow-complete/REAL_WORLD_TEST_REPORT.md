# Real-World Test Report

## Environment

The campaign ran on bare-metal `redshadow`, Kali GNU/Linux, kernel
`7.0.12+kali-amd64`, x86_64, 8 logical CPUs, and approximately 30 GiB RAM.
The exact target-matched source export was
`/var/tmp/orchestra-kernel-source-7.0.12/linux-source-7.0`.

## Build and regression

| Gate | Result | Evidence |
| --- | --- | --- |
| Userspace build | PASS | `build/final-fix34/regression.stdout` |
| Static/document/security checks | PASS | `make check` output in the same log |
| Unit/integration/security tests | PASS | `make test`; 30/30 named unit tests |
| Target-matched BPF/bridge/loader build | PASS | `build/fix34/build.stdout` |
| Installed artifact manifest | PASS | `/usr/local/lib/orchestra-os/build/build-manifest.txt` |
| Strict capability check | PASS | `recovery/final-fix34/check.stdout` |
| Diff and shell syntax | PASS | `recovery/final-fix34/return_codes.txt` |

## Kernel runtime

| Capability | Result | Evidence |
| --- | --- | --- |
| Verifier and attach | PASS | `sched_ext/p0-fix34/load.stdout` |
| Exact-TID ownership and forward progress | PASS | `sched_ext/p0-fix34/results.csv`, zero failures |
| RUN | PASS | P0 owned finite workload |
| SLEEP | PASS | `sched_ext/fix34-actions-live/status.sleep.deferred` and `.released` |
| THROTTLE | PASS | `sched_ext/fix34-actions-live/status.throttle.deferred` and `.released` |
| YIELD | PASS | `sched_ext/fix34-policy-live/status.after` |
| MIGRATE | PASS | `sched_ext/fix34-migrate-live/status.after` |
| Signal freshness | PASS | `sched_ext/fix34-actions-live/status.signal.stale`; stale counter increases and stale action is not applied |
| Policy lifecycle | PASS | `sched_ext/fix34-policy-live/status.after`; policy lookup and commit counters increase |
| S1/S2/S3/S4/Q | PASS, limited | `sched_ext/fix34-actions-live/status.signal.stale`; all four components and Q are exposed |
| Controller actuator adaptation | PASS, bounded | `sched_ext/fix34-actuator-live/status.deficit` and `.recovery` |
| Clean unload | PASS | `sched_ext/p0-fix34/unload.stdout` and final state inventory |

## Workloads

| Workload | Result | Evidence |
| --- | --- | --- |
| CFS CPU/I/O/mixed stress | PASS | `stress/cfs/results.csv` |
| ORCHESTRA CPU/I/O/mixed stress | PASS, owned | `stress/orchestra-final/results/results.csv` |
| CFS/scx_simple/ORCHESTRA CPU comparison | PASS, exploratory | `workload/benchmark-final-three-way/results.csv` |
| CFS/scx_simple/ORCHESTRA mixed comparison | PASS, exploratory | `workload/benchmark-final-three-way-mixed/results.csv` |
| Memory pressure | BLOCKED | existing `stress` command is not installed |

## Interpretation

The results establish `KERNEL_PROTOTYPED` plus limited
`EXPERIMENTALLY_VALIDATED` bare-metal evidence. They do not establish
`DEPLOYMENT_READY`. Timing rows are exploratory: they use short runs and do
not constitute a performance acceptance threshold or a Q comparison against
CFS.

## Remaining gates

- RT/deadline coexistence and starvation/fairness under priority load.
- Full actuator matrix, rollback, and causal performance response.
- Online predictor convergence and authenticated kernel signal frames.
- CPU hotplug, cgroups, PID reuse, map capacity, and broader fault campaigns.
- Multi-node NUMA, distributed scheduling, and long-duration soak/recovery.
- Memory pressure with the repository-approved existing tool.
