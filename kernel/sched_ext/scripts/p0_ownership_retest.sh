#!/usr/bin/env bash
set -euo pipefail

# P0 ownership and forward-progress retest.
#
# This is deliberately loader-scoped. It never removes arbitrary bpffs pins,
# enumerates unrelated struct_ops links, or interprets a publication return
# code as scheduler ownership. A timed result is usable only after the exact
# target TID has positive accepted, dispatched, and running telemetry.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
# shellcheck source=/dev/null
. "$REPO_ROOT/scripts/path_safety.sh"
EVIDENCE_DIR=${1:-"/tmp/p0-ownership-$(date +%Y%m%d-%H%M%S)"}
orchestra_ensure_private_dir "$EVIDENCE_DIR" || {
    echo "refusing unsafe evidence directory: $EVIDENCE_DIR" >&2
    exit 2
}

BUILD_DIR="$EVIDENCE_DIR/build"
BPF=${ORCHESTRA_BPF:-}
BRIDGE=${ORCHESTRA_BRIDGE:-}
LOADER=${ORCHESTRA_LOADER:-}
WORK=${ORCHESTRA_WORKLOAD:-}
WORK_ITERATIONS=${ORCHESTRA_WORK_ITERATIONS:-100000000}
FORWARD_TIMEOUT=${ORCHESTRA_FORWARD_TIMEOUT:-20}
OWNERSHIP_POLLS=${ORCHESTRA_OWNERSHIP_POLLS:-200}

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
if [ "$NCPU" -gt 1 ]; then
    TARGET_CPU=${CPU_IDS[1]}
else
    TARGET_CPU=${CPU_IDS[0]}
fi

FAILS=0
LOADED=0
CURRENT_PID=
CURRENT_GATE=
RUN_DIR=
PIDS=()
FORWARD_OWNERSHIP_OK=0

log() {
    echo "$(date +%H:%M:%S) $*" | tee -a "$EVIDENCE_DIR/run.log"
}

fail() {
    FAILS=$((FAILS + 1))
    log "FAIL: $*"
    echo "FAIL:$*" >> "$EVIDENCE_DIR/failures.txt"
}

pass() {
    log "PASS: $*"
}

state() {
    cat /sys/kernel/sched_ext/state 2>/dev/null || echo disabled
}

cleanup_workers() {
    local pid
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    PIDS=()
    CURRENT_PID=
    CURRENT_GATE=
}

cleanup() {
    cleanup_workers
    if [ "$LOADED" -eq 1 ]; then
        if sudo "$LOADER" --unload >"$EVIDENCE_DIR/unload_cleanup.stdout" \
            2>"$EVIDENCE_DIR/unload_cleanup.stderr"; then
            LOADED=0
        else
            log "ERROR: loader cleanup failed; no broad BPF cleanup attempted"
        fi
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

case "$FORWARD_TIMEOUT" in
    ''|*[!0-9]*) fail "invalid forward timeout: $FORWARD_TIMEOUT"; exit 2 ;;
esac
case "$OWNERSHIP_POLLS" in
    ''|*[!0-9]*) fail "invalid ownership polls: $OWNERSHIP_POLLS"; exit 2 ;;
esac
if [ "$FORWARD_TIMEOUT" -lt 1 ] || [ "$OWNERSHIP_POLLS" -lt 1 ]; then
    fail "timeout and ownership polls must be positive"
    exit 2
fi

if [ -n "$BPF$BRIDGE$LOADER" ]; then
    if [ -z "$BPF" ] || [ -z "$BRIDGE" ] || [ -z "$LOADER" ]; then
        fail "ORCHESTRA_BPF, ORCHESTRA_BRIDGE, and ORCHESTRA_LOADER must be supplied together"
        exit 2
    fi
else
    log "Building target-matched artifacts out of tree"
    ORCHESTRA_BUILD_DIR="$BUILD_DIR" \
        bash "$SCRIPT_DIR/build_stage7_out_of_tree.sh"
    BPF="$BUILD_DIR/orchestra_scx_stage7.bpf.o"
    BRIDGE="$BUILD_DIR/orchestra_bridge"
    LOADER="$BUILD_DIR/orchestra_loader"
fi

for required in "$BPF" "$BRIDGE" "$LOADER"; do
    [ -x "$required" ] || [ -f "$required" ] || {
        fail "missing runtime artifact: $required"
        exit 2
    }
done

if [ -z "$WORK" ]; then
    WORK="$BUILD_DIR/fixed_work"
    orchestra_ensure_private_dir "$BUILD_DIR" || {
        fail "unsafe build directory: $BUILD_DIR"
        exit 2
    }
    cc -O2 -std=c11 -Wall -Wextra -Werror "$SCRIPT_DIR/fixed_work.c" \
        -o "$WORK"
