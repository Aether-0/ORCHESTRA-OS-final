#!/bin/bash
# ORCHESTRA-OS Stage 6 — check kernel configuration for sched_ext support.
#
# Usage:
#   ./check_kernel_config.sh [KERNEL_SRC_DIR]
#
# If KERNEL_SRC_DIR is given, checks the .config in that directory.
# Otherwise, checks the running kernel via /proc/config.gz or /boot/config-$(uname -r).

set -euo pipefail

KSRC="${1:-}"

required_configs=(
    CONFIG_BPF=y
    CONFIG_BPF_SYSCALL=y
    CONFIG_BPF_JIT=y
    CONFIG_DEBUG_INFO_BTF=y
    CONFIG_SCHED_CLASS_EXT=y
    CONFIG_BPF_EVENTS=y
)

optional_configs=(
    CONFIG_KPROBES=y
    CONFIG_FTRACE=y
    CONFIG_DEBUG_FS=y
    CONFIG_BPF_KPROBE_OVERRIDE=y
)

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "INFO:  $*"; }
ok()   { echo "OK:    $*"; }

find_config() {
    if [ -n "$KSRC" ]; then
        [ -f "$KSRC/.config" ] || die "$KSRC/.config not found"
        cat "$KSRC/.config"
        return
    fi
    if [ -f /proc/config.gz ]; then
        zcat /proc/config.gz
    elif [ -f "/boot/config-$(uname -r)" ]; then
        cat "/boot/config-$(uname -r)"
    else
        die "cannot find kernel config; specify KERNEL_SRC_DIR"
    fi
}

CONFIG=$(find_config)

info "KERNEL_SRC=${KSRC:-running kernel}"
info "---"

failed=0
for req in "${required_configs[@]}"; do
    name="${req%%=*}"
    expected="${req#*=}"
    actual=$(echo "$CONFIG" | grep "^${name}=" || echo "${name}=notset")
    if [ "$actual" = "${name}=${expected}" ]; then
        ok "$req"
    else
        echo "MISSING: $name (required: $expected, got: $actual)" >&2
        failed=1
    fi
done

for opt in "${optional_configs[@]}"; do
    name="${opt%%=*}"
    expected="${opt#*=}"
    actual=$(echo "$CONFIG" | grep "^${name}=" || echo "${name}=notset")
    if [ "$actual" = "${name}=${expected}" ]; then
        ok "$opt"
    else
        info "  (optional) $opt → got $actual"
    fi
done

if [ "$failed" -ne 0 ]; then
    echo ""
    die "Missing $failed required kernel configuration(s). Enable the listed options and rebuild the kernel."
fi

info "All required kernel configs present."
