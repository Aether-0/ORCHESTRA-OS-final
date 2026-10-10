# Scheduler correctness update — 10 October 2026

This update applies scheduler fixes on top of main revision
`56e07b0f2cd2d5b54f26d9b005fe1cad2144d8d6`. It is newer than the v1.1.1
release and does not retroactively validate that release or the paper's
historical measurements.

## Changes

- Route registered enqueue through the canonical policy, controller,
  capability, executor and decision-recording path.
- Keep host-wide full switching explicit. Adaptive admission does not isolate
  scheduler ownership; attached RUN fallback is distinct from Linux scheduling.
- Correct scaled metric means, state-conditioned S3, exact geometric-mean Q,
  temporal penalties and coordination-window counter reuse. Restore full
  controller actuation, evaluation and rollback.
- Revalidate deferred actions against admission freshness, generation,
  policy provenance and controller permission; recover through global RUN.
- Read coherent control/meta/entry/meta/control policy snapshots with exact
  entry generations and bounded retries.
- Record effective execution only from running callbacks, retain decision
  provenance and fallback reasons, and clear stale action state on RUN fallback.
- Preserve charged THROTTLE runtime across directive renewals.
- Bound bridge pipe I/O and child shutdown, including stopped/unresponsive children.

Periodic feedback uses the existing timer even while the global RUN queue is
busy. Separate stack frames and internal per-CPU working maps bound BPF stack
use. The fixed-point fourth-root search retains its exact mathematical result
while avoiding exponential verifier paths.

## Validation

- Portable userspace, bridge and loader compilation: PASS.
- Target-matched scheduler compilation: PASS on `7.1.5+kali-amd64`.
- Complete BPF object verification using `bpftool prog loadall`: PASS.
  Programs were verified without `autoattach` or struct_ops registration;
  temporary program pins were removed and sched_ext remained disabled.
- `make check test-unit security-test`: PASS in the publication checkout.
  All requested GCC/Clang unit passes completed with 32/32 named cases,
  native BPF-header logic checks and sanitizer regressions. Security tests
  include 50,000 ABI/state mutations, policy liveness, installation paths,
  release archive safety and evidence-bundle identity checks.
- Documentation links (124 maintained documents), tracked-content security
  scan and whitespace checks: PASS.

Native logic tests exercise the actual BPF header branches with bounded
map/clock/lock mocks: signal arithmetic, observation counting, window reuse,
controller actuation and rollback. Additional tests cover exact Q bounds,
state-conditioned coherence, THROTTLE renewals, revoked-action state, bridge
I/O timeouts and a stopped child process. Source invariants guard the
registered decision path and dispatch/execution distinction.

## Evidence limits

BPF program verification is separate from struct_ops attachment. No scheduler
was attached for this update. Ownership, effective actions, live policy-writer
races, CPU hotplug, failure recovery, performance and long-duration stability
require the dedicated-machine runtime protocol. Kernel objects are specific
to their kernel/BTF target; this is not a universal Linux BPF binary or a
DEPLOYMENT_READY certification. No historical or new performance result is
inferred from these regression checks.

## Reproduce the build

```bash
make check test-unit security-test
ORCHESTRA_KERNEL_SRC=/path/to/exact/kernel/source \
  ORCHESTRA_BUILD_DIR=/tmp/orchestra-verified-build \
  ./scripts/build.sh --all
```

Kernel source must match the running kernel. Verification-only loading also
requires an appropriate privileged environment; it does not authorize
attaching a host-wide scheduler.
