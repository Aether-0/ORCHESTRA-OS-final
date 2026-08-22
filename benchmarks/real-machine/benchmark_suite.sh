#!/usr/bin/env bash
set -euo pipefail

# ORCHESTRA-OS real-machine benchmark suite.
#
# The ORCHESTRA path uses the repository loader and an exact-TID ownership
# gate. It never removes arbitrary bpffs entries or detaches links it did not
# create. A timing row is marked ownership-gate-failed unless every target
# task has positive accepted, dispatched, and running telemetry before release.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
# shellcheck source=/dev/null
. "$REPO_ROOT/scripts/path_safety.sh"

if [[ -v ORCHESTRA_BENCH_OUTDIR ]]; then
    OUTDIR=$ORCHESTRA_BENCH_OUTDIR
else
    OUTDIR="/tmp/orchestra-bench-$(date +%Y%m%d-%H%M%S)"
fi
orchestra_ensure_private_dir "$OUTDIR" || {
    echo "refusing unsafe benchmark output directory: $OUTDIR" >&2
    exit 2
}
df -P "$OUTDIR" >"$OUTDIR/storage_before.txt" 2>&1 || true

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
    echo "unable to determine an allowed CPU set" >&2
    exit 2
fi
BPF="$REPO_ROOT/kernel/sched_ext/orchestra_scx_stage7.bpf.o"
BRIDGE="$REPO_ROOT/kernel/sched_ext/bridge/orchestra_bridge"
LOADER="$REPO_ROOT/kernel/sched_ext/bridge/orchestra_loader"
WORKLOAD="$REPO_ROOT/kernel/sched_ext/scripts/fixed_work"
WORK_ITERATIONS=80000000
SECS=8
WAIT_TIMEOUT=
OWNERSHIP_POLLS=200
WORKER_SET="1 2 4"
WORKLOAD_MODE=cpu

[[ -v ORCHESTRA_BPF ]] && BPF=$ORCHESTRA_BPF
[[ -v ORCHESTRA_BRIDGE ]] && BRIDGE=$ORCHESTRA_BRIDGE
[[ -v ORCHESTRA_LOADER ]] && LOADER=$ORCHESTRA_LOADER
[[ -v ORCHESTRA_WORKLOAD ]] && WORKLOAD=$ORCHESTRA_WORKLOAD
[[ -v ORCHESTRA_WORK_ITERATIONS ]] && WORK_ITERATIONS=$ORCHESTRA_WORK_ITERATIONS
[[ -v BENCH_SECS ]] && SECS=$BENCH_SECS
[[ -v ORCHESTRA_WAIT_TIMEOUT ]] && WAIT_TIMEOUT=$ORCHESTRA_WAIT_TIMEOUT
[[ -v ORCHESTRA_OWNERSHIP_POLLS ]] && OWNERSHIP_POLLS=$ORCHESTRA_OWNERSHIP_POLLS
[[ -v ORCHESTRA_WORKERS ]] && WORKER_SET=$ORCHESTRA_WORKERS
[[ -v ORCHESTRA_WORKLOAD_MODE ]] && WORKLOAD_MODE=$ORCHESTRA_WORKLOAD_MODE

case "$WORKLOAD_MODE" in
    cpu|mixed) ;;
    *) echo "invalid ORCHESTRA_WORKLOAD_MODE: $WORKLOAD_MODE" >&2; exit 2 ;;
esac
case "$SECS" in
    ''|*[!0-9]*) echo "invalid BENCH_SECS: $SECS" >&2; exit 2 ;;
esac
if [ -z "$WAIT_TIMEOUT" ]; then
    WAIT_TIMEOUT=$((SECS + 15))
fi
case "$WAIT_TIMEOUT" in
    ''|*[!0-9]*) echo "invalid ORCHESTRA_WAIT_TIMEOUT: $WAIT_TIMEOUT" >&2; exit 2 ;;
esac
if [ "$WAIT_TIMEOUT" -lt 1 ]; then
    echo "ORCHESTRA_WAIT_TIMEOUT must be positive" >&2
    exit 2
fi

