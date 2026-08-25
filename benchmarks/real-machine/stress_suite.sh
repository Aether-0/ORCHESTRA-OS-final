#!/usr/bin/env bash
set -euo pipefail

# ORCHESTRA-OS real-machine stress suite.
# Usage: bash stress_suite.sh [duration_seconds] [mode: cfs|orchestra]
#
# The ORCHESTRA path uses the existing bridge and proves exact-TID ownership
# for each CPU, memory, I/O, and mixed worker before measuring its phase. The
# stress(1) memory helper's child identities are discovered through the
# process tree and admitted individually; failure to discover or admit every
# requested child remains explicitly un-attributed.

DURATION=${1:-60}
MODE=${2:-cfs}
OUTDIR=${ORCHESTRA_STRESS_OUTDIR:-"/tmp/orchestra-stress-$(date +%Y%m%d-%H%M%S)"}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
# shellcheck source=/dev/null
. "$REPO_ROOT/scripts/path_safety.sh"
orchestra_ensure_private_dir "$OUTDIR" || {
    echo "refusing unsafe stress output directory: $OUTDIR" >&2
    exit 2
}
WORKDIR="$OUTDIR/.work-$$"
if [ -e "$WORKDIR" ]; then
    echo "BLOCKED_WORKDIR_COLLISION: $WORKDIR" >&2
    exit 2
fi
orchestra_ensure_private_dir "$WORKDIR" || {
    echo "refusing unsafe stress work directory: $WORKDIR" >&2
    exit 2
}
OWNERSHIP_POLLS=${ORCHESTRA_OWNERSHIP_POLLS:-200}
PHASE_TIMEOUT=${ORCHESTRA_PHASE_TIMEOUT:-}

log()  { echo "$(date +%H:%M:%S) $*" | tee -a "$OUTDIR/stress.log"; }
fail() { log "FAIL: $*"; echo "FAIL:$*" >> "$OUTDIR/failures.txt"; }

PIDS=()
STRESS_PID=
THERMAL_MONITOR_PID=
MEM_CHILD_PIDS=()

thermal_snapshot() {
    local output=$1 zone temp type trip trip_type

    {
        echo "timestamp=$(date -Iseconds)"
        for zone in /sys/class/thermal/thermal_zone*; do
            [ -r "$zone/temp" ] || continue
            type=$(cat "$zone/type" 2>/dev/null || echo unknown)
            temp=$(cat "$zone/temp" 2>/dev/null || echo unavailable)
            printf 'zone=%s type=%s temp_mC=%s' "$(basename "$zone")" \
                "$type" "$temp"
            for trip in "$zone"/trip_point_*_temp; do
                [ -r "$trip" ] || continue
                trip_type=$(cat "${trip%_temp}_type" 2>/dev/null || echo unknown)
                printf ' %s_mC=%s_%s' "$(basename "${trip%_temp}")" \
                    "$(cat "$trip" 2>/dev/null || echo unavailable)" "$trip_type"
            done
            echo
        done
    } >"$output"
}

thermal_monitor() {
    local parent_pid=$1
    local samples="$OUTDIR/thermal_samples.csv"
    local event="$OUTDIR/thermal_event.txt"
    local zone temp trip trip_type critical timestamp

    echo "timestamp,zone,type,temp_mC,critical_mC" >"$samples"
    while :; do
        timestamp=$(date -Iseconds)
        for zone in /sys/class/thermal/thermal_zone*; do
            [ -r "$zone/temp" ] || continue
            temp=$(cat "$zone/temp" 2>/dev/null || echo unavailable)
            case "$temp" in
                ''|*[!0-9-]*) continue ;;
            esac
            critical=0
            for trip in "$zone"/trip_point_*_temp; do
                [ -r "$trip" ] || continue
                trip_type=$(cat "${trip%_temp}_type" 2>/dev/null || echo unknown)
                if [ "$trip_type" = critical ]; then
                    trip=$(cat "$trip" 2>/dev/null || echo 0)
                    case "$trip" in
                        ''|*[!0-9-]*) continue ;;
                    esac
                    if [ "$trip" -gt "$critical" ]; then
                        critical=$trip
                    fi
                fi
            done
            printf '%s,%s,%s,%s,%s\n' "$timestamp" "$(basename "$zone")" \
                "$(cat "$zone/type" 2>/dev/null || echo unknown)" "$temp" "$critical" \
                >>"$samples"
            if [ "$critical" -gt 0 ] && [ "$temp" -ge "$critical" ]; then
                printf 'timestamp=%s zone=%s temp_mC=%s critical_mC=%s\n' \
                    "$timestamp" "$(basename "$zone")" "$temp" "$critical" >"$event"
                kill -TERM "$parent_pid" 2>/dev/null || true
                return 0
            fi
        done
        sleep 1
    done
}

