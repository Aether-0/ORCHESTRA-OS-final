#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
if [ -r "$SCRIPT_DIR/path_safety.sh" ]; then
    # shellcheck source=/dev/null
    . "$SCRIPT_DIR/path_safety.sh"
else
    echo "missing path-safety helper: $SCRIPT_DIR/path_safety.sh" >&2
    exit 1
fi
BUILD_DIR=${ORCHESTRA_BUILD_DIR:-/var/tmp/orchestra-os-build-$(id -u)}
want_userspace=0
want_bridge=0
want_kernel=0

usage() {
    cat <<'EOF'
usage: scripts/build.sh [--userspace] [--bridge] [--kernel] [--all]

The default builds the portable userspace and bridge. --kernel additionally
requires a target-matched kernel source, BTF, and BPF toolchain; it never
modifies the repository or installs a kernel.
EOF
}

for argument in "$@"; do
    case "$argument" in
        --userspace) want_userspace=1 ;;
        --bridge) want_bridge=1 ;;
        --kernel) want_kernel=1 ;;
        --all) want_userspace=1; want_bridge=1; want_kernel=1 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "unknown option: $argument" >&2; usage >&2; exit 2 ;;
    esac
done
if [ "$want_userspace$want_bridge$want_kernel" = 000 ]; then
    want_userspace=1
    want_bridge=1
fi

case "$BUILD_DIR" in
    /var/tmp/orchestra-os-build-*|/tmp/orchestra-os-build-*|/*) ;;
    *) echo "build directory must be an absolute path" >&2; exit 2 ;;
esac
case "$BUILD_DIR" in
    "$REPO_ROOT"|"$REPO_ROOT"/*)
        echo "refusing a build directory inside the repository: $BUILD_DIR" >&2
        exit 2
        ;;
esac
if ! orchestra_ensure_private_dir "$BUILD_DIR"; then
    echo "refusing unsafe build directory (symlink, ownership, or writable-parent check failed): $BUILD_DIR" >&2
    exit 1
fi

if [ "$want_userspace" -eq 1 ]; then
    make -C "$REPO_ROOT" userspace
fi
if [ "$want_bridge" -eq 1 ]; then
    ORCHESTRA_BUILD_DIR="$BUILD_DIR" make -C "$REPO_ROOT" bridge
fi
if [ "$want_kernel" -eq 1 ]; then
    ORCHESTRA_BUILD_DIR="$BUILD_DIR" \
    ORCHESTRA_BPF_SOURCE="${ORCHESTRA_BPF_SOURCE:-$REPO_ROOT/kernel/sched_ext/bpf/orchestra_sched.bpf.c}" \
        bash "$REPO_ROOT/kernel/sched_ext/scripts/build_stage7_out_of_tree.sh"
fi

echo "build_dir=$BUILD_DIR"
echo "BUILD_COMPLETE"
