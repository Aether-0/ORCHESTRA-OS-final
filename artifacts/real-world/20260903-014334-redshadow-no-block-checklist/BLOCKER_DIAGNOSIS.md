# Why the original checklist was blocked

The original execution ledger has **301** blocked rows; **300** remain outside the new scope after the current clock-synchronization recheck. The causes are independent: implementation maturity, missing tools/harnesses, host topology, and safety boundaries. The new no-block checklist is a runnable scope, not a claim that omitted capabilities are complete.

## Root-cause categories

| Category | Rows | Root cause | Removal path |
|---|---:|---|---|
| `IMPLEMENTATION_OR_MEASUREMENT_MISSING` | 44 | Split the broad acceptance item into a supported observable check; implement or instrument the missing behavior before reopening the original row. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `STRESS_REGIME_NOT_CONTROLLED` | 33 | Use a controlled stress driver with declared CPU/thermal stop conditions and progressive durations. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `IMPLEMENTATION_MISSING_PREDICTOR` | 27 | Use supported external prediction-input transport for this checklist; reserve quality validation for a build exposing predictor outputs. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NO_HERD_OR_ABLATION_HARNESS` | 24 | Provide existing synchronized-population and ablation controls; do not infer from single-worker runs. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `MISSING_OBSERVABILITY_TOOLING` | 22 | Install/authorize perf or provide an equivalent existing trace collector. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `MISSING_WORKLOAD_TOOLING_OR_PEER` | 20 | Install/authorize stress/fio or provide a valid network peer and existing benchmark. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `FAULT_INJECTION_OR_PREDICTOR_PATH_MISSING` | 20 | Provide an approved injector for the existing path, and separately expose predictor failure/recovery states. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `SAFETY_AUTHORIZATION_OR_REBOOT_BOUNDARY` | 14 | Run on a disposable host with a known fallback kernel and explicit reboot/install authorization. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `CONTAINER_TEST_NOT_RUN` | 11 | Add a disposable container/cgroup test job with PID-namespace ownership handling. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NO_DYNAMIC_WORKLOAD_HARNESS` | 10 | Provide an existing regime-transition driver. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `TOPOLOGY_OR_PRESSURE_PROTOCOL_MISSING` | 9 | Use a host with the requested topology or add an approved pressure-specific workload. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `POWER_OR_FREQUENCY_TOOLING_BOUNDARY` | 9 | Run under each governor with energy/frequency tooling on a controlled power state. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `MISSING_TEST_MATRIX_ENVIRONMENTS` | 9 | Add disposable kernel/distro/architecture matrix jobs or VM images. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `HOST_TOPOLOGY_LIMIT` | 7 | Use a multi-socket/multi-NUMA host or mark the topology-specific item N/A. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `ABLATION_OR_COMPARATOR_MISSING` | 7 | Provide an existing ablation switch and install the declared comparator scheduler. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `DESTRUCTIVE_OPERATION_NOT_AUTHORIZED` | 7 | Use a disposable image and explicit approval for uninstall/upgrade/rollback. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `IMPLEMENTATION_MISSING_CRYPTO_INTEGRITY` | 5 | Keep cryptographic checks out of this execution scope until the kernel verifier exists; retain CLI freshness/replay checks. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NO_SAFE_FAULT_INJECTOR` | 5 | Provide an approved producer/consumer fault injector and recovery protocol. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NO_APPROVED_FUZZ_HARNESS` | 5 | Provide an approved existing harness and crash-recovery protocol. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `MISSING_REPRODUCIBILITY_ARTIFACT` | 4 | Archive the missing config/trace/analysis artifact. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `GPU_OR_IRQ_INFRASTRUCTURE_BOUNDARY` | 4 | Provide a safe GPU/IRQ workload and latency collector. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NOT_OBSERVED_IN_PRIOR_RUN` | 3 | Add the observation to the next command-logged run. | See the per-item action in `EXCLUDED_SCOPE.csv`. |
| `NO_SEED_PARAMETER` | 1 | Expose a deterministic seed in the existing harness. | See the per-item action in `EXCLUDED_SCOPE.csv`. |

## Current rechecks

- Time synchronization is no longer excluded: `timedatectl` reports `NTPSynchronized=yes` and the row is PASS in the new checklist.
- A direct target-matched signal transport run loaded sched_ext, published a prediction-bearing frame (`prediction_used=1`, confidence 875, predicted CPU 730), read it back with `--status`, and unloaded cleanly. This proves input transport, not predictor training or prediction quality.
- The kernel README says the runtime consumes bounded prediction records, does not train a predictor, and does not verify the userspace HMAC. Predictor-quality and cryptographic rows therefore remain outside the runnable scope.

## Separate regressions (not scope exclusions)

- `make check`/aggregate `make test` fail on the tracked hard-coded `/home/...` documentation path caught by the security scan.
- `make test-integration` fails at tick 10: `rejected frame cannot be valid or justify an action transition`.
- These failures remain visible in the original campaign report and are not removed by the no-block scope.

The likely tick-10 mechanism is visible without changing code: `publish_frame()` serializes a tampered copy of the payload in `prepare_canonical_signal_frame()`, while the main loop subsequently calls `compute_metrics()` with the original `applied` structure. The validator sees `rejected_frames` increase but also sees the untampered frame as current/valid, which violates its deliberate fail-closed rule. This is a hypothesis to confirm with a focused trace, not a repaired result.

## Exact original reason-code counts

| Reason code | Rows |
|---|---:|
| `BLOCKED_NOT_IMPLEMENTED_OR_MISSING_MEASUREMENT` | 44 |
| `BLOCKED_NOT_RUN_OR_THERMAL_INTENSITY_NOT_CONTROLLED` | 31 |
| `BLOCKED_PREDICTOR_NOT_IMPLEMENTED_IN_KERNEL` | 27 |
| `BLOCKED_NO_SAFE_FAULT_INJECTION_OR_MISSING_PREDICTOR` | 20 |
| `BLOCKED_NO_APPROVED_HERD_OR_ABLATION_HARNESS` | 15 |
| `BLOCKED_MISSING_PERF_OR_DETAILED_TELEMETRY` | 12 |
| `BLOCKED_MISSING_STRESS_OR_NETWORK_INFRASTRUCTURE` | 11 |
| `BLOCKED_CONTAINER_OR_CGROUP_TEST_NOT_RUN` | 11 |
| `BLOCKED_NO_DYNAMIC_WORKLOAD_HARNESS` | 10 |
| `BLOCKED_NO_APPROVED_HERD_WORKLOAD` | 9 |
| `BLOCKED_SINGLE_SOCKET_OR_MISSING_PRESSURE_PROTOCOL` | 9 |
| `BLOCKED_GOVERNOR_OR_ENERGY_TEST_NOT_AVAILABLE` | 9 |
| `BLOCKED_MULTI_KERNEL_OR_MULTI_DISTRO_NOT_AVAILABLE` | 9 |
| `BLOCKED_KERNEL_INSTALL_OR_REBOOT_NOT_AUTHORIZED` | 8 |
| `BLOCKED_SINGLE_NUMA_NODE` | 7 |
| `BLOCKED_NO_IMPLEMENTED_ABLATION_OR_MISSING_SCHEDULER` | 7 |
| `BLOCKED_DESTRUCTIVE_UPGRADE_UNINSTALL_NOT_AUTHORIZED` | 7 |
| `BLOCKED_REBOOT_OR_SUSPEND_NOT_AUTHORIZED` | 6 |
| `BLOCKED_CRYPTOGRAPHIC_SIGNAL_INTEGRITY_NOT_IMPLEMENTED` | 5 |
| `BLOCKED_NO_SAFE_FAULT_INJECTION_INTERFACE` | 5 |
| `BLOCKED_MISSING_TEST_INFRASTRUCTURE` | 5 |
| `BLOCKED_NO_APPROVED_FUZZ_HARNESS` | 5 |
| `BLOCKED_INSUFFICIENT_OBSERVABILITY_OR_SAFE_INJECTOR` | 4 |
| `BLOCKED_INSUFFICIENT_OBSERVABILITY` | 4 |
| `BLOCKED_MISSING_ARTIFACT_OR_TOOL` | 4 |
| `BLOCKED_MISSING_GPU_OR_IRQ_WORKLOAD_INFRASTRUCTURE` | 4 |
| `BLOCKED_MISSING_SAFE_WORKLOAD_INFRASTRUCTURE` | 3 |
| `BLOCKED_NOT_RUN_IN_SCOPE` | 3 |
| `BLOCKED_NOT_RUN_OR_NOT_CONTROLLED` | 2 |
| `BLOCKED_MISSING_TOOL_OR_KERNEL_INTERFACE` | 2 |
| `BLOCKED_NOT_OBSERVED` | 1 |
| `BLOCKED_MISSING_FIO_OR_RANDOM_IO_TOOL` | 1 |
| `BLOCKED_NO_SEED_PARAMETER` | 1 |

## Full-scope prerequisites

1. Fix the tracked documentation path and investigate the tick-10 integration transition.
2. Implement/expose predictor quality metrics and a kernel cryptographic signal verifier.
3. Authorize missing tools (`perf`, `stress`/`stress-ng`, `fio`, `numactl`, sensors) on a dedicated test image; add a valid network peer and `scx_simple` where needed.
4. Add safe dynamic, herd/anti-synchronization, ablation, container, fuzz and multi-seed harnesses.
5. Use disposable multi-kernel/multi-distro/multi-NUMA/ARM/VM environments.
6. Keep reboot, suspend, hotplug and upgrade tests behind explicit recovery authorization.
