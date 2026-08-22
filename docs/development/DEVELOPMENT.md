# ORCHESTRA-OS developer guide

## Repository layout

```text
kernel/sched_ext/       BPF scheduler, ABI headers, bridge, loader, scripts
orchestra_paper_cpu_demo/ portable research/userspace implementation
userspace/              stable boundary for future userspace services
scripts/                build, capability, policy, install, and control CLI
config/examples/        versioned policy examples
examples/               documented smoke and workload examples
docs/                   architecture, operations, research, and validation
tests/                  existing unit/integration/source contract tests
benchmarks/             existing research and real-machine harnesses
artifacts/              historical evidence; not a runtime dependency
```

Stage names in historical paths are evidence labels, not new architecture
boundaries. The stable product BPF entry point is
`kernel/sched_ext/bpf/orchestra_sched.bpf.c`.

## Build targets

```bash
make                 # portable userspace build
make check            # compiler, schema, script, and source checks
make test             # unit and integration checks
make bridge           # external bridge and loader build
make product          # userspace plus bridge
make kernel-bpf       # target-matched external BPF build
make clean
make clean-product ORCHESTRA_BUILD_DIR=/var/tmp/orchestra-os-build-1234
```

The kernel target requires an exact running-kernel source/BTF environment and
is intentionally separate from the portable default.

## ABI versioning

The product bundle is in `orchestra_product_abi.h`; current contracts are
bridge v2, kernel v8, and control v10. Any record crossing a userspace/BPF
boundary must have fixed-width fields, magic, ABI version, value size/schema,
generation, and explicit scaling/units. Add fields only in reserved space or
through a new version; never change the meaning of an old field in place.

Update, in order:

1. the ABI header and static/source contract tests;
2. the BPF map declaration and loader map specification;
3. the bridge serializer/deserializer;
4. userspace policy/telemetry schemas;
5. documentation and migration notes;
6. target-matched build and runtime validation.

## BPF maps and callback boundaries

The loader's map specification is the authoritative attach schema. It checks
name, type, key size, value size, and capacity before loading and pins every
map before struct_ops attach. New maps must be added to both the BPF object
and `orchestra_loader.c`, with scoped cleanup and a source-level test.

Callbacks should remain thin. Put state/policy/controller/action semantics in
the canonical decision functions, and put only event-specific queue work in
`enqueue`, `dispatch`, `running`, `stopping`, and `enable` callbacks.

## Extending policy safely

Add a policy field by defining its unit/range/default in the ABI, validating
it before inactive-bank publication, copying it through the generation
commit, consuming it only after schema/generation checks, and adding a
failure/fallback telemetry field. Update `config/README.md` and at least one
example. A policy request is not effective action evidence.

## Adding telemetry

Record the causal sequence separately:

```text
requested → accepted → dispatched → effective → fallback
```

Include task identity/start time, signal/policy/controller generations, CPU,
timestamps, action IDs, and fallback reason where relevant. For coordination,
retain S1, S2, S3, S4, Q, window interval, observation population, and excluded
protected tasks together.

## Adding an action capability

Add the action to no new sixth enum: the canonical set is fixed. Define the
capability bit, fallback behavior, validation, backend operation, telemetry,
bridge option, config documentation, and source/unit/runtime tests. If the
kernel cannot execute the requested semantics, expose capability-adjusted and
fallback outcomes instead of pretending success.

## Predictor and controller changes

Predictor changes must define bounded arithmetic, model generation, confidence,
freshness/expiry, and observed-state fallback. Controller changes must retain
deficit-to-actuator causality, step bounds, rate limits, hysteresis,
saturation, previous-known-good state, rollback, recovery, and the two
timescales. Test both improvement and adverse response; a changing parameter
alone is not successful control.

## Validation discipline

Run `make check`, `make test`, `git diff --check`, the compatibility report,
policy dry runs, and the target-matched BPF build as applicable. Privileged
attach, ownership, action effectiveness, RT coexistence, hotplug, soak, and
rollback require a dedicated authorized host and must not be inferred from a
compile or simulation result.
