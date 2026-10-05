# ORCHESTRA-OS Threat Model

**Model version:** 1.0
**Product version:** 1.0.0 research stable
**Assessment date:** 2026-08-25

## Scope and security posture

ORCHESTRA-OS is a local Linux scheduler control plane with a userspace
observer path and an optional sched_ext kernel path. Its primary protected
property is not confidentiality; it is **safe scheduling behavior under
malformed, stale, unauthorized, inconsistent, or unavailable adaptive
state**.

The intended failure direction is:

    untrusted or uncertain input
        -> reject / invalidate publication
        -> observed-state or RUN decision
        -> conventional Linux scheduling
        -> telemetry and operator diagnosis

The model covers one host. There is no distributed scheduler protocol or
remote Signal Bus in the current product.

## Protected assets

| Asset | Security property | Failure impact |
| --- | --- | --- |
| Conventional scheduling fallback | Availability and progress | Runnable work could be lost or tasks could be starved |
| RT/deadline isolation | Integrity and real-time safety | Adaptive control could interfere with protected workloads |
| Signal/prediction state | Authenticity, freshness, consistency | Forged or stale state could drive bad actions |
| Policy banks and generations | Atomicity and replay resistance | Old/new policies could be mixed or replayed |
| Task identity | Authorization and PID-reuse resistance | A directive could target a different process |
| BPF maps, links, pins | Integrity and lifecycle safety | Foreign state could be detached or deleted |
| Controller state | Stability and bounded adaptation | Overflow, oscillation, or poisoned actuators |
| Telemetry and evidence | Correct attribution | Operators could misdiagnose or overclaim results |
| Installed binaries/configuration | Integrity and recoverability | Root execution or uninstall could be redirected |
| Build provenance | Supply-chain integrity | Unapproved kernel artifact could be activated |

## Trust boundaries

### T1 — unprivileged process to administrative control plane

An ordinary process may attempt to submit policy files, invoke a CLI, create
symlinks, race temporary paths, exhaust maps, or target a reused PID.

Controls:

- root-only bridge operations;
- root-safe installed command/bridge/policy paths;
- exact task identity including start-boottime;
- bounded numeric/action/CPU validation;
- no raw map manipulation for normal use;
- no privilege escalation from the CLI.

### T2 — policy file to policy loader

The policy file is operator input and may be malformed or intentionally
hostile.

Controls:

- safe path-chain, non-writable parent, and no-follow open/exec checks;
- regular-file and, for root publication, root-ownership/non-writable checks;
- UTF-8 and JSON validation;
- duplicate-key rejection;
- schema, enum, integer, range, index, duplicate-entry, byte, entry, and
  parser-recursion bounds;
- per-command and total publication timeouts.

### T3 — policy loader/bridge to BPF maps

The bridge is a privileged writer to fixed ABI maps. A partial multi-map update
is a central integrity threat.

Controls:

- exact map name/type/key/value/capacity checks;
- map spin locks and bounded readback;
- non-OK publication status before payload/meta changes;
- final OK status only after write/readback;
- policy double-bank and monotonic generation;
- abort/recovery of inactive state;
- BPF cross-map generation/mode/controller equality checks.

### T4 — signal/prediction transport to BPF scheduler

The current transport is a local trusted map, not a network protocol.

Controls:

- magic, ABI, size, schema, epoch, sequence, freshness, expiry, fixed-point
  bounds, controller/policy coherence;
- monotonic signal generation and no runtime regression;
- observed-state fallback;
- non-OK publication rejection.

Missing control:

- cryptographic authentication/key management in the kernel path. The
  userspace research HMAC path does not protect this boundary.

### T5 — BPF scheduler to kernel runtime

The BPF object is target-kernel code and can influence scheduling.

Controls:

- exact target BTF/UAPI build contract;
- verifier requirement;
- capability negotiation;
- bounded loops and fixed-width maps;
- action validation and fallback;
- RT/deadline bypass;
- task affinity/online CPU checks;
- watchdog/lifecycle and loader ownership checks.

Runtime status:

- source and userspace contracts are tested;
- the recorded fix34 host passed verifier, attach, exact-TID ownership,
  bounded effective actions, and scoped teardown;
