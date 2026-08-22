#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PREFIX=${ORCHESTRA_PREFIX:-/usr/local}
BUILD_DIR=${ORCHESTRA_BUILD_DIR:-/var/tmp/orchestra-os-build-$(id -u)}
CONFIG_DIR=${ORCHESTRA_CONFIG_DIR:-/etc/orchestra-os}
no_build=0
with_kernel=0

usage() { echo "usage: $0 [--prefix PATH] [--build-dir PATH] [--config-dir PATH] [--with-kernel] [--no-build]"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; PREFIX=$2; shift 2 ;;
        --build-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; BUILD_DIR=$2; shift 2 ;;
        --config-dir) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; CONFIG_DIR=$2; shift 2 ;;
        --with-kernel) with_kernel=1; shift ;;
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

safe_source_artifact() {
    local path=$1 permissions

    [ -f "$path" ] || return 1
    [ ! -L "$path" ] || return 1
    permissions=$(stat -c '%A' -- "$path" 2>/dev/null) || return 1
    [ "${permissions:5:1}" != w ] || return 1
    [ "${permissions:8:1}" != w ] || return 1
}

require_source_artifact() {
    local label=$1 path=$2
    if ! safe_source_artifact "$path"; then
        echo "unsafe $label source artifact: $path" >&2
        echo "refusing to copy a missing, symlinked, or group/world-writable artifact as root" >&2
        exit 1
    fi
}

require_plain_source() {
    local label=$1 path=$2
    if [ ! -f "$path" ] || [ -L "$path" ]; then
        echo "unsafe $label source: $path" >&2
        echo "refusing to follow a missing or symlinked source as root" >&2
        exit 1
    fi
}

manifest_hash_matches() {
    local key=$1 path=$2 manifest=$3 expected actual
    expected=$(awk -F= -v wanted="$key" '$1 == wanted { print $2; exit }' "$manifest")
    [[ "$expected" =~ ^[[:xdigit:]]{64}$ ]] || return 1
    actual=$(sha256sum -- "$path" | awk '{print $1}')
    [ "$actual" = "$expected" ]
}

BIN_DIR="$PREFIX/bin"
LIB_ROOT="$PREFIX/lib/orchestra-os"
if [ "$no_build" -eq 0 ]; then
    build_args=(--userspace --bridge)
    if [ "$with_kernel" -eq 1 ]; then
        build_args+=(--kernel)
    fi
    ORCHESTRA_BUILD_DIR="$BUILD_DIR" bash "$REPO_ROOT/scripts/build.sh" "${build_args[@]}"
fi

if [ "$with_kernel" -eq 1 ]; then
    for required_artifact in orchestra_bridge orchestra_loader orchestra_scx_stage7.bpf.o build-manifest.txt; do
        if [ ! -f "$BUILD_DIR/$required_artifact" ]; then
            echo "kernel installation requested but artifact is missing: $BUILD_DIR/$required_artifact" >&2
            echo "build a target-matched kernel artifact or omit --with-kernel for observer-only installation" >&2
            exit 1
        fi
        require_source_artifact "$required_artifact" "$BUILD_DIR/$required_artifact"
    done
    if ! manifest_hash_matches bridge_sha256 "$BUILD_DIR/orchestra_bridge" \
        "$BUILD_DIR/build-manifest.txt" ||
       ! manifest_hash_matches loader_sha256 "$BUILD_DIR/orchestra_loader" \
        "$BUILD_DIR/build-manifest.txt" ||
       ! manifest_hash_matches bpf_object_sha256 "$BUILD_DIR/orchestra_scx_stage7.bpf.o" \
        "$BUILD_DIR/build-manifest.txt"; then
        echo "kernel artifact hash does not match build-manifest.txt" >&2
        echo "refusing installation; rebuild the target-matched artifact set" >&2
        exit 1
    fi
fi

if [ -f "$BUILD_DIR/orchestra_bridge" ]; then
    require_source_artifact "bridge" "$BUILD_DIR/orchestra_bridge"
fi

install -d -m 0755 "$BIN_DIR" "$LIB_ROOT/scripts" "$LIB_ROOT/build" \
    "$LIB_ROOT/config/examples" "$LIB_ROOT/config/systemd" "$LIB_ROOT/docs" "$CONFIG_DIR"
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
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/orchestra_loader" ]; then
    install -m 0755 "$BUILD_DIR/orchestra_loader" "$LIB_ROOT/build/orchestra_loader"
fi
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ]; then
    install -m 0644 "$BUILD_DIR/orchestra_scx_stage7.bpf.o" "$LIB_ROOT/build/orchestra_scx_stage7.bpf.o"
fi
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/build-manifest.txt" ]; then
    install -m 0644 "$BUILD_DIR/build-manifest.txt" "$LIB_ROOT/build/build-manifest.txt"
fi
if [ -d "$REPO_ROOT/config/examples" ]; then
    for config_file in "$REPO_ROOT"/config/examples/*.json; do
        [ -f "$config_file" ] || continue
        require_plain_source "configuration" "$config_file"
        install -m 0644 "$config_file" "$LIB_ROOT/config/examples/$(basename "$config_file")"
        target="$CONFIG_DIR/$(basename "$config_file")"
        if [ -L "$target" ]; then
            echo "refusing to follow a symlinked configuration target: $target" >&2
            exit 1
        fi
        if [ ! -e "$target" ]; then install -m 0644 "$config_file" "$target"; fi
    done
fi
if [ -f "$REPO_ROOT/README.md" ]; then
    install -m 0644 "$REPO_ROOT/README.md" "$LIB_ROOT/README.md"
fi
if [ -d "$REPO_ROOT/docs" ]; then
    while IFS= read -r -d '' document; do
        relative=${document#"$REPO_ROOT/docs/"}
        destination="$LIB_ROOT/docs/$relative"
        install -d -m 0755 "$(dirname -- "$destination")"
        install -m 0644 "$document" "$destination"
    done < <(find "$REPO_ROOT/docs" -type f -print0)
fi
if [ "$PREFIX" = /usr/local ] && [ -f "$REPO_ROOT/config/systemd/orchestra.service" ]; then
    require_plain_source "systemd unit" "$REPO_ROOT/config/systemd/orchestra.service"
    service_target="$PREFIX/lib/systemd/system/orchestra.service"
    if [ -L "$service_target" ]; then
        echo "refusing to follow a symlinked systemd unit: $service_target" >&2
        exit 1
    fi
    if [ -e "$service_target" ] && ! cmp -s "$REPO_ROOT/config/systemd/orchestra.service" "$service_target"; then
        echo "refusing to overwrite an existing non-ORCHESTRA systemd unit: $service_target" >&2
        exit 1
    fi
    install -d -m 0755 "$PREFIX/lib/systemd/system"
    install -m 0644 "$REPO_ROOT/config/systemd/orchestra.service" \
        "$LIB_ROOT/config/systemd/orchestra.service"
    install -m 0644 "$REPO_ROOT/config/systemd/orchestra.service" \
        "$service_target"
fi

echo "installed ORCHESTRA-OS into $LIB_ROOT"
echo "command=$BIN_DIR/orchestra"
echo "config=$CONFIG_DIR"
if [ "$with_kernel" -eq 1 ]; then
    echo "kernel_artifacts=installed"
else
    echo "kernel_artifacts=not requested (observer-only package)"
fi
echo "scheduler=not enabled by installer"
