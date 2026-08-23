#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Stage 9 Benchmark ==="
cd "$(dirname "$0")/.."
output_dir=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-stage9.XXXXXX")
cleanup() { rm -rf -- "$output_dir"; }
trap cleanup EXIT HUP INT TERM
python3 benchmarks/stage8/benchmark_runner.py \
  --manifest experiments/manifests/paper_cpu_exploratory_v5.json \
  --output-dir "$output_dir" 2>/dev/null || echo "See benchmarks/stage9/"