fi
[ -x "$WORK" ] || { fail "workload is not executable: $WORK"; exit 2; }

{
    echo "timestamp=$(date -Iseconds)"
    echo "hostname=$(hostname)"
    echo "kernel=$(uname -r)"
    echo "cpus=$NCPU"
    echo "cpu_list=$CPU_LIST"
    echo "scheduler_pre=$(state)"
    echo "bpf=$BPF"
    echo "bridge=$BRIDGE"
    echo "loader=$LOADER"
    echo "workload=$WORK"
} > "$EVIDENCE_DIR/environment.txt"
: > "$EVIDENCE_DIR/failures.txt"
echo "test,status,details" > "$EVIDENCE_DIR/results.csv"

if [ "$(id -u)" -ne 0 ] && ! sudo -n true >/dev/null 2>&1; then
    fail "BLOCKED_PRIVILEGE: non-interactive root or sudo is required for sched_ext runtime tests"
    echo "scheduler_runtime,BLOCKED_PRIVILEGE,sudo_noninteractive_unavailable" \
        >> "$EVIDENCE_DIR/results.csv"
    exit 2
fi

sudo bpftool prog list > "$EVIDENCE_DIR/bpf_prog_pre.txt" 2>&1 || true
sudo bpftool map list > "$EVIDENCE_DIR/bpf_map_pre.txt" 2>&1 || true
sudo bpftool link list > "$EVIDENCE_DIR/bpf_link_pre.txt" 2>&1 || true
find /sys/fs/bpf -maxdepth 2 -print > "$EVIDENCE_DIR/bpffs_pre.txt" 2>/dev/null || true

run_cfs_baseline() {
    local start end elapsed
    log "CFS baseline: $WORK_ITERATIONS fixed iterations"
    start=$(date +%s%N)
    if "$WORK" "$WORK_ITERATIONS" >"$EVIDENCE_DIR/cfs.stdout" \
        2>"$EVIDENCE_DIR/cfs.stderr"; then
        end=$(date +%s%N)
        elapsed=$(( (end - start) / 1000000 ))
        echo "cfs_baseline,PASS,elapsed_ms=$elapsed" >> "$EVIDENCE_DIR/results.csv"
        pass "CFS baseline elapsed=${elapsed}ms"
    else
        echo "cfs_baseline,FAIL,workload_exit_failure" >> "$EVIDENCE_DIR/results.csv"
        fail "CFS baseline workload failed"
        return 1
    fi
}

load_scheduler() {
    [ "$(state)" = "disabled" ] || {
        fail "sched_ext is already active: $(state)"
        return 1
    }
    if ! sudo "$LOADER" --load "$BPF" >"$EVIDENCE_DIR/load.stdout" \
        2>"$EVIDENCE_DIR/load.stderr"; then
        fail "loader attach failed"
        return 1
    fi
    LOADED=1
    if [ "$(state)" != "enabled" ]; then
        fail "loader returned without enabled sched_ext state"
        return 1
    fi
    pass "scheduler loaded through exact loader"
}