ORCHESTRA_LOADED=0
PIDS=
SCX_PID=
RUN_DIR=
GATE=
SCHED=
WORKER_FAILURE_REASON=

log() {
    echo "$(date +%H:%M:%S) $*" | tee -a "$OUTDIR/bench.log"
}

state() {
    cat /sys/kernel/sched_ext/state 2>/dev/null || echo disabled
}

cleanup() {
    local pid
    for pid in $PIDS; do
        kill "$pid" 2>/dev/null || true
    done
    for pid in $PIDS; do
        wait "$pid" 2>/dev/null || true
    done
    if [ -n "$SCX_PID" ]; then
        kill "$SCX_PID" 2>/dev/null || true
        wait "$SCX_PID" 2>/dev/null || true
        SCX_PID=
    fi
    if [ "$ORCHESTRA_LOADED" -eq 1 ]; then
        if ! sudo "$LOADER" --unload >"$OUTDIR/orchestra_unload_cleanup.log" 2>&1; then
            log "ERROR: loader cleanup failed; refusing broad BPF cleanup"
        else
            ORCHESTRA_LOADED=0
        fi
    fi
    # Capture the after-state only after owned workers, helper schedulers and
    # any ORCHESTRA attachment have been torn down.
    df -P "$OUTDIR" >"$OUTDIR/storage_after.txt" 2>&1 || true
}
trap cleanup EXIT

status_counts() {
    local tid=$1
    local text
    text=$(sudo "$BRIDGE" --status --target-pid "$tid" 2>/dev/null || true)
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
    ' <<<"$text"
}

wait_for_ownership() {
    local pid counts accepted dispatched running effective attempt all_owned=1
    : >"$RUN_DIR/ownership.log"
    for pid in $PIDS; do
        counts="0 0 0 0"
        for attempt in $(seq 1 "$OWNERSHIP_POLLS"); do
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
            >>"$RUN_DIR/ownership.log"
        if [ "$accepted" -le 0 ] ||
           [ "$dispatched" -le 0 ] ||
           [ "$running" -le 0 ]; then
            all_owned=0
        fi
    done
    [ "$all_owned" -eq 1 ]
}

ownership_totals() {
    awk '
        {
            for (i = 1; i <= NF; i++) {
                split($i, pair, "=")
                if (pair[1] == "accepted") accepted += pair[2]
                else if (pair[1] == "dispatched") dispatched += pair[2]
                else if (pair[1] == "running") running += pair[2]
                else if (pair[1] == "effective") effective += pair[2]
            }
        }
        END { printf "%u %u %u %u\n", accepted, dispatched, running, effective }
    ' "$RUN_DIR/ownership.log"
}

launch_workers() {
    local workers=$1 duration=$2 cpu i
    RUN_DIR="$OUTDIR/run-$(date +%Y%m%d-%H%M%S)-$SCHED-$workers"
    orchestra_ensure_private_dir "$RUN_DIR" || {
        echo "refusing unsafe benchmark run directory: $RUN_DIR" >&2
        return 2
    }
    GATE="$RUN_DIR/release"
    PIDS=

    for i in $(seq 1 "$workers"); do
        cpu=${CPU_IDS[$(((i - 1) % NCPU))]}
        if [ "$WORKLOAD_MODE" = "mixed" ] && [ $((i % 2)) -eq 1 ]; then
            taskset -c "$cpu" bash -c '
                gate=$1
                duration=$2
                path=$3
                while [ ! -e "$gate" ]; do sleep 0.01; done
                end=$(( $(date +%s) + duration ))
                while [ $(date +%s) -lt "$end" ]; do
                    dd if=/dev/zero of="$path" bs=4k count=10 2>/dev/null
                    rm -f "$path"
                    sleep 0.05
                done
            ' _ "$GATE" "$duration" "$RUN_DIR/io-$i" \
                >"$RUN_DIR/worker-$i.stdout" 2>"$RUN_DIR/worker-$i.stderr" &
        elif [ -x "$WORKLOAD" ]; then
            taskset -c "$cpu" bash -c '
                gate=$1
                workload=$2
                iterations=$3
                while [ ! -e "$gate" ]; do sleep 0.01; done
                exec "$workload" "$iterations"
            ' _ "$GATE" "$WORKLOAD" "$WORK_ITERATIONS" \
                >"$RUN_DIR/worker-$i.stdout" 2>"$RUN_DIR/worker-$i.stderr" &
        else
            taskset -c "$cpu" bash -c '
                gate=$1
                duration=$2
                while [ ! -e "$gate" ]; do sleep 0.01; done
                end=$(( $(date +%s) + duration ))
                while [ $(date +%s) -lt "$end" ]; do :; done
            ' _ "$GATE" "$duration" \
                >"$RUN_DIR/worker-$i.stdout" 2>"$RUN_DIR/worker-$i.stderr" &
        fi
        PIDS="$PIDS $!"
    done
}

