# Verification Rerun — Extended Matrix — 2026-08-25

This record extends the fix34 bare-metal evidence using existing repository
runners and standard Linux tools. It does not modify ORCHESTRA source, tests,
kernel configuration, packages, or persistent system state.

## Additional Results

| Check | Result | Evidence |
| --- | --- | --- |
| Fixed-iteration workload build | PASS | existing `fixed_work.c` compiled with `-Wall -Wextra -Werror` |
| Fixed-work CPU matrix | PASS, exploratory | CFS and ORCHESTRA completed at 1/2/4/8 workers; every ORCHESTRA row passed ownership |
| Fixed-work mixed matrix | PASS, exploratory | CFS and ORCHESTRA completed at 1/2/4/8 workers; every ORCHESTRA row passed ownership |
| RT admission: `SCHED_FIFO` | PASS, limited | actual FIFO task rejected by opt-in/publish/status with return code `6` |
| RT admission: `SCHED_RR` | PASS, limited | actual RR task rejected by opt-in/publish/status with return code `6` |
| RT admission: `SCHED_DEADLINE` | PASS, limited | actual deadline task rejected by opt-in/publish/status with return code `6` |
| Loopback network transfer | PASS, exploratory | 32 MiB CFS and ORCHESTRA transfers completed; owned rerun recorded endpoint dispatch |
| Owned network endpoint gate | PASS, limited | server `accepted=2 dispatched=2 running=2`; client `accepted=21 dispatched=21 running=21` |
| Medium owned stress | PASS | 30-second CPU, I/O, and mixed phases completed with zero errors/warnings |
| Medium memory phase | BLOCKED | existing suite cannot run without `stress` and cannot prove child ownership |
| Cleanup after all probes | PASS | every attach unloaded; final state disabled with no ORCHESTRA bpffs pins |

## Raw Evidence

- Commands and return codes: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/COMMANDS.log`
- CPU comparison: `/tmp/orchestra-realworld-20260825-verify-redshadow/continued/benchmark-fixed-cpu/results.csv`
- Mixed comparison: `/tmp/orchestra-realworld-20260825-verify-redshadow/continued/benchmark-fixed-mixed/results.csv`
- RT class matrix: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/rt-class-matrix/`
- Corrected FIFO probe: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/rt-coexistence-final/`
- Owned network transfer: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/network-loopback-owned/`
- Medium stress: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/stress-orchestra-30/results.csv`

The network timings and benchmark rows are descriptive exploratory evidence;
they do not establish a causal scheduler-performance improvement. The RT
matrix proves admission exclusion for the three tested Linux real-time
classes, not priority behavior, starvation freedom, deadlock freedom, or
full mixed-class coexistence under sustained load.

## Still Incomplete

- Memory pressure requires an approved installed stress tool and an exact-TID
  protocol for child workers.
- `scx_simple` is unavailable on this host.
- Kernel cryptographic signal authentication, predictor convergence, complete
  controller rollback causality, and signed release provenance remain absent
  or unvalidated.
- Hotplug/fault matrices, fairness under RT load, NUMA behavior, distributed
  scheduling, the exact paper acceptance gate, and long-duration soak remain
  open.

The final host state after this matrix is `sched_ext=disabled`; `/sys/fs/bpf`
contains only its root mount.
