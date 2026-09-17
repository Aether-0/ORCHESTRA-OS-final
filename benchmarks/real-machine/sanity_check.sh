#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA Real-Machine Sanity Check ==="
echo ""

check() { if command -v "$1" &>/dev/null; then echo "  [OK] $1"; else echo "  [MISSING] $1 ($2)"; fi; }

echo "Kernel: $(uname -r) | CPUs: $(nproc) | RAM: $(free -h | awk '/Mem/{print $2}')"
echo ""

echo "Required tools:"
check gcc "dnf install gcc"
check clang "dnf install clang"
check make "dnf install make"
check bpftool "dnf install bpftool"
check python3 "dnf install python3"
echo ""

echo "Kernel capabilities:"
for opt in SCHED_CLASS_EXT DEBUG_INFO_BTF BPF_SYSCALL BPF_JIT; do
    if grep -q "CONFIG_${opt}=y" /boot/config-$(uname -r) 2>/dev/null; then
        echo "  [OK] CONFIG_$opt=y"
    else
        echo "  [MISSING] CONFIG_$opt"
        echo "    Boot the ORCHESTRA kernel or rebuild with $opt=y"
    fi
done
echo ""

if [ -d /sys/kernel/sched_ext ]; then
    echo "[OK] sched_ext: $(cat /sys/kernel/sched_ext/state)"
else
    echo "[BLOCKED] sched_ext not available"
fi

if [ -f /sys/kernel/btf/vmlinux ]; then
    echo "[OK] BTF: $(ls -lh /sys/kernel/btf/vmlinux | awk '{print $5}')"
    echo "  SHA-256: $(sha256sum /sys/kernel/btf/vmlinux | awk '{print $1}')"
else
    echo "[BLOCKED] BTF not available"
fi

echo ""
echo "Network: $(ip addr show | awk '
    /inet / && $2 !~ /^127[.]0[.]0[.]1\// && !found {
        print $2
        found = 1
    }
')"
echo "Disk:   $(df -h / | awk 'NR==2{print $4}') free"