wait_for_workers() {
    local deadline=$((SECONDS + WAIT_TIMEOUT))
    local pid victim status failed=0

    WORKER_FAILURE_REASON=
    : >"$RUN_DIR/worker_status.log"

    for pid in $PIDS; do
        while kill -0 "$pid" 2>/dev/null; do
            if [ "$SECONDS" -ge "$deadline" ]; then
                log "FAIL: worker watchdog expired after ${WAIT_TIMEOUT}s; terminating owned workers"
                for victim in $PIDS; do
                    kill "$victim" 2>/dev/null || true
                done
                for victim in $PIDS; do
                    wait "$victim" 2>/dev/null || true
                done
                WORKER_FAILURE_REASON=worker-watchdog-expired
                printf 'pid=%s status=watchdog-expired\n' "$pid" \
                    >>"$RUN_DIR/worker_status.log"
                return 1
            fi
            sleep 0.05
        done
        if wait "$pid" 2>/dev/null; then
            status=0
        else
            status=$?
            failed=1
            WORKER_FAILURE_REASON=worker-exit-nonzero
        fi
        printf 'pid=%s status=%s\n' "$pid" "$status" >>"$RUN_DIR/worker_status.log"
    done
    return "$failed"
}

configure_orchestra_ownership() {
    local pid
    if [ ! -x "$BRIDGE" ]; then
        log "ORCHESTRA blocked: bridge is not executable: $BRIDGE"
        return 1
    fi
    for pid in $PIDS; do
        if ! sudo "$BRIDGE" --opt-in --target-pid "$pid" \
            >"$RUN_DIR/opt-in-$pid.log" 2>&1; then
            return 1
        fi
        if ! sudo "$BRIDGE" --publish --action RUN --target-pid "$pid" \
            >"$RUN_DIR/publish-$pid.log" 2>&1; then
            return 1
        fi
    done
    wait_for_ownership
}

run_test() {
    local scheduler=$1 workers=$2 duration=$3
    local ctx0 ctx1 ctxd start_ns end_ns elapsed_ms
    local accepted=0 dispatched=0 running=0 effective=0
    local owned=n/a notes=completed

    SCHED=$scheduler
    log "  $scheduler: $workers workers, $duration seconds/fixed workload..."
    ctx0=$(awk '/^ctxt/{print $2}' /proc/stat)
    launch_workers "$workers" "$duration"

    if [ "$scheduler" = "orchestra" ]; then
        if configure_orchestra_ownership; then
            owned=yes
        else
            owned=no
            notes=ownership-gate-failed
        fi
    fi

    start_ns=$(date +%s%N)
    touch "$GATE"
    if ! wait_for_workers; then
        if [ "$notes" = completed ]; then
            notes=$WORKER_FAILURE_REASON
        else
            notes="$notes;$WORKER_FAILURE_REASON"
        fi
    fi
    end_ns=$(date +%s%N)
    elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
    ctx1=$(awk '/^ctxt/{print $2}' /proc/stat)
    ctxd=$((ctx1 - ctx0))

    if [ "$scheduler" = "orchestra" ] && [ -f "$RUN_DIR/ownership.log" ]; then
        read -r accepted dispatched running effective <<<"$(ownership_totals)"
    fi

    echo "$scheduler,$WORKLOAD_MODE,$workers,$duration,$elapsed_ms,$ctxd,$accepted,$dispatched,$running,$effective,$owned,$notes" \
        >>"$OUTDIR/results.csv"
    log "    ${elapsed_ms}ms ctx_delta=$ctxd accepted=$accepted dispatched=$dispatched running=$running owned=$owned"
    PIDS=
}

