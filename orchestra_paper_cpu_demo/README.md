# ORCHESTRA-OS paper-aligned real-CPU userspace prototype

This program exercises a paper-aligned ORCHESTRA control path with real Linux
processes and hardware observations. Its maturity is **userspace-validated**
only: Linux CFS/EEVDF still performs final dispatch, preemption, fairness, and
safety. It is not a Linux scheduling class, kernel prototype, hard-real-time
implementation, or production security result.

The canonical research path contains no process-group layer, group budgets,
group coordinator, dynamic grouping, or group-level fault recovery.

## Implemented research mechanisms

- real worker processes created with `fork()`;
- aggregate CPU and memory observations from `/proc` plus an explicitly labeled
  CPU-derived thermal proxy;
- pre-run calibration of a fixed-gain lightweight predictor and robust
  observation-noise estimate;
- prediction confidence with observed-state fallback below the declared
  confidence threshold;
- one core-local, generation-stamped two-slot signal transport: each slot holds
  the same 160-byte canonical payload-plus-tag image as twenty atomic words;
  workers read that mapping while using only a separate two-entry reader-gate
  mapping for bounded slot pins;
- a versioned 128-byte canonical signal serialization with fixed byte order,
  schema/tier/source identity, sequence, monotonic freshness, key epoch, and
  HMAC-SHA256 verification;
- bounded last-known-good use followed by a userspace `THROTTLE` fallback;
- a complete current-frame acceptance gate before reward assignment, controller
  updates, or Q-table consensus, so rejected frames remain telemetry-only;
- per-process tabular Q-learning using state schema v2, whose CPU, memory, and
  thermal buckets align with every directive threshold;
- directive-consistent local reward (`+0.6`/`-0.6`), bounded difference reward
  at weight `w = 0.3`, and epsilon annealing from `0.30` toward `0.02`;
- robust-noise-derived perceptual jitter with default multiplier `c = 1.5`;
- bounded three-actuator control every 20 ticks: S4 controls jitter, S3/S4
  control switching penalty, and S3 controls policy consensus;
- controller reason, step, beta, next-parameter, saturation, and consensus
  telemetry;
- canonical `RUN`, `SLEEP`, `MIGRATE`, `THROTTLE`, and `YIELD` userspace
  approximations;
- per-worker bounded action-attempt telemetry: selected action, attempt and
  effective-result enums, bounded errno, requested/observed CPU, monotonic
  sleep duration, yield/throttle attempts, and fallback reason;
- historical selected-action compliance (`S2` / `S2_selected`) plus separate
  observable userspace action-attempt effectiveness (`S2_effective`);
- S1, historical selected-action S2, entropy-defined S3, mass-switch-sensitive
  S4, and the exact zero-preserving geometric mean
  `Q = (S1*S2*S3*S4)^(1/4)`; and
- experimental `S4_burst` telemetry that distinguishes isolated/staggered
  selected-action changes, valid directive transitions, same-transition bursts,
  and bounded reverse-transition oscillation diagnostics without changing S4
  or Q; and
- an instrumented observed-state reactive reference mode; and
- optional post-authentication tamper injection.

The state/control decision and signal-frame contracts are recorded in
`docs/adr/0001-userspace-state-metric-controller-contract.md` and
`docs/adr/0002-userspace-signal-frame-contract.md`. The bounded generation-
stamped publication transport, reader-gate ownership contract, and legacy
reference option are recorded in
`docs/adr/0006-generation-stamped-signal-publication.md`.

## Important approximations and limitations

- The paper calibrated on an independent held-out trace. This program calibrates
  immediately before each run on the same host; it does not establish predictor
  generalization.
- The paper reports a controlled discrete-event simulation. This program is
  asynchronous and observes a real, endogenous host load changed by its own
  worker actions. Its results are not comparable to the paper's `0.868/0.861`
  values.
- The reactive reference uses observed state but retains the same authenticated
  transport for measurement. It is not the paper's exact no-cryptography
  baseline.
- Only one local tier exists. Node, NUMA, cluster, distributed synchronization,
  and hierarchical control are not implemented.
- A random master key is inherited across `fork()` and locally derives epochs.
  Provisioning, rotation overlap, revocation, join/leave, and compromise recovery
  remain unsolved.
- `SCHED_FIFO` requires root or `CAP_SYS_NICE`. If entry fails, the configured
  exempt worker remains only a deterministic userspace bypass loop; that run is
  not hard-real-time evidence.
- Selected actions are self-management requests. `S2_selected` measures only
  selected-action/directive agreement. `S2_effective` measures conservative,
  observable userspace action-attempt results; neither proves kernel scheduler
  compliance, kernel dispatch intent, a context switch after yield, durable CPU
  placement, or CPU-capacity reduction.
