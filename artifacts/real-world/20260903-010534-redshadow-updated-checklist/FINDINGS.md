# Findings

## Facts established in this campaign

1. The DOCX conversion is complete and auditable. The source hash is `b3e047049b63a470f9ebd37f08d1e35bafd7061028eeca37a29176a3cadd426d`; the Markdown contains 36 phase headings and 587 checkbox items. Conversion metadata is in [`CONVERSION_METADATA.json`](CONVERSION_METADATA.json).
2. Repository userspace compilation succeeds. `make test-unit` runs 30/30 named unit tests and the compiler/sanitizer checks. `make security-test`, the source-contract test and documentation-link checker pass.
3. The aggregate repository gate is not passing. `make check` and `make test` return 2 because `scripts/security-scan.sh` reports a hard-coded `/home/aether/Documents/ORCHESTRA-OS-final` path in `docs/demo/REALTIME_CFS_ORCHESTRA_DEMO.md`. This tracked-document finding is preserved in [`build/make_check.stderr`](build/make_check.stderr) and [`build/security_scan.stderr`](build/security_scan.stderr).
4. `make test-integration` independently returns 2 at tick 10: `rejected frame cannot be valid or justify an action transition`. This is retained in [`build/test_integration.stderr`](build/test_integration.stderr).
5. The distro kernel-header wrapper is not sufficient for the repository’s out-of-tree build: the default path is rejected as unreadable and the common-header path lacks `tools/sched_ext/include/scx/common.bpf.h`. An existing exact 7.0.12 source export in `/var/tmp` allows the documented BPF/bridge/loader build to pass. Build hashes and manifest are in [`build/kernel_exact/`](build/kernel_exact/).
6. The controlled P0 runtime gate loads `orchestra_scx_v8`, proves exact-TID ownership, records finite forward progress, observes bounded effective action counters for all five canonical actions, and unloads cleanly. The raw result is [`sched_ext/p0-exact-evidence/results.csv`](sched_ext/p0-exact-evidence/results.csv); per-action status files distinguish effective counters from mere requests.
7. Current CFS/ORCHESTRA comparisons used the same machine, workload mode, worker counts (1/2/4), two-second duration and three repetitions. All ORCHESTRA rows passed exact-TID ownership. The current per-repetition ORCHESTRA/CFS elapsed ratios averaged 0.91 (1 worker), 0.86 (2 workers), and 0.71 (4 workers), with notable spread at one worker. These are exploratory measurements on an active host; they are not a deployment threshold or universal performance claim. The missing `scx_simple` binary is recorded as blocked.
8. CFS and ORCHESTRA stress smoke runs pass CPU, I/O, mixed and kernel-health checks with thermal monitoring active. CFS memory stress is blocked because `stress` is not installed. ORCHESTRA memory ownership is additionally unproven because the existing harness cannot identify its child workers.
9. Existing bridge probes reject a nonexistent PID, invalid action, disallowed CPU, duplicate signal sequence and out-of-range confidence; a valid signal and `--require-signal` directive are accepted. Policy-bank stage/commit/abort works in the supported TRAIN/ADAPT lifecycle; the EVALUATE commit guard rejects mutation as designed.
10. The current signal path is local-trust. It exposes sequence/freshness/status fields, but it does not provide the paper’s cryptographic HMAC verifier. Modified-frame, wrong-key and cryptographic-authentication checklist items therefore remain blocked/inconclusive rather than being inferred from CLI validation.
11. A live FIFO transition probe is inconclusive because the task loses its kernel identity record when changed to RT; the result cannot be interpreted as a direct adaptive refusal. Prior same-host bounded FIFO/RR policy-matrix evidence remains the stronger RT result. No hard-real-time guarantee is claimed.
12. The machine has one NUMA node and 4 physical/8 logical CPUs. There is no safe basis for cross-node, distributed, ARM64, multi-distro, suspend/reboot, upgrade/rollback, energy, GPU or approved fuzz validation in this campaign.
13. The final scheduler state is disabled and the final BPF inventory shows unrelated pre-existing state preserved. No implementation, test, benchmark, kernel, BPF or bridge source was edited.

## Interpretation and hypotheses

- The tick-10 failure is most likely a current integration-test/implementation contract mismatch in rejected-frame transition handling. The precise cause requires tracing the frame-validation state machine; this campaign did not alter it.
- The differing comparison ratios are consistent with host-load/frequency variability and the small two-second sample, not proof of a scheduler-wide gain. The prior formal same-host archive also reports workload-dependent direction, so no universal speedup should be inferred.
- The default build failure is an environment/source-input issue, not evidence that the BPF source itself cannot build: the exact target source export built successfully.
- Missing perf/stress/fio/numactl/sensors/scx_simple and a lack of safe fault/ablation/dynamic-workload interfaces explain most BLOCKED items. Installing tools or changing persistent configuration was not authorized.

## Recommended next investigations

- Remove or parameterize the tracked absolute demo path so the security scan and aggregate gate can run cleanly.
- Reproduce and instrument the tick-10 integration transition, preserving the rejected frame, validation result and action-state timeline.
- Add a supported predictor output/metrics path, cryptographic signal verification and safe fault-injection controls before claiming the paper’s WP2/WP9 behavior.
- Provide approved harnesses for memory ownership, random I/O, network, dynamic transitions, herd/anti-synchronization, ablations, perf overhead, multi-seed statistics and fuzzing.
- Repeat comparison runs on an idle/test image with a predeclared frequency policy and longer trials; retain each raw run and publish ownership evidence alongside performance data.
