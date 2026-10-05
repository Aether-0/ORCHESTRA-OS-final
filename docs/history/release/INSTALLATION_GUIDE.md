# Installation Guide

## Prerequisites
```bash
sudo dnf install -y git gcc gcc-c++ clang llvm make cmake \
  libbpf libbpf-devel elfutils-libelf-devel bpftool bc bison flex \
  dwarves openssl-devel kernel-devel python3
```

## 1. Clone
```bash
git clone https://github.com/Aether-0/ORCHESTRA-OS.git
cd ORCHESTRA-OS
```

## 2. Build Userspace
```bash
make clean && make && make test
```

## 3. Build Kernel (skip if already on sched_ext kernel)
```bash
export KSRC="$HOME/src/linux-v6.12.96"
export KBUILD="$HOME/build/linux-v6.12.96-stage8"
git clone --branch v6.12.96 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git "$KSRC"
make -C "$KSRC" O="$KBUILD" x86_64_defconfig
# Enable: SCHED_CLASS_EXT, DEBUG_INFO_BTF, XFS_FS, E1000, SATA_AHCI
# Disable: MODULE_SIG
make -C "$KSRC" O="$KBUILD" -j4
sudo cp "$KBUILD/arch/x86/boot/bzImage" /boot/vmlinuz-6.12.96-orch
sudo dracut --force /boot/initramfs-6.12.96-orch.img 6.12.96
sudo grubby --add-kernel=/boot/vmlinuz-6.12.96-orch --initrd=/boot/initramfs-6.12.96-orch.img --title="ORCHESTRA 6.12.96" --make-default
sudo reboot
```

## 4. Build BPF Scheduler
```bash
cd kernel/sched_ext
# Generate vmlinux.h
sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > include/vmlinux.h
# Build
clang -O2 -target bpf -g -nostdinc -D__BPF__ \
  -I include -I $KSRC/tools/lib -I $KSRC/tools/bpf/bpftool/libbpf \
  -I $KSRC/include -I $KSRC/include/uapi \
  -I $KSRC/arch/x86/include -I $KSRC/arch/x86/include/generated \
  -I $KSRC/tools/sched_ext/include -I /usr/include/bpf \
  -Wno-missing-declarations -Wno-visibility -Wno-address-of-packed-member \
  -c orchestra_scx_stage7.bpf.c -o orchestra_scx_stage7.bpf.o
cc -O2 -Wall -Wextra -I include bridge/orchestra_bridge.c -o bridge/orchestra_bridge -lbpf
```

## 5. Load Scheduler
```bash
sudo bpftool struct_ops register orchestra_scx_stage7.bpf.o /sys/fs/bpf/orch
cat /sys/kernel/sched_ext/state  # Should show "enabled"
```

## 6. Remove Scheduler
```bash
sudo rm -rf /sys/fs/bpf/*
sudo bpftool link detach id $(sudo bpftool link list 2>&1 | grep struct_ops | awk -F: '{print $1}' | tail -1)
```

## Recovery
If the scheduler causes issues, boot the fallback kernel:
1. Reboot, press Esc at GRUB
2. Select the previous kernel
3. Remove the ORCHESTRA kernel entry