- Affinity syscall success is not treated as completed migration: a later
  bounded `sched_getcpu()` observation must match the valid requested CPU.
  Even that observation does not prove durable placement or kernel intent.
- Sleep duration uses `CLOCK_MONOTONIC`; a request is effective only within its
  documented lower and upper tolerance. Throttle effectiveness means the local
  bounded operation ran, not that Linux reduced the task's CPU capacity.
- `S4_burst` is an experimental userspace selected-action diagnostic. It does
  not observe kernel dispatch transitions, does not prove a scheduler stability
  property, and is not included in historical `Q`. An accepted directive change
  can reduce a multiworker burst penalty but never makes a population-wide
  switch free.
- The generation-stamped signal transport provides bounded coherent userspace
  snapshots only. It does not establish measured publication speed, kernel
  synchronization correctness, a scheduler hot-path budget, or kernel
  integration. A writer refuses a bounded contention/exhaustion condition
  rather than overwrite a reader-pinned signal slot.
- No conventional Linux scheduler fallback can be implemented by a userspace
  process that never replaced the Linux scheduler; Linux remains in control
  throughout.

## Build and test

From the repository root:

```bash
make
make test
```

`make test` runs expanded GCC and Clang warning checks when available, sanitized
unit tests, bounded concurrent generation-publication stress, and bounded
four-worker integration runs for controller cadence, tamper rejection,
rejection-gated control, CSV invariants, shared-mapping signal publication, and
signal-driven teardown. Generated test data stays in temporary directories.

### Signal-publication build modes

The default build uses the generation-stamped two-slot signal-publication
transport. It keeps the canonical payload/tag mapping read-only in workers and
uses a separate bounded reader-gate map for the two pin/lease entries. Readers
make at most ten snapshot attempts; publication does not wait indefinitely for
a reader-pinned inactive slot.

The former byte-wise atomic transport is retained only for test/reference work:

```bash
make clean
make CFLAGS='-O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -DORCHESTRA_SIGNAL_PUBLICATION_LEGACY'
```

This is a clean-rebuild compile-time choice, not a runtime option or a
performance baseline. Record source and binary hashes and do not silently pool
legacy and default transport runs in one benchmark summary. Rebuild normally
before producing canonical default-path evidence.

### Focused publication coverage and local microbenchmark

Run the bounded publication stress and integration coverage directly with:

```bash
./tests/unit/run.sh
./tests/integration/run.sh
```

The first script includes the generation-stamped concurrent reader/publisher
stress under GCC ASan/UBSan; the second includes read-only shared-map,
contention, retry, and bounded-teardown checks. These are implementation tests,
not scheduler-performance measurements.

For a separately recorded, bounded local comparison of the legacy reference
and generation-stamped transports, use a new output directory:

```bash
python3 tools/benchmark/run_signal_publication_microbenchmark.py \
  --repository-root . \
  --output-dir /tmp/orchestra-signal-publication-microbenchmark-v1-20260804 \
  --cc gcc \
  --repetitions 2 \
  --iterations 1000 \
  --warmup 100 \
  --seed 20260804 \
  --max-readers 4 \
  --startup-timeout-ms 1000 \
  --max-runtime-ms 3000 \
  --drain-reads 4 \
  --timeout-sec 8 \
  --timeout-grace-sec 2
```

The runner builds both transport modes with the strict warning policy and
preserves command lines, hashes, environment, raw measurements, failures, and
invocation-level summaries. Its separate snapshot-copy and full verified-read
timings are local userspace API observations; neither mode is a Linux scheduler
benchmark. See the [v1 microbenchmark protocol](../docs/experiments/signal_publication_microbenchmark_v1.md)
for exact metrics, validity rules, and non-claims.

The local Makefile's `clean` target removes only the compiled binary. Historical
CSV samples are never deleted by a build command.

## Guarded manual runs

Start with four workers, no real-time attempt, an explicit seed, and an external
timeout on a development or test machine:

```bash
timeout --signal=INT --kill-after=2s 12s nice -n 10 \
  ./orchestra_paper_cpu_demo/orchestra_paper_cpu \
  --workers 4 \
  --rt-exempt 0 \
  --duration 4 \
  --interval-ms 100 \
  --calibration 1 \
  --mode orchestra \
  --seed 104729 \
  > /tmp/orchestra.csv
```

For the observed-state reactive reference, change `--mode` to `baseline`. To
exercise integrity rejection, add `--tamper-every 5`; the final
`rejected_frames` value counts per-reader rejections, not injected events.

## Reproducible bounded benchmark

The v1 smoke protocol runs three independent invocations per mode and records
raw data, failures, environment, hashes, validation, warm-up exclusion, and
run-level uncertainty:

