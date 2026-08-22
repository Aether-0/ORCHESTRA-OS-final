# ORCHESTRA-OS Security Findings Ledger

Assessment date: 2026-08-23. Severity describes the impact if the affected
boundary is used in the declared deployment model. “Fixed” means the source
change is present and the listed regression/equivalent evidence passed. It
does not mean that a privileged kernel runtime gate was executed.

## Ledger

| ID | Severity | Status | Component | Class |
| --- | --- | --- | --- | --- |
| SEC-001 | High | FIXED | installer/build/loader | Privileged artifact and path substitution |
| SEC-002 | High | FIXED | uninstaller/package tree | Unsafe recursive removal / confused deputy |
| SEC-003 | High | FIXED | policy rollback | Generation replay / ABA |
| SEC-004 | High | FIXED | loader unload | Unsafe detach and pin teardown |
| SEC-005 | High | FIXED | bridge/BPF publication | Stale payload accepted during partial publication |
| SEC-006 | Medium | FIXED | bridge policy transaction | Cross-map metadata inconsistency |
| SEC-007 | Medium | FIXED | policy loader | Unbounded/ambiguous/malformed input |
| SEC-008 | Medium | FIXED | deferred timer | Stale action after directive replacement |
| SEC-009 | Medium | FIXED | bridge/controller arithmetic | Generation overflow and invalid actuator history |
| SEC-010 | Medium | FIXED | bridge privilege boundary | Direct invocation without explicit root gate |
| SEC-011 | Low | FIXED | research workload | Predictable temporary-file symlink exposure |
| SEC-012 | Medium | RESIDUAL/BLOCKED | signal bus | No cryptographic authentication in kernel transport |
| SEC-013 | Medium | RESIDUAL/BLOCKED | release pipeline | Unsigned provenance/SBOM/dependency review |
| SEC-014 | Medium | BLOCKED | kernel runtime | Verifier/attach/ownership/recovery not run on current host |
| SEC-015 | Low | ACCEPTED LIMIT | telemetry | Lifetime counters wrap modulo 2^64 |

## Detailed findings

### SEC-001 — privileged artifact and path substitution

**Root cause.** The earlier installer/build workflow accepted caller-selected
build and installation paths with lexical checks only. A root invocation could
copy a symlinked, writable, or otherwise attacker-controlled artifact into a
root-executed location; an out-of-tree build directory was not consistently
validated across entry points.

**Reproduction.** Review the pre-hardening `--no-build --with-kernel` path with
an artifact or destination symlink and observe that the privileged copy path
was reached without a complete ownership/parent-chain gate. The regression
target is `tests/security/test_install_paths.sh`; its symlink destination
case must now fail and leave the target untouched.

**Remediation.** `scripts/path_safety.sh` validates absolute paths, rejects
dot components and symlink components, permits writable parents only for
root-owned sticky temporary directories, and requires private ownership for
created directories. The installer validates artifact regular-file status,
ownership, mode, parent chain, and build-manifest hashes. Build output is
kept outside the repository.

**Validation.** `bash tests/security/run.sh`, `make check`, and `make test`
passed. The privileged kernel artifact path remains runtime-blocked because
the current host cannot run an authorized root install/attach campaign.

**Residual risk.** A trusted root can still deliberately install a malicious
artifact; package signatures and independent provenance are separate release
requirements (SEC-013).

### SEC-002 — unsafe recursive uninstall / confused deputy

**Root cause.** Earlier uninstall behavior trusted a configurable prefix and
could recursively remove a tree without proving that it was an ORCHESTRA tree
owned by the current installation. Modified commands, untracked files, and
operator configuration could be exposed to deletion.

**Reproduction.** Place an operator-created file below the product tree and
run uninstall. The pre-hardening behavior could proceed; the current
regression must refuse and preserve the file.

**Remediation.** The installer writes a marker containing product/version,
hashes, and a complete typed entry manifest. Uninstall requires a regular
owner- and mode-checked marker, verifies the command/control-script hashes,
refuses unexpected or symlinked entries, preserves configuration by default,
and only removes recognized configuration paths after explicit
`--remove-config`. Non-matching systemd units are preserved.

**Validation.** `tests/security/test_install_paths.sh` covers untracked-file
refusal, successful safe uninstall, configuration preservation, and command
symlink refusal. The complete `make test` gate passed.

### SEC-003 — policy rollback generation replay

