# Preregistered three-arm confirmatory protocol

## Status

**Frozen before confirmatory data collection. No block has run.**

This protocol is the only approved path for a comparative performance claim
from the current target-matched 7.1.5 artifact. It supersedes neither the
archived photo evidence nor the current exploratory pure-CPU rows; it keeps
them separate.

## Research question

For a fixed foreground workload and fixed background workload, does ORCHESTRA
change verified foreground completion time relative to both default Linux and
a predeclared Linux cgroup-v2 background control, while preserving verified
background progress and required safety outcomes?

## Frozen candidate

Use the artifact set named in `TARGET_BUILD_MANIFEST.txt` and verified again
in `POST_EXECUTION_PROVENANCE_RECHECK.md`. A changed kernel, BTF, BPF source,
bridge source, loader source, staged patch hash, object, bridge, loader,
workload fixture, CPU affinity, or cgroup quota invalidates this protocol and
requires a new pre-registration.

## Conditions

1. `CFS_DEFAULT`: foreground and background under ordinary Linux scheduling.
2. `CGROUP_CONTROL`: identical foreground/background work and affinity, with
   the background in a cgroup-v2 CPU controller using a quota calibrated on
   separate pilot data and frozen before block 1.
3. `ORCHESTRA`: identical foreground/background work and affinity, with the
   fresh artifact attached through the scoped loader. Every ORCHESTRA trial
   must prove exact-PID `accepted > 0`, `dispatched > 0`, and `running > 0`
   before release.

`scx_simple` is not a fourth arm because it is not installed on this host.

## Block schedule and randomization

The 20 block orders are fixed in `RANDOMIZATION_SCHEDULE.csv` (SHA-256
`841f5676c7895c3ec740a8e8bbb1aa63bd92207e17eb2c709eb4a452bbd39a10`).
The schedule uses seed `20260916` and a constrained balanced permutation
shuffle: all six possible orders occur three times, then two seed-selected
orders are chosen so each condition/position cell occurs six or seven times.

The schedule generator was CPython `3.14.6`, using
`random.Random(20260916)`, `itertools.permutations` over the condition tuple
`(CFS_DEFAULT, CGROUP_CONTROL, ORCHESTRA)`, a uniformly selected pair of
permutations with no shared condition-position cell, and `rng.shuffle` over
the resulting 20 rows. The two additional selected orderings are
`CGROUP_CONTROL,ORCHESTRA,CFS_DEFAULT` and
`ORCHESTRA,CFS_DEFAULT,CGROUP_CONTROL`; each therefore occurs four times and
the other four orderings occur three times.

The two earlier schedule-generation attempts are retained as
`RANDOMIZATION_SCHEDULE_FIRST_ATTEMPT.csv` and
`RANDOMIZATION_SCHEDULE_SECOND_ATTEMPT.csv`. They were replaced before any
trial because they lacked the final condition-position balance; no outcome was
observed before the correction.

Run all three scheduled conditions once per block. Do not skip, reorder,
replace, or repeat a completed block because of its result.

## Preconditions and safety stop

Before block 1:

- record machine/kernel/BTF/artifact identities and full BPF/bpffs inventory;
- capture input fixture hashes, free storage, allowed CPU set, CPU frequency
  state, cgroup configuration, and pre-existing kernel-health lines;
- demonstrate the host-specific thermal monitoring and stop procedure on a
  cooled host; and
- retain the original cgroup quota calibration records separately from the
  confirmatory data.

During every condition, capture package/core temperatures, hardware throttle
counters, kernel log delta, process states, output correctness, storage, and
all ORCHESTRA telemetry. Stop the campaign phase if the hardware approaches
its reported critical trip, if thermal throttling becomes a material
confounder, or if a serious kernel-health event occurs. Do not invent a
universal temperature threshold; use this host's reported trips and the
observed thermal response.

## Validity and failure rules

A completed row is invalid for performance analysis when any of these occurs:

- workload timeout, nonzero exit, or output/hash/dimension verification fails;
- ORCHESTRA attach, exact-PID ownership, telemetry read, or clean unload fails;
- a serious new kernel health line appears;
- an unplanned interruption prevents the protocol from completing; or
- required environment or measurement fields are missing.

Retain invalid rows, their raw outputs, and their configuration-specific
failure rates. Do not reinterpret a CFS-run task as an ORCHESTRA result.

## Outcomes and analysis

Primary outcome: foreground completion time in milliseconds, evaluated as the
within-block paired difference of ORCHESTRA against `CFS_DEFAULT` and
`CGROUP_CONTROL` separately.

Co-primary service outcome: verified completed background work and its
completion time or verified output bytes/items.

For each configuration, report all raw rows, valid/invalid counts, mean,
median, sample SD, min, max, paired effect, and 95% confidence interval. Keep
CPU-only and mixed foreground/background results separate. Report the current
pure-CPU regression alongside any positive workload-specific result.

## Claim limit

A successful execution supports only a workload-, host-, kernel-, artifact-,
and protocol-specific comparison. It cannot establish general scheduler
superiority, hard-real-time safety, kernel cryptographic integrity, NUMA or
distributed capability, or deployment readiness.
