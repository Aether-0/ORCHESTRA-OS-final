#!/usr/bin/env bash
set -euo pipefail
json=0
strict=0
for argument in "$@"; do
    case "$argument" in
        --json) json=1 ;;
        --strict) strict=1 ;;
        --help|-h) echo "usage: $0 [--json] [--strict]"; exit 0 ;;
        *) echo "unknown option: $argument" >&2; exit 2 ;;
    esac
done

kernel=$(uname -r)
arch=$(uname -m)
cpu_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)
kernel_status=PASS
observer_status=PASS
warning_count=0
failure_count=0
declare -a results=()

record() {
    local name=$1 state=$2 detail=$3
    results+=("$name|$state|$detail")
    case "$state" in
        WARNING) warning_count=$((warning_count + 1)) ;;
        FAIL) failure_count=$((failure_count + 1)); kernel_status=FAIL ;;
    esac
}
have_command() { command -v "$1" >/dev/null 2>&1; }

config_value() {
    local key=$1 file value
    for file in "/boot/config-$kernel" "/usr/lib/modules/$kernel/config"; do
        if [ -r "$file" ]; then
            value=$(awk -v key="$key" '
                index($0, key "=") == 1 && !found {
                    print substr($0, length(key) + 2)
                    found = 1
                }
            ' "$file")
            [ -n "$value" ] && { echo "$value"; return; }
        fi
    done
    if [ -r /proc/config.gz ] && have_command zcat; then
        value=$(zcat /proc/config.gz 2>/dev/null | awk -v key="$key" '
            index($0, key "=") == 1 && !found {
                print substr($0, length(key) + 2)
                found = 1
            }
        ' || true)
        [ -n "$value" ] && { echo "$value"; return; }
    fi
    echo unknown
}
check_tool() {
    if have_command "$1"; then
        record "tool:$1" PASS "$(command -v "$1")"
    else
        record "tool:$1" WARNING "not installed; observer mode remains available"
        kernel_status=WARNING
    fi
}

if [ "$(uname -s)" = Linux ]; then record os PASS Linux; else record os FAIL "$(uname -s)"; observer_status=FAIL; fi
case "$arch" in
    x86_64|aarch64) record architecture PASS "$arch; target-matched build family" ;;
    *) record architecture WARNING "$arch; Tier 0 observer only until validated" ;;
esac
if [ -r /sys/kernel/sched_ext/state ]; then
    record sched_ext PASS "state=$(tr -d '\n' </sys/kernel/sched_ext/state)"
else
    record sched_ext WARNING "sched_ext state interface is unavailable"
    kernel_status=WARNING
fi
if [ -r /sys/kernel/btf/vmlinux ]; then record btf PASS /sys/kernel/btf/vmlinux; else record btf WARNING "kernel BTF unavailable"; kernel_status=WARNING; fi
for key in CONFIG_SCHED_CLASS_EXT CONFIG_DEBUG_INFO_BTF CONFIG_BPF_SYSCALL CONFIG_BPF_JIT; do
    value=$(config_value "$key")
    case "$value" in
        y|m) record "config:$key" PASS "$value" ;;
        unknown) record "config:$key" WARNING "kernel config could not be read" ;;
        *) record "config:$key" FAIL "$value" ;;
    esac
done
for tool in cc clang make python3 bpftool; do check_tool "$tool"; done
if printf '#include <bpf/libbpf.h>\n' | cc -E - >/dev/null 2>&1; then
    record libbpf-header PASS "compiler can include bpf/libbpf.h"
else
    record libbpf-header WARNING "libbpf development headers unavailable"
    kernel_status=WARNING
fi
if have_command pkg-config && pkg-config --exists libbpf 2>/dev/null; then
    record libbpf-library PASS "pkg-config libbpf"
else
    record libbpf-library WARNING "libbpf pkg-config metadata unavailable"
    kernel_status=WARNING
fi
for library in libelf libzstd; do
    if have_command pkg-config && pkg-config --exists "$library" 2>/dev/null; then
        record "${library}-library" PASS "pkg-config $library"
    else
        record "${library}-library" WARNING "development metadata unavailable"
        kernel_status=WARNING
    fi
done
if [ "$cpu_count" -ge 1 ] 2>/dev/null; then record cpu-count PASS "$cpu_count online CPUs"; else record cpu-count FAIL "unable to determine online CPUs"; observer_status=FAIL; fi
if [ "$(id -u)" -eq 0 ]; then record privilege PASS "effective uid 0"; else record privilege WARNING "run lifecycle commands as root"; kernel_status=WARNING; fi
if mountpoint -q /sys/fs/bpf 2>/dev/null; then record bpffs PASS /sys/fs/bpf; else record bpffs WARNING "bpffs is not mounted"; kernel_status=WARNING; fi
if [ -f /sys/fs/cgroup/cgroup.controllers ]; then record cgroup-v2 PASS /sys/fs/cgroup; else record cgroup-v2 WARNING "cgroup v2 not detected"; fi

if [ "$json" -eq 1 ]; then
    printf '{"kernel":"%s","architecture":"%s","online_cpus":%s,"observer":"%s","kernel_activation":"%s","warnings":%s,"failures":%s,"checks":[' "$kernel" "$arch" "$cpu_count" "$observer_status" "$kernel_status" "$warning_count" "$failure_count"
    first=1
    for result in "${results[@]}"; do
        IFS='|' read -r name state detail <<< "$result"
        detail=${detail//\\/\\\\}
        detail=${detail//\"/\\\"}
        [ "$first" -eq 1 ] || printf ','
        first=0
        printf '{"name":"%s","status":"%s","detail":"%s"}' "$name" "$state" "$detail"
    done
    printf ']}\n'
else
    echo "ORCHESTRA-OS capability report"
    echo "kernel=$kernel architecture=$arch online_cpus=$cpu_count"
    for result in "${results[@]}"; do
        IFS='|' read -r name state detail <<< "$result"
        printf '%-24s %-7s %s\n' "$name" "$state" "$detail"
    done
    echo "observer_tier=$observer_status"
    echo "kernel_activation=$kernel_status"
    if [ "$kernel_status" = PASS ]; then echo "capability_tier=Tier 1 candidate"; else echo "capability_tier=Tier 0 observer"; fi
fi
if [ "$strict" -eq 1 ] && [ "$kernel_status" != PASS ]; then exit 1; fi
if [ "$observer_status" = FAIL ]; then exit 1; fi
