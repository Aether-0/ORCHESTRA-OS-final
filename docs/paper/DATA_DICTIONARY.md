# Data dictionary

The tables in `data/` are copied from already-collected evidence. They are not
new benchmark runs.

## `parameter_inventory.csv`

One row per material comparison parameter. `orchestra_value` records a
source-defined contract or an explicitly selected experiment setting;
`cfs_value_or_reference` records either the corresponding protocol reference
or a host-observed CFS knob. The two columns are intentionally not treated as
semantically identical. `measurement_or_boundary` states the evidence class
and the limit on what the parameter supports.

## `photo_archival_pairs.csv`

One row per phase. `mode` is `cfs` or `orchestra`; `elapsed_ms` is foreground
batch completion; `bg_cpu_ticks` is the observed aggregate background service;
`owned_all` and `throttle_effective` are ownership/action gates. `state_after`
must be `disabled` for a clean phase.

## `log_archival_pairs.csv`

One row per phase of the five-pair controlled log experiment. `bg_ticks` is the
aggregate background service, `bpf_runtime_ns` is scheduler-observed runtime for
the ORCHESTRA phase, `throttle_acc`/`throttle_def`/`deferred_rel` are accepted,
deferred, and released throttle events, and `verify_ok` is exact foreground
output verification.

## `fixed_work_summary.csv`

Across three repetitions, the table reports mean and sample standard deviation
of elapsed completion time for fixed CPU or mixed work. `ratio` is
`ORCHESTRA_mean / CFS_mean`; values above 1 indicate ORCHESTRA took longer.
`ownership=yes` means the ORCHESTRA task gate passed for every summarized row.

## `runtime_action_matrix.csv`

The runtime assertion table separates requested, accepted, dispatched, effective,
and fallback observations. It is not a throughput benchmark. In particular,
counter increments alone are not interpreted as universal action effectiveness.

## `signal_microbenchmark_summary.json`

This is an isolated userspace publication/read API summary. It is not kernel
authentication, scheduler ownership, or end-to-end performance evidence.

## `longrun_results.csv`

The 600-second phase table records CPU, I/O, mixed, and memory rows. The memory
row is blocked because child-worker ownership could not be proven; it is not a
memory-stress pass.

## Supporting tables

`fixed_work_raw/` contains the six original per-run fixed-work tables used to
form `fixed_work_summary.csv`. `stress/` contains the short and medium CFS and
ORCHESTRA CPU/I/O/mixed phases. `rt_policy_matrix.txt`,
`rt_contention_matrix.txt`, and `kernel_integrity_matrix.txt` retain the
bounded admission, contention, replay, and stale-signal observations.
