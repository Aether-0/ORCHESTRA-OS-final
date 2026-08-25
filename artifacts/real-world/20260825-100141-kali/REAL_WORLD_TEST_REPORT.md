# ORCHESTRA-OS real-machine validation report

## Outcome

Overall result: **PARTIALLY_VALIDATED**.

The current checkout compiled successfully, the exact Linux `7.0.12` kernel
source build produced a target-matched eBPF object, and the existing
userspace/integration/security regression suites passed when run directly.
Safe CFS-only benchmark and stress baselines also completed. sched_ext attach,
ownership, effective-action, and ORCHESTRA performance phases were **BLOCKED_FOR_SAFETY**:
the live desktop user was present and this checkout does not establish that the
host is disposable/dedicated or that scheduler recovery is authorized.

No ORCHESTRA scheduler was attached during this campaign. Therefore this
report contains no verifier-acceptance, task-ownership, effective-action,
ORCHESTRA timing, S1/S2/S3/S4/Q, or controller-runtime claim for this host.

## Campaign and environment

| Field | Value |
| --- | --- |
| Campaign | `20260825-100141-kali-5nvoRG` |
| Date | `2026-08-25` |
| Host | `kali` |
| OS | Kali GNU/Linux Rolling `2026.3` |
| Kernel | `7.0.12+kali-amd64` |
| Commit | `94664aeb001d8b3552245aedfd78254fbb5f13b8` |
| Branch | `main` |
| CPU | Intel Core i7-13700, 16 physical / 24 online CPUs (`0-23`) |
| NUMA | one node |
| RAM | approximately 15 GiB |
| Governor | `powersave` (unchanged) |
| sched_ext before/after | `disabled` / `disabled` |
| Existing BPF state | unrelated systemd LSM/cgroup/socket-filter programs; preserved |
| bpffs pins before/after | only `/sys/fs/bpf` / only `/sys/fs/bpf` |

Raw command output and measurements are retained externally under the campaign
directory named above. Generated binaries and kernel source were kept outside
the repository.

## Results

| Phase | Result | Evidence and interpretation |
| --- | --- | --- |
| `./scripts/check-system.sh --json` | PASS with warnings | Observer PASS; kernel activation WARNING only because `pkg-config` metadata for libbpf/libelf/libzstd is unavailable. |
| `./scripts/check-system.sh --strict` | BLOCKED | Correctly returned `1` for the capability warnings; no strict activation claim. |
| `git diff --check` | PASS | No whitespace errors. |
| `make clean && make` | PASS | Existing userspace product compiled with GCC 15.3.0. |
| `make check` | FAIL | Compiler/schema/doc checks completed; repository security scan failed on the pre-existing modified `AGENTS.md` absolute `/home/...` entries. `AGENTS.md` was not changed by this campaign. |
| `make test` | FAIL | Stopped at the same `make check` security-scan failure. |
| `make test-unit` | PASS | 30/30 named unit tests; GCC/ASan/UBSan, legacy reference, and Clang passes completed; benchmark-validator (24), signal-runner (4), and sched_ext source invariants passed. |
| `make test-integration` | PASS | MAP_SHARED publication stress and baseline/ORCHESTRA/tamper/controller/signal-stop CSV scenarios passed. |
| `make security-test` | PASS | Policy-loader, ABI/state mutation, installer path, and sched_ext safety regressions passed. |
| `./scripts/build.sh --userspace --bridge` | PASS | External bridge and loader artifacts built. |
| target-matched `./scripts/build.sh --kernel` | PASS | Exact Linux `7.0.12` source archive, live BTF, UAPI, and helper generator used; BPF object is target-specific. |
| policy dry-run from repository path | FAIL/blocked by safety contract | Root correctly rejected the user-owned `/home` path. |
| policy dry-run from root-owned `/var/tmp` copy | PASS | Five-action policy parsed and generated bridge commands; no maps were published. |
| CFS CPU benchmark | PASS | Maintained runner completed 1/2/4/8-worker fixed-work rows; ORCHESTRA phase stopped before attach because no repository loader artifact was present. |
| CFS mixed benchmark | PASS | Maintained runner completed 1/2/4/8-worker rows; `scx_simple` missing and ORCHESTRA phase stopped before attach. |
| CFS stress suite, 3 seconds | PASS | CPU, bounded memory (2 x 3189 MiB), I/O, mixed, and health-check rows all passed; no new panic/stall/RCU/hung-task lines. |
| sched_ext attach/ownership/actions | BLOCKED_FOR_SAFETY | Not run; no runtime evidence was fabricated from compilation or CFS baselines. |

## Clean committed-HEAD recheck

Because the live checkout contains a user-edited `AGENTS.md`, the committed
revision was exported to an external clean worktree with an isolated Git index
pointing at `HEAD`. The following rerun completed successfully:

- `make clean && make`: PASS;
- `make check`: PASS, including `SECURITY_SCAN_PASS`;
- `make test`: PASS;
- `./scripts/build.sh --userspace --bridge`: PASS;
- target-matched `./scripts/build.sh --kernel`: PASS.

