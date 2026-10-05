# Known Recommendations & Fixes — ORCHESTRA Kernel Path

**Claim class:** VirtualBox userspace / kernel-prototyped (not bare-metal)
**Evidence date:** 2026-08-13
**Object under test:** `kernel/sched_ext/orchestra_scx_stage7.bpf.o`
**Guest:** orchestra-scx-lab · Fedora 40 · Linux 6.12.96 · 4 vCPU / 8 GB

Companion reports:

- [Before / After Report (HTML)](./artifacts/test-results/2026-08-13/vbox-mcp/ORCHESTRA_Before_After_Report.html)
- [Kernel Benchmark Report (HTML)](./artifacts/test-results/2026-08-13/vbox-mcp/ORCHESTRA_Kernel_Benchmark_Report.html)
- [Kernel Improvement Report (HTML)](./artifacts/test-results/2026-08-13/vbox-mcp/ORCHESTRA_Kernel_Improvement_Report.html)
- [VBox Test Ladder Summary](./artifacts/test-results/2026-08-13/vbox-mcp/SUMMARY.md)

---

## Executive verdict

The highest-priority problem from the 2026-08-13 campaign is **not** “ORCHESTRA is X% slower.” It is that with `SCX_OPS_SWITCH_PARTIAL`, Stage 8 showed **`enqueue=0` and `run=0`** while **`dispatch` kept climbing**. Many runs labeled “ORCHESTRA” may have loaded sched_ext ops without actually scheduling the busy-loop workers (still largely on CFS), plus idle/dispatch callback noise.

Fix order:

1. Prove task ownership under ORCHESTRA
2. Fast-path enqueue for default RUN
3. Real MIGRATE / SLEEP / THROTTLE semantics
4. Honest harness metrics
5. Bare-metal statistical re-benchmark

---

## Evidence snapshot

### Stage 8 multicore probe

| Label | CPUs | enqueue | dispatch | run | elapsed_ms |
|-------|------|---------|----------|-----|------------|
| mcpu1 | 1 | 0 | 6926 | 0 | 10017 |
| mcpu2 | 2 | 0 | 12660 | 0 | 10040 |
| mcpu4 | 4 | 0 | 13234 | 0 | 10100 |

### Stage 8 stability (1802 s)

- `sched_ext` state remained **enabled**
- Memory ~383–393 MB
- `dispatch` rose ~13.5k → ~105k
- `run` / `yield` / `migrate` / `throttle` / `sleep` stayed **0**

### Stage 9 (4 CPU × 30s busy loops)

| Scheduler | elapsed_ms | Cumulative `/proc` ctxt | Δ vs CFS |
|-----------|------------|-------------------------|----------|
| CFS | 29991 | 17844721 | baseline |
| scx_simple | 29747 | 18624312 | +779591 |
| ORCHESTRA | 29447 | 19502095 | +1657374 |

Fixed-duration busy loops make wall time a weak overhead metric.

---

## P0 — Must fix before claiming kernel comparisons

### 1. Partial switching: workloads may not be on ORCHESTRA

**Why:** Ops use partial switch. Enqueue/run did not advance under load while dispatch did.

**Where:** [`kernel/sched_ext/orchestra_scx_stage7.bpf.c`](./kernel/sched_ext/orchestra_scx_stage7.bpf.c) — `SCX_OPS_DEFINE` / `.flags = SCX_OPS_SWITCH_PARTIAL`

**Recommended fix:**

- For controlled experiments: evaluate full switch carefully, **or** explicitly move worker PIDs into the sched_ext class.
- Gate every “ORCHESTRA” benchmark on rising `enqueue_count` / `task_enable_count` (ownership proof).
- Instrument `.running` / `.enable` for per-task ownership traces (currently empty/light).

### 2. Enqueue hot path does too much

**Why:** Prior VM study ~+20% vs CFS on short CPU jobs; higher ctxt activity under ORCHESTRA aligns with BPF callback + map lookup cost.

**Where:** `orchestra_sched_enqueue()` in [`orchestra_scx_stage7.bpf.c`](./kernel/sched_ext/orchestra_scx_stage7.bpf.c)

Current path always: telemetry → active directive → identity → controller gate → action switch → `bridge_record_task()` map write.

**Recommended fix:**