- hotplug, broad recovery, cross-kernel behavior, and long-duration stability
  remain unvalidated.

### T6 — loader/install/uninstall to host filesystem and bpffs

Controls:

- root-only lifecycle;
- root-owned/non-symlink/non-writable artifacts;
- path component checks;
- build-manifest hashes;
- exact pin schemas;
- marker and typed tree manifest;
- no broad bpffs cleanup;
- detach-before-unpin;
- preserve modified/unrelated service/configuration.

## Attacker and fault capabilities

### In scope

- unprivileged local process with normal filesystem/process permissions;
- malicious or malformed policy document supplied to the administrative
  loader;
- stale, duplicated, reordered, oversized, or inconsistent signal/policy
  records presented through allowed control interfaces;
- PID reuse, process exit races, CPU affinity/online changes, map capacity
  exhaustion, controller generation exhaustion, and partial map write failure;
- compromised build workspace or operator-selected symlink/path inputs when
  the caller has not yet acquired root;
- foreign sched_ext ownership or pre-existing bpffs objects.

### Trusted/out of scope

- a malicious root administrator;
- a compromised running kernel, BTF, or verifier;
- arbitrary kernel memory writes;
- a remote/distributed scheduler peer (not implemented);
- hardware faults or physical attacks;
- a signed package authority compromise (release provenance is a separate
  future control).

## Attack-surface matrix

| Surface | Abuse case | Expected result |
| --- | --- | --- |
| scripts/policy_load.py | Duplicate key, recursion, 1 MiB+ input, bool as integer, duplicate index | Reject before bridge execution |
| Bridge CLI | Invalid action/PID/CPU/timing, direct non-root invocation | Reject with bounded error; no map update |
| Policy commit | Entry failure, meta/control write failure, generation max | Inactive abort or non-OK fail-closed state; no trusted new action |
| Signal publish | Replay, stale/future/over-age, partial update | Sequence/schema/freshness rejection; non-OK publication during update |
| Task identity | PID reuse or task exit during update | Start-boottime mismatch rejects target |
| Deferred DSQ | Directive clear/replace/expire while queued | Release revalidation; RUN fallback or safe retry |
| Migration | Offline or disallowed target | Affinity/online check and fallback |
| RT class | FIFO/RR/deadline enrollment | Bridge admission refusal; live coexistence still requires validation |
| Controller | Actuator overflow/history corruption | Bounds validation, saturation, disabled/rollback state |
| Loader | Foreign pins, symlink artifact, attach/unload timeout | Refuse; retain pins on teardown timeout |
| Installer | Modified command, symlink destination, unowned artifact | Refuse overwrite/install |
| Uninstaller | Untracked tree entry, symlink, modified unit/config | Refuse removal/preserve unrelated state |
| Telemetry | Requested/effective confusion or counter wrap | Separate fields; wrap documented as observational |

## Security invariants

1. A value not passing schema, size, epoch, generation, freshness, and bounds
   checks cannot influence an adaptive action.
2. A protected RT/deadline task cannot be admitted by the adaptive bridge
   path.
3. A PID without the matching start-boottime identity cannot receive a
   directive.
4. A non-OK publication status cannot be consumed as current scheduler state.
5. A policy generation never decreases or wraps; rollback receives a new
   generation.
6. A deferred action is revalidated before release.
7. A capability failure becomes an explicit fallback, never a fake supported
   action.
8. A loader teardown timeout preserves recovery references.
9. Installation/uninstallation never follows a symlink or removes an
   untracked product-tree path.
10. Counters and telemetry do not grant authorization or control.

## Abuse cases requiring future validation

- kernel-side authenticated Signal Bus frames with key rotation and replay
  tests;
- BPF verifier rejection and bounded instruction/stack/map complexity;
- concurrent map writer plus timer/callback release on target kernels;
- task ownership under CPU hotplug, cgroup changes, and task exit;
- all five effective actions and RT/deadline coexistence;
- controller saturation, oscillation, and rollback under live load;
- package signature, SBOM, dependency/SCA and reproducible-build verification;
- fuzzing of native BPF/bridge map records on a target-matched harness.

These are not silently assumed PASS. Their current state is recorded in
SECURITY_VALIDATION_REPORT.md.