cleanup() {
    local pid
    if [ -n "$THERMAL_MONITOR_PID" ]; then
        kill "$THERMAL_MONITOR_PID" 2>/dev/null || true
        wait "$THERMAL_MONITOR_PID" 2>/dev/null || true
        THERMAL_MONITOR_PID=
    fi
    if [ -n "$STRESS_PID" ]; then
        kill "$STRESS_PID" 2>/dev/null || true
        wait "$STRESS_PID" 2>/dev/null || true
        STRESS_PID=
    fi
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    rm -rf -- "$WORKDIR"
    thermal_snapshot "$OUTDIR/thermal_after.txt" 2>/dev/null || true
    df -P "$OUTDIR" >"$OUTDIR/storage_after.txt" 2>&1 || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

case "$DURATION" in
    ''|*[!0-9]*) log "BLOCKED_INVALID_DURATION: $DURATION"; exit 2 ;;
esac
if [ "$DURATION" -lt 1 ]; then
    log "BLOCKED_INVALID_DURATION: duration must be at least one second"
    exit 2
fi
if [ -z "$PHASE_TIMEOUT" ]; then
    PHASE_TIMEOUT=$((DURATION + 30))
fi
case "$PHASE_TIMEOUT" in
    ''|*[!0-9]*) log "BLOCKED_INVALID_PHASE_TIMEOUT: $PHASE_TIMEOUT"; exit 2 ;;
esac
if [ "$PHASE_TIMEOUT" -lt 1 ]; then
    log "BLOCKED_INVALID_PHASE_TIMEOUT: must be positive"
    exit 2
fi
case "$MODE" in
    cfs|orchestra) ;;
    *) log "BLOCKED_INVALID_MODE: $MODE"; exit 2 ;;
esac

expand_cpu_list() {
    local list=$1 part start end cpu
    local -a parts=()

    IFS=',' read -r -a parts <<<"$list"
    for part in "${parts[@]}"; do
        if [[ "$part" == *-* ]]; then
            start=${part%-*}
            end=${part#*-}
            for ((cpu = start; cpu <= end; cpu++)); do
                echo "$cpu"
            done
        elif [[ "$part" =~ ^[0-9]+$ ]]; then
            echo "$part"
        fi
    done
}

CPU_LIST=$(awk '$1 == "Cpus_allowed_list:" {print $2}' /proc/self/status)
mapfile -t CPU_IDS < <(expand_cpu_list "$CPU_LIST")
NCPU=${#CPU_IDS[@]}
if [ "$NCPU" -lt 1 ]; then
    log "BLOCKED_NO_ALLOWED_CPUS: unable to determine the process CPU set"
    exit 2
fi
MEM_MB=$(free -m | awk '/^Mem:/{print $2}')
MEM_AVAILABLE_MB=$(free -m | awk '/^Mem:/{print $7}')
VM_WORKERS=${ORCHESTRA_STRESS_VM_WORKERS:-2}
case "$VM_WORKERS" in
    ''|*[!0-9]*) log "BLOCKED_INVALID_MEMORY_WORKERS: $VM_WORKERS"; exit 2 ;;
esac
if [ "$VM_WORKERS" -lt 1 ]; then
    log "BLOCKED_INVALID_MEMORY_WORKERS: must be positive"
    exit 2
fi
case "$OWNERSHIP_POLLS" in
    ''|*[!0-9]*) log "BLOCKED_INVALID_OWNERSHIP_POLLS: $OWNERSHIP_POLLS"; exit 2 ;;
esac
if [ "$OWNERSHIP_POLLS" -lt 1 ]; then
    log "BLOCKED_INVALID_OWNERSHIP_POLLS: must be positive"
    exit 2
