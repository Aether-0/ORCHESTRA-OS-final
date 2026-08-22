#!/usr/bin/env bash
set -euo pipefail

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$TEST_DIR/../.." && pwd)
PYTHONPYCACHEPREFIX=${PYTHONPYCACHEPREFIX:-/tmp/orchestra-os-security-pyc}

PYTHONPYCACHEPREFIX="$PYTHONPYCACHEPREFIX" python3 "$TEST_DIR/test_policy_loader.py"
PYTHONPYCACHEPREFIX="$PYTHONPYCACHEPREFIX" python3 "$TEST_DIR/fuzz_policy_loader.py" --iterations 1000
bash "$TEST_DIR/test_install_paths.sh"
python3 "$ROOT/tests/unit/test_orchestra_scx_source.py"

echo "PASS security regression suite"
