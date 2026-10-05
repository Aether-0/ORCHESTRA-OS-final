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
