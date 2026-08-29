# ORCHESTRA-OS Security Validation Report

**Product:** ORCHESTRA-OS 1.0.0 research stable
**Assessment date:** 2026-08-25
**Repository revision at assessment start:** c85ababc0c2c9a2ae1b47b265fd5b5fa414abf1d
**Final hardening revision:** c85ababc0c2c9a2ae1b47b265fd5b5fa414abf1d
**Disposition:** research-stable userspace release; not production-certified

## Gate summary

| Gate | State | Evidence / reason |
| --- | --- | --- |
| Repository audit and findings ledger | PASS | SECURITY_AUDIT.md and SECURITY_FINDINGS.md |
| Strict userspace build | PASS | make clean && make |
| Strict source/build checks | PASS | make check |
| ASan/UBSan userspace tests | PASS | tests/unit/run.sh through make test |
| GCC/Clang warning matrix | PASS | tests/unit/run.sh |
| Publication concurrency stress | PASS | GCC and Clang MAP_SHARED generation tests |
| Integration/malformed-input validation | PASS | tests/integration/run.sh through make test |
| Policy/ABI parser/property/mutation tests | PASS | tests/security/run.sh; 1000 policy and 50,000 ABI/state mutations under ASan/UBSan |
| Installer/uninstaller security regression | PASS | disposable-prefix lifecycle test |
| ABI/source invariants | PASS | tests/unit/test_orchestra_scx_source.py |
| Secret/path scan | PASS | scripts/security-scan.sh |
| Documentation link check | PASS | make check |
| BPF target compile | PASS | fix34 target-matched build and installed manifest |
| BPF verifier acceptance | PASS | fix34 loader attach on `7.0.12+kali-amd64` |
| Privileged attach and ownership | PASS, limited | exact-TID P0 gate; current host only |
| Five effective kernel actions | PASS, limited | bounded RUN/SLEEP/THROTTLE/YIELD/MIGRATE probes with ownership proof |
| RT/deadline coexistence | BLOCKED | protected RT admission boundary observed; full coexistence matrix not run |
| Fault/hotplug/recovery/unload | PASS, limited | invalid PID/CPU rejection and scoped unload; hotplug/broad recovery remain |
| Kernel signal cryptographic authentication | BLOCKED | current transport is explicitly non-cryptographic |
| Signed provenance/SBOM/SCA/penetration test | BLOCKED | release-security work not present in this campaign |

## Exact commands executed

The final gate was:

    make clean && make
    make check
    make test
    git diff --check

Security-specific commands also passed:

    bash tests/security/run.sh
    python3 tests/security/test_policy_loader.py
    python3 tests/security/fuzz_policy_loader.py --iterations 1000
    bash tests/security/test_install_paths.sh
    python3 tests/unit/test_orchestra_scx_source.py
    scripts/security-scan.sh

The userspace security tests ran under GCC and Clang with AddressSanitizer and
UndefinedBehaviorSanitizer through tests/unit/run.sh. The concurrency test
completed with zero regressions, torn-frame detections, invalid reads,
duplicates, reader crashes, reader hangs, or retry exhaustions in the
reported runs. The integration suite passed signal publication contention,
tamper/rejection, policy lifecycle, validator, and teardown scenarios.

## Host-specific runtime addendum

The current host is Kali Linux x86_64 on kernel 7.0.12+kali-amd64. sched_ext,
BTF, clang, gcc, make, and bpftool are present. The fix34 target-matched
artifact passed verifier acceptance, attach, exact-TID ownership, bounded
action probes, and scoped unload. Invalid PID and CPU requests were rejected.
Every runtime phase returned to `sched_ext=disabled` with no ORCHESTRA pins.

The earlier 2026-08-23 blockers were:

- no authorized non-interactive sudo/root session;
- the running-kernel build directory does not provide the complete source
  inputs required by the repository's target-matched BPF build;
- libbpf development headers/pkg-config inputs are unavailable;
- the machine is not treated as a dedicated scheduler crash/reboot target.

Those conditions describe the earlier assessment and are superseded only for
the current host by the fix34 evidence. No claim is made for full RT/deadline
coexistence, hotplug, long-duration stability, kernel-side cryptographic
signal authentication, signed provenance, or another kernel/architecture.

## Vulnerabilities discovered and status

Fixed findings:

- privileged artifact/path trust;
- unsafe recursive uninstall and modified-file adoption;
- policy rollback generation replay;
- unsafe loader unpin ordering;
- stale payload acceptance during partial directive/signal publication;
- cross-map policy publication inconsistency;
- unbounded/ambiguous policy parser inputs;
- stale deferred action release;
- controller generation/actuator-history validation;
- missing direct bridge root gate;
- predictable temporary-file use in the research workload.

Residual or blocked findings:

- no kernel-side cryptographic signal authentication;
- unsigned build provenance and incomplete SBOM/dependency review;
- target-kernel verifier/attach/runtime evidence;
- telemetry lifetime-counter wrap as an accepted observational limit.

Detailed reproduction, remediation, regression coverage, and residual risk are
in SECURITY_FINDINGS.md.

## Constraint disposition

The old 64-entry policy loader limit was removed because it did not match the
256-entry kernel ABI. Retained bounds are documented in LIMITATIONS.md and
cover map capacity, verifier work, stale-state freshness, action duration,
parser/resource usage, controller arithmetic, and test containment. No
retained active-path limit is undocumented.

## Release decision

This revision is suitable for observer/userspace research and controlled
target-specific kernel testing. It is **not** suitable for a claim of
production-certified kernel scheduling, universal security, cryptographic
Signal Bus authenticity, or universal performance improvement. Advance to
1.0.0 only after the remaining runtime, provenance, and signal-authentication
gates are independently completed with preserved evidence.
