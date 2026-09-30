#!/usr/bin/env bash
set -euo pipefail

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$TEST_DIR/../.." && pwd)
PYTHONPYCACHEPREFIX=${PYTHONPYCACHEPREFIX:-/tmp/orchestra-os-security-pyc}
FUZZ_TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-security-fuzz.XXXXXX")

cleanup() {
    case "$FUZZ_TMP_DIR" in
        "${TMPDIR:-/tmp}"/orchestra-security-fuzz.*)
            rm -rf -- "$FUZZ_TMP_DIR"
            ;;
        *)
            echo "refusing to clean unexpected fuzz path: $FUZZ_TMP_DIR" >&2
            ;;
    esac
}
trap cleanup EXIT HUP INT TERM

PYTHONPYCACHEPREFIX="$PYTHONPYCACHEPREFIX" python3 "$TEST_DIR/test_policy_loader.py"
PYTHONPYCACHEPREFIX="$PYTHONPYCACHEPREFIX" python3 "$TEST_DIR/fuzz_policy_loader.py" --iterations 1000
cc -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wformat=2 -Werror -Wno-unused-function \
    -I"$ROOT/kernel/sched_ext/include" \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$TEST_DIR/fuzz_abi_state.c" -o "$FUZZ_TMP_DIR/fuzz_abi_state"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$FUZZ_TMP_DIR/fuzz_abi_state" 50000
bash "$TEST_DIR/test_install_paths.sh"
python3 "$ROOT/tests/unit/test_orchestra_scx_source.py"

bash "$TEST_DIR/test_release_verifier.sh"
python3 "$TEST_DIR/test_policy_liveness.py"
bash "$TEST_DIR/test_artifact_bundle.sh"
echo "PASS security regression suite"
