# ORCHESTRA-OS security model

The current hardening assessment and evidence package are maintained in the
security documents [`SECURITY_AUDIT.md`](SECURITY_AUDIT.md),
[`SECURITY_FINDINGS.md`](SECURITY_FINDINGS.md),
[`THREAT_MODEL.md`](THREAT_MODEL.md),
[`SECURITY_TESTING.md`](SECURITY_TESTING.md),
[`LIMITATIONS.md`](../../LIMITATIONS.md), and
[`SECURITY_VALIDATION_REPORT.md`](SECURITY_VALIDATION_REPORT.md).

ORCHESTRA-OS changes kernel scheduling only through an explicitly installed,
target-matched sched_ext artifact set and an administrator-controlled loader.
The security objective is fail-closed scheduling control: an invalid or stale
input must become observed-state/RUN/conventional scheduling, not an
unbounded kernel action.

This document describes the current research-release boundary. It is not a
claim that the prototype has completed a production security review.

## Trust boundaries

| Boundary | Trusted inputs | Defensive rule |
| --- | --- | --- |
| Kernel/BPF | target kernel, BTF/UAPI, verifier, sched_ext | build and validate against the exact running kernel; never copy a BPF object between kernels |
| Loader | root-owned installed loader, BPF object, and manifest | reject symlinks and group/world-writable files/directories; verify loader/object hashes before activation |
| bpffs | ORCHESTRA pins created in `/sys/fs/bpf/orchestra` | exact map/link schema and ownership are required; cleanup is scoped to this directory |
| Bridge | root operator, versioned maps, fixed-width CLI values | exact ABI/schema, identity, generation, bounds, expiry, and RT admission checks |
| Signal/prediction | versioned local records | magic/schema/generation/freshness/confidence gates; observed-state fallback |
| Policy | reviewed administrator input | bounded entries, actions, state indices, timing, controller states, and generation-safe publication |
| Scheduler owner | the active `root/ops` name and expected struct_ops link | `enable`, `run`, and `disable` refuse a foreign or ambiguous owner |

The local root administrator and the kernel are trusted in this model. A
root compromise is outside the boundary: root can alter the loader, kernel,
bpffs, policy, or the scheduler itself.

## Current controls

- Observer mode is the default; installation does not enable sched_ext.
- Kernel mode requires a strict capability check and an explicit administrator
  action.
- The foreground `orchestra run` command owns the attach it created and
  performs bounded cleanup on exit. It does not detach an already-owned
  scheduler merely because its monitor exits.
- The loader pins maps before struct_ops attach, checks exact map names,
  types, sizes, and capacities, and refuses unload unless the active scheduler
  owner, pinned link, pin directory, and all expected map schemas match.
- Task directives carry PID/TGID/start-time identity and generation/freshness
  checks. Protected `SCHED_FIFO`, `SCHED_RR`, and `SCHED_DEADLINE` tasks are
  refused by the adaptive bridge path.
- The five actions are bounded and capability-adjusted. Requested, accepted,
  dispatched, effective, and fallback outcomes remain separate telemetry.
- Prediction and signal records use fixed-width fields, explicit schemas,
  model/generation metadata, freshness/expiry, confidence, and observed-state
  fallback.
- Policy publication uses bounded double-bank/generation semantics; a partial
  update cannot become the active policy.
- The controller retains bounds, rate limits, hysteresis, saturation,
  previous-known-good state, rollback, and recovery states.
- Installer and uninstaller paths reject unsafe prefixes, preserve existing
  configuration by default, avoid broad bpffs cleanup, and preserve a modified
  systemd unit rather than overwriting/removing it silently.
- CI runs non-privileged build, source, test, documentation, and secret scans.
  Target-matched kernel builds are isolated to an explicitly labeled
  self-hosted runner; hosted CI does not claim verifier or attach results.

## Important residual risks

1. The kernel signal frame is a local validated transport, not a cryptographic
   HMAC/authenticated message path. Generation, freshness, identity, and
   schema checks do not provide cryptographic authenticity.
2. The build manifest detects post-build artifact changes but is not a signed
   provenance statement. A production release needs signed artifacts, SBOM,
   dependency/SCA review, and a verified release process.
3. Loader, bridge, and BPF behavior remains kernel/API-family dependent. A
   successful compile does not establish verifier acceptance or safe attach on
   another kernel.
4. The current repository has source/unit evidence plus limited privileged
   ownership, effective-action, and scoped-unload evidence on the recorded
   host. Fault recovery, hotplug, broad RT coexistence, and long-duration
   runtime gates remain pending.
5. No distributed or remote scheduling protocol is implemented. Network
   attack claims and cluster isolation are therefore out of scope.
6. The CLI is an administrative control plane, not a multi-tenant policy
   service. Systems that delegate policy publication must add an explicit
   authorization boundary and audit policy.

## Operator security checklist

Before kernel activation:

1. Use a dedicated test host with an approved recovery path.
2. Capture the current sched_ext state, active BPF links/maps, kernel version,
   and bpffs contents.
3. Run `orchestra check-system --strict`.
4. Build from the exact running kernel's source/BTF/UAPI and retain the build
   manifest.
5. Install with `scripts/install.sh --with-kernel`; do not copy artifacts from
   another host or kernel.
6. Review policy files and their ownership before publishing them.
7. Verify `state=enabled` and `ops=orchestra_scx_v8`, then prove workload
   ownership in telemetry before collecting performance results.
8. Disable through `orchestra disable` or the managed service; never delete
   all of `/sys/fs/bpf`.
9. Preserve kernel logs and raw telemetry for every failure, including the
   first failure after a retry.

## Security testing status

The repository has source-level invariants and userspace validation for input
bounds, schema/generation/freshness behavior, policy lifecycle, fallback,
artifact checks, and cleanup ownership. The `1.0.0` release adds checksummed
native packages and SPDX SBOMs. CI provenance attestations are conditional on
GitHub support for the repository visibility and ownership type. Cryptographic
signal authentication inside the kernel transport and production penetration
testing remain outside the research-stable claim.

## Vulnerability reporting

Do not commit secrets, private keys, kernel dumps, or machine-specific logs to
an issue or pull request. For a suspected vulnerability, use the private
repository's GitHub security advisory channel when available; otherwise contact
the repository owner privately with reproduction steps, affected commit,
impact, and a minimal proof. Allow time for coordinated remediation before
public disclosure.
