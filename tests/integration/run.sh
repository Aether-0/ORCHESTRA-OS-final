#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_DIR=$(mktemp -d /tmp/orchestra-integration.XXXXXX)
ARTIFACT_DIR=${ORCHESTRA_INTEGRATION_ARTIFACT_DIR:-}

if [[ -n "$ARTIFACT_DIR" ]]; then
    if [[ -e "$ARTIFACT_DIR" ]]; then
        echo "FAIL integration artifact directory already exists: $ARTIFACT_DIR" >&2
        rm -rf -- "$TEST_DIR"
        exit 1
    fi
    mkdir -p -- "$ARTIFACT_DIR"
fi

cleanup() {
    if [[ -n "$ARTIFACT_DIR" ]]; then
        shopt -s nullglob
        local result_files=("$TEST_DIR"/*.csv "$TEST_DIR"/*.log)
        if ((${#result_files[@]} > 0)); then
            cp -a -- "${result_files[@]}" "$ARTIFACT_DIR"/
        fi
    fi
    rm -rf -- "$TEST_DIR"
}

trap cleanup EXIT

BIN="$TEST_DIR/orchestra_paper_cpu"
SRC="$REPO_DIR/orchestra_paper_cpu_demo/orchestra_paper_cpu.c"
VALIDATE="$SCRIPT_DIR/validate_paper_cpu_csv.py"
SCHEMA_V7="orchestra.paper_cpu.metrics/v7"
PUBLICATION_INTEGRATION_SOURCE="$SCRIPT_DIR/test_signal_publication_integration.c"
PUBLICATION_INTEGRATION_BIN="$TEST_DIR/test_signal_publication_integration"

gcc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wformat=2 -Werror -o "$BIN" "$SRC" -lm

gcc -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wformat=2 -Werror -o "$PUBLICATION_INTEGRATION_BIN" \
    "$PUBLICATION_INTEGRATION_SOURCE" -lm
timeout 15s "$PUBLICATION_INTEGRATION_BIN"

PYTHONPYCACHEPREFIX="$TEST_DIR/pycache" \
    python3 "$SCRIPT_DIR/test_validate_paper_cpu_csv.py"

set +e
timeout --signal=INT --kill-after=2s 3s "$BIN" --duration nonsense \
    >"$TEST_DIR/invalid.csv" 2>"$TEST_DIR/invalid.log"
INVALID_RC=$?
set -e
if [[ $INVALID_RC -ne 2 ]]; then
    echo "FAIL invalid numeric option returned $INVALID_RC, expected 2" >&2
    exit 1
fi

timeout --signal=INT --kill-after=2s 8s nice -n 10 "$BIN" \
    --workers 4 --rt-exempt 0 --duration 1 --interval-ms 100 \
    --calibration 1 --mode baseline --seed 101 \
    >"$TEST_DIR/baseline.csv" 2>"$TEST_DIR/baseline.log"
python3 "$VALIDATE" "$TEST_DIR/baseline.csv" --mode baseline --schema "$SCHEMA_V7" \
    --min-rows 8 --max-rows 12

timeout --signal=INT --kill-after=2s 10s nice -n 10 "$BIN" \
    --workers 4 --rt-exempt 0 --duration 3 --interval-ms 100 \
    --calibration 1 --mode orchestra --seed 202 \
    >"$TEST_DIR/orchestra.csv" 2>"$TEST_DIR/orchestra.log"
python3 "$VALIDATE" "$TEST_DIR/orchestra.csv" --mode orchestra --schema "$SCHEMA_V7" \
    --min-rows 25 --max-rows 32

timeout --signal=INT --kill-after=2s 9s nice -n 10 "$BIN" \
    --workers 4 --rt-exempt 0 --duration 2 --interval-ms 100 \
    --calibration 1 --mode orchestra --seed 303 --tamper-every 3 \
    >"$TEST_DIR/tamper.csv" 2>"$TEST_DIR/tamper.log"
python3 "$VALIDATE" "$TEST_DIR/tamper.csv" --mode orchestra --schema "$SCHEMA_V7" \
    --min-rows 16 --max-rows 22 --expect-rejections

timeout --signal=INT --kill-after=2s 10s nice -n 10 "$BIN" \
    --workers 4 --rt-exempt 0 --duration 3 --interval-ms 100 \
    --calibration 1 --mode orchestra --seed 353 --tamper-every 20 \
    >"$TEST_DIR/controller-tamper.csv" 2>"$TEST_DIR/controller-tamper.log"
python3 "$VALIDATE" "$TEST_DIR/controller-tamper.csv" --mode orchestra --schema "$SCHEMA_V7" \
    --min-rows 25 --max-rows 32 --expect-rejections \
    --expect-controller-skip 20

# The program's bounded worker cleanup includes a TERM grace period followed
# by a KILL/reap grace period.  The external timeout must outlive that cleanup
# window or it can kill the parent while it is still reaping children.
timeout --preserve-status --signal=INT --kill-after=5s 3s nice -n 10 "$BIN" \
    --workers 4 --rt-exempt 0 --duration 0 --interval-ms 100 \
    --calibration 1 --mode orchestra --seed 404 \
    >"$TEST_DIR/signal-stop.csv" 2>"$TEST_DIR/signal-stop.log"
python3 "$VALIDATE" "$TEST_DIR/signal-stop.csv" --mode orchestra --schema "$SCHEMA_V7" --min-rows 5

if ps -eo args= | awk -v binary="$BIN" '$1 == binary { found=1 } END { exit found ? 0 : 1 }'; then
    echo "FAIL child process survived integration teardown" >&2
    exit 1
fi

echo "PASS integration: MAP_SHARED signal-publication contention/retry/death and high-frequency multiprocess coverage, v2-v7 validator contracts, policy lifecycle, conditioned coordination, CLI, cadence, experimental burst telemetry, rejected-frame control gate, tamper, and signal teardown"
