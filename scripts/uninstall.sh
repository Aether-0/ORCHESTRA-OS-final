#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=/dev/null
. "$SCRIPT_DIR/path_safety.sh"

PREFIX=${ORCHESTRA_PREFIX:-/usr/local}
CONFIG_DIR=${ORCHESTRA_CONFIG_DIR:-/etc/orchestra-os}
# Preserve configuration by default.  Deleting operator policy/configuration
# is an explicit action rather than an incidental uninstall side effect.
KEEP_CONFIG=1

usage() {
    echo "usage: $0 [--prefix PATH] [--config-dir PATH] [--keep-config] [--remove-config]"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            PREFIX=$2
            shift 2
            ;;
        --keep-config)
            KEEP_CONFIG=1
            shift
            ;;
        --remove-config)
            KEEP_CONFIG=0
            shift
            ;;
        --config-dir)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            CONFIG_DIR=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$PREFIX" in
    ""|/) echo "refusing unsafe uninstall prefix: '$PREFIX'" >&2; exit 2 ;;
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
        echo "uninstall from $PREFIX requires root; rerun with sudo" >&2
        exit 1
    fi
fi

BIN="$PREFIX/bin/orchestra"
LIB="$PREFIX/lib/orchestra-os"
MARKER="$LIB/.orchestra-install"
PACKAGE_MARKER="$LIB/.package-managed"
SERVICE_TARGET="$PREFIX/lib/systemd/system/orchestra.service"

if [ -f "$PACKAGE_MARKER" ] || [ -L "$PACKAGE_MARKER" ]; then
    echo "this installation is owned by a package manager; use apt, dnf, or apk" >&2
    echo "refusing source-tree uninstall of package-managed files" >&2
    exit 1
fi

if ! orchestra_safe_existing_dir "$LIB" ||
   [ ! -f "$MARKER" ] || [ -L "$MARKER" ] ||
   ! orchestra_safe_destination_file "$MARKER" ||
   [ "$(stat -c '%u' -- "$MARKER" 2>/dev/null)" != "$(id -u)" ] ||
   [ "$(stat -c '%a' -- "$MARKER" 2>/dev/null)" != 644 ] ||
   ! grep -qx 'ORCHESTRA_INSTALL_MANIFEST_V1' "$MARKER"; then
    echo "refusing to remove an unverified ORCHESTRA installation: $LIB" >&2
    echo "the installer marker is missing, unsafe, or does not identify this product" >&2
    exit 1
fi

expected_bin=$(awk -F= '$1 == "bin_sha256" { print $2; exit }' "$MARKER")
expected_script=$(awk -F= '$1 == "script_sha256" { print $2; exit }' "$MARKER")
[[ "$expected_bin" =~ ^[[:xdigit:]]{64}$ ]] || {
    echo "invalid ORCHESTRA install marker" >&2
    exit 1
}
[[ "$expected_script" =~ ^[[:xdigit:]]{64}$ ]] || {
    echo "invalid ORCHESTRA install marker" >&2
    exit 1
}

if [ -e "$BIN" ] || [ -L "$BIN" ]; then
    if [ ! -f "$BIN" ] || [ -L "$BIN" ] ||
       [ "$(sha256sum -- "$BIN" | awk '{print $1}')" != "$expected_bin" ]; then
        echo "refusing to remove a modified or symlinked command: $BIN" >&2
        exit 1
    fi
fi
if [ ! -f "$LIB/scripts/orchestra" ] || [ -L "$LIB/scripts/orchestra" ] ||
   [ "$(sha256sum -- "$LIB/scripts/orchestra" | awk '{print $1}')" != "$expected_script" ]; then
    echo "refusing to remove a modified installed control script" >&2
    exit 1
fi