The clean-HEAD kernel object hash was
`74747faf81c62ffe632db4b4920ec8778427a179482d269c0723f39d5d1f8a3e`.
The difference from the live-checkout object hash is expected from the
different absolute source path embedded in debug information; the source,
kernel, UAPI, BTF, compiler, and loader contracts were otherwise the same.
This clean recheck validates the committed implementation and still does not
prove verifier acceptance, sched_ext ownership, effective actions, or runtime
performance.

The CFS timing rows are exploratory baselines only. They are not a comparison
against ORCHESTRA and do not establish a performance advantage.

## Post-campaign blocker follow-up

After the original campaign, the repository-side benchmark path defect was
fixed and published at implementation commit
`15f5c99f3d4c0eaa3c6008f7d422bca5099062ca`. The runner now honors an explicit
`ORCHESTRA_BUILD_DIR` and derives the externally built BPF object, bridge, and
loader from that directory. Shell syntax, whitespace, unit, integration, and
security checks passed for the change. No scheduler was attached for this
follow-up.

The comparison scheduler was built from the exact Linux `7.0.12` source export
at `/var/tmp/orchestra-scx-simple-build-20260825-kali/build/bin/scx_simple`.
The build required only external-tool overrides for the host's available
`ld` and `ar` in place of missing `ld.lld` and `llvm-ar`. The resulting binary
SHA-256 is
`52c68ac711c7660618f786649756145e445456a7f976defef014281d1aacfd45`.
The binary was inspected and its help path was exercised; it was not started
as a scheduler.

The strict capability gate was also rerun successfully using a temporary,
out-of-tree extraction of the available Kali `pkg-config`/`pkgconf` packages;
no packages were installed into the host. `libbpf`, `libelf`, and `libzstd`
metadata all passed and `kernel_activation=PASS`. The host's persistent
package state remains unchanged, so a normal installed-toolchain rerun still
requires package authorization if desired.

The follow-up raw evidence is under
`/tmp/orchestra-realworld-20260825-100141-kali-5nvoRG/followup-dependencies/`;
the refreshed evidence-manifest SHA-256 is
`7b40234aae7e8501ddc43c5e8e73f29f1459367a4e15953b7a7fe3518e965349`.
These fixes remove build and artifact-path blockers but do not change the
original `BLOCKED_FOR_SAFETY` sched_ext runtime result.

## Kernel build provenance

The exact source archive was `/usr/src/linux-source-7.0.tar.xz`, whose SHA-256
was `6626c3e01e91b755f49f859e580eb9d724adb7c076c7c0d9d2cc34d62965bab4`.
Its Makefile reported `7.0.12` and contained the required sched_ext headers,
`bpf_helpers.h`, matching BPF UAPI, and `scripts/bpf_doc.py`.

The external build manifest SHA-256 was
`f26455ce2d0139a086d1b5ca61a72d4551ec6066d588a93c30e9ed148628faa5`.

| Artifact | SHA-256 |
| --- | --- |
| live `/sys/kernel/btf/vmlinux` | `3f39484930b332629a5864a1a703b0a39320cc1584f6e8a00dfbc6375b58ec76` |
| generated `vmlinux.h` | `96b223c8eaa9763f6caa0998188cf632aa0133c096bf526edc0043db3e675a51` |
| `orchestra_scx_stage7.bpf.o` | `a5aa5473d29c1e4287f448f7d95c056172caaa4bc92d0641e188ee0a2d316626` |
| `orchestra_bridge` | `62078c52ec224568cf71a3152d8c03ada3e80894fe228cdc123ff1750008e25d` |
| `orchestra_loader` | `af1aa651804a483a3952798e0e1f6f05f35545a2b8400fc2f733f32da22939d2` |

The object is an eBPF ELF relocatable containing `.struct_ops`, `.maps`,
`.BTF`, and `.BTF.ext`. This proves compilation and object structure only;
the verifier and loader were intentionally not exercised.

## Safety and cleanup

The pre-runtime inventory recorded a live graphical session (`sharda` on
`seat0`), so the required disposable-host premise was not established. No
CPU governor, kernel, bootloader, package set, persistent configuration, or
foreign BPF state was changed. The post-run inventory confirmed sched_ext
disabled, unchanged unrelated BPF inventory hashes, no ORCHESTRA pins, no
remaining benchmark workload, and unchanged temporary-workload cleanup.
Thermal samples stayed below reported critical trips; the final CFS stress
health delta was clean.

## Open gates and next action

The next runtime campaign requires explicit confirmation that the host is a
dedicated/recoverable scheduler test target, followed by the mandated
pre-attach inventory and the repository loader-scoped P0 ownership gate. Only
after accepted, dispatched, and running telemetry is proven should effective
actions or timing be reported. Missing tools remain `BLOCKED_MISSING_DEPENDENCY`
for `scx_simple`, `perf`, `stress-ng`, `fio`, `iperf3`, and `shellcheck`.