**Root cause.** Rollback selected a retained policy bank and reused its old
generation. A delayed directive or reader could mistake the reused value for a
new publication (an ABA/replay condition).

**Reproduction.** In the old logic, compare the retained bank generation with
the post-rollback metadata generation; it rewound. The current source
invariant rejects any return of the old rewind behavior, and bridge unit tests
exercise the no-wrap helper.

**Remediation.** Rollback relabels the retained inactive bank with a strictly
new generation. `UINT64_MAX` exhaustion fails closed instead of wrapping.

**Validation.** `tests/unit/test_orchestra_bridge.c`,
`tests/unit/test_orchestra_scx_source.py`, `tests/security/run.sh`, and the
strict bridge syntax check passed.

### SEC-004 — unsafe loader unload and pin teardown

**Root cause.** Earlier unload removed the link pin before confirming that the
kernel had completed the sched_ext disable transition. A timeout could leave
an active scheduler without the expected recovery pin.

**Reproduction.** Static lifecycle review showed unpin-before-disabled order;
the current source invariant requires the raw link detach and waits for the
disabled state before unpinning.

**Remediation.** `orchestra_loader.c` keeps an open link reference and pin,
uses `BPF_LINK_DETACH`, polls for `disabled`, and retains pins/maps on timeout.
Cleanup remains scoped to the exact ORCHESTRA pin set.

**Validation.** Source invariants and strict source checks passed. Live link
detach/verifier/ownership validation is `BLOCKED` by missing libbpf headers,
incomplete exact-kernel build inputs, and unavailable non-interactive root.

### SEC-005 — stale payload accepted during partial publication

**Root cause.** Control metadata could be marked `BRIDGE_PUB_OK` before the
corresponding directive or signal payload was written. An observer could see a
new generation and an old payload during that interval.

**Reproduction.** The old publication order was visible in the bridge source:
control update first, payload update second. The BPF snapshot accepted only
the control status and generation constraints, so the intermediate state was
not explicitly fail-closed.

**Remediation.** Directive, signal, and policy publication first write a
non-OK status, then write/read back the payload or inactive policy bank, and
only then publish `BRIDGE_PUB_OK`. BPF rejects every non-OK control snapshot.
Final control readback is required; failure deletes or isolates the payload
and records a non-OK status.

**Validation.** `test_orchestra_scx_source.py` asserts all three staging
paths; bridge strict syntax, unit tests, security tests, and full `make test`
passed. Live concurrent map validation remains a blocked kernel gate.

### SEC-006 — cross-map policy metadata inconsistency

**Root cause.** Policy metadata and control state are stored in separate maps.
An update failure after one map changed could leave an inconsistent pair.

**Remediation.** Publication status is invalidated before the meta flip;
control/meta/policy generation, bank, mode, and controller state must agree in
the BPF policy lookup. The bridge verifies and attempts rollback, and the
policy loader invokes `--policy-abort` after entry/commit failure. A rollback
failure leaves the control record non-OK so the kernel falls back to RUN.

**Validation.** Source invariants and policy-loader lifecycle tests passed.
The map-fault and verifier behavior require a privileged target kernel and
remain `BLOCKED`, so this is source-level fixed with runtime evidence pending.

### SEC-007 — unbounded, ambiguous, or malformed policy input

**Root cause.** The prior loader had an accidental 64-entry limit but no
complete file-size, duplicate-key, recursion, or bridge-command duration
boundary. JSON ambiguity and oversized input could produce excessive work or
different operator/parser interpretations.

**Remediation.** The loader now accepts the ABI's 256 states, caps input at
1 MiB, rejects duplicate JSON keys, rejects recursion overflow, validates all
numeric fields as bounded integers, rejects duplicates/out-of-range indices,
requires safe bridge files, rejects writable parent components before a
privileged open/exec, caps each bridge operation at 10 seconds, and caps a
full publication at 300 seconds. A failed operation calls the bounded
inactive-bank abort path.

**Validation.** `tests/security/test_policy_loader.py` covers the 256-entry
boundary, duplicate indices, duplicate JSON keys, symlinks, writable bridge
parents, oversized input, malformed JSON, recursion, invalid actions and
booleans. The deterministic mutation target ran for 1000 iterations with
both accepted and rejected populations.

### SEC-008 — stale deferred action after directive replacement

**Root cause.** A deferred task outlives the enqueue callback. The timer could
use the old task-state generation after a directive was cleared, expired, or
replaced.

