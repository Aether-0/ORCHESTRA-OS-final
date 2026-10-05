# ORCHESTRA-OS Security Audit

**Assessment date:** 2026-08-25
**Product:** ORCHESTRA-OS `1.0.0` research stable
**Scope:** repository-wide source, ABI, userspace, BPF/sched_ext, bridge,
loader, policy, controller, installer, uninstaller, scripts, tests, build
and documentation paths.

## Executive conclusion

The audit found and remediated concrete security-relevant defects in
privileged artifact handling, install/uninstall ownership, policy generation
rollback, publication ordering, loader teardown, deferred-task release,
controller generation arithmetic, and the direct bridge privilege boundary.
Malformed policy, duplicate JSON keys, oversized input, parser recursion,
unsafe path components, symlink destinations, and bounded control-plane waits
now have regression coverage.

No Critical vulnerability was verified in the reviewed source. No known
unresolved High vulnerability remains within the declared trusted-root model
after the fixes recorded in [`SECURITY_FINDINGS.md`](SECURITY_FINDINGS.md).
The current host has now passed a target-matched BPF verifier, attach,
ownership, effective-action, and scoped teardown gate. That is not a
production-security certification: hotplug, broad fault/recovery, long soak,
signed provenance, and cryptographic authentication of the kernel signal map
remain residual release gaps.

## Method and evidence

The review used the repository specifications and security guidance first,
then inspected implementation and test evidence. The read-only review covered
all tracked source and operational paths, including historical compatibility
implementations. Concrete checks included:

- ABI/map/schema and generation-flow inspection;
- privileged path, ownership, symlink, and cleanup review;
- parser and policy mutation/property tests;
- deterministic ABI/signal/controller/bridge-state mutation testing under
  ASan/UBSan;
- GCC and Clang strict warning builds;
- AddressSanitizer and UndefinedBehaviorSanitizer userspace tests;
- MAP_SHARED publication concurrency/integration tests;
- source invariants for BPF-only safety properties;
- installer/uninstaller lifecycle tests;
- repository-wide secret and developer-path scanning;
- `git diff --check` and documentation-link validation.

The exact commands are in [`SECURITY_TESTING.md`](SECURITY_TESTING.md), the
finding ledger is in [`SECURITY_FINDINGS.md`](SECURITY_FINDINGS.md), and the
gate-by-gate disposition is in
[`SECURITY_VALIDATION_REPORT.md`](SECURITY_VALIDATION_REPORT.md).

## Security objectives

The security boundary is fail-closed scheduling:

```text
validate → authenticate/establish trust → authorize → bounds-check
→ execute → verify result → record telemetry → fallback on uncertainty
```

For the current kernel prototype, “authenticate” means exact ABI/schema,
identity, scheduler epoch, generation, freshness, ownership, and capability
coherence. It does **not** mean a kernel-side HMAC verification. The observer
and research userspace signal path has HMAC test coverage, but that property
must not be transferred to the BPF bridge without a real kernel implementation
and key-management design.

## Trust boundaries

| Boundary | Protected asset | Current control | Residual boundary |
| --- | --- | --- | --- |
| Policy file → policy loader | Policy semantics and bounded execution | UTF-8/JSON/schema/type/range/duplicate-key checks; root-owned input when run as root; 1 MiB/256-entry/timeout bounds | Root operator can intentionally submit a harmful policy |
| Policy loader → bridge | Command integrity and map transaction | Safe bridge artifact, list-based `exec`, bounded subprocesses, abort/recovery path | Bridge calls are sequential and require an authorized root control plane |
| Bridge → BPF maps | ABI and task-control state | Fixed-size records, exact map schemas, locks, publication status, generation/identity/freshness checks | Local map transport is not cryptographically authenticated |
| BPF map → scheduler callback | Scheduling correctness | Snapshot/retry, controller/policy equality, capability gates, action validation, RUN/conventional fallback | Current-host verifier/action evidence is limited to the recorded target and short runs |
| Loader → kernel/sched_ext | Attach, ownership, and teardown | Root-only operation, trusted artifact path, exact pin schema, ownership check, detach-before-unpin | Kernel API-family compatibility requires target validation |
| Installer/uninstaller → filesystem | Root filesystem integrity | Non-symlink/private path checks, marker/hash/manifest ownership, modified-file refusal, config preservation | A compromised root or trusted package source is outside scope |
| Telemetry → operator/researcher | Evidence integrity | Requested/accepted/dispatched/effective/fallback fields and schema checks | Lifetime counters wrap modulo 2^64; they are not control inputs |

The trusted-root assumption is explicit: a local root administrator, the
running kernel, and the target BTF/UAPI are trusted. A root compromise can
replace the loader, BPF object, maps, policy, or kernel and is outside this
prototype's protection boundary.

## Findings summary

