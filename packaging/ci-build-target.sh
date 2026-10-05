#!/bin/sh
set -eu
[ "${ORCHESTRA_PACKAGE_CONTAINER:-}" = 1 ] || { echo 'This dependency installer is only for disposable CI containers.' >&2; exit 2; }
: "${DISTRO:?}" "${FAMILY:?}" "${FORMAT:?}" "${PACKAGE_ARCH:?}" "${SOURCE_DATE_EPOCH:?}"
case "$FAMILY" in
    apt)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends bash build-essential ca-certificates dpkg-dev git libbpf-dev libelf-dev libzstd-dev make pkg-config python3 tar zstd
        ;;
    dnf|dnf-crb)
        if [ "$FAMILY" = dnf-crb ]; then
            dnf -y install dnf-plugins-core
            dnf config-manager --set-enabled crb
        fi
        dnf -y install bash ca-certificates elfutils-libelf-devel gcc gcc-c++ git libbpf-devel libzstd-devel make pkgconf-pkg-config python3 rpm-build tar zstd
        ;;
    zypper)
        zypper --non-interactive refresh
        zypper --non-interactive install bash ca-certificates gcc gcc-c++ git libbpf-devel libelf-devel libzstd-devel make pkg-config python3 rpm-build tar zstd
        ;;
    apk)
        apk add --no-cache bash build-base ca-certificates coreutils elfutils-dev git libbpf-dev linux-headers make musl-dev pkgconf python3 tar zlib-dev zstd-dev zstd
        ;;
    pacman)
        pacman -Syu --noconfirm --needed base-devel bash ca-certificates git libbpf libelf python zlib zstd
        ;;
    *) echo "Unknown build family: $FAMILY" >&2; exit 2 ;;
esac
# Older distro libbpf/UAPI headers cannot describe safe struct_ops ownership.
# Supply the build dependency without changing ORCHESTRA implementation.
if ! printf '#include <bpf/bpf.h>\nint main(void) { struct bpf_link_info i = {0}; (void)bpf_map_get_info_by_fd; (void)bpf_link_get_info_by_fd; return i.struct_ops.map_id + BPF_LINK_TYPE_STRUCT_OPS; }\n' | cc -Werror -fsyntax-only -x c - >/dev/null 2>&1; then
    dependency=$(mktemp -d /tmp/orchestra-libbpf.XXXXXX)
    ORCHESTRA_LIBBPF_ARCHIVE="$dependency/libbpf-v1.7.0.tar.gz"
    export ORCHESTRA_LIBBPF_ARCHIVE
    python3 - <<'PYDOWNLOAD'
import hashlib, os, urllib.request
archive=os.environ['ORCHESTRA_LIBBPF_ARCHIVE']
url='https://github.com/libbpf/libbpf/archive/refs/tags/v1.7.0.tar.gz'
with urllib.request.urlopen(url, timeout=120) as response:
    data=response.read()
if hashlib.sha256(data).hexdigest() != '7ab5feffbf78557f626f2e3e3204788528394494715a30fc2070fcddc2051b7b':
    raise SystemExit('libbpf source checksum mismatch')
open(archive, 'wb').write(data)
PYDOWNLOAD
    tar -xf "$ORCHESTRA_LIBBPF_ARCHIVE" -C "$dependency"
    prefix="$dependency/install"
    make -C "$dependency/libbpf-1.7.0/src" -j2 BUILD_STATIC_ONLY=1 PREFIX="$prefix" LIBDIR="$prefix/lib" install
    # The loader uses the bundled static library and native libelf/zlib.
    sed -i '/^Libs:/s/$/ -lelf -lz/' "$prefix/lib/pkgconfig/libbpf.pc"
    C_INCLUDE_PATH="$dependency/libbpf-1.7.0/include/uapi:$prefix/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
    PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export C_INCLUDE_PATH PKG_CONFIG_PATH
    echo 'BUILD_DEPENDENCY libbpf=1.7.0 linkage=static source_sha256=7ab5feffbf78557f626f2e3e3204788528394494715a30fc2070fcddc2051b7b'
fi
work=$(mktemp -d /tmp/orchestra-native-source.XXXXXX)
cp -a /src/. "$work/"
cd "$work"
git config --global --add safe.directory "$work"
printf 'DISTRO=%s ARCH=%s SOURCE=%s\n' "$DISTRO" "$(uname -m)" "$(git rev-parse HEAD)"
cat /etc/os-release
cc --version
make clean
make
make check
build=$(mktemp -d /tmp/orchestra-native-build.XXXXXX)
ORCHESTRA_BUILD_DIR="$build" bash scripts/build.sh --userspace --bridge
candidate=$(mktemp -d /tmp/orchestra-native-packages.XXXXXX)
bash packaging/build-package.sh --format "$FORMAT" --distro "$DISTRO" --output "$candidate" --build-dir "$build" --arch "$PACKAGE_ARCH"
package=$(find "$candidate" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.rpm' -o -name '*.apk' -o -name '*.pkg.tar.zst' \) -print -quit)
[ -n "$package" ]
bash packaging/package-smoke.sh "$package" "$FORMAT" "$DISTRO"
bash packaging/generate-sbom.sh "$package" "$package.spdx.json"
cp "$package" "$package.spdx.json" /out/
echo "NATIVE_TARGET_PASS distro=$DISTRO arch=$PACKAGE_ARCH"