```bash
python3 tools/benchmark/run_paper_cpu_benchmark.py \
  --manifest experiments/manifests/paper_cpu_smoke_v1.json \
  --schema experiments/schemas/paper_cpu_metrics_v2.json \
  --binary orchestra_paper_cpu_demo/orchestra_paper_cpu \
  --source orchestra_paper_cpu_demo/orchestra_paper_cpu.c \
  --repository-root . \
  --output-dir /tmp/orchestra-paper-cpu-smoke-v1
```

The output directory must not already exist. The mode summaries are descriptive,
unpaired, and endogenous; they do not establish a causal performance difference
or a Linux scheduler improvement. See
`docs/experiments/paper_cpu_smoke_v1.md` for the protocol and claim boundary.
The bounded 2026-08-03 local run is reported in
`docs/experiments/paper_cpu_smoke_v1_result_2026-08-03.md`.

Reproducible PNG and SVG visualizations are in the
[figure set](../docs/experiments/figures/paper_cpu_smoke_v1_2026-08-03/README.md).
The evidence-gated path from the current prototype to a release decision is in
the [release-readiness plan](../docs/operations/release-readiness-plan.md); the
converted [Markdown brief](../docs/operations/release-readiness-plan.md)
has a [build manifest](../docs/operations/release-readiness-brief-manifest.md).

## Metrics schema and historical samples

New output uses the append-only 121-column
`orchestra.paper_cpu.metrics/v7` contract in
`experiments/schemas/paper_cpu_metrics_v7.json`. Its first 120 columns retain
the v6 policy-lifecycle contract, which in turn retains the reviewed v5/v4/v3
prefixes. v7 requires `coordination_semantics_version=7`, uses
`S3_conditioned` as canonical S3, and defines canonical `S4` as
`min(historical S4, S4_burst)`. These are userspace data-pipeline semantics,
not evidence of kernel scheduler coherence or dispatch.

The v6 policy fields record mode, generation, persistence status, and update
suppression. They describe the userspace policy lifecycle only; they are not
kernel controller state. The older v4 contract remains available for historical
reproduction, and v2-v7 observations must not be silently pooled.

`S2` remains an exact alias for `S2_selected`: the fraction of eligible,
non-exempt workers whose selected userspace action matches the valid directive.
`S2_effective` is instead the fraction of eligible workers whose current
action attempt meets that action's conservative observable outcome rule. It is
explicitly **userspace action-attempt effectiveness based on observable
post-action outcomes, not kernel scheduler compliance**. It is not added to Q.
Rejected and stale frames cannot produce effective success. Per-action
fractions equal `1` when their attempt count is zero to mean *not applicable*;
always inspect their matching count.

`S4` remains the historical `1 - changed/eligible` selected-action metric and
the only temporal-stability term in `Q`. `S4_burst` is a separately named
experimental value. It uses a fixed 5-by-5 selected-action transition matrix,
an explicit accepted/fresh directive-transition justification rule, and an
eight-entry bounded accepted-frame history. It uses a population scale so one
worker is never labeled a synchronized burst; it penalizes same-pair bursts
and exact rapid reversals. Stale, rejected, unauthenticated, unavailable, or
partial directives cannot justify a change or enter the history. This is a
bounded userspace diagnostic, not a kernel scheduling or performance claim.

The historical v3/v4 exploratory pipelines remain readable but must remain
separate from v7. The current bounded v7 exploratory pipeline can be run with:

```bash
python3 tools/benchmark/run_paper_cpu_benchmark.py \
  --manifest experiments/manifests/paper_cpu_exploratory_v7.json \
  --schema experiments/schemas/paper_cpu_metrics_v7.json \
  --binary orchestra_paper_cpu_demo/orchestra_paper_cpu \
  --source orchestra_paper_cpu_demo/orchestra_paper_cpu.c \
  --repository-root . \
  --output-dir /tmp/orchestra-paper-cpu-exploratory-v7
```

This is a bounded data-pipeline validation protocol. It records
invocation-level summaries and failed or interrupted runs, but does not treat
CSV rows as independent repetitions or establish a causal scheduler-performance
improvement. A synthetic deterministic scenario may cover burst semantics when
the host's natural trace does not contain a meaningful burst; it is not a
natural-workload result.

The repository's existing `sample_*.csv` files use the older 21-column format,
lack complete provenance, and are retained only as historical artifacts. Do not
mix them with v2-v7 output or use them for a baseline-versus-ORCHESTRA
comparison. Do not silently pool v2-v7 observations in one statistical
summary.

## Safety

The program intentionally creates CPU load and changes worker affinity. Use a
disposable VM, dedicated test host, or bounded low-priority run. Do not use
`--duration 0` without an external timeout, do not run the smoke manifest with
`sudo`, and do not run scheduler experiments on a production machine.