fi
DEFAULT_STRESS_MB=$((MEM_AVAILABLE_MB / (VM_WORKERS * 2)))
STRESS_MB=${ORCHESTRA_STRESS_MEM_MB:-$DEFAULT_STRESS_MB}
case "$STRESS_MB" in
    ''|*[!0-9]*) log "BLOCKED_INVALID_MEMORY_SIZE: $STRESS_MB"; exit 2 ;;
esac
if [ "$STRESS_MB" -lt 1 ] ||
   [ $((STRESS_MB * VM_WORKERS)) -gt $((MEM_AVAILABLE_MB / 2)) ]; then
    log "BLOCKED_MEMORY_SAFETY: requested=$((STRESS_MB * VM_WORKERS))MB available=$MEM_AVAILABLE_MB MB limit=$((MEM_AVAILABLE_MB / 2))MB"
    exit 2
fi

if [ "$MODE" = "orchestra" ]; then
    if [ "$(id -u)" -ne 0 ] && ! sudo -n true >/dev/null 2>&1; then
        log "BLOCKED_PRIVILEGE: non-interactive root or sudo is required for orchestra mode"
        exit 2
    fi
    SCHED_STATE=$(cat /sys/kernel/sched_ext/state 2>/dev/null || echo "disabled")
    if [ "$SCHED_STATE" != "enabled" ]; then
        log "BLOCKED_SCHEDULER_NOT_ENABLED: state=$SCHED_STATE"
        exit 2
    fi
    if [ -z "${ORCHESTRA_BRIDGE:-}" ]; then
        ORCHESTRA_BRIDGE=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../kernel/sched_ext/bridge" && pwd)/orchestra_bridge
    fi
    if [ ! -x "$ORCHESTRA_BRIDGE" ]; then
        log "BLOCKED_MISSING_BRIDGE: $ORCHESTRA_BRIDGE"
        exit 2
    fi
fi

PHASE_NAME=
PHASE_OWNED=not_applicable
PHASE_NOTE=
OWNERSHIP_LOG=

wait_with_watchdog() {
    local pid=$1 label=$2 deadline=$3
    local status

    while kill -0 "$pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$deadline" ]; then
            log "FAIL: $label watchdog expired after ${PHASE_TIMEOUT}s; terminating owned process $pid"
            kill "$pid" 2>/dev/null || true
            sleep 1
            kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            return 1
        fi
        sleep 0.05
    done
    if wait "$pid" 2>/dev/null; then
        return 0
    else
        status=$?
    fi
    log "FAIL: $label exited with status $status"
    return 1
}

status_counts() {
    local tid=$1
    local status

    status=$(sudo "$ORCHESTRA_BRIDGE" --status --target-pid "$tid" 2>/dev/null || true)
    awk '
        /^task / {
            for (i = 1; i <= NF; i++) {
                if (index($i, "accepted=") == 1) accepted = substr($i, 10) + 0
                else if (index($i, "dispatched=") == 1) dispatched = substr($i, 12) + 0
                else if (index($i, "running=") == 1) running = substr($i, 9) + 0
                else if (index($i, "effective=") == 1) effective = substr($i, 10) + 0
            }
        }
        END { printf "%u %u %u %u\n", accepted, dispatched, running, effective }
    ' <<<"$status"
}