- Fast-path default RUN; skip task-map writes when not required.
- Cache last accepted directive generation (BPF global/per-CPU); skip full re-parse when unchanged.
- Sample non-critical bookkeeping; keep fail-closed identity checks.
- Profile with `bpftool prog profile` / `perf` after ownership is proven.

---

## P1 — Correctness and fidelity

### 3. Canonical actions incomplete

| Action | Current behavior | Fix location |
|--------|------------------|--------------|
| MIGRATE | Still `SCX_DSQ_LOCAL`; target CPU not really applied | `BRIDGE_ACT_MIGRATE` (~339–362) |
| SLEEP | Short/min slice deferral, not true sleep | `BRIDGE_ACT_SLEEP` (~398–428) |
| THROTTLE | Reduced slice only | `BRIDGE_ACT_THROTTLE` (~364–395) |
| YIELD | Local dispatch with different slice vs RUN | `BRIDGE_ACT_YIELD` — clarify semantics |

Also documented in [`docs/final_report/15_LIMITATIONS.md`](./docs/final_report/15_LIMITATIONS.md).

### 4. `.dispatch` is counter-only

**Where:** `orchestra_sched_dispatch()` in `orchestra_scx_stage7.bpf.c`

**Recommended fix:** Implement real consume/balance, **or** split telemetry into idle-dispatch vs enqueue-driven dispatch so reports stay honest.

### 5. Bridge / control-plane fragility

**Where:**

- [`kernel/sched_ext/bridge/orchestra_bridge.c`](./kernel/sched_ext/bridge/orchestra_bridge.c)
- [`kernel/sched_ext/include/orchestra_bridge_v1.h`](./kernel/sched_ext/include/orchestra_bridge_v1.h)
- Load scripts under `kernel/sched_ext/scripts/` and `benchmarks/real-machine/`

**Issues:** Maps must be re-pinned after every load; controller is userspace-only; benches without published directives to worker PIDs do not exercise adaptive policy.

**Recommended fix:**

- Auto-pin helper after `struct_ops register`.
- Benchmark protocol: publish RUN/YIELD/THROTTLE to **worker PIDs**, verify via bridge status + telemetry.
- Later (WP2): integrity/freshness if maps cross trust boundaries.

---

## P2 — Measurement and scale

### 6. Harness defects (blocking science)

1. `benchmarks/real-machine/benchmark_suite.sh` — `wait $(jobs -p)` hangs on background `scx_simple`.
2. Context switches from cumulative `/proc/stat` — not per-run deltas.
3. Fixed-duration busy loops → wall time ≈ duration for all schedulers.
4. `kernel/sched_ext/scripts/stage8_validate.sh` — `set -e` aborts on rejected invalid PID; multicore helper used invalid bash `volatile`.

**Preferred workloads after P0 ownership fix:** yield-throughput (`sched_yield`), completion-time jobs, wakeup-latency (`nanosleep` excess) — see [`docs/final_report/14_BENCHMARK_RESULTS.md`](./docs/final_report/14_BENCHMARK_RESULTS.md).

### 7. Platform limits

- VirtualBox only so far; bare-metal required for publishable performance claims.
- NUMA / 8+ worker scaling not validated on this path.
- Bridge maps: local trust only (no cryptographic authentication).

---

## Priority roadmap

| Pri | Fix | Primary files | Expected effect |
|-----|-----|---------------|-----------------|
| P0 | Prove/force tasks onto ORCHESTRA; gate benches on enqueue/run | `orchestra_scx_stage7.bpf.c` + harness | Valid kernel comparisons |
| P0 | Fast-path enqueue for default RUN | `orchestra_scx_stage7.bpf.c` | Lower BPF overhead |
| P1 | Real MIGRATE DSQ / CPU targeting | MIGRATE branch | Canonical MIGRATE |
| P1 | Honest SLEEP / THROTTLE semantics | SLEEP / THROTTLE branches | Research fidelity |
| P1 | Fix harness metrics & scx hang | `benchmarks/real-machine/*`, stage8/9 scripts | Trustworthy numbers |
| P2 | Bare-metal + statistical protocol | WP7 methodology | Publishable claims |
| P2 | Map pin UX + optional integrity | bridge + load scripts | Reliability / WP2 |

---

## Definition of “improved”

A kernel change counts as improved only when all hold:

