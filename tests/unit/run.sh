#!/usr/bin/env bash
set -euo pipefail

unit_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$unit_dir/../.." && pwd)
test_source="$unit_dir/test_orchestra_paper_cpu.c"
publication_stress_source="$unit_dir/test_signal_publication_stress.c"
bridge_test_source="$unit_dir/test_orchestra_bridge.c"
coordination_test_source="$unit_dir/test_orchestra_coordination.c"
test_tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-unit.XXXXXX")

cleanup() {
    case "$test_tmp_dir" in
        "${TMPDIR:-/tmp}"/orchestra-unit.*)
            rm -rf -- "$test_tmp_dir"
            ;;
        *)
            printf 'Refusing to clean unexpected temporary path: %s\n' "$test_tmp_dir" >&2
            ;;
    esac
}
trap cleanup EXIT HUP INT TERM

common_flags=(
    -O1
    -g
    -std=c11
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wformat=2
    -Werror
)

gcc_binary="$test_tmp_dir/test_orchestra_gcc"
gcc "${common_flags[@]}" \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$test_source" -lm -o "$gcc_binary"

printf '%s\n' 'Running GCC ASan+UBSan unit tests'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$gcc_binary"

publication_stress_gcc_binary="$test_tmp_dir/test_signal_publication_stress_gcc"
gcc "${common_flags[@]}" -pthread \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$publication_stress_source" -lm -o "$publication_stress_gcc_binary"
printf '%s\n' 'Running GCC ASan+UBSan generation-publication thread stress'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$publication_stress_gcc_binary"

bridge_gcc_binary="$test_tmp_dir/test_orchestra_bridge_gcc"
gcc "${common_flags[@]}" \
    -Wno-unused-function \
    -I"$repo_dir/kernel/sched_ext/include" \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$bridge_test_source" -o "$bridge_gcc_binary"
printf '%s\n' 'Running GCC ASan+UBSan bridge parser/ABI tests'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$bridge_gcc_binary"

coordination_gcc_binary="$test_tmp_dir/test_orchestra_coordination_gcc"
gcc "${common_flags[@]}" \
    -I"$repo_dir/kernel/sched_ext/include" \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$coordination_test_source" -o "$coordination_gcc_binary"
printf '%s\n' 'Running GCC ASan+UBSan native coordination/controller tests'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$coordination_gcc_binary"

legacy_gcc_binary="$test_tmp_dir/test_orchestra_gcc_legacy"
gcc "${common_flags[@]}" \
    -DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=1 \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    "$test_source" -lm -o "$legacy_gcc_binary"

printf '%s\n' 'Running GCC ASan+UBSan legacy-publication reference unit tests'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30s "$legacy_gcc_binary"

printf '%s\n' 'Checking direct GCC legacy-reference source build'
gcc "${common_flags[@]}" \
    -DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=1 \
    -fsyntax-only "$repo_dir/orchestra_paper_cpu_demo/orchestra_paper_cpu.c"

if command -v clang >/dev/null 2>&1; then
    clang_binary="$test_tmp_dir/test_orchestra_clang"
    clang "${common_flags[@]}" "$test_source" -lm -o "$clang_binary"
    printf '%s\n' 'Running Clang policy-warning unit tests'
    timeout 30s "$clang_binary"

    publication_stress_clang_binary="$test_tmp_dir/test_signal_publication_stress_clang"
    clang "${common_flags[@]}" -pthread \
        "$publication_stress_source" -lm -o "$publication_stress_clang_binary"
    printf '%s\n' 'Running Clang generation-publication thread stress'
    timeout 30s "$publication_stress_clang_binary"

    bridge_clang_binary="$test_tmp_dir/test_orchestra_bridge_clang"
    clang "${common_flags[@]}" -Wno-unused-function \
        -I"$repo_dir/kernel/sched_ext/include" \
        "$bridge_test_source" -o "$bridge_clang_binary"
    printf '%s\n' 'Running Clang bridge parser/ABI tests'
    timeout 30s "$bridge_clang_binary"

    coordination_clang_binary="$test_tmp_dir/test_orchestra_coordination_clang"
    clang "${common_flags[@]}" \
        -I"$repo_dir/kernel/sched_ext/include" \
        "$coordination_test_source" -o "$coordination_clang_binary"
    printf '%s\n' 'Running Clang native coordination/controller tests'
    timeout 30s "$coordination_clang_binary"

    legacy_clang_binary="$test_tmp_dir/test_orchestra_clang_legacy"
    clang "${common_flags[@]}" \
        -DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=1 \
        "$test_source" -lm -o "$legacy_clang_binary"
    printf '%s\n' 'Running Clang legacy-publication reference unit tests'
    timeout 30s "$legacy_clang_binary"

    printf '%s\n' 'Checking direct Clang legacy-reference source build'
    clang "${common_flags[@]}" \
        -DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=1 \
        -fsyntax-only "$repo_dir/orchestra_paper_cpu_demo/orchestra_paper_cpu.c"
fi

printf '%s\n' 'Running benchmark-validator unit tests'
PYTHONPYCACHEPREFIX="$test_tmp_dir/pycache" \
    python3 "$unit_dir/test_benchmark_validator.py"

printf '%s\n' 'Running signal-publication microbenchmark-runner unit tests'
PYTHONPYCACHEPREFIX="$test_tmp_dir/pycache" \
    python3 "$unit_dir/test_signal_publication_microbenchmark_runner.py"

printf '%s\n' 'Running sched_ext source safety invariants'
PYTHONPYCACHEPREFIX="$test_tmp_dir/pycache" \
    python3 "$unit_dir/test_orchestra_scx_source.py"

printf '%s\n' 'All requested unit-test compiler passes completed'