configure_orchestra_ownership() {
    local pid counts accepted dispatched running effective attempt
    local all_owned=1

    OWNERSHIP_LOG="$OUTDIR/ownership-${PHASE_NAME}.log"
    : >"$OWNERSHIP_LOG"
    for pid in "${PIDS[@]}"; do
        if ! sudo "$ORCHESTRA_BRIDGE" --opt-in --target-pid "$pid" \
            >>"$OWNERSHIP_LOG" 2>&1; then
            printf 'pid=%s opt_in=failed\n' "$pid" >>"$OWNERSHIP_LOG"
            return 1
        fi
        if ! sudo "$ORCHESTRA_BRIDGE" --publish --action RUN --target-pid "$pid" \
            >>"$OWNERSHIP_LOG" 2>&1; then
            printf 'pid=%s publish=failed\n' "$pid" >>"$OWNERSHIP_LOG"
            return 1
        fi
    done

    for pid in "${PIDS[@]}"; do
        counts="0 0 0 0"
        for ((attempt = 1; attempt <= OWNERSHIP_POLLS; attempt++)); do
            counts=$(status_counts "$pid")
            read -r accepted dispatched running effective <<<"$counts"
            if [ "$accepted" -gt 0 ] &&
               [ "$dispatched" -gt 0 ] &&
               [ "$running" -gt 0 ]; then
                break
            fi
            sleep 0.01
        done
        read -r accepted dispatched running effective <<<"$counts"
        printf 'pid=%s accepted=%s dispatched=%s running=%s effective=%s\n' \
            "$pid" "$accepted" "$dispatched" "$running" "$effective" \
            >>"$OWNERSHIP_LOG"
        if [ "$accepted" -le 0 ] ||
           [ "$dispatched" -le 0 ] ||
           [ "$running" -le 0 ]; then
            all_owned=0
        fi
    done
    [ "$all_owned" -eq 1 ]
}

descendant_pids() {
    local parent=$1 child

    for child in $(pgrep -P "$parent" 2>/dev/null || true); do
        echo "$child"
        descendant_pids "$child"
    done
}

configure_memory_ownership() {
    local attempt

    MEM_CHILD_PIDS=()
    for ((attempt = 1; attempt <= OWNERSHIP_POLLS; attempt++)); do
        mapfile -t MEM_CHILD_PIDS < <(descendant_pids "$STRESS_PID")
        if [ "${#MEM_CHILD_PIDS[@]}" -ge "$VM_WORKERS" ]; then
            break
        fi
        sleep 0.01
    done
    if [ "${#MEM_CHILD_PIDS[@]}" -lt "$VM_WORKERS" ]; then
        return 1
    fi

    PIDS=("${MEM_CHILD_PIDS[@]}")
    if configure_orchestra_ownership; then
        PIDS=()
        return 0
    fi
    PIDS=()
    return 1
}

release_phase_workers() {
    local gate=$1
    local pid deadline=$((SECONDS + PHASE_TIMEOUT))

    PHASE_OWNED=not_applicable
    PHASE_NOTE=
    if [ "$MODE" = "orchestra" ]; then
        if configure_orchestra_ownership; then
            PHASE_OWNED=yes
            PHASE_NOTE=exact-tid-ownership-confirmed
        else
            PHASE_OWNED=no
            PHASE_NOTE=ownership-gate-failed
            log "  $PHASE_NAME: ownership gate failed; releasing workers as unowned evidence"
        fi
    fi
    touch "$gate"
    for pid in "${PIDS[@]}"; do
        if ! wait_with_watchdog "$pid" "$PHASE_NAME" "$deadline"; then
            errors=$((errors + 1))
        fi
    done
    PIDS=()
}

phase_result() {
    if [ "$1" -eq 0 ]; then
        echo PASS
    else
        echo FAIL
    fi
}

cat > "$OUTDIR/results.csv" << EOF
test,duration_s,cpu_workers,memory_mb,io_files,errors,warnings,completed,ownership,notes
EOF

DMESG_BEFORE="$OUTDIR/dmesg_before.txt"
dmesg --ctime 2>/dev/null > "$DMESG_BEFORE" || :
INITIAL_SCHED_STATE=$(cat /sys/kernel/sched_ext/state 2>/dev/null || echo disabled)

log "=== ORCHESTRA Stress Suite ==="
log "Mode: $MODE | Duration: ${DURATION}s | allowed CPUs: $CPU_LIST | RAM: ${MEM_MB}MB | available: ${MEM_AVAILABLE_MB}MB"
log "Memory safety budget: ${VM_WORKERS} workers x ${STRESS_MB}MB = $((STRESS_MB * VM_WORKERS))MB"
log "Phase watchdog: ${PHASE_TIMEOUT}s"
log "Scheduler state before workloads: $INITIAL_SCHED_STATE"
log "Output: $OUTDIR"
df -P "$OUTDIR" >"$OUTDIR/storage_before.txt" 2>&1 || true
thermal_snapshot "$OUTDIR/thermal_before.txt"
if compgen -G '/sys/class/thermal/thermal_zone*/temp' >/dev/null; then
    thermal_monitor "$$" &
    THERMAL_MONITOR_PID=$!
    log "Thermal monitor active; it stops the campaign at a reported critical trip point"
