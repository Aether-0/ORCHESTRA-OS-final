# Next-version roadmap

This roadmap proposes v1.2.0-rc1. It does not declare that release available or
upgrade the scheduler beyond kernel-prototyped maturity. The current version
remains 1.1.1; filename cleanup changes no scheduling or ABI semantics.

## Current-version cleanup

- Put the scheduler implementation in `kernel/sched_ext/bpf/orchestra_sched.bpf.c`.
- Put the active loader in `kernel/sched_ext/loader/orchestra_loader.c`.
- Use descriptive build, ownership, reproduction, and runtime-validation scripts.
- Name the bridge ABI header for its actual v2 contract and spell out coordination.
- Update current build/install/test/documentation references together. Preserve
  historical evidence and keep old command/header/source wrappers during migration.

## Release-candidate priorities

| Priority | Work | Evidence required before declaring it complete |
| --- | --- | --- |
| 1 | Strengthen behavioral regressions | Exercise policy-bank publication and rollback, deferred revocation, controller recovery, throttle budget continuity, and execution-counter provenance; retain negative cases |
| 2 | Validate the recently repaired runtime | On a dedicated host, prove task ownership and effects for RUN, SLEEP, MIGRATE, THROTTLE, and YIELD; verify clean unload and recovery, preserving every failure |
| 3 | Establish fairness requirements | Measure per-task service and starvation under mixed load; specify how nice, weight, and cgroup CPU controls should behave before implementing them |
| 4 | Produce comparable performance evidence | Match workload, affinity, workers, duration, and collection protocol; use at least three paired exploratory repetitions, and five or more for stronger claims; report foreground and background service |
| 5 | Measure overhead and scaling | Measure timer cost, deferred backlog, action latency, migrations, and per-task service across supported worker counts before tuning limits or cadence |
| 6 | Expand release verification | Retain amd64/arm64 CI and package checks; record kernel-specific BPF compilation, verifier acceptance, attachment, and runtime results separately |

## Stable-release gate

Userspace checks, unit/integration tests, security regressions, target-matched
BPF/bridge/loader compilation, and package validation must pass. Dedicated-host
ownership, action, unload/recovery, and medium-duration stability evidence must
also be reviewed. Kernel runtime work remains subject to the test campaign's
safety gate; compilation is not a substitute for it.

Publish compatibility only for combinations actually tested. Distribution
packages do not make a BPF object portable across all distro kernels. Do not
claim deployment readiness, cryptographic kernel signal integrity, NUMA-aware
scheduling, or distributed scaling from the filename cleanup or existing CI.

Keep changes separated into naming, behavior/tests, measured validation, and
release commits so reviewers can identify the evidence supporting each change.
