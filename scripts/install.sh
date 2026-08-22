#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PREFIX=${ORCHESTRA_PREFIX:-/usr/local}
BUILD_DIR=${ORCHESTRA_BUILD_DIR:-/var/tmp/orchestra-os-build-$(id -u)}
CONFIG_DIR=${ORCHESTRA_CONFIG_DIR:-/etc/orchestra-os}
no_build=0

usage() { echo "usage: $0 [--prefix PATH] [--build-dir PATH] [--config-dir PATH] [--no-build]"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; PREFIX=$2; shift 2 ;;
        --build-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; BUILD_DIR=$2; shift 2 ;;
        --config-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; CONFIG_DIR=$2; shift 2 ;;
        --no-build) no_build=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$PREFIX" in
    ""|/) echo "refusing unsafe installation prefix: '$PREFIX'" >&2; exit 2 ;;
esac
case "$CONFIG_DIR" in
    ""|/) echo "refusing unsafe configuration path: '$CONFIG_DIR'" >&2; exit 2 ;;
esac
if [ "$(id -u)" -ne 0 ]; then
    writable_parent=$PREFIX
    while [ ! -e "$writable_parent" ] && [ "$writable_parent" != / ]; do
        writable_parent=$(dirname -- "$writable_parent")
    done
    if [ ! -w "$writable_parent" ]; then
        echo "installation to $PREFIX requires root; rerun with sudo" >&2
        exit 1
    fi
fi

BIN_DIR="$PREFIX/bin"
LIB_ROOT="$PREFIX/lib/orchestra-os"
if [ "$no_build" -eq 0 ]; then
    ORCHESTRA_BUILD_DIR="$BUILD_DIR" bash "$REPO_ROOT/scripts/build.sh" --userspace --bridge
fi

install -d -m 0755 "$BIN_DIR" "$LIB_ROOT/scripts" "$LIB_ROOT/build" \
    "$LIB_ROOT/config/examples" "$LIB_ROOT/docs" "$CONFIG_DIR"
install -m 0755 "$REPO_ROOT/scripts/orchestra" "$BIN_DIR/orchestra"
install -m 0755 "$REPO_ROOT/scripts/orchestra" "$LIB_ROOT/scripts/orchestra"
install -m 0755 "$REPO_ROOT/scripts/check-system.sh" "$LIB_ROOT/scripts/check-system.sh"
install -m 0755 "$REPO_ROOT/scripts/build.sh" "$LIB_ROOT/scripts/build.sh"
install -m 0755 "$REPO_ROOT/scripts/install.sh" "$LIB_ROOT/scripts/install.sh"
install -m 0755 "$REPO_ROOT/scripts/uninstall.sh" "$LIB_ROOT/scripts/uninstall.sh"
install -m 0755 "$REPO_ROOT/scripts/policy_load.py" "$LIB_ROOT/scripts/policy_load.py"
install -m 0644 "$REPO_ROOT/VERSION" "$LIB_ROOT/VERSION"
if [ -f "$BUILD_DIR/orchestra_bridge" ]; then
    install -m 0755 "$BUILD_DIR/orchestra_bridge" "$LIB_ROOT/build/orchestra_bridge"
fi
if [ -f "$BUILD_DIR/orchestra_loader" ]; then
    install -m 0755 "$BUILD_DIR/orchestra_loader" "$LIB_ROOT/build/orchestra_loader"
fi
if [ -f "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ]; then
    install -m 0644 "$BUILD_DIR/orchestra_scx_stage7.bpf.o" "$LIB_ROOT/build/orchestra_scx_stage7.bpf.o"
fi
if [ -d "$REPO_ROOT/config/examples" ]; then
    for config_file in "$REPO_ROOT"/config/examples/*.json; do
        [ -f "$config_file" ] || continue
        install -m 0644 "$config_file" "$LIB_ROOT/config/examples/$(basename "$config_file")"
        target="$CONFIG_DIR/$(basename "$config_file")"
        if [ ! -e "$target" ]; then install -m 0644 "$config_file" "$target"; fi
    done
fi
if [ -f "$REPO_ROOT/README.md" ]; then
    install -m 0644 "$REPO_ROOT/README.md" "$LIB_ROOT/README.md"
fi
if [ -d "$REPO_ROOT/docs" ]; then
    cp -a "$REPO_ROOT/docs/." "$LIB_ROOT/docs/"
fi

echo "installed ORCHESTRA-OS into $LIB_ROOT"
echo "command=$BIN_DIR/orchestra"
echo "config=$CONFIG_DIR"
echo "scheduler=not enabled by installer"
