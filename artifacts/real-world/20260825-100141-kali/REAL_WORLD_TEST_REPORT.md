# ORCHESTRA-OS real-machine validation report

## Outcome

Overall result: **PARTIALLY_VALIDATED**.

The current checkout compiled successfully, the exact Linux `7.0.12` kernel
source build produced a target-matched eBPF object, and the existing
userspace/integration/security regression suites passed when run directly.
The original campaign stopped before scheduler attach because the live desktop
user made the disposable-host premise uncertain. After explicit authorization
for this machine as a scheduler test target, a bounded loader-scoped runtime
continuation completed.

That continuation establishes **KERNEL_PROTOTYPED** evidence for attach,
exact-TID ownership, bounded forward progress, requested actions, matched CFS
versus ORCHESTRA workloads, and clean unload. It does not establish
deployment readiness, performance superiority, NUMA/distributed behavior,
long-run stability, kernel-side signal authentication, or complete actuator
causality.

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
| `./scripts/check-system.sh --strict` | BLOCKED | The normal host still lacks persistent `pkg-config` metadata; an external temporary extraction passed the strict gate without changing host packages. |
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
| CFS CPU benchmark | PASS | Maintained runner completed the earlier 1/2/4/8-worker baseline; the authorized continuation also completed matched 1/2/4-worker rows. |
| CFS mixed benchmark | PASS | Maintained runner completed the earlier 1/2/4/8-worker baseline; the authorized continuation also completed matched 1/2/4-worker rows. |
| CFS stress suite, 3 seconds | PASS | CPU, bounded memory (2 x 3189 MiB), I/O, mixed, and health-check rows all passed; no new panic/stall/RCU/hung-task lines. |
| sched_ext attach/ownership/actions | PASS, bounded | Authorized P0 continuation attached through the exact loader, proved accepted/dispatched/running telemetry for an exact target TID, published YIELD/MIGRATE/THROTTLE/SLEEP requests, and unloaded cleanly. Effective-action scope remains bounded; see the continuation section. |
| ORCHESTRA CPU benchmark | PASS, bounded | Matched 80M-iteration rows for 1/2/4 workers completed with ownership confirmed; no performance advantage claim. |
| ORCHESTRA mixed benchmark | PASS, bounded | Matched CPU/I/O rows for 1/2/4 workers completed with ownership confirmed; no performance advantage claim. |
| ORCHESTRA stress, 3 seconds | PARTIALLY_VALIDATED | CPU, I/O, mixed, and health rows passed with ownership confirmed; memory stress remained `BLOCKED_OWNERSHIP_NOT_PROVEN` because `stress --vm` child TIDs are not exposed. |
| `scx_simple` runtime comparison | BLOCKED | Binary build succeeded, but this campaign did not start it: the existing harness tears down that scheduler with `kill`, which is not an approved scheduler-unload mechanism. |

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
original pre-authorization safety result; the later authorized continuation is
recorded below.

## Authorized runtime continuation

The user explicitly authorized this host for a controlled scheduler runtime
test after the original safety stop. The continuation used the same host and
target-matched artifacts, with the mandatory pre-attach inventory preserved at
`/tmp/orchestra-runtime-20260825-kali-wL8qsm/pre-attach-inventory.stdout`.
The live graphical session remained present, so the run stayed bounded and
used only the repository loader/bridge control plane.

The P0 ownership retest completed with zero failures:

- exact loader attach: PASS; sched_ext transitioned to `enabled`;
- CFS baseline: PASS, 2,000,000 iterations, 6 ms;
- exact-TID forward progress: PASS, 60 ms, `accepted=2 dispatched=2 running=1`;
- action requests: YIELD, MIGRATE, THROTTLE, and SLEEP were each published
  only after the positive ownership gate;
- per-task post-request status showed positive effective counts for the four
  probes (14, 15, 10, and 9 respectively), with zero fallback/errors in the
  captured task records;
- exact loader unload: PASS; final sched_ext state was `disabled`.

The action result is intentionally bounded. The P0 result proves that the
bridge request reached an admitted task and that the task status recorded
effective activity; it does not by itself prove broad actuator causality. In
particular, the captured global YIELD/MIGRATE dispatch counters were zero in
those short probes. No coordination Q claim is made.

