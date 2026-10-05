# Claim–evidence map

These are archived observations on a ThinkPad T14 Gen 1 with an Intel
Core i5-10310U (4 cores / 8 logical CPUs), approximately 31 GiB RAM and
Linux 7.0.12 for the compact comparison tables. The broader manuscript also
discusses Linux 7.1.5. The exact recorded build identity is in
[data/target_build_manifest.txt](data/target_build_manifest.txt).

| Question | Evidence | Supported interpretation | Limit |
| --- | --- | --- | --- |
| Foreground protection | [P20 pairs](data/photo_archival_pairs.csv), [statistics](data/photo_archival_statistics.txt) | 20 counterbalanced pairs; mean paired completion-time reduction 74.24%, paired t 95% interval 72.72–75.76%; 20/20 wins | Background CPU service fell 97.69%; package-throttle counts differ (733 vs 14). This is service reallocation on one host. |
| Pure CPU throughput | [FW summary](data/fixed_work_summary.csv), [per-run rows](data/fixed_work_raw) | ORCHESTRA/default completion ratios 1.58, 2.01, 2.05, 1.73 at 1/2/4/8 workers | Three repetitions per mode; slowdown is retained, not omitted. |
| Mixed CPU/I/O | [FW summary](data/fixed_work_summary.csv) | Ratios 0.86, 0.94, 0.90 at 1/2/4 workers | Small sample; directional observation. |
| Action outcomes | [Action matrix](data/runtime_action_matrix.csv) | Separates requests, dispatch and effective observations | RUN dispatch/running counts are not event-paired; the count discrepancy remains unresolved. Archived THROTTLE behavior does not certify the current build. |
| Stability and missing coverage | [Long-run rows](data/longrun_results.csv), [checklist](data/checklist_status.csv) | Archived CPU/I/O/mixed 600-second rows pass; memory ownership is blocked. Checklist retains 38 PASS, 2 FAIL, 7 BLOCKED | Survival and counters alone are not deployment certification. |
| Signal integrity | [Userspace publication summary](data/signal_microbenchmark_summary.json), [kernel-local matrix](data/kernel_integrity_matrix.txt) | Userspace publication and bounded kernel identity/freshness checks are separate | These do not establish cryptographic authentication of kernel control frames. |
| Resource-control comparison | [Frozen protocol](protocol/PREREGISTERED_THREE_ARM_PROTOCOL.md) | Planned default Linux / cgroup-v2 / ORCHESTRA comparison | No confirmatory block has run; no advantage over equivalent cgroup control is established. |

CSV mode `cfs` is the historical label for the default Linux baseline on the
recorded kernel. It does not mean those modern Linux kernels ran the old CFS
algorithm rather than EEVDF. Raw labels are retained to preserve integrity.

The percentages above are reductions in completion time, not an unqualified
throughput speedup. Simulation coordination scores are not hardware benchmark
results. The [software validation report](../validation/PUBLIC_RELEASE_1_0_1.md)
concerns later offline hardening, not a repetition of these experiments.
