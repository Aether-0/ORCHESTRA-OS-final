# Installing ORCHESTRA-OS

This guide installs the current `1.0.0-rc1` product tools. The installer does
not enable sched_ext and does not change boot configuration, sysctls, CPU
governors, or unrelated BPF state.

## 1. Supported starting point

The observer tier runs on supported Linux systems with a C compiler, Make,
Python 3, and the repository userspace dependencies. Kernel mode requires a
Linux kernel exposing sched_ext, BTF, BPF, the target architecture support,
and the privileges needed for BPF load/attach. x86_64 is the validated build
architecture in the current evidence set; arm64 is a target-specific build
path requiring its own kernel/API verification. Other architectures remain
observer-only until separately certified.

The kernel source must match `uname -r` exactly. One BPF object must not be
copied between unrelated kernels.

## 2. Check the host

```bash
git clone <repository-url> ORCHESTRA-OS
cd ORCHESTRA-OS
./scripts/check-system.sh
```

The report prints `PASS`, `WARNING`, or `FAIL` per capability and identifies
the observer and kernel tiers. Use JSON for automation:

```bash
./scripts/check-system.sh --json | python3 -m json.tool
```

Before activation, require the strict gate:

```bash
./scripts/check-system.sh --strict
```

If this fails, use observer mode and do not attempt a loader attach.

## 3. Build the portable product

```bash
make clean
make
make check
make test
./scripts/build.sh --userspace --bridge
```

Build outputs go outside the repository by default, at
`/var/tmp/orchestra-os-build-$(id -u)`. Set `ORCHESTRA_BUILD_DIR` to an
explicit external directory when collecting evidence.

## 4. Build a target-matched sched_ext artifact

Only continue if the strict host check passes and the exact kernel source
tree contains the required sched_ext/libbpf inputs:

```bash
export ORCHESTRA_KERNEL_SRC="/lib/modules/$(uname -r)/build"
export ORCHESTRA_BUILD_DIR="/var/tmp/orchestra-os-build-$(id -u)"
./scripts/build.sh --kernel
cat "$ORCHESTRA_BUILD_DIR/build-manifest.txt"
```

The build generates `vmlinux.h` from the running kernel's BTF in the external
build directory. It records source/UAPI/helper/BTF/object/bridge/loader
hashes. A mismatch or missing input is a blocked compatibility result, not a
reason to substitute headers from another kernel.

## 5. Install tools and configuration

The normal system install is privileged because it writes `/usr/local` and
`/etc/orchestra-os`:

```bash
sudo ./scripts/install.sh --build-dir "$ORCHESTRA_BUILD_DIR"
```

The installer copies the bridge, loader, BPF object when present, CLI,
configuration examples, and documentation. Existing files under
`/etc/orchestra-os` are preserved. The scheduler remains disabled.

For a reversible non-system rehearsal:

```bash
TEST_PREFIX="/var/tmp/orchestra-os-install-$$"
./scripts/install.sh --prefix "$TEST_PREFIX" \
  --config-dir "/var/tmp/orchestra-os-config-$$" \
  --build-dir "$ORCHESTRA_BUILD_DIR"
```

The current installer intentionally exposes configuration through the
`ORCHESTRA_CONFIG_DIR` environment variable; if a custom directory is used,
set it for both install and uninstall.

## 6. Enable only after the runtime gate

On a dedicated test system with authorized root access and an artifact built
for the running kernel:

```bash
sudo /usr/local/bin/orchestra check-system --strict
sudo /usr/local/bin/orchestra enable
cat /sys/kernel/sched_ext/state
sudo /usr/local/bin/orchestra status
```

Expected state is `enabled`. The loader performs exact map schema negotiation,
scoped pinning, and struct_ops attach. A successful loader return alone is
insufficient; verify the sysfs state and scheduler ownership telemetry.

## 7. First workload and telemetry

Start with observer evidence:

```bash
./examples/basic/run.sh
./examples/cpu_pressure/run.sh 10
```

With kernel mode active, inspect the actual maps and ownership before using
performance data:

```bash
sudo /usr/local/bin/orchestra telemetry
sudo /usr/local/bin/orchestra controller status
```

## 8. Disable and uninstall

Return to conventional scheduling first:

```bash
sudo /usr/local/bin/orchestra disable
cat /sys/kernel/sched_ext/state
```

Remove only ORCHESTRA-installed files, preserving configuration for review:

```bash
sudo /usr/local/bin/orchestra uninstall --keep-config
```

To remove the installed example configuration as well, use the standalone
script with an explicitly reviewed configuration path:

```bash
sudo ORCHESTRA_CONFIG_DIR=/etc/orchestra-os \
  ./scripts/uninstall.sh
```

Uninstall is loader-scoped and never removes unrelated bpffs pins.
