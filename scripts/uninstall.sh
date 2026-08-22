#!/usr/bin/env bash
set -euo pipefail

PREFIX=${ORCHESTRA_PREFIX:-/usr/local}
CONFIG_DIR=${ORCHESTRA_CONFIG_DIR:-/etc/orchestra-os}
KEEP_CONFIG=0

usage() {
    echo "usage: $0 [--prefix PATH] [--config-dir PATH] [--keep-config]"
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
SERVICE_TARGET="$PREFIX/lib/systemd/system/orchestra.service"

if [ "$PREFIX" = /usr/local ] && command -v systemctl >/dev/null 2>&1 &&
   systemctl is-active --quiet orchestra.service 2>/dev/null; then
    echo "stopping orchestra.service before removal"
    systemctl disable --now orchestra.service
fi

if [ -x "$BIN" ] && [ -r /sys/kernel/sched_ext/state ]; then
    state=$(tr -d '\n' </sys/kernel/sched_ext/state)
    if [ "$state" != disabled ]; then
        echo "disabling ORCHESTRA before removal"
        "$BIN" disable
    fi
fi

rm -f -- "$BIN"
if [ "$PREFIX" = /usr/local ] && [ -e "$SERVICE_TARGET" ]; then
    if [ -f "$LIB/config/systemd/orchestra.service" ] &&
       cmp -s "$LIB/config/systemd/orchestra.service" "$SERVICE_TARGET"; then
        rm -f -- "$SERVICE_TARGET"
    else
        echo "preserving non-matching systemd unit: $SERVICE_TARGET" >&2
    fi
fi
case "$LIB" in
    "$PREFIX/lib/orchestra-os") rm -rf -- "$LIB" ;;
    *) echo "refusing unexpected installation path: '$LIB'" >&2; exit 2 ;;
esac

if [ "$KEEP_CONFIG" -eq 0 ] && [ -d "$CONFIG_DIR" ]; then
    case "$CONFIG_DIR" in
        /etc/orchestra-os|/var/lib/orchestra-os|/var/tmp/orchestra-os-*|/tmp/orchestra-os-*)
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
