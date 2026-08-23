# ORCHESTRA-OS final product status

## Product identity

| Field | Value |
| --- | --- |
| Product | ORCHESTRA-OS |
| Version | `1.0.0-rc1` |
| Product ABI | 1.0.0 |
| Bridge ABI | v2 |
| Kernel state/policy ABI | v8 |
| Native coordination/controller ABI | v10 |
| Release class | Research-grade release candidate |
| Current evidence class | Userspace validated; kernel prototyped and source/build validated |

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
| sched_ext attach and ownership | Kernel-prototyped; current-host live gate blocked |
| RUN/YIELD/MIGRATE/THROTTLE/SLEEP backend semantics | Implemented in prototype; effective hardware matrix pending |
| Signal integrity | Generation/freshness/schema/identity checks implemented; kernel cryptographic verification not claimed |
| Privileged artifact integrity | Root-owned/non-symlink/non-writable checks and build-manifest hashes; signed provenance not claimed |
| Prediction | Bounded fixed-point consumption and fallback implemented; hardware convergence pending |
| S1/S2/S3/S4/Q | Native source/controller contract implemented; real-machine report pending |
| Controller | Bounded source contract implemented; live causal response/rollback pending |
| NUMA hierarchy | Bounded ABI slots exist; NUMA behavior not validated |
| Distributed tier | Not implemented |

## Validation status

The complete gate table is in
[`docs/validation/FINAL_VALIDATION_REPORT.md`](docs/validation/FINAL_VALIDATION_REPORT.md).
The current host has no authorized non-interactive root session for verifier,
attach, ownership, or unload testing and is not treated as a dedicated
scheduler test machine. Therefore this product is not labeled `1.0.0` or
deployment-ready.

## Known limitations

1. sched_ext APIs and kfunc/DSQ behavior are kernel/API-family dependent;
   BPF artifacts must be built and verified per target kernel.
2. The current bridge signal transport is a validated kernel-local map
   contract, not proof of cryptographic HMAC verification inside BPF.
3. Current real-machine evidence does not establish live verifier acceptance,
   ownership, effective action behavior, RT coexistence, hotplug safety,
   NUMA behavior, or production soak stability.
4. The exact paper N=40/4-exempt/3000-tick/500-warm-up/seed-42/five-seed gate
   is not represented by a canonical reproducible runner in this release.
5. No universal performance advantage over CFS/EEVDF is claimed; comparisons
   require ownership proof and workload-specific controlled evidence.
6. Distributed scheduling, package signing/SBOM provenance, production
   security review, external fuzzing, and dependency/SCA review remain
   release work beyond this candidate. The repository now has bounded local
   policy and ABI/state mutation targets; those do not replace independent
   fuzzing or kernel-runtime testing.

## Next release gates

Use a dedicated authorized x86_64 and arm64 matrix to pass target-matched
verifier/attach, exact ownership, all five effective actions, protected RT
coexistence, fault/recovery/unload, telemetry/controller, paper, comparison,
and soak gates. Only then should the version move from `1.0.0-rc1` to
`1.0.0`.