# The marker records every installed file and directory.  Refuse to recurse
# through a package tree that has acquired an untracked path, preventing an
# uninstall from deleting operator-created files under the product prefix.
manifest_entries=$(mktemp "${TMPDIR:-/tmp}/orchestra-uninstall.XXXXXX")
actual_entries=$(mktemp "${TMPDIR:-/tmp}/orchestra-uninstall.XXXXXX")
cleanup() {
    rm -f -- "$manifest_entries" "$actual_entries"
}
trap cleanup EXIT HUP INT TERM
awk '/^entries_begin$/{inside=1; next} /^entries_end$/{inside=0} inside{print}' \
    "$MARKER" | LC_ALL=C sort > "$manifest_entries"
find "$LIB" -mindepth 1 ! -path "$MARKER" -printf '%y\t%P\n' |
    LC_ALL=C sort > "$actual_entries"
if ! cmp -s "$manifest_entries" "$actual_entries"; then
    echo "refusing recursive removal: installed tree contains unexpected paths" >&2
    echo "preserving $LIB for manual review" >&2
    exit 1
fi
if find "$LIB" -mindepth 1 -type l -print -quit | grep -q .; then
    echo "refusing recursive removal of a tree containing symlinks: $LIB" >&2
    exit 1
fi

managed_service=0
if [ "$PREFIX" = /usr/local ] && [ -e "$SERVICE_TARGET" ]; then
    if [ ! -L "$SERVICE_TARGET" ] && [ -f "$LIB/config/systemd/orchestra.service" ] &&
       cmp -s "$LIB/config/systemd/orchestra.service" "$SERVICE_TARGET"; then
        managed_service=1
    else
        echo "preserving non-matching or symlinked systemd unit: $SERVICE_TARGET" >&2
    fi
fi
if [ "$managed_service" -eq 1 ] && command -v systemctl >/dev/null 2>&1 &&
   systemctl is-active --quiet orchestra.service 2>/dev/null; then
    echo "stopping the ORCHESTRA-managed orchestra.service before removal"
    systemctl disable --now orchestra.service
fi

control_bin=""
if [ -x "$BIN" ]; then
    control_bin=$BIN
elif [ -x "$LIB/scripts/orchestra" ]; then
    control_bin="$LIB/scripts/orchestra"
fi
if [ -n "$control_bin" ] && [ -r /sys/kernel/sched_ext/state ]; then
    state=$(tr -d '\n' </sys/kernel/sched_ext/state)
    if [ "$state" != disabled ]; then
        echo "disabling ORCHESTRA before removal"
        "$control_bin" disable
    fi
fi

if [ -e "$BIN" ]; then
    orchestra_safe_destination_file "$BIN" || {
        echo "refusing unsafe command destination: $BIN" >&2
        exit 1
    }
    rm -f -- "$BIN"
fi
if [ "$managed_service" -eq 1 ]; then
    orchestra_safe_destination_file "$SERVICE_TARGET" || {
        echo "refusing unsafe systemd unit destination: $SERVICE_TARGET" >&2
        exit 1
    }
    rm -f -- "$SERVICE_TARGET"
fi
case "$LIB" in
    "$PREFIX/lib/orchestra-os")
        orchestra_safe_existing_dir "$LIB" || exit 1
        rm -rf -- "$LIB"
        ;;
    *) echo "refusing unexpected installation path: '$LIB'" >&2; exit 2 ;;
esac

if [ "$KEEP_CONFIG" -eq 0 ] && [ -d "$CONFIG_DIR" ]; then
    case "$CONFIG_DIR" in
        /etc/orchestra-os|/var/lib/orchestra-os|/var/tmp/orchestra-os-*|/tmp/orchestra-os-*)
            orchestra_safe_existing_dir "$CONFIG_DIR" || {
                echo "refusing unsafe configuration path: $CONFIG_DIR" >&2
                exit 1
            }
            find "$CONFIG_DIR" -mindepth 1 -type l -print -quit | grep -q . && {
                echo "refusing to remove symlink-containing configuration: $CONFIG_DIR" >&2
                exit 1
            }
            rm -rf -- "$CONFIG_DIR"
            ;;
        *)
            echo "refusing to remove unrecognized configuration path: '$CONFIG_DIR'" >&2
            exit 2
            ;;
    esac
else
    echo "configuration preserved at $CONFIG_DIR"
fi

echo "ORCHESTRA-OS files removed from $PREFIX"
