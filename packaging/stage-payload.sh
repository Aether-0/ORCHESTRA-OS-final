#!/usr/bin/env bash
set -euo pipefail

# Build a package-manager payload without using the source-tree installer.
# Package managers own every staged file; no maintainer script attaches or
# enables sched_ext.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

usage() {
    echo "usage: $0 STAGE_DIR BUILD_DIR DISTRO ARCH" >&2
}

[ "$#" -eq 4 ] || { usage; exit 2; }
STAGE=$1
BUILD_DIR=$2
DISTRO=$3
ARCH=$4

case "$STAGE" in
    /*) ;;
    *) echo "stage path must be absolute: $STAGE" >&2; exit 2 ;;
esac
case "$BUILD_DIR" in
    /*) ;;
    *) echo "build path must be absolute: $BUILD_DIR" >&2; exit 2 ;;
esac

VERSION=$(tr -d '\n' < "$ROOT/VERSION")
PAYLOAD="$STAGE/usr/lib/orchestra-os"

rm -rf -- "$STAGE"
mkdir -p "$PAYLOAD" "$STAGE/usr/bin" "$STAGE/etc/orchestra-os" \
    "$STAGE/usr/lib/systemd/system" "$STAGE/usr/share/doc/orchestra-os" \
    "$STAGE/var/lib/orchestra-os/build"

# Keep source available for an explicit target-matched kernel build, while
# excluding private evidence, generated output, and machine-specific files.
tar -C "$ROOT" \
    --exclude='./.git' \
    --exclude='./.github' \
    --exclude='./artifacts' \
    --exclude='./release' \
    --exclude='./packaging' \
    --exclude='./C:\\FastMCP\\boot.log' \
    --exclude='*.zip' \
    --exclude='*.pdf' \
    --exclude='*.log' \
    --exclude='*.html' \
    --exclude='./orchestra_paper_cpu_demo/orchestra_paper_cpu' \
    --exclude='./research/experiments/process-group-prototype/orchestra_real_cpu' \
    -cf - . | tar -C "$PAYLOAD" -xf -
chmod 0755 "$PAYLOAD"

install -m 0755 "$ROOT/scripts/orchestra" "$STAGE/usr/bin/orchestra"
install -m 0755 "$ROOT/orchestra_paper_cpu_demo/orchestra_paper_cpu" \
    "$PAYLOAD/orchestra_paper_cpu_demo/orchestra_paper_cpu"

for artifact in orchestra_bridge orchestra_loader; do
    if [ ! -x "$BUILD_DIR/$artifact" ]; then
        echo "missing required userspace artifact: $BUILD_DIR/$artifact" >&2
        exit 1
    fi
    install -m 0755 "$BUILD_DIR/$artifact" \
        "$STAGE/var/lib/orchestra-os/build/$artifact"
done

for config_file in "$ROOT"/config/examples/*.json; do
    [ -f "$config_file" ] || continue
    install -m 0644 "$config_file" \
        "$STAGE/etc/orchestra-os/$(basename -- "$config_file")"
done

sed \
    -e 's#/usr/local/bin/orchestra#/usr/bin/orchestra#g' \
    "$ROOT/config/systemd/orchestra.service" \
    > "$STAGE/usr/lib/systemd/system/orchestra.service"
chmod 0644 "$STAGE/usr/lib/systemd/system/orchestra.service"

if [[ "$DISTRO" == alpine* ]] && [ -f "$ROOT/packaging/openrc/orchestra" ]; then
    mkdir -p "$STAGE/etc/init.d"
    install -m 0755 "$ROOT/packaging/openrc/orchestra" \
        "$STAGE/etc/init.d/orchestra"
fi

for document in README.md FINAL_PRODUCT_STATUS.md LIMITATIONS.md LICENSE; do
    [ -f "$ROOT/$document" ] || continue
    install -m 0644 "$ROOT/$document" \
        "$STAGE/usr/share/doc/orchestra-os/$document"
done
for document in docs/installation/INSTALL.md docs/security/SECURITY.md \
    docs/releases/v1.0.0-research-stable.md; do
    [ -f "$ROOT/$document" ] || continue
    install -m 0644 "$ROOT/$document" \
        "$STAGE/usr/share/doc/orchestra-os/$(basename -- "$document")"
done

cat > "$PAYLOAD/.package-managed" <<EOF
ORCHESTRA_PACKAGE_MANAGED_V1
package=orchestra-os
version=$VERSION
distribution=$DISTRO
architecture=$ARCH
EOF
chmod 0644 "$PAYLOAD/.package-managed"

if git -C "$ROOT" rev-parse HEAD >/dev/null 2>&1; then
    source_commit=$(git -C "$ROOT" rev-parse HEAD)
else
    source_commit=unavailable
fi
cat > "$PAYLOAD/RELEASE-METADATA" <<EOF
product=ORCHESTRA-OS
version=$VERSION
source_commit=$source_commit
distribution=$DISTRO
architecture=$ARCH
release_class=research-stable
kernel_artifacts=target-specific-and-not-shipped
kernel_build=explicit-local-build-only
scheduler_install=disabled-by-default
EOF
chmod 0644 "$PAYLOAD/RELEASE-METADATA"

echo "STAGE_COMPLETE"
echo "stage=$STAGE"
echo "version=$VERSION"
echo "distribution=$DISTRO"
echo "architecture=$ARCH"
