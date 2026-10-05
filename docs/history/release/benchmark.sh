#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Benchmark ==="
cd "$(dirname "$0")/.."
bash benchmarks/stage9/benchmark_compare.sh 2>/dev/null || echo "See benchmarks/stage9/ for detailed runner"