else
    echo "thermal_sensors=unavailable" >"$OUTDIR/thermal_status.txt"
    log "Thermal monitor unavailable: no readable thermal zones"
fi

# ---- CPU Stress ----
log "--- CPU Stress ($NCPU workers) ---"
STRESS_START=$(date +%s)
errors=0
warnings=0
PHASE_NAME=cpu
GATE="$WORKDIR/cpu.release"
PIDS=()
for i in $(seq 1 "$NCPU"); do
    taskset -c "${CPU_IDS[$((i - 1))]}" bash -c '
        gate=$1
        duration=$2
        while [ ! -e "$gate" ]; do sleep 0.01; done
        end=$(( $(date +%s) + duration ))
        while [ $(date +%s) -lt "$end" ]; do :; done
    ' _ "$GATE" "$DURATION" &
    PIDS+=("$!")
done
release_phase_workers "$GATE"
elapsed=$(( $(date +%s) - STRESS_START ))
echo "cpu_stress,$elapsed,$NCPU,0,0,$errors,$warnings,$(phase_result "$errors"),$PHASE_OWNED,$PHASE_NOTE" >> "$OUTDIR/results.csv"
log "  CPU: ${elapsed}s, $errors errors, ownership=$PHASE_OWNED"

# ---- Memory Stress ----
log "--- Memory Stress (${STRESS_MB}MB) ---"
errors=0
warnings=0
if command -v stress &>/dev/null; then
    stress --vm "$VM_WORKERS" --vm-bytes "${STRESS_MB}M" --timeout "${DURATION}s" 2>/dev/null &
    STRESS_PID=$!
    memory_owned=not_applicable
    memory_note=stress-completed
    if [ "$MODE" = "orchestra" ]; then
        PHASE_NAME=memory
        if configure_memory_ownership; then
            memory_owned=yes
            memory_note=exact-tid-ownership-confirmed
        else
            memory_owned=not_proven
            memory_note=child-ownership-gate-failed
            log "BLOCKED_OWNERSHIP_NOT_PROVEN: stress --vm child admission did not complete"
        fi
    fi
    if ! wait_with_watchdog "$STRESS_PID" memory "$((SECONDS + PHASE_TIMEOUT))"; then
        errors=$((errors + 1))
    fi
    STRESS_PID=
    if [ "$MODE" = "orchestra" ] && [ "$memory_owned" != yes ]; then
        echo "mem_stress,$DURATION,0,$((STRESS_MB * VM_WORKERS)),0,$errors,$warnings,BLOCKED_OWNERSHIP_NOT_PROVEN,$memory_owned,$memory_note" >> "$OUTDIR/results.csv"
    else
        echo "mem_stress,$DURATION,0,$((STRESS_MB * VM_WORKERS)),0,$errors,$warnings,$(phase_result "$errors"),$memory_owned,$memory_note" >> "$OUTDIR/results.csv"
    fi
else
    log "BLOCKED_MISSING_TOOL: stress is required for memory pressure; disk writes are not a memory substitute"
    echo "mem_stress,0,0,$((STRESS_MB * VM_WORKERS)),0,0,0,BLOCKED_MISSING_TOOL,not_applicable,stress-command-missing" >> "$OUTDIR/results.csv"
fi
log "  Memory: done"

# ---- I/O Stress ----
log "--- I/O Stress ---"
IO_START=$(date +%s)
IO_DIR="$WORKDIR/io"
orchestra_ensure_private_dir "$IO_DIR" || {
    echo "refusing unsafe I/O work directory: $IO_DIR" >&2
    exit 2
}
errors=0
warnings=0
PHASE_NAME=io
GATE="$WORKDIR/io.release"
PIDS=()
IO_WORKERS=$((NCPU / 2 > 0 ? NCPU / 2 : 1))
for i in $(seq 1 "$IO_WORKERS"); do
    bash -c '
        gate=$1
        duration=$2
        path=$3
        while [ ! -e "$gate" ]; do sleep 0.01; done
        end=$(( $(date +%s) + duration ))
        while [ $(date +%s) -lt "$end" ]; do
            dd if=/dev/urandom of="$path" bs=4k count=100 2>/dev/null
            cat "$path" > /dev/null 2>/dev/null
        done
    ' _ "$GATE" "$DURATION" "$IO_DIR/file-$i" &
    PIDS+=("$!")