1. Ownership gate: ORCHESTRA runs show non-zero enqueue/run (or enable) under the workload.
2. Action contracts for RUN / YIELD / MIGRATE / THROTTLE / SLEEP are documented and tested at kernel semantics.
3. Hot-path cost is measured with ownership proven (not VBox busy-loop wall time alone).
4. Harness reports per-run metrics; stock scx hang is fixed.
5. Claims remain labeled by maturity class (VirtualBox vs bare-metal).

---

## Related artifacts

```text
artifacts/test-results/2026-08-13/vbox-mcp/
├── ORCHESTRA_Before_After_Report.html
├── ORCHESTRA_Kernel_Benchmark_Report.html
├── ORCHESTRA_Kernel_Improvement_Report.html
├── SUMMARY.md
├── graphs/
├── p0-ownership-retest/
├── p0-owned-bench/
```

Wording for publication: “the VirtualBox guest demonstrated…” — not deployment-ready, not bare-metal performance proof.

---

## Retest after applying this document (2026-08-13)

**Claim class:** VirtualBox / kernel-prototyped
**Object:** rebuilt `orchestra_scx_stage7.bpf.o` with the P0/P1 changes below
**Harness:** `kernel/sched_ext/scripts/p0_ownership_retest.sh` then `BENCH_SECS=5 benchmarks/real-machine/benchmark_suite.sh`
**Evidence:**
- [`p0-ownership-retest/`](./artifacts/test-results/2026-08-13/vbox-mcp/p0-ownership-retest/)
- [`p0-owned-bench/`](./artifacts/test-results/2026-08-13/vbox-mcp/p0-owned-bench/)

### What was implemented

| Item | Change |
|------|--------|
| P0 ownership | Keep `SCX_OPS_SWITCH_PARTIAL`. Workers use `orchestra_bridge --opt-in` (`sched_setattr(SCHED_EXT)`). Gate on `enqueue`, `running`, and `task_enable`. |
| P0 enqueue skip | Lone spinning tasks never hit `.enqueue`. Added `SCX_OPS_ENQ_LAST`. Shared `orchestra_place()` from `.select_cpu` and `.enqueue`. |
| P0 fast path | Cached directive in `dir_cache_map`; identity mismatch / missing directive uses `fast_run` (no task-map write). |
| P0 pin UX | `orchestra_bridge --pin-maps`. |
| P1 MIGRATE | `SCX_DSQ_LOCAL_ON \| cpu` + `scx_bpf_kick_cpu`. Cookie default 0 (proc starttime ≠ `start_boottime`). |
| P1 telemetry | `.running`, `idle_dispatch_count`, `fastpath_run_count`. Idle `.dispatch` is not treated as work. |
| P1 harness | Opt-in workers; `jobs -p` no longer waits on `scx_simple`; ctxt **deltas**; `sudo bash -c 'rm -rf /sys/fs/bpf/*'` (sticky bpf fs). |

### Results (guest fedora, Linux 6.12.96, 4 vCPU)

| Check | Result |
|-------|--------|
| CFS worker without opt-in | enqueue 0→0, running 0→0; idle_dispatch rose (PASS) |
| Opt-in RUN worker | enqueue=9 run=13 running=91 enable=1 (PASS) |
| Opt-in MIGRATE cpu 1 | migrate_requested=27 effective=27 (PASS) |
| Fail count | 0 |

Owned 5 s busy-loop bench (`owned=yes` on every ORCHESTRA row). Wall time ≈ duration for all three schedulers (not a throughput metric). Per-run `ctx_delta`:

| Scheduler | 1w | 2w | 4w | enqueue / running (4w) |
|-----------|----|----|----|------------------------|
| CFS | 34113 | 69258 | 108379 | n/a |
| scx_simple | 16391 | 28996 | 48117 | n/a |
| ORCHESTRA (owned) | 15609 | 30260 | 55720 | 69002 / 80557 |

Completion-time (`fixed_work` 80e6 iters): CFS 160 ms vs opted-in ORCHESTRA 7723 ms on this **single** VirtualBox run. That is **not** a publishable overhead number: `ENQ_LAST` increases place frequency, n=1, no repetition, VM noise. It does show the worker was on the SCX path.

`cgroup cpu.weight` dmesg warnings remain (same class as `scx_simple`); they are not ownership failures.

SLEEP/THROTTLE still use slice approximation (`scx_bpf_consume` was previously unsafe on this 6.12 path). Bare-metal statistical benches remain P2.
