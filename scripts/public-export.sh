#!/usr/bin/env bash
set -euo pipefail

# Create the intentionally curated source tree used for public releases.
# Private real-machine evidence and machine-local files must never enter the
# public repository or source archive.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

if [ "$#" -ne 1 ]; then
    echo "usage: $0 DEST_DIR" >&2
    exit 2
fi
DEST=$1

case "$DEST" in
    /*) ;;
    *) echo "destination must be an absolute path: $DEST" >&2; exit 2 ;;
esac
case "$DEST" in
    "$ROOT"|"$ROOT"/*)
        echo "refusing export destination inside the source tree: $DEST" >&2
        exit 2
        ;;
esac

if [ -e "$DEST" ]; then
    [ -d "$DEST" ] || { echo "destination is not a directory: $DEST" >&2; exit 2; }
    if [ -n "$(find "$DEST" -mindepth 1 -print -quit)" ]; then
        echo "destination must be empty: $DEST" >&2
        exit 2
    fi
else
    mkdir -p -- "$DEST"
fi

# Keep the public tree focused on maintained source, documentation, tests,
# packaging, and CI. The repository's historical checklists and agent notes
# contain machine-specific campaign material and are deliberately omitted.
paths=(
    CITATION.cff
    CODE_OF_CONDUCT.md
    CONTRIBUTING.md
    LICENSE
    LIMITATIONS.md
    Makefile
    NOTICE
    README.md
    SUPPORT.md
    VERSION
    .gitignore
    .github/SECURITY.md
    .github/workflows/build-sched-ext.yml
    .github/workflows/ci.yml
    .github/workflows/release.yml
    benchmarks
    config
    docs
    examples
    experiments
    kernel
    orchestra_paper_cpu_demo
    packaging
    research
    scripts
    tests
    tools
    userspace
)

tar -C "$ROOT" \
    --exclude='./.git' \
    --exclude='./artifacts' \
    --exclude='./release' \
    --exclude='*.zip' \
    --exclude='*.pdf' \
    --exclude='*.html' \
    --exclude='*.log' \
    --exclude='./C:\\FastMCP\\boot.log' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.so' \
    --exclude='*.bpf.o' \
    --exclude='*.bpf.skel.h' \
    --exclude='vmlinux.h' \
    --exclude='orchestra_paper_cpu_demo/orchestra_paper_cpu' \
    --exclude='research/experiments/process-group-prototype/orchestra_real_cpu' \
    --exclude='tools/benchmark/signal_publication_microbenchmark' \
    --exclude='tools/benchmark/cpu_3min_bench' \
    --exclude='tools/benchmark/single_worker_stress' \
    --exclude='*/__pycache__' \
    --exclude='*/.pytest_cache' \
    -cf - "${paths[@]}" | tar -C "$DEST" -xf -

for required in README.md LICENSE VERSION Makefile docs/status/FINAL_PRODUCT_STATUS.md packaging/build-package.sh \
    scripts/security-scan.sh .github/workflows/release.yml; do
    [ -f "$DEST/$required" ] || {
        echo "required public export path missing: $required" >&2
        exit 1
    }
done

if forbidden=$(find "$DEST" -type f \( \
    -name '*.zip' -o -name '*.pdf' -o -name '*.html' -o -name '*.log' -o \
    -name 'vmlinux.h' -o -name '*.bpf.o' -o -name '*.bpf.skel.h' -o \
    -name 'orchestra_loader' -o -name 'orchestra_bridge' -o -name 'fixed_work' -o \
    -name 'orchestra_paper_cpu' -o -name 'orchestra_real_cpu' -o \
    -name 'signal_publication_microbenchmark' -o -name 'cpu_3min_bench' -o \
    -name 'single_worker_stress' -o -name '*.o' -o -name '*.a' -o -name '*.so' \) \
    -print -quit); then
    if [ -n "$forbidden" ]; then
        echo "forbidden generated/private file in export: $forbidden" >&2
        exit 1
    fi
fi

if sensitive=$(grep -RInI -E \
    -e '/home/[A-Za-z0-9_.-]+/' \
    -e '/root/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+' \
    "$DEST" 2>/dev/null); then
    echo "machine-local or private repository reference in export:" >&2
    echo "$sensitive" >&2
    exit 1
fi

# Git tracks executable bits; normalize public file/directory permissions so
# archives reproduce across checkouts with different group-write defaults.
find "$DEST" -type d -exec chmod 0755 {} +
find "$DEST" -type f ! -perm -100 ! -perm -010 ! -perm -001 -exec chmod 0644 {} +
find "$DEST" -type f \( -perm -100 -o -perm -010 -o -perm -001 \) -exec chmod 0755 {} +
echo "PUBLIC_EXPORT_PASS $DEST"
