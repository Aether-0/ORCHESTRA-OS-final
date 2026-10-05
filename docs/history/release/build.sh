#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Build ==="
cd "$(dirname "$0")/.."
make clean && make && make test
echo "Build complete."