status_counts() {
    local tid=$1
    local status

    status=$(sudo "$BRIDGE" --status --target-pid "$tid" 2>/dev/null || true)
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

launch_fixed_worker() {
    local name=$1 iterations=$2 cpu=${CPU_IDS[0]}
    RUN_DIR="$EVIDENCE_DIR/$name"
    mkdir -p "$RUN_DIR"
    CURRENT_GATE="$RUN_DIR/release"
    taskset -c "$cpu" bash -c '
        gate=$1
        workload=$2
        iterations=$3
        while [ ! -e "$gate" ]; do sleep 0.01; done
        exec "$workload" "$iterations"
    ' _ "$CURRENT_GATE" "$WORK" "$iterations" >"$RUN_DIR/worker.stdout" \
        2>"$RUN_DIR/worker.stderr" &
    CURRENT_PID=$!
    PIDS+=("$CURRENT_PID")
    echo "$CURRENT_PID" > "$RUN_DIR/pid"
}

launch_loop_worker() {
    local name=$1 duration=$2
    RUN_DIR="$EVIDENCE_DIR/$name"
    mkdir -p "$RUN_DIR"
    CURRENT_GATE="$RUN_DIR/release"
    bash -c '
        gate=$1
        duration=$2
        while [ ! -e "$gate" ]; do sleep 0.01; done
        end=$(( $(date +%s) + duration ))
        while [ $(date +%s) -lt "$end" ]; do :; done
    ' _ "$CURRENT_GATE" "$duration" >"$RUN_DIR/worker.stdout" \
        2>"$RUN_DIR/worker.stderr" &
    CURRENT_PID=$!
    PIDS+=("$CURRENT_PID")
    echo "$CURRENT_PID" > "$RUN_DIR/pid"
}

admit_run() {
    local pid=$1
    if ! sudo "$BRIDGE" --opt-in --target-pid "$pid" \
        >"$RUN_DIR/opt_in.stdout" 2>"$RUN_DIR/opt_in.stderr"; then
        return 1
    fi
    if ! sudo "$BRIDGE" --publish --action RUN --target-pid "$pid" \
        >"$RUN_DIR/publish_run.stdout" 2>"$RUN_DIR/publish_run.stderr"; then
        return 1
    fi
}

wait_for_ownership() {
    local pid=$1 counts accepted dispatched running effective attempt
    counts="0 0 0 0"
    for ((attempt = 1; attempt <= OWNERSHIP_POLLS; attempt++)); do
        counts=$(status_counts "$pid")
        read -r accepted dispatched running effective <<<"$counts"
        if [ "$accepted" -gt 0 ] && [ "$dispatched" -gt 0 ] &&
           [ "$running" -gt 0 ]; then
            printf 'accepted=%s dispatched=%s running=%s effective=%s\n' \
                "$accepted" "$dispatched" "$running" "$effective" \
                > "$RUN_DIR/ownership_gate.txt"
            return 0
        fi
        sleep 0.01
    done
    read -r accepted dispatched running effective <<<"$counts"
    printf 'accepted=%s dispatched=%s running=%s effective=%s\n' \
        "$accepted" "$dispatched" "$running" "$effective" \
        > "$RUN_DIR/ownership_gate.txt"
    return 1
}
run_forward_progress() {
    local pid deadline start end elapsed process_state workload_status
    log "Owned finite forward-progress gate"
    launch_fixed_worker forward_progress "$WORK_ITERATIONS"
    pid=$CURRENT_PID
    if ! admit_run "$pid" || ! wait_for_ownership "$pid"; then
        echo "owned_forward_progress,INCONCLUSIVE_OWNERSHIP_NOT_PROVEN,positive_exact_tid_gate_missing" \
            >> "$EVIDENCE_DIR/results.csv"
        fail "exact-TID ownership gate failed for PID $pid"
        touch "$CURRENT_GATE"
        wait "$pid" 2>/dev/null || true
        PIDS=()
        CURRENT_PID=
        CURRENT_GATE=
        return 1
    fi
    FORWARD_OWNERSHIP_OK=1
    start=$(date +%s%N)
    touch "$CURRENT_GATE"
    deadline=$((SECONDS + FORWARD_TIMEOUT))
    while kill -0 "$pid" 2>/dev/null; do
        process_state=$(ps -o stat= -p "$pid" 2>/dev/null || true)
        case "$process_state" in
            Z*) break ;;
        esac
        if [ "$SECONDS" -ge "$deadline" ]; then
            echo "owned_forward_progress,INCONCLUSIVE_FORWARD_PROGRESS_TIMEOUT,timeout_seconds=$FORWARD_TIMEOUT" \
                >> "$EVIDENCE_DIR/results.csv"
            fail "owned finite workload did not complete within ${FORWARD_TIMEOUT}s"
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            PIDS=()
            CURRENT_PID=
            CURRENT_GATE=
            return 1
        fi
        sleep 0.05
    done
    end=$(date +%s%N)
    if wait "$pid" 2>/dev/null; then
        workload_status=0
    else
        workload_status=$?
    fi
    if [ "$workload_status" -ne 0 ]; then
        echo "owned_forward_progress,FAIL,workload_exit=$workload_status" \
            >> "$EVIDENCE_DIR/results.csv"
        fail "owned finite workload exited with status $workload_status"
        PIDS=()
        CURRENT_PID=
        CURRENT_GATE=
        return 1
    fi
    elapsed=$(( (end - start) / 1000000 ))
    echo "owned_forward_progress,PASS,elapsed_ms=$elapsed;$(cat "$RUN_DIR/ownership_gate.txt")" \
        >> "$EVIDENCE_DIR/results.csv"
    pass "owned finite workload elapsed=${elapsed}ms"
    PIDS=()
    CURRENT_PID=
    CURRENT_GATE=
    return 0
}