**Remediation.** `deferred_timerfn()` calls `deferred_directive_is_current()`
and reuses the full validated directive loader before release. Any mismatch,
expiry, controller transition, or publication instability is converted into a
work-conserving RUN fallback; failed movement restores the prior state for a
later bounded retry.

**Validation.** Source invariants require the revalidation and global RUN
fallback. Strict tests passed. Actual BPF verifier and concurrent map/timer
behavior remain blocked.

### SEC-009 — controller generation and actuator-history overflow

**Root cause.** Increment expressions could wrap at `UINT64_MAX`, and
controller validation did not check previous/rollback actuator values or bank
generation correspondence.

**Remediation.** Generation helpers refuse wraparound, deadlines saturate,
actuator mutation refuses a generation-exhausted update without changing the
value, controller publication disables on exhaustion, and validation covers
all actuator history and active/staging/previous bank generation links.

**Validation.** ASan/UBSan GCC and Clang controller tests cover saturation,
generation overflow, deadline saturation, invalid history, and bank mismatch.

### SEC-010 — direct bridge invocation lacked an explicit privilege gate

**Root cause.** The bridge relied on BPF map permissions and surrounding
scripts rather than rejecting direct non-root execution at its own entrypoint.

**Remediation.** `main()` rejects non-root invocation before dispatching any
command. RT scheduling classes are separately refused during task admission.

**Validation.** Bridge unit/source invariants and strict compile tests passed.

### SEC-011 — predictable `/tmp` temporary file in research workload

**Root cause.** The process-group research workload used a predictable
temporary filename, creating a local symlink/race exposure if run with higher
privilege.

**Remediation.** It now uses `mkstemp`, sets mode 0600, unlinks immediately,
and bounds the write.

**Validation.** Strict `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wformat=2 -Werror` syntax validation passed. This remains research-only code
and is not a kernel security proof.

### SEC-012 — kernel signal transport is not cryptographically authenticated

**Status.** `RESIDUAL/BLOCKED`, not silently fixed.

The bridge signal frame has schema, epoch, sequence, freshness, bounds,
controller, and policy coherence checks. The ABI explicitly documents that
the kernel-local map transport is not a kernel HMAC verifier. The userspace
paper/reference implementation has HMAC-SHA256 tests, but its property does
not protect the sched_ext map path.

**Impact.** A process that can write the trusted local map or an authorized
root control-plane client can forge a syntactically valid signal. Under the
declared trusted-root model this is outside the attacker boundary; in a
multi-tenant or compromised-publisher deployment it is a release-blocking
integrity gap.

**Required follow-up.** Define kernel-compatible authenticated framing, key
storage/rotation, verifier-cost bounds, replay semantics, and failure
telemetry; then add target-matched BPF and runtime negative tests. Do not
claim cryptographic signal security before that work.

### SEC-013 — unsigned provenance and incomplete supply-chain assurance

**Status.** `RESIDUAL/BLOCKED`.

The build manifest hashes artifacts and the installer verifies hashes and
ownership. It is not a signed provenance statement and does not replace an
SBOM, dependency/SCA review, reproducible-release verification, or key
rotation/attestation process.

**Required follow-up.** Sign target-specific artifacts and manifests, publish
an SBOM, pin/review dependencies, scan release history and CI, and verify the
release in an isolated environment.

### SEC-014 — target-kernel runtime security evidence unavailable

**Status.** `BLOCKED`, not a source defect.

The current host exposes sched_ext/BTF but lacks an authorized non-interactive
root session; the exact kernel build tree is incomplete for the repository's
target-matched build and libbpf development headers are absent. Therefore the
following were not claimed: verifier acceptance, attach, ownership, all five
effective actions, RT coexistence, timer/hotplug recovery, clean unload, or
long-duration stability.

### SEC-015 — telemetry counters wrap

**Status.** `ACCEPTED LIMIT`.

Telemetry counters are fixed-width unsigned values and wrap modulo 2^64.
They are observational counters only; no admission, action, controller, or
fallback decision relies on an exact lifetime total. Window generations and
security-relevant sequence values have separate non-wrapping checks. A future
telemetry ABI may add saturating counters if lifetime accounting is required.

## Static audit conclusion

No reproducible C buffer overflow, use-after-free, double-free, or null
dereference was verified in the reviewed bridge/controller paths. This is a
static/source conclusion, not proof of BPF verifier safety or hardware runtime
correctness. Those claims require the blocked gates in
[`SECURITY_VALIDATION_REPORT.md`](SECURITY_VALIDATION_REPORT.md).
