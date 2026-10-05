#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Stage 9 Benchmark ==="
cd "$(dirname "$0")/.."
python3 benchmarks/stage8/benchmark_runner.py --manifest experiments/manifests/paper_cpu_exploratory_v5.json --output-dir /tmp/stage9-bench 2>/dev/null || echo "See benchmarks/stage9/"
