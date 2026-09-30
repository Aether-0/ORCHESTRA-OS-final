# ORCHESTRA-OS

ORCHESTRA-OS is a research-grade, capability-tiered Linux scheduling
prototype that coordinates observed runtime state, bounded prediction,
policy, controller gates, and sched_ext actions. The current product line is
`1.0.1-rc1`, an offline-validated hardening candidate based on the research-stable
`1.0.0` observer/control plane and native package lifecycle. Privileged sched_ext verifier, attachment, ownership, and
hardware-runtime behavior remain target-specific experimental gates.

The safe default is observer/userspace operation. Kernel scheduling is
target-specific and is enabled only after the host capability check, a
target-matched BPF build, exact map/schema validation, and administrator
activation. When a capability, signal, policy, controller, or action check
fails, the scheduler preserves a bounded last-known-good/observed state and
falls back to `RUN` or conventional Linux scheduling.

## Architecture

```mermaid
flowchart TD
    H[Hardware and kernel runtime] --> A[State acquisition]
    A --> S[Versioned Signal Bus]
    S --> W[Historical state window]
    W --> P[Bounded prediction and confidence]
    P --> B[Scheduler state builder]
    B --> L[Policy lookup]
    L --> C[Controller and safety gate]
    C --> V[Capability and action validation]
    V --> X[RUN / YIELD / MIGRATE / THROTTLE / SLEEP]
    X --> E[sched_ext execution or safe fallback]
    E --> O[Scheduler observations]
    O --> Q[S1 / S2 / S3 / S4 and geometric-mean Q]
    Q --> F[Deficit classification and bounded actuators]
    F --> C
```

The canonical decision path is one pipeline:

```text
read signal → read prediction → build state → policy lookup
→ controller gate → validate action → execute action → record result
```

The five actions are exactly `RUN`, `SLEEP`, `MIGRATE`, `THROTTLE`, and
`YIELD`. Action records retain requested, controller-adjusted,
capability-adjusted, actual, and fallback outcomes separately.

The corrected coordination index is:

```text
Q = (S1 × S2 × S3 × S4)^(1/4)
```

All four component scores and the aggregation window are required for a
valid Q report. Temporal stability (`S4`) is retained so synchronized mass
switching cannot appear healthy merely because actions agree at one instant.

## Product boundaries and capability tiers

| Tier | Behavior | Current evidence |
| --- | --- | --- |
| Observer | Portable userspace simulation, prediction/metrics contracts, diagnostics, and reports; no scheduling changes | Build, unit, integration, and source checks pass |
| Safe canary | Target-matched sched_ext object, exact map negotiation, explicit opt-in, bounded fallback, and loader-scoped cleanup | Kernel prototype source/build validated; live gate requires privilege and a compatible host |
| Certified kernel | sched_ext ownership, all action outcomes, RT bypass, watchdog/recovery, soak, and rollback evidence | Not claimed by this research-stable release |
| Conventional fallback | CFS/EEVDF remains active whenever ORCHESTRA cannot safely own or decide | Implemented as the fail-closed boundary |

Current ABI contracts are additive and explicit: bridge ABI v2, kernel state
ABI v8, and native coordination/controller ABI v10, bundled by the product
ABI v1 header. The sched_ext BPF object is built against the target kernel's
BTF/UAPI and is not a universal binary. The stable source entry point is
`kernel/sched_ext/bpf/orchestra_sched.bpf.c`; the historical stage7 filename
is retained as the compatibility implementation body and output name.

## Quick start: observer and source validation

The release workflow publishes native all-in-one packages for Ubuntu
24.04/26.04, Debian 13, Fedora 44, and Alpine 3.24 on amd64/x86_64 and
arm64/aarch64. Package installation is observer-only and never compiles or
attaches sched_ext. See the [research-stable release guide](docs/releases/v1.0.0-research-stable.md)
for checksums, attestations, and target-kernel activation boundaries.

From the repository root:

```bash
./scripts/orchestra version
./scripts/orchestra check-system
make clean
make
make check
make test
./scripts/orchestra validate
```

The check is non-mutating by default. Use `./scripts/check-system.sh --json`
for machine-readable output. `--strict` is required before kernel activation
and fails unless the host passes the kernel capability gate.

Build userspace and the bridge into an external directory:

```bash
./scripts/build.sh --userspace --bridge
```

Build the target-matched scheduler only after the compatibility check passes
and an exact kernel source export is available:

```bash
ORCHESTRA_KERNEL_SRC=/lib/modules/"$(uname -r)"/build \
  ./scripts/build.sh --kernel
```

The builder records BTF, UAPI, helper-definition, compiler, source, and
artifact hashes in `ORCHESTRA_BUILD_DIR/build-manifest.txt`. It refuses a
kernel-version mismatch and never writes generated files into the source
tree.

