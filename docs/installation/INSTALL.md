# Installing ORCHESTRA-OS

This guide installs the current `1.1.1` research software. The installer does
not enable sched_ext and does not change boot configuration, sysctls, CPU
governors, or unrelated BPF state.

## 1. Native packages

See the [current download guide](../releases/v1.1.1.md) and the release's
PLATFORM_RESULTS.md for the actual distribution/architecture results.
Native compilation and package smoke tests cover x86_64 and ARM64; kernel
runtime compatibility remains a separate target-specific gate.


The recommended installation uses the distro-native package for the current
architecture from the GitHub Release. Packages are observer-only: they do not
compile, load, enable, or attach sched_ext, and their service is disabled by
default. Use the package manager for upgrades and removal; the bundled
`orchestra install` and `orchestra uninstall` commands refuse to overwrite a
package-managed installation.

For target-specific sched_ext experiments, install the optional kernel-build
prerequisites and run `sudo orchestra build --kernel` on the exact target host.
A BPF object from another kernel or distribution must not be copied into the
build directory.

## 2. Supported starting point

The observer tier runs on supported Linux systems with a C compiler, Make,
Python 3, and the repository userspace dependencies. Kernel mode requires a
Linux kernel exposing sched_ext, BTF, BPF, the target architecture support,
and the privileges needed for BPF load/attach. x86_64 is the validated build
architecture in the current evidence set; arm64 is a target-specific build
path requiring its own kernel/API verification. Other architectures remain
observer-only until separately certified.

The kernel source must match `uname -r` exactly. One BPF object must not be
copied between unrelated kernels.

## 2.1 Install build dependencies

The project never installs packages automatically. Choose the command for the
distribution and review it before running it. The portable observer build
needs the C toolchain and Python; kernel artifacts additionally need clang,
bpftool, libbpf development headers/runtime, libelf, zlib, zstd, pkg-config,
and kernel/sched_ext headers from the exact target source export.

Debian, Ubuntu, and Kali example:

```bash
sudo apt-get update
sudo apt-get install build-essential clang llvm bpftool libbpf-dev \
  libelf-dev zlib1g-dev libzstd-dev pkg-config python3 dwarves
```

Fedora example:

```bash
sudo dnf install gcc gcc-c++ make clang llvm bpftool libbpf-devel \
  elfutils-libelf-devel zlib-devel libzstd-devel pkgconf-pkg-config \
  python3 dwarves
```

If the distribution's libbpf is older or its kernel headers omit sched_ext
inputs, do not mix headers from a different kernel. Use the compatibility
report and remain in observer mode until an exact target source export is
available.

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

The default command is intentionally observer-only. To install kernel-mode
artifacts, opt in explicitly:

```bash
sudo ./scripts/install.sh --with-kernel --build-dir "$ORCHESTRA_BUILD_DIR"
```

`--with-kernel` requires `orchestra_bridge`, `orchestra_loader`, the
target-matched BPF object, and `build-manifest.txt`. It rejects symlinked or
group/world-writable artifacts and unsafe installation paths, and checks the recorded SHA-256 values before
copying them into the root-owned installation. This is an integrity check,
not a signature or proof of publisher authenticity; signed release artifacts
remain a production gate.

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

## 6. Run or enable only after the runtime gate

On a dedicated test system with authorized root access and an artifact built
for the running kernel:

```bash
sudo /usr/local/bin/orchestra check-system --strict
sudo /usr/local/bin/orchestra run --interval 5
```

`run` is the recommended first kernel-mode workflow: it attaches in the
foreground, prints ownership and telemetry status, and detaches the instance
it attached when interrupted with Ctrl-C. The loader and CLI refuse to
operate on a foreign sched_ext owner. If the scheduler is already owned by
ORCHESTRA, `run` observes it and leaves that existing owner active when it
exits.

For a detached/manual lifecycle:

```bash
sudo /usr/local/bin/orchestra enable
cat /sys/kernel/sched_ext/state
sudo /usr/local/bin/orchestra status
```

Expected state is `enabled`. The loader performs exact map schema negotiation,
scoped pinning, and struct_ops attach. A successful loader return alone is
insufficient; verify the sysfs state and scheduler ownership telemetry.

## 7. Optional systemd service

The `--with-kernel` install places an opt-in unit at
`/usr/local/lib/systemd/system/orchestra.service`; it is not enabled by the
installer. On a dedicated authorized test machine, start it explicitly:

```bash
sudo systemctl enable --now orchestra.service
sudo systemctl status orchestra.service --no-pager
sudo journalctl -u orchestra.service -n 100 --no-pager
```

Stop it through systemd, which invokes the ownership-checked disable path:

```bash
sudo systemctl disable --now orchestra.service
```

## 8. First workload and telemetry

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

## 9. Disable and uninstall

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

Uninstall is loader-scoped and never removes unrelated bpffs pins. It also
preserves a systemd unit if it no longer matches the ORCHESTRA-managed unit,
so a locally modified service file is not silently deleted. When the managed
service is active, the uninstall script first asks systemd to stop and disable
that exact unit; a failure stops removal.
