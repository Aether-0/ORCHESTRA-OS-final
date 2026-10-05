#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Full Test Suite ==="
cd "$(dirname "$0")/.."
make clean && make && make test
echo "=== Clang Static Analysis ==="
clang --analyze -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror -o /tmp/orch.plist orchestra_paper_cpu_demo/orchestra_paper_cpu.c
echo "Analyzer: $?"
