#!/usr/bin/env bash
# Deprecated command name; forward arguments to the supported entry point.
set -euo pipefail
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec bash "$SCRIPT_DIR/validate_runtime.sh" "$@"
