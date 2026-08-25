# ORCHESTRA-OS Real-Machine Benchmarks

## Quick Start

```bash
# 1. Check your machine
bash sanity_check.sh

# 2. Run stress tests (60 seconds)
bash stress_suite.sh 60 cfs        # CFS baseline
bash stress_suite.sh 60 orchestra  # ORCHESTRA (load scheduler first)

# 3. Run benchmarks (CFS vs scx_simple vs ORCHESTRA)
bash benchmark_suite.sh

# Full 1/2/4/8-worker mixed comparison
bash full_compare.sh
```

## Scripts

| Script | Purpose |
|--------|---------|
| `sanity_check.sh` | Check kernel, tools, sched_ext readiness |
| `stress_suite.sh` | CPU, memory, I/O, mixed workload stress |
| `benchmark_suite.sh` | Location-independent CFS vs scx_simple vs ORCHESTRA comparison with an exact-TID ownership gate |
| `full_compare.sh` | 1/2/4/8-worker mixed comparison using the same safe runner |

## Stress Tests

```
CPU stress     → N workers × 100% CPU spin loop
Memory stress  → bounded pressure (two workers together use at most half of available RAM)
I/O stress     → Parallel read/write operations
Mixed workload → CPU + I/O combined
Health check   → Kernel log scan for panics/stalls
```

In `orchestra` mode, CPU, I/O, mixed, and memory workers must each pass
exact-TID opt-in, RUN publication, and positive accepted/dispatched/running
telemetry before the phase is attributed to ORCHESTRA. The memory phase
discovers `stress --vm` child workers from the process tree and admits each
child individually. If discovery or admission fails, that row remains
`BLOCKED_OWNERSHIP_NOT_PROVEN` and is not attributed to ORCHESTRA.

## Benchmark Workloads

- 1 worker  × 30 seconds
- 2 workers × 30 seconds
- 4 workers × 30 seconds
- 8 workers × 30 seconds

Compared across CFS, scx_simple, ORCHESTRA.

## Output

All results in `/tmp/orchestra-bench-<timestamp>/results.csv` and `/tmp/orchestra-stress-<timestamp>/results.csv`.

The benchmark runner uses the repository artifact paths by default. When the
artifacts were built out of tree (the required kernel-build mode), set
`ORCHESTRA_BUILD_DIR` to that exact build directory; the runner then derives
the BPF object, bridge, and loader paths from it. Explicit `ORCHESTRA_BPF`,
`ORCHESTRA_BRIDGE`, and `ORCHESTRA_LOADER` values take precedence. For
example:

```bash
ORCHESTRA_BUILD_DIR=/var/tmp/orchestra-os-build-$(id -u) \
  bash benchmark_suite.sh
```

The runner uses the existing loader for ORCHESTRA attach/unload and refuses to
detach unrelated sched_ext links or delete unrelated bpffs pins. ORCHESTRA
rows are not performance results unless every target TID has positive
accepted, dispatched, and running telemetry before release. Set
`ORCHESTRA_SCX_SIMPLE` when the comparison scheduler is built outside
`/usr/bin`; the fixed-iteration helper returns success after completing the
requested work, and each benchmark run records worker exit status and stderr.
Both runners use the process's allowed CPU set rather than assuming CPU IDs
start at zero. Set `ORCHESTRA_OWNERSHIP_POLLS` to adjust the bounded ownership
wait and `ORCHESTRA_WAIT_TIMEOUT` to bound owned-worker teardown. The stress
suite similarly uses `ORCHESTRA_PHASE_TIMEOUT` and creates a unique per-run
work directory, so interrupted runs do not remove a previous run's files. It
records filesystem space before/after teardown and samples thermal zones; if a
reported critical trip point is reached, the stress campaign terminates its
own workers and records the event.