done
release_phase_workers "$GATE"
rm -rf -- "$IO_DIR"
elapsed=$(( $(date +%s) - IO_START ))
echo "io_stress,$elapsed,0,0,$IO_WORKERS,$errors,$warnings,$(phase_result "$errors"),$PHASE_OWNED,$PHASE_NOTE" >> "$OUTDIR/results.csv"
log "  I/O: ${elapsed}s, $errors errors, ownership=$PHASE_OWNED"

# ---- Mixed Workload ----
log "--- Mixed Workload ---"
MIX_START=$(date +%s)
half=$((NCPU / 2 > 0 ? NCPU / 2 : 1))
io_workers=$((NCPU - half))
errors=0
warnings=0
PHASE_NAME=mixed
GATE="$WORKDIR/mixed.release"
MIX_DIR="$WORKDIR/mixed"
orchestra_ensure_private_dir "$MIX_DIR" || {
    echo "refusing unsafe mixed-workload directory: $MIX_DIR" >&2
    exit 2
}
PIDS=()
for i in $(seq 1 "$half"); do
    taskset -c "${CPU_IDS[$((i - 1))]}" bash -c '
        gate=$1
        duration=$2
        while [ ! -e "$gate" ]; do sleep 0.01; done
        end=$(( $(date +%s) + duration ))
        while [ $(date +%s) -lt "$end" ]; do :; done
    ' _ "$GATE" "$DURATION" &
    PIDS+=("$!")
done
if [ "$io_workers" -gt 0 ]; then
    for i in $(seq $((half + 1)) "$NCPU"); do
        taskset -c "${CPU_IDS[$((i - 1))]}" bash -c '
            gate=$1
            duration=$2
            path=$3
            while [ ! -e "$gate" ]; do sleep 0.01; done
            end=$(( $(date +%s) + duration ))
            while [ $(date +%s) -lt "$end" ]; do
                dd if=/dev/zero of="$path" bs=4k count=10 2>/dev/null
                rm -f "$path"
                sleep 0.1
            done
        ' _ "$GATE" "$DURATION" "$MIX_DIR/mix-$i" &
        PIDS+=("$!")
    done
fi
release_phase_workers "$GATE"
rm -rf -- "$MIX_DIR"
elapsed=$(( $(date +%s) - MIX_START ))
echo "mixed,$elapsed,$half,0,$io_workers,$errors,$warnings,$(phase_result "$errors"),$PHASE_OWNED,$PHASE_NOTE" >> "$OUTDIR/results.csv"
log "  Mixed: ${elapsed}s, $errors errors, ownership=$PHASE_OWNED"

# ---- Kernel Health Check ----
log "--- Health Check ---"
WARNINGS=0
DMESG_AFTER="$OUTDIR/dmesg_after.txt"
dmesg --ctime 2>/dev/null > "$DMESG_AFTER" || :
NEW_HEALTH_LINES=$(comm -13 <(sort "$DMESG_BEFORE") <(sort "$DMESG_AFTER") |
    grep -i "panic\|BUG\|stall\|hung_task\|RCU" || true)
if [ -n "$NEW_HEALTH_LINES" ]; then
    WARNINGS=1
    printf '%s\n' "$NEW_HEALTH_LINES" > "$OUTDIR/new_health_warnings.txt"
fi
FINAL_SCHED_STATE=$(cat /sys/kernel/sched_ext/state 2>/dev/null || echo disabled)
echo "health_check,0,0,0,0,0,$WARNINGS,$([ "$WARNINGS" -eq 0 ] && echo PASS || echo WARN),not_applicable,sorted-dmesg-delta" >> "$OUTDIR/results.csv"

log "Scheduler state after workloads: $FINAL_SCHED_STATE"
log "=== COMPLETE ==="
log "Results: $OUTDIR/results.csv"
cat "$OUTDIR/results.csv"
