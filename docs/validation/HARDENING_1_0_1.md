# 1.0.1-rc1 hardening candidate

Base: v1.0.0 commit ad49f0e92370f2029eb34a88415287c1be833604.

Changes:
- Policy file opens are nonblocking so FIFOs are rejected before reading.
- Publication stops when its overall deadline expires. Abort has a separate,
  bounded ten-second recovery budget. Bridge execution errors return 126 and
  follow the abort path; timeouts return 124.
- SLEEP/controller and THROTTLE telemetry deadlines use the existing saturating
  arithmetic helper. No ABI layouts or canonical actions change.
- Explicit artifact bundle validation checks kernel, ownership, paths and hashes;
  duplicate required manifest fields are rejected.
- Reviewed local tar/CycloneDX release verifier added with archive safety tests,
  including rejection of duplicate archive members. It is for trusted executable
  release bundles, not a sandbox for arbitrary archives.

Validation is offline only. No sched_ext attach, installation, stress, or
performance testing is performed. Current THROTTLE efficacy, kernel concurrency,
verifier acceptance and deployment readiness are not established by these fixes.
Existing runtime findings retain their original artifact identities.

The original asynchronous security baseline overlapped edits and failed on a
not-yet-created test file; retain that log as a campaign sequencing error.
The frozen v1.0.0 baseline is checked separately. Final command logs and results
are delivered beside the candidate source archive.

## Results — 30 September 2026

PASS: make check, test-unit, test-integration, security-test, userspace/bridge/
loader build, and target-matched kernel BPF compilation for 7.1.5+kali-amd64.
PASS: five policy liveness regressions; valid/mismatched/duplicate/corrupt/
missing/symlinked bundle fixtures; archive safety and duplicate-member rejection.
PASS: explicit bundle verification of the newly compiled target artifacts.
The original release FIFO hang, expired-deadline execution, and unhandled
execution exception were reproduced offline. Frozen release security tests pass.

Reviewed loader ownership/schema validation, detach-before-cleanup behavior,
policy abort, generation exhaustion checks, and saturated runtime accounting
were retained. No claim of a complete concurrency audit is made. The bundle
fixtures isolate identity checks; they do not emulate kernel attachment or
replace the existing path-security regressions.