The matched benchmark parameters were fixed-work 80,000,000 iterations,
1/2/4 workers, 3-second watchdog duration, and the same CPU affinity protocol
for CFS and ORCHESTRA. The elapsed milliseconds were:

| Workload | Workers | CFS | ORCHESTRA | Ownership evidence |
| --- | ---: | ---: | ---: | --- |
| CPU | 1 | 2553 | 2738 | `2/2/2`, owned=yes |
| CPU | 2 | 2916 | 2892 | `7/7/7`, owned=yes |
| CPU | 4 | 2917 | 2795 | `30/30/30`, owned=yes |
| mixed CPU/I/O | 1 | 2558 | 2749 | `2/2/2`, owned=yes |
| mixed CPU/I/O | 2 | 2916 | 2844 | `6/6/6`, owned=yes |
| mixed CPU/I/O | 4 | 2967 | 2846 | `27/27/27`, owned=yes |

The values in the ownership column are accepted/dispatched/running totals at
the release gate. They are ownership evidence, not a performance metric.
The raw benchmark directories are
`/tmp/orchestra-runtime-20260825-kali-wL8qsm/bench-cpu/` and
`/tmp/orchestra-runtime-20260825-kali-wL8qsm/bench-mixed/`.

The bounded ORCHESTRA stress continuation ran for 3 seconds per CPU, I/O, and
mixed phase with 24 allowed CPUs. CPU (24 workers), I/O (12 files), mixed (12
CPU and 12 I/O workers), and the sorted dmesg health delta all passed with
zero errors/warnings and exact ownership confirmed. The memory row remained
`BLOCKED_OWNERSHIP_NOT_PROVEN`: `stress --vm` creates child workers whose TIDs
are not admitted by the existing bridge protocol. Thermal samples peaked at
72°C in the reported zones, below the reported critical trips, and no thermal
event or new health warning occurred. The raw stress directory is
`/tmp/orchestra-runtime-20260825-kali-wL8qsm/stress-orchestra/`.

The independent final inventory recorded sched_ext `disabled`, no ORCHESTRA
matches in `bpftool prog list`, only the pre-existing `/sys/fs/bpf` root pin,
and no remaining campaign workers. The raw mandatory final inventory is at
`/tmp/orchestra-runtime-20260825-kali-wL8qsm/final-inventory/`.

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
`.BTF`, and `.BTF.ext`. The authorized continuation also loaded this exact
object through the repository loader and the kernel accepted it on this host;
that does not prove portability to another kernel or deployment readiness.

## Safety and cleanup

The pre-runtime inventory recorded a live graphical session (`sharda` on
`seat0`); the later runtime was authorized as a bounded test despite that
context. No CPU governor, kernel, bootloader, package set, persistent
configuration, or foreign BPF state was changed. The final inventory confirmed
sched_ext disabled, no ORCHESTRA matches, no ORCHESTRA pins, no remaining
benchmark workers, and cleanup of campaign-created workload files. Unrelated
system-managed BPF programs were preserved. Thermal samples stayed below
reported critical trips and the stress health delta was clean.

## Open gates and next action

The next work is implementation/evidence expansion, not a reason to promote
this release to deployment-ready:

1. Add an approved graceful lifecycle for the external `scx_simple` comparison
   (or update the harness to use one) before running that phase.
2. Expose and opt in the actual memory-stress child TIDs, or replace that
   workload with an existing process model whose identities the bridge can
   admit.
3. Repeat the matched benchmark with a pre-registered repetition count and
   report distributions, not single-run timing rows. Report the coordination
   metric only with all S1/S2/S3/S4 components, window, population, exclusions,
   and formula recorded.
4. Run the still-open security provenance, predictor convergence, actuator
   rollback, multicore/NUMA, soak, and recovery gates. These remain
   `INSUFFICIENT_EVIDENCE` or `BLOCKED_NOT_IMPLEMENTED`, not passes by
   implication from P0.
5. Keep the dirty-checkout `AGENTS.md` security-scan result separate from the
   clean committed-HEAD regression result; do not weaken the scanner to make
   the user-owned policy file pass.

At the original campaign timestamp, the host inventory marked `scx_simple`,
`perf`, `stress-ng`, `fio`, `iperf3`, and `shellcheck` as
`BLOCKED_MISSING_DEPENDENCY`; the follow-up supplied external `scx_simple` and
`pkg-config` artifacts, but did not install persistent host packages or
provide the remaining optional tools.