| Severity | Fixed | Residual/blocked | Notes |
| --- | ---: | ---: | --- |
| Critical | 0 | 0 verified | No Critical source vulnerability was reproduced |
| High | 5 | 0 within the declared trust model | Cryptographic kernel signal authentication becomes a High release gap if untrusted publishers are added to scope |
| Medium | 5 | 3 | Remaining items are runtime/provenance/coverage gaps, not silently accepted behavior |
| Low | 1 | 1 accepted limit | Research-only temporary-file fix and documented telemetry-wrap semantics |
| Informational | 0 | 0 | Informational architecture notes are recorded in the threat model and limitations inventory |

The detailed status and regression evidence are authoritative in
[`SECURITY_FINDINGS.md`](SECURITY_FINDINGS.md).

## Implemented hardening

### Privileged filesystem and supply-chain boundary

- Root-facing install/build paths reject dot components, symlinks, writable
  parents, unsafe existing directories, and unsafe destination files.
- Kernel artifacts must be regular, non-symlinked, non-group/world-writable,
  owner-appropriate files; installed kernel objects are checked against the
  build manifest.
- The installer refuses to adopt an unmarked existing product tree or
  overwrite a modified command. The manifest records installed tree entries,
  preventing recursive removal of operator-created files.
- Uninstall preserves configuration by default, requires explicit
  `--remove-config`, refuses untracked/symlinked package contents, and does
  not remove a non-matching systemd unit.
- BPF loader pins are scoped to the ORCHESTRA directory and are never removed
  by a broad bpffs cleanup.

### ABI, state, and replay protection

- Bridge v2, kernel state/policy v8, and native coordination/controller v10
  records have explicit magic, version, size, schema, capability, and
  fixed-width fields.
- Scheduler epochs and task start-boottime identity prevent PID reuse from
  becoming task authorization.
- Signal sequence rollback is rejected; runtime state cannot regress to an
  older signal generation.
- Controller, policy metadata, and directive generations are checked for
  equality before adaptive policy can influence a decision.
- Policy rollback selects a retained bank under a **new monotonic
  generation**, avoiding ABA/replay acceptance.

### Publication and lifecycle safety

- Directive, signal, and policy transactions mark the control record
  non-OK before changing payload or bank metadata. `BRIDGE_PUB_OK` is exposed
  only after payload write and readback validation.
- BPF rejects non-OK publication status, so an old payload cannot remain
  authoritative during a failed or partial update.
- Policy loader failures invoke an explicit inactive-bank abort/recovery path.
- Loader unload detaches the link and waits for `sched_ext` to report
  disabled before removing the link pin or map pins. A timeout preserves the
  references for recovery.
- Deferred timer release revalidates the current directive. A cleared,
  replaced, expired, or inconsistent directive is promoted to RUN or remains
  queued for a later safe attempt.

### Resource, arithmetic, and parser safety

- Policy input has bounded bytes, entries, indices, numeric values, JSON
  nesting failure handling, duplicate-key rejection, per-command timeout,
  and total publication timeout.
- Generation increments saturate by refusing wraparound; controller deadline
  arithmetic saturates at `UINT64_MAX`.
- Controller actuator current, previous, rollback, default, and bank
  generations are validated before use.
- The process-group research workload uses `mkstemp`, private permissions,
  and immediate unlinking instead of a predictable `/tmp` filename.
- The loader validates every existing parent component of a root-facing BPF
  artifact path, and a later unload can recover a complete validated pin set
  after the kernel has finished a prior asynchronous detach.

## Threats considered

The audit covered forged/replayed/stale signals, malformed policy and state,
generation rollback/wraparound, PID reuse, unauthorized RT control, policy
bank inconsistency, bridge and loader path substitution, symlink/TOCTOU
cleanup, BPF map exhaustion, deferred-task starvation, migration/yield
storms, controller saturation, telemetry ambiguity, partial initialization,
and teardown failure. The full threat model is in
[`THREAT_MODEL.md`](THREAT_MODEL.md).

## Validation disposition

The final non-privileged gate passed with `make clean && make`, `make check`,
and `make test`. The latter includes strict compile checks, ASan/UBSan unit
tests, GCC/Clang variants, publication concurrency, integration, malformed
CSV/policy tests, mutation testing, installer/uninstaller tests, source
invariants, documentation links, and secret scanning.

The following remain `BLOCKED` or residual on the current Kali host:

- RT/deadline coexistence, CPU hotplug, broad fault recovery, and long soak;
- long-duration kernel soak and target architecture matrix;
- cryptographic signal authentication in the kernel transport;
- independent dependency/SCA, signed provenance, SBOM, and penetration
  testing.

See [`SECURITY_VALIDATION_REPORT.md`](SECURITY_VALIDATION_REPORT.md) for exact
commands and reasons.

## Release recommendation

The `1.0.0` label applies to the observer/userspace and native package
surface. It is suitable for research validation and controlled, target-specific
kernel testing. Do not describe it as a universally secure or
production-certified scheduler: kernel-side signal authentication, broad
recovery, and generalized soak evidence remain outside this release claim.
