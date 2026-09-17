#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
# shellcheck source=/dev/null
. "$SCRIPT_DIR/path_safety.sh"
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

# A normal system install is invoked through sudo after an unprivileged build.
# Trust artifacts owned by either the effective installer user or sudo's
# original caller, while still rejecting artifacts owned by an unrelated
# account or writable by a group/other users.
INSTALLER_UID=$(id -u)
CALLER_UID=$INSTALLER_UID
if [ "$INSTALLER_UID" -eq 0 ] && [[ ${SUDO_UID:-} =~ ^[0-9]+$ ]]; then
    CALLER_UID=$SUDO_UID
fi

safe_source_artifact() {
    local path=$1 permissions owner

    [ -f "$path" ] || return 1
    [ ! -L "$path" ] || return 1
    orchestra_safe_path_chain "$(dirname -- "$path")" || return 1
    owner=$(stat -c '%u' -- "$path" 2>/dev/null) || return 1
    [ "$owner" = "$INSTALLER_UID" ] || [ "$owner" = "$CALLER_UID" ] || return 1
    permissions=$(stat -c '%A' -- "$path" 2>/dev/null) || return 1
    [ "${permissions:5:1}" != w ] || return 1
    [ "${permissions:8:1}" != w ] || return 1
}

require_source_artifact() {
    local label=$1 path=$2
    if ! safe_source_artifact "$path"; then
        echo "unsafe $label source artifact: $path" >&2
        echo "refusing a missing, symlinked, foreign-owned, or group/world-writable artifact" >&2
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
INSTALL_MARKER="$LIB_ROOT/.orchestra-install"
PACKAGE_MARKER="$LIB_ROOT/.package-managed"

if [ -f "$PACKAGE_MARKER" ] || [ -L "$PACKAGE_MARKER" ]; then
    echo "this installation is owned by a package manager; use apt, dnf, or apk" >&2
    echo "refusing source-tree installation over package-managed files" >&2
    exit 1
fi

safe_install_file() {
    local source=$1 destination=$2 mode=$3

    require_plain_source "installation" "$source"
    orchestra_safe_destination_file "$destination" || {
        echo "unsafe installation destination: $destination" >&2
        exit 1
    }
    install -m "$mode" "$source" "$destination"
}

if ! orchestra_safe_path_chain "$PREFIX"; then
    echo "unsafe installation prefix path (symlink or writable parent): $PREFIX" >&2
    exit 1
fi
if [ -e "$INSTALL_MARKER" ] || [ -L "$INSTALL_MARKER" ]; then
    if [ ! -f "$INSTALL_MARKER" ] || [ -L "$INSTALL_MARKER" ] ||
       ! orchestra_safe_existing_dir "$LIB_ROOT" ||
       [ "$(stat -c '%u' -- "$INSTALL_MARKER" 2>/dev/null)" != "$(id -u)" ] ||
       [ "$(stat -c '%a' -- "$INSTALL_MARKER" 2>/dev/null)" != 644 ] ||
       ! grep -qx 'ORCHESTRA_INSTALL_MANIFEST_V1' "$INSTALL_MARKER"; then
        echo "refusing to overwrite an unverified ORCHESTRA installation: $LIB_ROOT" >&2
        exit 1
    fi
    expected_script=$(awk -F= '$1 == "script_sha256" { print $2; exit }' \
        "$INSTALL_MARKER")
    if [[ ! "$expected_script" =~ ^[[:xdigit:]]{64}$ ]] ||
       [ ! -f "$LIB_ROOT/scripts/orchestra" ] ||
       [ -L "$LIB_ROOT/scripts/orchestra" ] ||
       [ "$(sha256sum -- "$LIB_ROOT/scripts/orchestra" | awk '{print $1}')" != "$expected_script" ]; then
        echo "refusing to overwrite a modified installed control script" >&2
        exit 1
    fi
    if find "$LIB_ROOT" -mindepth 1 -type l -print -quit | grep -q .; then
        echo "refusing to overwrite an installation containing symlinks: $LIB_ROOT" >&2
        exit 1
    fi
    if ! cmp -s \
        <(awk '/^entries_begin$/{inside=1; next} /^entries_end$/{inside=0} inside{print}' \
            "$INSTALL_MARKER" | LC_ALL=C sort) \
        <(find "$LIB_ROOT" -mindepth 1 ! -path "$INSTALL_MARKER" \
            -printf '%y\t%P\n' | LC_ALL=C sort); then
        echo "refusing to overwrite an installation containing untracked or missing paths" >&2
        exit 1
    fi
elif [ -e "$LIB_ROOT" ] || [ -L "$LIB_ROOT" ]; then
    echo "existing installation has no trusted ORCHESTRA marker: $LIB_ROOT" >&2
    echo "refusing to overwrite or adopt unrelated files" >&2
    exit 1
fi
if [ -e "$BIN_DIR/orchestra" ] || [ -L "$BIN_DIR/orchestra" ]; then
    if [ ! -f "$BIN_DIR/orchestra" ] || [ -L "$BIN_DIR/orchestra" ]; then
        echo "refusing to overwrite an unsafe existing command: $BIN_DIR/orchestra" >&2
        exit 1
    fi
    if [ -f "$INSTALL_MARKER" ]; then
        expected_bin=$(awk -F= '$1 == "bin_sha256" { print $2; exit }' "$INSTALL_MARKER")
        actual_bin=$(sha256sum -- "$BIN_DIR/orchestra" | awk '{print $1}')
        if [[ ! "$expected_bin" =~ ^[[:xdigit:]]{64}$ ]] ||
           [ "$actual_bin" != "$expected_bin" ]; then
            echo "refusing to overwrite a modified command: $BIN_DIR/orchestra" >&2
            exit 1
        fi
    else
        echo "refusing to overwrite an unowned existing command: $BIN_DIR/orchestra" >&2
        exit 1
    fi
fi

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
if [ -f "$BUILD_DIR/orchestra_paper_cpu" ]; then
    require_source_artifact "userspace executable" "$BUILD_DIR/orchestra_paper_cpu"
fi

# Validate all externally supplied kernel artifacts before creating any
# destination directories. A failed --no-build/--with-kernel install must not
# leave a partially initialized prefix behind.
if ! orchestra_ensure_private_dir "$BIN_DIR"; then
    echo "unsafe installation destination directory" >&2
    exit 1
fi
for destination_dir in "$LIB_ROOT/scripts" "$LIB_ROOT/build" \
    "$LIB_ROOT/config" "$LIB_ROOT/config/examples" \
    "$LIB_ROOT/config/systemd" "$LIB_ROOT/docs" "$CONFIG_DIR"; do
    if ! orchestra_ensure_private_dir "$destination_dir"; then
        echo "unsafe installation directory: $destination_dir" >&2
        exit 1
    fi
done

safe_install_file "$REPO_ROOT/scripts/orchestra" "$BIN_DIR/orchestra" 0755
safe_install_file "$REPO_ROOT/scripts/orchestra" "$LIB_ROOT/scripts/orchestra" 0755
safe_install_file "$REPO_ROOT/scripts/check-system.sh" "$LIB_ROOT/scripts/check-system.sh" 0755
safe_install_file "$REPO_ROOT/scripts/path_safety.sh" "$LIB_ROOT/scripts/path_safety.sh" 0755
safe_install_file "$REPO_ROOT/scripts/build.sh" "$LIB_ROOT/scripts/build.sh" 0755
safe_install_file "$REPO_ROOT/scripts/install.sh" "$LIB_ROOT/scripts/install.sh" 0755
safe_install_file "$REPO_ROOT/scripts/uninstall.sh" "$LIB_ROOT/scripts/uninstall.sh" 0755
safe_install_file "$REPO_ROOT/scripts/policy_load.py" "$LIB_ROOT/scripts/policy_load.py" 0755
safe_install_file "$REPO_ROOT/VERSION" "$LIB_ROOT/VERSION" 0644
safe_install_file "$REPO_ROOT/LICENSE" "$LIB_ROOT/LICENSE" 0644
if [ -f "$BUILD_DIR/orchestra_paper_cpu" ]; then
    safe_install_file "$BUILD_DIR/orchestra_paper_cpu" \
        "$LIB_ROOT/build/orchestra_paper_cpu" 0755
fi
if [ -f "$BUILD_DIR/orchestra_bridge" ]; then
    safe_install_file "$BUILD_DIR/orchestra_bridge" "$LIB_ROOT/build/orchestra_bridge" 0755
fi
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/orchestra_loader" ]; then
    safe_install_file "$BUILD_DIR/orchestra_loader" "$LIB_ROOT/build/orchestra_loader" 0755
fi
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ]; then
    safe_install_file "$BUILD_DIR/orchestra_scx_stage7.bpf.o" \
        "$LIB_ROOT/build/orchestra_scx_stage7.bpf.o" 0644
