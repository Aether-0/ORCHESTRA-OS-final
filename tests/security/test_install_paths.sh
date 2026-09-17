#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-install-security.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT HUP INT TERM

PREFIX="$WORK/prefix"
CONFIG="$WORK/config"

# A privileged build must never write product artifacts into the filesystem
# root, even when the caller supplies an absolute build directory.
if ORCHESTRA_BUILD_DIR=/ bash "$ROOT/scripts/build.sh" --userspace \
    >"$WORK/root-build.out" 2>"$WORK/root-build.err"; then
    echo "builder accepted the filesystem root as an output directory" >&2
    exit 1
fi
grep -q 'filesystem root' "$WORK/root-build.err"
if make -s -C "$ROOT" ORCHESTRA_BUILD_DIR=/ bridge \
    >"$WORK/root-bridge.out" 2>"$WORK/root-bridge.err"; then
    echo "Makefile bridge accepted the filesystem root as an output directory" >&2
    exit 1
fi
grep -q 'filesystem root' "$WORK/root-bridge.err"

bash "$ROOT/scripts/install.sh" \
    --prefix "$PREFIX" --config-dir "$CONFIG" \
    --build-dir "$WORK/build" --no-build >/dev/null
[ -f "$PREFIX/lib/orchestra-os/.orchestra-install" ]
[ -x "$PREFIX/bin/orchestra" ]
[ -f "$PREFIX/lib/orchestra-os/LICENSE" ]
[ -f "$CONFIG/safe.json" ]

# The manifest must make recursive removal refuse operator-created files.
printf '%s\n' 'operator data' > "$PREFIX/lib/orchestra-os/operator-data"
if bash "$ROOT/scripts/install.sh" \
    --prefix "$PREFIX" --config-dir "$CONFIG" \
    --build-dir "$WORK/build" --no-build \
    >"$WORK/reinstall-unexpected.out" 2>"$WORK/reinstall-unexpected.err"; then
    echo "reinstall adopted an untracked operator file" >&2
    exit 1
fi
[ -e "$PREFIX/lib/orchestra-os/operator-data" ]
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

# A supplied portable executable must be installed inside the manifest-tracked
# product tree and removed by the normal uninstall path.
PORTABLE_BUILD="$WORK/portable-build"
PORTABLE_PREFIX="$WORK/portable-prefix"
PORTABLE_CONFIG="$WORK/portable-config"
mkdir -p -- "$PORTABLE_BUILD"
printf '#!/usr/bin/env sh\nexit 0\n' >"$PORTABLE_BUILD/orchestra_paper_cpu"
chmod 0755 -- "$PORTABLE_BUILD/orchestra_paper_cpu"
bash "$ROOT/scripts/install.sh" \
    --prefix "$PORTABLE_PREFIX" --config-dir "$PORTABLE_CONFIG" \
    --build-dir "$PORTABLE_BUILD" --no-build >/dev/null
[ -x "$PORTABLE_PREFIX/lib/orchestra-os/build/orchestra_paper_cpu" ]
"$PORTABLE_PREFIX/bin/orchestra" paper-cpu --help
PACKAGE_REINSTALL="$WORK/package-reinstall"
PACKAGE_REINSTALL_CONFIG="$WORK/package-reinstall-config"
"$PORTABLE_PREFIX/bin/orchestra" install \
    --prefix "$PACKAGE_REINSTALL" --config-dir "$PACKAGE_REINSTALL_CONFIG" \
    --build-dir "$PORTABLE_PREFIX/lib/orchestra-os/build" >/dev/null
[ -x "$PACKAGE_REINSTALL/lib/orchestra-os/build/orchestra_paper_cpu" ]
"$PACKAGE_REINSTALL/bin/orchestra" uninstall \
    --prefix "$PACKAGE_REINSTALL" --config-dir "$PACKAGE_REINSTALL_CONFIG" \
    >/dev/null
bash "$ROOT/scripts/uninstall.sh" \
    --prefix "$PORTABLE_PREFIX" --config-dir "$PORTABLE_CONFIG" >/dev/null
[ ! -e "$PORTABLE_PREFIX/lib/orchestra-os" ]

# A sudo installation must accept a private artifact owned by the original
# caller. This is the documented build-as-user/install-as-root workflow.
if [ "$(id -u)" -eq 0 ]; then
    SUDO_BUILD="$WORK/sudo-build"
    SUDO_PREFIX="$WORK/sudo-prefix"
    SUDO_CONFIG="$WORK/sudo-config"
    SUDO_TEST_UID=65534
    mkdir -p -- "$SUDO_BUILD"
    printf '#!/usr/bin/env sh\nexit 0\n' >"$SUDO_BUILD/orchestra_paper_cpu"
    chmod 0755 -- "$SUDO_BUILD/orchestra_paper_cpu"
    chown "$SUDO_TEST_UID" -- "$SUDO_BUILD/orchestra_paper_cpu"
    SUDO_UID=$SUDO_TEST_UID bash "$ROOT/scripts/install.sh" \
        --prefix "$SUDO_PREFIX" --config-dir "$SUDO_CONFIG" \
        --build-dir "$SUDO_BUILD" --no-build >/dev/null
    [ -x "$SUDO_PREFIX/lib/orchestra-os/build/orchestra_paper_cpu" ]
    bash "$ROOT/scripts/uninstall.sh" \
        --prefix "$SUDO_PREFIX" --config-dir "$SUDO_CONFIG" >/dev/null

    FOREIGN_BUILD="$WORK/foreign-build"
    mkdir -p -- "$FOREIGN_BUILD"
    printf '#!/usr/bin/env sh\nexit 0\n' >"$FOREIGN_BUILD/orchestra_paper_cpu"
    chmod 0755 -- "$FOREIGN_BUILD/orchestra_paper_cpu"
    chown 65534 -- "$FOREIGN_BUILD/orchestra_paper_cpu"
    if SUDO_UID=65533 bash "$ROOT/scripts/install.sh" \
        --prefix "$WORK/foreign-prefix" --config-dir "$WORK/foreign-config" \
        --build-dir "$FOREIGN_BUILD" --no-build \
        >"$WORK/install-foreign.out" 2>"$WORK/install-foreign.err"; then
        echo "installer accepted an artifact owned by an unrelated account" >&2
        exit 1
    fi
    [ ! -e "$WORK/foreign-prefix" ]
fi

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