run_worker_set() {
    local scheduler=$1
    local workers
    for workers in $WORKER_SET; do
        case "$workers" in
            ''|*[!0-9]*) log "Skipping invalid worker count: $workers"; continue ;;
        esac
        if [ "$workers" -gt "$NCPU" ]; then
            log "Skipping $scheduler/$workers: exceeds available CPUs ($NCPU)"
            continue
        fi
        run_test "$scheduler" "$workers" "$SECS"
    done
}

load_orchestra() {
    if [ ! -x "$LOADER" ]; then
        log "ORCHESTRA blocked: loader is not executable: $LOADER"
        return 1
    fi
    if [ ! -f "$BPF" ]; then
        log "ORCHESTRA blocked: BPF object is missing: $BPF"
        return 1
    fi
    if [ "$(state)" != "disabled" ]; then
        log "ORCHESTRA blocked: sched_ext is already active ($(state)); no links will be detached"
        return 1
    fi
    if [ "$(id -u)" -ne 0 ] && ! sudo -n true >/dev/null 2>&1; then
        log "ORCHESTRA blocked: non-interactive root or sudo is required"
        return 1
    fi
    if ! sudo "$LOADER" --load "$BPF" >"$OUTDIR/orchestra_load.log" 2>&1; then
        log "ORCHESTRA load failed; see $OUTDIR/orchestra_load.log"
        return 1
    fi
    ORCHESTRA_LOADED=1
    [ "$(state)" = "enabled" ]
}

unload_orchestra() {
    if [ "$ORCHESTRA_LOADED" -eq 0 ]; then
        return 0
    fi
    if ! sudo "$LOADER" --unload >"$OUTDIR/orchestra_unload.log" 2>&1; then
        log "ORCHESTRA unload failed; see $OUTDIR/orchestra_unload.log"
        return 1
    fi
    ORCHESTRA_LOADED=0
    if [ "$(state)" != "disabled" ]; then
        log "ORCHESTRA unload did not return sched_ext to disabled"
        return 1
    fi
}

echo "scheduler,workload,workers,duration_s,elapsed_ms,ctx_delta,accepted,dispatched,running,effective,owned,notes" \
    >"$OUTDIR/results.csv"

log "=== Real-Machine Benchmark Suite ==="
log "Repo: $REPO_ROOT | allowed CPUs: $CPU_LIST | workload=$WORKLOAD_MODE | Output: $OUTDIR | duration=$SECS seconds"
log "Worker watchdog: ${WAIT_TIMEOUT}s"

log "--- CFS baseline ---"
run_worker_set cfs

if [ -x /usr/bin/scx_simple ]; then
    log "--- scx_simple ---"
    if [ "$(id -u)" -eq 0 ] || sudo -n true >/dev/null 2>&1; then
        sudo -n /usr/bin/scx_simple >"$OUTDIR/scx_simple.log" 2>&1 &
        SCX_PID=$!
        sleep 2
        if [ "$(state)" = "enabled" ]; then
            run_worker_set scx_simple
        else
            log "scx_simple did not enable sched_ext; phase blocked"
        fi
        kill "$SCX_PID" 2>/dev/null || true
        wait "$SCX_PID" 2>/dev/null || true
        SCX_PID=
        sleep 1
        if [ "$(state)" != "disabled" ]; then
            log "scx_simple did not unload cleanly; no link cleanup attempted"
        fi
    else
        log "scx_simple blocked: non-interactive root or sudo is required"
    fi
else
    log "scx_simple not installed; phase blocked"
fi

log "--- ORCHESTRA ---"
if load_orchestra; then
    run_worker_set orchestra
    unload_orchestra || true
else
    log "ORCHESTRA comparison phase blocked before attach"
fi

log "=== COMPLETE ==="
cat "$OUTDIR/results.csv"
echo "Full results: $OUTDIR"
