# ORCHESTRA-OS final product status

## Product identity

| Field | Value |
| --- | --- |
| Product | ORCHESTRA-OS |
| Version | `1.1.0` |
| Product ABI | 1.0.0 |
| Bridge ABI | v2 |
| Kernel state/policy ABI | v8 |
| Native coordination/controller ABI | v10 |
| Release class | Research software and native packages; kernel runtime unverified for this release |
| Current evidence class | Userspace validated; kernel prototyped with bounded target-specific experimental validation |

## Implemented subsystems

- versioned state and signal records with generation/freshness/schema gates;
- bounded prediction records, confidence, expiry, model generation, and
  observed-state fallback;
- two-bank generation-published policy lifecycle;
- one canonical scheduler decision pipeline;
- exactly five actions: RUN, YIELD, MIGRATE, THROTTLE, SLEEP/DEFER;
- capability negotiation and explicit action fallback;
- task admission/identity records and requested-versus-effective telemetry;
- native S1/S2/S3/S4 coordination windows and geometric-mean Q;
- deficit classification and bounded multi-actuator feedback controller;
- hysteresis, rate limits, saturation, rollback, and recovery states;
- Hybrid Safety admission boundary for protected real-time classes;
- loader-scoped map schema validation, pinning, attach, unload, and cleanup;
- observer-safe compatibility checker, build workflow, CLI, policy loader,
  install/uninstall tooling, configuration examples, and documentation.
- foreground `run`/`monitor` lifecycle commands and an opt-in systemd unit;
- ownership-checked unload plus root-safe artifact and build-manifest
  integrity gates; tracked host binaries are excluded from the source
  repository.

## Capability and architecture status

| Capability | Status |
| --- | --- |
| Portable observer/userspace path | Implemented and validated |
| x86_64 target-matched BPF build path | Implemented; source/build validated |
| arm64 target build path | Compatibility-backed; separate verifier/attach matrix required |
| sched_ext attach and ownership | Kernel-prototyped; current-host exact-TID gate passed |
| RUN/YIELD/MIGRATE/THROTTLE/SLEEP backend semantics | Implemented in prototype; limited current-host effective-action evidence |
| Signal integrity | Generation/freshness/schema/identity checks implemented; kernel cryptographic verification not claimed |
| Privileged artifact integrity | Root-owned/non-symlink/non-writable checks and build-manifest hashes; signed provenance not claimed |
| Prediction | Bounded fixed-point consumption and fallback implemented; hardware convergence pending |
| S1/S2/S3/S4/Q | Native source/controller contract implemented; limited current-host live report |
| Controller | Bounded source contract implemented; limited actuator adaptation/recovery observed |
| NUMA hierarchy | Bounded ABI slots exist; NUMA behavior not validated |
| Distributed tier | Not implemented |

## Validation status

Runtime observations below are historical upstream records, not new kernel
validation performed by the v1.1.0 packaging pipeline. Native package PASS
results are reported separately in each release PLATFORM_RESULTS.md.


The complete gate table is in
[`docs/validation/FINAL_VALIDATION_REPORT.md`](docs/validation/FINAL_VALIDATION_REPORT.md).
The current host has passed a controlled target-matched verifier, attach,
ownership, action, and unload gate. It is still a research-grade release
artifact, not deployment-ready. The stable claim is limited to the
observer/userspace and package lifecycle; sched_ext remains opt-in and
target-specific. Kernel-side cryptographic signal authentication, broad
hardware/runtime coverage, distributed scheduling, and hard-real-time
guarantees are not claimed.

## Known limitations

1. sched_ext APIs and kfunc/DSQ behavior are kernel/API-family dependent;
   BPF artifacts must be built and verified per target kernel.
2. The current bridge signal transport is a validated kernel-local map
   contract, not proof of cryptographic HMAC verification inside BPF.
3. Current real-machine evidence establishes only a limited target-specific
   verifier/ownership/action gate; it does not establish RT coexistence,
   hotplug safety, NUMA behavior, or production soak stability.
4. The exact paper N=40/4-exempt/3000-tick/500-warm-up/seed-42/five-seed gate
   is not represented by a canonical reproducible runner in this release.
5. No universal performance advantage over CFS/EEVDF is claimed; comparisons
   require ownership proof and workload-specific controlled evidence.
6. Distributed scheduling, distro package signing, production security review,
   external fuzzing, and dependency/SCA review remain outside this release.
   Release assets include SPDX SBOMs and checksums. CI build-provenance
   attestations are published when GitHub supports them for the repository
   visibility and ownership type; user-owned private repositories do not have
   that hosted capability. The repository's bounded local mutation targets do
   not replace independent fuzzing or kernel-runtime testing.

## Next release gates

Future releases should use a dedicated authorized x86_64 and arm64 matrix to
expand target-matched verifier/attach, exact ownership, all five effective
actions, protected RT coexistence, fault/recovery/unload, telemetry/controller,
paper, comparison, and soak evidence. Those expansion gates do not change the
bounded research-stable claim of `1.0.0`.
