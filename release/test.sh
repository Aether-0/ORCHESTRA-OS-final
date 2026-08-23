#!/bin/bash
set -euo pipefail
echo "=== ORCHESTRA-OS: Full Test Suite ==="
cd "$(dirname "$0")/.."
make clean && make && make test
echo "=== Clang Static Analysis ==="
analysis_output=$(mktemp "${TMPDIR:-/tmp}/orchestra-analyze.XXXXXX.plist")
cleanup() { rm -f -- "$analysis_output"; }
trap cleanup EXIT HUP INT TERM
clang --analyze -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror -o "$analysis_output" orchestra_paper_cpu_demo/orchestra_paper_cpu.c
echo "Analyzer: $?"
