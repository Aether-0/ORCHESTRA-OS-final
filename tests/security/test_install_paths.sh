#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-install-security.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT HUP INT TERM

PREFIX="$WORK/prefix"
CONFIG="$WORK/config"

bash "$ROOT/scripts/install.sh" \
    --prefix "$PREFIX" --config-dir "$CONFIG" \
    --build-dir "$WORK/build" --no-build >/dev/null
[ -f "$PREFIX/lib/orchestra-os/.orchestra-install" ]
[ -x "$PREFIX/bin/orchestra" ]
[ -f "$CONFIG/safe.json" ]

# The manifest must make recursive removal refuse operator-created files.
printf '%s\n' 'operator data' > "$PREFIX/lib/orchestra-os/operator-data"
if bash "$ROOT/scripts/uninstall.sh" --prefix "$PREFIX" --config-dir "$CONFIG" \
    >"$WORK/uninstall-unexpected.out" 2>"$WORK/uninstall-unexpected.err"; then
    echo "uninstall removed an untracked file" >&2
    exit 1
fi
[ -e "$PREFIX/lib/orchestra-os/operator-data" ]
rm -f -- "$PREFIX/lib/orchestra-os/operator-data"

bash "$ROOT/scripts/uninstall.sh" --prefix "$PREFIX" --config-dir "$CONFIG" >/dev/null
[ ! -e "$PREFIX/lib/orchestra-os" ]
[ -d "$CONFIG" ]

# A destination symlink must never be followed by a privileged install.
SYMLINK_PREFIX="$WORK/symlink-prefix"
mkdir -p -- "$SYMLINK_PREFIX/bin"
ln -s -- "$WORK/should-not-be-overwritten" "$SYMLINK_PREFIX/bin/orchestra"
if bash "$ROOT/scripts/install.sh" \
    --prefix "$SYMLINK_PREFIX" --config-dir "$WORK/symlink-config" \
    --build-dir "$WORK/symlink-build" --no-build \
    >"$WORK/install-symlink.out" 2>"$WORK/install-symlink.err"; then
    echo "installer followed an existing command symlink" >&2
    exit 1
fi
[ -L "$SYMLINK_PREFIX/bin/orchestra" ]
[ ! -e "$WORK/should-not-be-overwritten" ]

# A root-capable install must reject group/world-writable kernel artifacts
# before creating a destination prefix. This covers the --no-build trust
# boundary where an operator supplies an externally built artifact set.
UNSAFE_BUILD="$WORK/unsafe-build"
mkdir -p -- "$UNSAFE_BUILD"
for artifact in orchestra_bridge orchestra_loader orchestra_scx_stage7.bpf.o; do
    printf '%s\n' 'test artifact' >"$UNSAFE_BUILD/$artifact"
    chmod 0664 -- "$UNSAFE_BUILD/$artifact"
done
printf 'bridge_sha256=%s\nloader_sha256=%s\nbpf_object_sha256=%s\n' \
    "$(sha256sum "$UNSAFE_BUILD/orchestra_bridge" | awk '{print $1}')" \
    "$(sha256sum "$UNSAFE_BUILD/orchestra_loader" | awk '{print $1}')" \
    "$(sha256sum "$UNSAFE_BUILD/orchestra_scx_stage7.bpf.o" | awk '{print $1}')" \
    >"$UNSAFE_BUILD/build-manifest.txt"
chmod 0644 -- "$UNSAFE_BUILD/build-manifest.txt"
if bash "$ROOT/scripts/install.sh" \
    --prefix "$WORK/unsafe-prefix" --config-dir "$WORK/unsafe-config" \
    --build-dir "$UNSAFE_BUILD" --with-kernel --no-build \
    >"$WORK/install-unsafe.out" 2>"$WORK/install-unsafe.err"; then
    echo "installer accepted a writable kernel artifact" >&2
    exit 1
fi
[ ! -e "$WORK/unsafe-prefix" ]

echo "PASS installer/uninstaller path-security regressions"
