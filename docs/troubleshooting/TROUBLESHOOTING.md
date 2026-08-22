# Troubleshooting ORCHESTRA-OS

Use the smallest safe diagnostic first. Preserve the command, exit code,
stderr, kernel state, and build manifest when recording a failure.

## sched_ext unavailable

**Symptoms:** `check-system` reports `FAIL` or `WARNING`; sysfs state is
`unavailable`; `enable` refuses activation.

**Cause:** the running kernel lacks sched_ext or the required configuration.

**Diagnose:**

```bash
uname -r
./scripts/check-system.sh --json
test -r /sys/kernel/sched_ext/state && cat /sys/kernel/sched_ext/state
```

**Fix:** use observer mode or boot a separately approved compatible kernel.
Do not change boot configuration as part of a routine product install.

## BTF or required kernel input missing

**Symptoms:** the target build reports `BLOCKED_MISSING_INPUT`, BTF is absent,
or `vmlinux.h` generation fails.

**Cause:** the running kernel's BTF or exact source/libbpf inputs are not
available.

**Diagnose:**

```bash
test -r /sys/kernel/btf/vmlinux && echo BTF_PRESENT
ORCHESTRA_KERNEL_SRC=/lib/modules/"$(uname -r)"/build \
  ./scripts/build.sh --kernel
```

**Fix:** provide the exact matching headers/source export, including sched_ext
and BPF helper inputs, or remain in observer mode. Never borrow another
kernel's generated header.

## clang, bpftool, libbpf, or compiler failure

**Symptoms:** `check-system` lists a missing tool or bridge build fails at a
header/link step.

**Cause:** an uninstalled prerequisite or a distribution packaging mismatch.

**Diagnose:**

```bash
command -v clang bpftool cc make python3
pkg-config --modversion libbpf 2>/dev/null || true
make bridge
```

**Fix:** install the documented distribution development packages through the
machine's normal administrator process. The product does not silently install
dependencies.

## Verifier or loader rejection

**Symptoms:** `orchestra enable` fails during `bpf_object__load` or attach;
kernel logs show verifier or sched_ext errors.

**Cause:** target API mismatch, invalid BPF program, map schema mismatch,
insufficient privilege, or another active sched_ext owner.

**Diagnose:**

```bash
./scripts/check-system.sh --strict
sha256sum "$ORCHESTRA_BUILD_DIR/orchestra_scx_stage7.bpf.o"
sudo bpftool prog list
sudo bpftool map list
sudo bpftool link list
sudo dmesg --ctime | tail -100
```

**Fix:** preserve the first error and rebuild against the exact target BTF/API.
Check ownership and permissions. Do not weaken compiler/verifier checks or
remove unrelated BPF objects.

## Permission or sudo failure

**Symptoms:** `operation requires root`, `EPERM`, or inability to create
`/sys/fs/bpf/orchestra`.

**Cause:** BPF map/attach operations require administrator authority and the
bpffs path must be writable and owned safely.

**Diagnose:**

```bash
id
sudo -n true
mountpoint /sys/fs/bpf
find /sys/fs/bpf -maxdepth 2 -print 2>/dev/null
```

**Fix:** use an authorized administrator session. If that is unavailable,
mark runtime validation blocked and continue source/build checks.

## Scheduler remains disabled

**Symptoms:** loader returns but `/sys/kernel/sched_ext/state` remains
`disabled`.

**Cause:** attach failed, the kernel rejected the struct_ops program, or the
loader did not reach the expected lifecycle state.

**Diagnose:**

```bash
cat /sys/kernel/sched_ext/state
sudo orchestra status
sudo dmesg --ctime | tail -200
```

**Fix:** treat the state check as authoritative. Preserve evidence, unload any
loader-owned partial state, and rebuild/diagnose. Do not claim activation from
the loader return code alone.

## ABI or policy mismatch

**Symptoms:** policy publication is rejected; bridge status says schema or
map incompatibility.

**Cause:** bridge v2, kernel v8, control v10, or product bundle fields do not
match, or the policy exceeds the bounded bank.

**Diagnose:**

```bash
./scripts/policy_load.py --bridge "$ORCHESTRA_BUILD_DIR/orchestra_bridge" \
  --dry-run config/examples/safe.json
grep -R "ORCHESTRA_.*ABI\|SCHEMA_VERSION" kernel/sched_ext/include
```

**Fix:** use the matching bridge/object pair and validate the policy schema.
Never reinterpret older fields silently.

## Stale prediction or unsupported action

**Symptoms:** telemetry shows prediction fallback, capability adjustment, or
`RUN` fallback.

**Cause:** expiry/confidence/horizon failure, unsupported target semantics,
CPU affinity/online failure, or controller safety state.

**Diagnose:** inspect generation, expiry, confidence, action capability mask,
fallback reason, and controller state in `orchestra telemetry`.

**Fix:** use observed-state/conservative configuration, correct the target or
policy, and repeat with bounded load. This behavior is a safety result, not a
failure to hide.

## Controller saturation, rollback, or missing telemetry

**Symptoms:** state is `SATURATED`, `ROLLBACK`, or `RECOVERY`; Q is absent;
map status is unavailable.

**Cause:** persistent deficit, actuator bound, invalid window, map access
failure, or no attached scheduler.

**Diagnose:**

```bash
sudo orchestra controller status
sudo orchestra telemetry
sudo dmesg --ctime | tail -100
```

**Fix:** stop policy changes, preserve the window/controller counters, then
disable. A missing Q must be reported as unavailable; do not substitute a
hand-calculated or CFS value.

## Unload failure

**Symptoms:** `disable` does not reach `disabled` or pins remain.

**Cause:** active tasks, kernel health condition, another owner, or a loader
cleanup error.

**Diagnose:**

```bash
cat /sys/kernel/sched_ext/state
sudo bpftool link list
find /sys/fs/bpf/orchestra -maxdepth 1 -print 2>/dev/null
sudo dmesg --ctime | tail -200
```

**Fix:** preserve evidence and do not remove all bpffs contents. Only the
loader-owned path may be cleaned after state is safe. Escalate a repeatable
unload failure as a high-severity runtime finding.
