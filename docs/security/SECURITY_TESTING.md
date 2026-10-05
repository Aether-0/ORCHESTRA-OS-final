# ORCHESTRA-OS Security Testing Guide

This document is a reproducible, non-destructive security validation
procedure. Commands are run from the repository root. Privileged scheduler
commands are intentionally separated from the non-privileged gate and must be
run only on a dedicated recovery-capable test host.

## 1. Record the environment

    date -Iseconds
    hostnamectl
    uname -a
    uname -r
    cat /etc/os-release
    git rev-parse HEAD
    git status --short

Before manipulating sched_ext, record unrelated BPF state and do not use
broad cleanup:

    sudo bpftool prog list
    sudo bpftool map list
    sudo bpftool link list
    find /sys/fs/bpf -maxdepth 2 -print 2>/dev/null
    cat /sys/kernel/sched_ext/state 2>/dev/null || true

If root/recovery/dedicated-host conditions are not available, perform the
non-privileged tests below and mark kernel tests **BLOCKED**.

## 2. Build and strict repository gate

    make clean
    make
    make check
    make test
    git diff --check

make test includes:

- GCC and Clang strict-warning userspace builds;
- ASan/UBSan unit tests;
- generation-publication thread stress;
- bridge and ABI tests;
- coordination/controller tests;
- integration signal publication and CSV validator tests;
- policy lifecycle tests;
- security regression tests;
- source invariants, docs links, and secret scan.

Run the security subset directly when iterating:

    bash tests/security/run.sh
    python3 tests/security/test_policy_loader.py
    python3 tests/security/fuzz_policy_loader.py --iterations 1000
    bash tests/security/test_install_paths.sh
    python3 tests/unit/test_orchestra_scx_source.py

The policy mutation target must report both accepted and rejected populations;
an all-rejected run is not evidence of a useful property test.

The same security runner also compiles and executes
`tests/security/fuzz_abi_state.c` under AddressSanitizer and UndefinedBehaviorSanitizer.
Its default campaign performs 50,000 deterministic mutations across bridge ABI,
signal, directive, policy metadata, controller, and option-parser state. The
target accepts an explicit bounded iteration count up to 200,000 when a longer
local campaign is appropriate:

    bash tests/security/run.sh
    # The runner's final line includes: PASS ABI/state mutation target iterations=50000

## 3. Strict source and sanitizer checks

Bridge syntax:

    gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
      -Wformat=2 -Werror -Ikernel/sched_ext/include \
      -fsyntax-only kernel/sched_ext/bridge/orchestra_bridge.c

Research workload syntax:

    gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
      -Wformat=2 -Werror -Ikernel/sched_ext/include \
      -fsyntax-only research/experiments/process-group-prototype/orchestra_real_cpu.c

The canonical sanitizer compiler matrix is already invoked by
tests/unit/run.sh. Do not disable sanitizers or weaken -Werror when
investigating a failure.

## 4. Input and policy adversarial tests

The policy tests cover:

- full 256-entry boundary;
- duplicate state indices;
- out-of-range indices;
- invalid action/controller/mode types;
- booleans where integers are required;
- duplicate JSON object keys;
- malformed/truncated UTF-8/JSON;
- deeply nested JSON recursion;
- symlink policy paths;
- writable bridge parent paths;
- oversized files;
- 64-bit boundary values;
- bounded bridge command and total publication waits.

For an expanded deterministic campaign:

    python3 tests/security/fuzz_policy_loader.py --iterations 100000

For a manually reviewed dry run, use a trusted bridge path and a policy file
that is not writable by another principal:

    ./scripts/policy_load.py \
      --bridge /var/tmp/orchestra-os-build-$(id -u)/orchestra_bridge \
      --dry-run config/examples/adaptive.json

Publishing requires root and an active, schema-compatible scheduler:

    sudo /usr/local/bin/orchestra policy load /etc/orchestra-os/adaptive.json

Never use a test policy to override RT/deadline tasks. The bridge independently
checks SCHED_FIFO, SCHED_RR, and SCHED_DEADLINE admission.

## 5. Filesystem and lifecycle tests

The installer regression uses a disposable temporary prefix and checks:

- safe marker creation;
- configuration preservation;
- refusal to remove operator-created files;
- safe completed uninstall;
- refusal to follow a command symlink.

