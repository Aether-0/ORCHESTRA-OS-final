# Kernel naming cleanup — 2026-10-10

This maintenance update retains version 1.1.1 and changes no scheduler
semantics, map names, ABI layouts, or canonical actions. The complete BPF body
was compared with parent revision `45c2a0b`; only include paths changed.

## Supported paths

| Component | Path under `kernel/sched_ext/` |
| --- | --- |
| Scheduler implementation | `bpf/orchestra_sched.bpf.c` |
| Loader | `loader/orchestra_loader.c` |
| Bridge ABI v2 | `include/orchestra_bridge_abi_v2.h` |
| Coordination helpers | `include/orchestra_coordination.h` |
| Target-matched builder | `scripts/build_scheduler.sh` |
| Ownership/action checks | `scripts/verify_ownership.sh` |
| Runtime reproduction | `scripts/reproduce_runtime.sh` |
| Runtime validation/comparison | `scripts/validate_runtime.sh` |

The canonical output is `orchestra_sched.bpf.o`. Deprecated script, header,
and BPF source names forward to the supported paths. A regular-file object
alias supports older consumers. Discovery and installation accept historical
objects only when the canonical filename is absent, retaining hash and path
checks. Archived evidence was preserved byte-for-byte.

## Local validation

Host kernel: `7.1.5+kali-amd64`; exact source:
`/var/tmp/orchestra-kernel-source-7.1.5-download/linux-source-7.1`.

- `make check test security-test`: passed, including GCC/Clang unit tests,
  multiprocess integration tests, ABI mutations, and security regressions.
- `scripts/build.sh --kernel` with external build output: scheduler BPF,
  userspace workload, bridge, and relocated loader compiled successfully.
- `bpftool prog loadall` on the canonical object: accepted by the running
  kernel verifier. No autoattach or struct_ops registration was performed;
  sched_ext remained disabled. Only the unique verifier pins were removed.
- Debian package build for Kali amd64: passed. `dpkg-shlibdeps` emitted its
  existing staging-location warnings; package creation completed successfully.
- Canonical/legacy bundle hash checks passed, including corruption rejection.

Local command output is retained outside the source tree in
`/home/aether/Downloads/ORCHESTRA-naming-20261010/`. This update does not provide
new runtime action, fairness, stress, or performance evidence. Those remain
explicit gates in the [next-version roadmap](../development/NEXT_VERSION.md).