run_action_probe() {
    local action=$1 pid publish_status
    log "Action probe: $action"
    launch_loop_worker "action_${action,,}" 5
    pid=$CURRENT_PID
    if ! admit_run "$pid" || ! wait_for_ownership "$pid"; then
        echo "action_${action},INCONCLUSIVE_OWNERSHIP_NOT_PROVEN,positive_exact_tid_gate_missing" \
            >> "$EVIDENCE_DIR/results.csv"
        fail "$action probe ownership gate failed for PID $pid"
        touch "$CURRENT_GATE"
        wait "$pid" 2>/dev/null || true
        PIDS=()
        CURRENT_PID=
        CURRENT_GATE=
        return 1
    fi
    touch "$CURRENT_GATE"
    publish_status=1
    case "$action" in
        MIGRATE)
            if sudo "$BRIDGE" --publish --action MIGRATE --target-pid "$pid" \
                --target-cpu "$TARGET_CPU" >"$RUN_DIR/publish_action.stdout" \
                2>"$RUN_DIR/publish_action.stderr"; then
                publish_status=0
            fi
            ;;
        THROTTLE)
            if sudo "$BRIDGE" --publish --action THROTTLE --target-pid "$pid" \
                --throttle-period-ns 10000000 --throttle-budget-ns 2000000 \
                >"$RUN_DIR/publish_action.stdout" 2>"$RUN_DIR/publish_action.stderr"; then
                publish_status=0
            fi
            ;;
        SLEEP)
            if sudo "$BRIDGE" --publish --action SLEEP --target-pid "$pid" \
                >"$RUN_DIR/publish_action.stdout" 2>"$RUN_DIR/publish_action.stderr"; then
                publish_status=0
            fi
            ;;
        YIELD)
            if sudo "$BRIDGE" --publish --action YIELD --target-pid "$pid" \
                >"$RUN_DIR/publish_action.stdout" 2>"$RUN_DIR/publish_action.stderr"; then
                publish_status=0
            fi
            ;;
        *)
            fail "unsupported action probe: $action"
            cleanup_workers
            return 1
            ;;
    esac
    sudo "$BRIDGE" --status --target-pid "$pid" >"$RUN_DIR/status_post.stdout" \
        2>"$RUN_DIR/status_post.stderr" || true
    if [ "$publish_status" -eq 0 ]; then
        echo "action_${action},OBSERVED_REQUEST,exact_tid_gate_passed" \
            >> "$EVIDENCE_DIR/results.csv"
        pass "$action request published after ownership gate"
    else
        echo "action_${action},FAIL,publish_failed" >> "$EVIDENCE_DIR/results.csv"
        fail "$action publish failed"
    fi
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    PIDS=()
    CURRENT_PID=
    CURRENT_GATE=
}

main() {
    log "Evidence directory: $EVIDENCE_DIR"
    if [ "$(state)" != "disabled" ]; then
        fail "CFS baseline cannot start while sched_ext is active: $(state)"
        return 1
    fi
    if ! run_cfs_baseline; then
        log "Skipping scheduler load because CFS baseline failed"
        return 1
    fi
    if ! load_scheduler; then
        echo "scheduler_load,BLOCKED_OR_LOAD_FAILED,state=$(state)" >> "$EVIDENCE_DIR/results.csv"
        return 1
    fi
    if run_forward_progress; then
        run_action_probe YIELD || true
        run_action_probe MIGRATE || true
        run_action_probe THROTTLE || true
        run_action_probe SLEEP || true
    else
        log "Skipping action probes because exact ownership/forward progress was not established"
    fi
    if [ "$LOADED" -eq 1 ] && sudo "$LOADER" --unload \
        >"$EVIDENCE_DIR/unload.stdout" 2>"$EVIDENCE_DIR/unload.stderr"; then
        LOADED=0
        [ "$(state)" = "disabled" ] && pass "scheduler unloaded cleanly" || fail "scheduler state after unload: $(state)"
    else
        fail "scheduler unload failed"
    fi
    sudo bpftool prog list > "$EVIDENCE_DIR/bpf_prog_post.txt" 2>&1 || true
    sudo bpftool map list > "$EVIDENCE_DIR/bpf_map_post.txt" 2>&1 || true
    sudo bpftool link list > "$EVIDENCE_DIR/bpf_link_post.txt" 2>&1 || true
    find /sys/fs/bpf -maxdepth 2 -print > "$EVIDENCE_DIR/bpffs_post.txt" 2>/dev/null || true
    log "Complete: failures=$FAILS evidence=$EVIDENCE_DIR"
    [ "$FAILS" -eq 0 ]
}

exit_status=0
main "$@" || exit_status=$?
echo "$FAILS" > "$EVIDENCE_DIR/fail_count.txt"
exit "$exit_status"