Run it with:

    bash tests/security/test_install_paths.sh

For an authorized package rehearsal, use an explicit disposable prefix:

    WORK=$(mktemp -d /var/tmp/orchestra-security-rehearsal.XXXXXX)
    trap 'rm -rf -- "$WORK"' EXIT
    bash scripts/install.sh --prefix "$WORK/prefix" \
      --config-dir "$WORK/config" --build-dir "$WORK/build" --no-build
    bash scripts/uninstall.sh --prefix "$WORK/prefix" \
      --config-dir "$WORK/config"

Do not run --remove-config against an operator configuration directory
unless its exact path and recovery need are approved.

## 6. Repository secret/path scan

    scripts/security-scan.sh

The scan rejects common credentials, private keys, tracked runtime objects,
and machine-specific absolute home-directory paths in active
source/configuration. Machine
paths in historical artifacts/ evidence are intentionally archival and are
excluded from that one path-pattern check; they are not treated as
credentials and must not be copied into release packages.

## 7. Optional static analysis

Run available analyzers without allowing them to rewrite files:

    if command -v semgrep >/dev/null 2>&1; then
      semgrep --config auto --error \
        --exclude artifacts --exclude release --exclude '*.bpf.o' \
        kernel/sched_ext/bridge scripts tests/security
    fi

clang-tidy, scan-build, cppcheck, Valgrind, and external dependency/SCA tools
are optional. Missing tools are a documented coverage limitation, not a PASS.

## 8. Target-matched BPF build and verifier gate

First run the non-mutating capability check:

    bash scripts/check-system.sh --strict

Then use the exact running kernel source/BTF/UAPI. The build must not reuse a
BPF object from another kernel:

    ORCHESTRA_KERNEL_SRC=/lib/modules/"$(uname -r)"/build \
      ORCHESTRA_BUILD_DIR=/var/tmp/orchestra-os-build-$(id -u) \
      ./scripts/build.sh --kernel

Capture the build manifest, compiler version, kernel source identity, BTF
hash, object hash, and stderr. If exact kernel headers, sched_ext headers,
libbpf, clang BPF target support, or BTF are missing, record BLOCKED with the
first dependency failure.

A successful object build is not verifier evidence. The verifier gate requires
the target kernel to accept the object, followed by schema and map inspection.

## 9. Privileged attach/ownership/actions gate

Only on an approved dedicated host:

    sudo /usr/local/bin/orchestra check-system --strict
    sudo /usr/local/bin/orchestra enable
    cat /sys/kernel/sched_ext/state
    cat /sys/kernel/sched_ext/root/ops
    sudo /usr/local/bin/orchestra status

Prove ownership for each workload before collecting performance results. For
each action, retain requested, accepted, dispatched, effective, and fallback
telemetry separately:

    sudo /usr/local/bin/orchestra telemetry
    sudo /usr/local/bin/orchestra controller status

Test RUN, YIELD, MIGRATE, THROTTLE, and SLEEP/DEFER with the existing bridge
interface. Include invalid PID/CPU, stale generation, expired directive,
policy failure, controller saturation, task exit race, and CPU-affinity or
hotplug fault cases. Do not create a new fault injector by writing into
kernel memory.

RT coexistence must use standard Linux tools and verify that protected tasks
never enter the adaptive path. A request counter is not proof of an effective
action.

## 10. Teardown and recovery gate

    sudo /usr/local/bin/orchestra disable
    cat /sys/kernel/sched_ext/state
    sudo bpftool link list
    find /sys/fs/bpf/orchestra -maxdepth 1 -print 2>/dev/null || true

If the loader times out, preserve the ORCHESTRA pins and kernel logs. Do not
delete all bpffs contents. Record whether the machine returned to conventional
scheduling and whether a reboot was needed.

## 11. Results discipline

Record command, timestamp, return code, stdout, stderr, kernel state, and
ORCHESTRA state for every test. Use only these result labels:

- PASS — the exact test executed and its acceptance condition passed;
- FAIL — the exact test executed and its condition failed;
- BLOCKED — a prerequisite/authorization/environment condition prevented
  execution;
- NOT APPLICABLE — the feature is outside the implementation scope.

Never turn a source invariant into a runtime PASS, a userspace HMAC test into
a kernel-authentication claim, or a scheduler load into an ownership claim.
