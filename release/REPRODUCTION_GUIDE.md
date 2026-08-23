# Reproduction Guide

## Environment

- **Host OS:** Debian/Kali Linux
- **Guest OS:** Fedora 40 (VirtualBox)
- **Kernel:** Linux v6.12.96 (exact build)
- **GCC:** 14.2.1
- **Clang:** 18.1.8
- **bpftool:** 7.5.0

## Userspace (any Linux)

```bash
cd ORCHESTRA-OS
make clean && make && make test
# Expected: 25/25 unit, 34/34 validator, 5/5 integration
```

## Kernel Build (requires 64GB disk, 8GB RAM)

```bash
export KSRC="$HOME/src/linux-v6.12.96"
export KBUILD="$HOME/build/linux-v6.12.96-stage8"
git clone --branch v6.12.96 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git "$KSRC"

make -C "$KSRC" O="$KBUILD" x86_64_defconfig
# Enable: SCHED_CLASS_EXT, DEBUG_INFO_BTF, XFS_FS, E1000, SATA_AHCI
make -C "$KSRC" O="$KBUILD" -j4
# bzImage at $KBUILD/arch/x86/boot/bzImage
```

## ORCHESTRA BPF

```bash
cd kernel/sched_ext
clang -O2 -target bpf -g -nostdinc -D__BPF__ \
  -I include -I $KSRC/tools/lib -I $KSRC/tools/bpf/bpftool/libbpf \
  -I $KSRC/include -I $KSRC/include/uapi \
  -I $KSRC/arch/x86/include -I $KSRC/arch/x86/include/generated \
  -I $KSRC/tools/sched_ext/include -I /usr/include/bpf \
  -Wno-missing-declarations -Wno-visibility -Wno-address-of-packed-member \
  -c orchestra_scx_stage7.bpf.c -o orchestra_scx_stage7.bpf.o

# Build bridge
cc -O2 -Wall -Wextra -I include bridge/orchestra_bridge.c -o bridge/orchestra_bridge -lbpf
```

## Load and Test (historical package only)

This archived procedure is superseded by the root `README.md` and
`docs/installation/INSTALL.md`. Use the loader so timer/map prerequisites,
ownership, and teardown are validated as one transaction.

```bash
sudo ./kernel/sched_ext/bridge/orchestra_loader --load \
  /var/tmp/orchestra-os-build-$(id -u)/orchestra_scx_stage7.bpf.o
cat /sys/kernel/sched_ext/state  # Should show "enabled"

# Pin maps, publish directive
sudo ./bridge/orchestra_bridge --publish --action RUN --target-pid 1

# Unload only the ORCHESTRA-owned scheduler and its validated pins
sudo ./kernel/sched_ext/bridge/orchestra_loader --unload
```

Never run a broad `/sys/fs/bpf` deletion or detach an arbitrary link ID.

## Expected Results

| Test | Expected |
|------|----------|
| make test | 25/25 unit, 34/34 validator, 5/5 integration |
| sched_ext state | transitions: disabled→enabled→disabled |
| Bridge status | magic=0x4f524342 |
| Invalid PID | exit code 6 |
| 5-min stability | state=enabled throughout |
| DSQ errors | 0 |
| Panics | 0 |
