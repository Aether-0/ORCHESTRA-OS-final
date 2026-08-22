#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
"$ROOT/scripts/orchestra" version
"$ROOT/scripts/orchestra" check-system
"$ROOT/scripts/orchestra" status
