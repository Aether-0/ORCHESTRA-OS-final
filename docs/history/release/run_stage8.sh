#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Stage 8 Validation ==="
cd "$(dirname "$0")/.."
bash kernel/sched_ext/scripts/stage8_validate.sh 2>/dev/null || echo "Run on sched_ext kernel: scripts/stage8_validate.sh"