## Install, run, inspect, disable

The installer preserves existing configuration and never enables a scheduler
implicitly:

```bash
sudo ./scripts/install.sh
sudo /usr/local/bin/orchestra status
```

The command above installs the observer/control-plane package. Kernel files
are installed only when explicitly requested with a target-matched build:

```bash
ORCHESTRA_BUILD_DIR=/var/tmp/orchestra-os-build-$(id -u)
sudo ./scripts/install.sh --with-kernel --build-dir "$ORCHESTRA_BUILD_DIR"
```

After an administrator-approved runtime gate, the scx-style foreground
workflow is:

```bash
sudo /usr/local/bin/orchestra run --interval 5
# Ctrl-C verifies the foreground cleanup path and returns to conventional scheduling
```

For a persistent service, install the optional unit and enable it explicitly:

```bash
sudo systemctl enable --now orchestra.service
sudo /usr/local/bin/orchestra status
sudo /usr/local/bin/orchestra telemetry
sudo /usr/local/bin/orchestra disable
sudo /usr/local/bin/orchestra uninstall --keep-config
```

`run` owns the foreground attach and detaches only an instance it attached.
`enable` is the detached/manual equivalent. Both perform a strict capability
check, require root-owned non-symlink kernel artifacts whose hashes match the
build manifest, use schema-checked loader attach, and verify scheduler
ownership. `disable` refuses to detach a foreign sched_ext owner, waits for
the kernel to report `disabled`, and the loader removes only its own
`/sys/fs/bpf/orchestra` pins.

If the host or build cannot pass these gates, stay in observer mode. Do not
copy a BPF object from another kernel and do not use a generic `kill` or broad
bpffs cleanup as a scheduler recovery mechanism.

## Policies and telemetry

Configuration examples are in [`config/examples`](config/examples), with
field/range documentation in [`config/README.md`](config/README.md). A
policy can be checked without touching maps:

```bash
./scripts/policy_load.py \
  --bridge /var/tmp/orchestra-os-build-"$(id -u)"/orchestra_bridge \
  --dry-run config/examples/adaptive.json
```

When kernel mode is active, publish and inspect it through the control plane:

```bash
sudo orchestra policy load /etc/orchestra-os/safe.json
sudo orchestra policy show
sudo orchestra controller status
sudo orchestra telemetry
```

## Documentation

- [Architecture](docs/architecture/ORCHESTRA_OS_ARCHITECTURE.md)
- [Installation](docs/installation/INSTALL.md)
- [Usage](docs/usage/USAGE.md)
- [Troubleshooting](docs/troubleshooting/TROUBLESHOOTING.md)
- [Developer guide](docs/development/DEVELOPMENT.md)
- [Research-to-code mapping](docs/research/RESEARCH_TO_CODE.md)
- [Final validation report](docs/validation/FINAL_VALIDATION_REPORT.md)
- [Final product status](FINAL_PRODUCT_STATUS.md)
- [Maintained diagrams](docs/diagrams/README.md)
- [Security model and operator checklist](docs/security/SECURITY.md)
- [Security audit](SECURITY_AUDIT.md)
- [Security findings ledger](SECURITY_FINDINGS.md)
- [Threat model](THREAT_MODEL.md)
- [Security testing](SECURITY_TESTING.md)
- [Limits and constraint rationale](LIMITATIONS.md)
- [Security validation report](SECURITY_VALIDATION_REPORT.md)
- [Foreground runtime example](examples/runtime/README.md)

## Research and claim discipline

The paper is the algorithmic authority for the pre-kernel simulation. Its
reported seed-42 and sensitivity values are not real-machine measurements.
The repository keeps historical experiments and VM evidence as archival
material; current claims are bounded by the evidence class recorded in
`FINAL_PRODUCT_STATUS.md` and the validation report. No universal speedup
over CFS/EEVDF is claimed.

## License

The product licensing notice is in [`LICENSE`](LICENSE). Kernel-facing files
carry their own GPL-2.0 SPDX notices, and inherited research/third-party
material retains its component license.

## Hardening candidate

See [candidate changes and validation](docs/validation/HARDENING_1_0_1.md).
`bash scripts/realworld_artifact_bundle.sh /absolute/build-directory` checks
one explicit root-owned runtime bundle without loading it. Kernel identity,
file hashes and path permissions must all match. No fallback replaces an
explicit invalid bundle.

`scripts/verify-release.sh` validates historical tar/CycloneDX bundles from
trusted publishers; it runs the packaged version/help commands. Native
DEB/RPM/APK releases instead use their published SHA256SUMS and SPDX records.