fi
if [ "$with_kernel" -eq 1 ] && [ -f "$BUILD_DIR/build-manifest.txt" ]; then
    safe_install_file "$BUILD_DIR/build-manifest.txt" \
        "$LIB_ROOT/build/build-manifest.txt" 0644
fi
if [ -d "$REPO_ROOT/config/examples" ]; then
    for config_file in "$REPO_ROOT"/config/examples/*.json; do
        [ -f "$config_file" ] || continue
        require_plain_source "configuration" "$config_file"
        safe_install_file "$config_file" \
            "$LIB_ROOT/config/examples/$(basename "$config_file")" 0644
        target="$CONFIG_DIR/$(basename "$config_file")"
        if [ -L "$target" ]; then
            echo "refusing to follow a symlinked configuration target: $target" >&2
            exit 1
        fi
        if [ ! -e "$target" ]; then safe_install_file "$config_file" "$target" 0644; fi
    done
fi
if [ -f "$REPO_ROOT/README.md" ]; then
    safe_install_file "$REPO_ROOT/README.md" "$LIB_ROOT/README.md" 0644
fi
if [ -d "$REPO_ROOT/docs" ]; then
    while IFS= read -r -d '' document; do
        relative=${document#"$REPO_ROOT/docs/"}
        destination="$LIB_ROOT/docs/$relative"
        orchestra_ensure_private_dir "$(dirname -- "$destination")" || {
            echo "unsafe documentation destination: $destination" >&2
            exit 1
        }
        safe_install_file "$document" "$destination" 0644
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
    orchestra_ensure_private_dir "$PREFIX/lib/systemd/system" || {
        echo "unsafe systemd directory" >&2
        exit 1
    }
    safe_install_file "$REPO_ROOT/config/systemd/orchestra.service" \
        "$LIB_ROOT/config/systemd/orchestra.service" 0644
    safe_install_file "$REPO_ROOT/config/systemd/orchestra.service" \
        "$service_target" 0644
fi

marker_tmp="$INSTALL_MARKER.tmp.$$"
entries_tmp="$INSTALL_MARKER.entries.$$"
if ! orchestra_safe_destination_file "$marker_tmp"; then
    echo "unsafe installation marker destination: $INSTALL_MARKER" >&2
    exit 1
fi
if ! orchestra_safe_destination_file "$entries_tmp"; then
    echo "unsafe installation manifest destination: $INSTALL_MARKER" >&2
    exit 1
fi
install -m 0600 /dev/null "$entries_tmp"
find "$LIB_ROOT" -mindepth 1 \
    ! -path "$INSTALL_MARKER" ! -path "$marker_tmp" ! -path "$entries_tmp" \
    -printf '%y\t%P\n' | LC_ALL=C sort > "$entries_tmp"
install -m 0600 /dev/null "$marker_tmp"
{
    echo "ORCHESTRA_INSTALL_MANIFEST_V1"
    echo "product=ORCHESTRA-OS"
    echo "version=$(tr -d '\n' < "$REPO_ROOT/VERSION")"
    echo "bin_sha256=$(sha256sum -- "$BIN_DIR/orchestra" | awk '{print $1}')"
    echo "script_sha256=$(sha256sum -- "$LIB_ROOT/scripts/orchestra" | awk '{print $1}')"
    echo "entries_begin"
    cat "$entries_tmp"
    echo "entries_end"
} > "$marker_tmp"
chmod 0600 "$marker_tmp"
mv -f -- "$marker_tmp" "$INSTALL_MARKER"
chmod 0644 "$INSTALL_MARKER"
rm -f -- "$entries_tmp"

echo "installed ORCHESTRA-OS into $LIB_ROOT"
echo "command=$BIN_DIR/orchestra"
echo "config=$CONFIG_DIR"
if [ "$with_kernel" -eq 1 ]; then
    echo "kernel_artifacts=installed"
else
    echo "kernel_artifacts=not requested (observer-only package)"
fi
echo "scheduler=not enabled by installer"
