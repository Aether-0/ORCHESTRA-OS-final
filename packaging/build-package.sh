#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
VERSION=$(tr -d '\n' < "$ROOT/VERSION")
FORMAT=
DISTRO=
OUTPUT=
BUILD_DIR=
ARCH=
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}

usage() {
    cat >&2 <<'EOF'
usage: packaging/build-package.sh --format deb|rpm|apk --distro NAME \
    --output DIR --build-dir DIR [--arch ARCH]
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --format) [ "$#" -ge 2 ] || { usage; exit 2; }; FORMAT=$2; shift 2 ;;
        --distro) [ "$#" -ge 2 ] || { usage; exit 2; }; DISTRO=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || { usage; exit 2; }; OUTPUT=$2; shift 2 ;;
        --build-dir) [ "$#" -ge 2 ] || { usage; exit 2; }; BUILD_DIR=$2; shift 2 ;;
        --arch) [ "$#" -ge 2 ] || { usage; exit 2; }; ARCH=$2; shift 2 ;;
        --help|-h) usage >&2; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

case "$FORMAT" in deb|rpm|apk) ;; *) echo "unsupported format: $FORMAT" >&2; exit 2 ;; esac
[ -n "$DISTRO" ] || { echo "--distro is required" >&2; exit 2; }
[ -n "$OUTPUT" ] || { echo "--output is required" >&2; exit 2; }
[ -n "$BUILD_DIR" ] || { echo "--build-dir is required" >&2; exit 2; }
mkdir -p "$OUTPUT"

if [ -z "$ARCH" ]; then
    ARCH=$(uname -m)
fi
case "$FORMAT:$ARCH" in
    deb:x86_64) ARCH=amd64 ;;
    deb:aarch64) ARCH=arm64 ;;
    deb:amd64|deb:arm64) : ;;
    rpm:x86_64|apk:x86_64) : ;;
    rpm:aarch64|apk:aarch64) : ;;
    *) echo "unsupported architecture '$ARCH' for $FORMAT" >&2; exit 2 ;;
esac

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-package-stage.XXXXXX")
cleanup() { rm -rf -- "$STAGE"; }
trap cleanup EXIT HUP INT TERM
bash "$SCRIPT_DIR/stage-payload.sh" "$STAGE" "$BUILD_DIR" "$DISTRO" "$ARCH"

case "$FORMAT" in
    deb)
        mkdir -p "$STAGE/DEBIAN"
        cat > "$STAGE/DEBIAN/control" <<EOF
Package: orchestra-os
Version: $VERSION-1
Section: admin
Priority: optional
Architecture: $ARCH
Multi-Arch: foreign
Maintainer: Aether-0 <143575444+Aether-0@users.noreply.github.com>
Depends: bash, coreutils, python3, libbpf1, libelf1, zlib1g, libzstd1
Suggests: clang, bpftool, gcc, make, pkg-config, libbpf-dev, libelf-dev, libzstd-dev
Homepage: https://github.com/Aether-0/ORCHESTRA-OS-Public
Description: ORCHESTRA-OS research-stable scheduler control plane
 Safe observer/control plane and explicitly opt-in target-matched sched_ext
 research prototype. Installation never attaches or enables a scheduler.
EOF
        printf '%s\n' /etc/orchestra-os/*.json > "$STAGE/DEBIAN/conffiles"
        find "$STAGE" -exec touch -d "@$SOURCE_DATE_EPOCH" {} +
        artifact="orchestra-os_${VERSION}-1_${DISTRO}_${ARCH}.deb"
        dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT/$artifact" >/dev/null
        ;;
    rpm)
        rpm_arch=$ARCH
        rpm_top=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-rpmbuild.XXXXXX")
        trap 'rm -rf -- "$STAGE" "$rpm_top"' EXIT HUP INT TERM
        find "$STAGE" -exec touch -d "@$SOURCE_DATE_EPOCH" {} +
        mkdir -p "$rpm_top/BUILD" "$rpm_top/BUILDROOT" "$rpm_top/RPMS" \
            "$rpm_top/SOURCES" "$rpm_top/SPECS" "$rpm_top/SRPMS"
        cp -a "$STAGE" "$rpm_top/SOURCES/payload"
        cp "$SCRIPT_DIR/orchestra-os.spec" "$rpm_top/SPECS/orchestra-os.spec"
        SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" rpmbuild -bb --target "$rpm_arch" \
            --define "_topdir $rpm_top" \
            --define "_orchestra_version $VERSION" \
            --define "_orchestra_payload $rpm_top/SOURCES/payload" \
            --define "use_source_date_epoch_as_buildtime 1" \
            --define "_buildhost orchestra-builder" \
            --define "_build_id_links none" \
            --define "__brp_compress /bin/true" \
            --define "__brp_remove_la_files /bin/true" \
            --define "__brp_strip /bin/true" \
            --define "__brp_strip_comment_note /bin/true" \
            --define "__brp_strip_static_archive /bin/true" \
            "$rpm_top/SPECS/orchestra-os.spec" >/dev/null
        built=$(find "$rpm_top/RPMS" -type f -name 'orchestra-os-*.rpm' -print -quit)
        [ -n "$built" ] || { echo "rpmbuild produced no package" >&2; exit 1; }
        artifact="orchestra-os-${VERSION}-1.${DISTRO}.${rpm_arch}.rpm"
        cp "$built" "$OUTPUT/$artifact"
        ;;
    apk)
        apk_arch=$ARCH
        artifact="orchestra-os-${VERSION}-r0.${DISTRO}.${apk_arch}.apk"
        find "$STAGE" -exec touch -d "@$SOURCE_DATE_EPOCH" {} +
        apk mkpkg --arch "$apk_arch" --files "$STAGE" \
            --info "name:orchestra-os" \
            --info "version:${VERSION}-r0" \
            --info "arch:${apk_arch}" \
            --info "origin:orchestra-os" \
            --info "description:ORCHESTRA-OS research-stable scheduler control plane" \
            --info "license:MIT" \
            --info "url:https://github.com/Aether-0/ORCHESTRA-OS-Public" \
            --info "depends:bash python3 coreutils libbpf libelf zlib zstd-libs" \
            --output "$OUTPUT/$artifact" >/dev/null
        ;;
esac

sha256sum "$OUTPUT/$artifact"
echo "PACKAGE_COMPLETE"
echo "artifact=$OUTPUT/$artifact"
