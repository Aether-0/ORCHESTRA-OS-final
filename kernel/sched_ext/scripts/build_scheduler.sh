#!/usr/bin/env bash
set -euo pipefail

# Build the canonical ORCHESTRA scheduler without modifying repository
# sources. Historical command names forward to this builder.
#
# Usage:
#   ORCHESTRA_KERNEL_SRC=/path/to/exact/kernel/source \
#     ORCHESTRA_BUILD_DIR=/external/output \
#     bash build_scheduler.sh
#
# The kernel source must match uname(2).  The source tree is also required to
# contain the sched_ext headers and the libbpf inputs used by that kernel; a
# distro kernel-header symlink is not sufficient when those tools are absent.
# If a source export omitted scripts/bpf_doc.py, ORCHESTRA_BPF_DOC may point to
# an explicitly reviewed generator.  The selected paths are recorded in the
# build log so such a build cannot be mistaken for a complete source export.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
# shellcheck source=/dev/null
. "$REPO_ROOT/scripts/path_safety.sh"
RUNNING_KERNEL=$(uname -r)
RUNNING_VERSION=${RUNNING_KERNEL%%+*}
KSRC=${1:-${ORCHESTRA_KERNEL_SRC:-/lib/modules/$RUNNING_KERNEL/build}}

if [ "$#" -gt 1 ]; then
    echo "usage: $0 [EXACT_KERNEL_SOURCE]" >&2
    exit 2
fi

if [ -n "${ORCHESTRA_BUILD_DIR:-}" ]; then
    BUILD_DIR=$ORCHESTRA_BUILD_DIR
else
    BUILD_DIR=/tmp/orchestra-scheduler-build-${RUNNING_KERNEL}-$(date +%Y%m%d-%H%M%S)
fi

blocked() {
    echo "BLOCKED_$*" >&2
    exit 2
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || blocked "MISSING_TOOL:$1"
}

require_file() {
    [ -f "$1" ] || blocked "MISSING_INPUT:$1"
}

require_command clang
require_command cc
require_command bpftool
require_file "$KSRC/Makefile"
require_file /sys/kernel/btf/vmlinux

SOURCE_VERSION=$(awk '
    /^VERSION[[:space:]]*=/ { version = $3 }
    /^PATCHLEVEL[[:space:]]*=/ { patchlevel = $3 }
    /^SUBLEVEL[[:space:]]*=/ { sublevel = $3 }
    /^EXTRAVERSION[[:space:]]*=/ { extra = $3 }
    END {
        if (version != "" && patchlevel != "" && sublevel != "")
            printf "%s.%s.%s%s\n", version, patchlevel, sublevel, extra
    }
' "$KSRC/Makefile")
[ -n "$SOURCE_VERSION" ] || blocked "UNREADABLE_KERNEL_VERSION:$KSRC/Makefile"
[ "$SOURCE_VERSION" = "$RUNNING_VERSION" ] ||
    blocked "KERNEL_VERSION_MISMATCH:running=$RUNNING_VERSION source=$SOURCE_VERSION"

require_file "$KSRC/tools/sched_ext/include/scx/common.bpf.h"
require_file "$KSRC/tools/sched_ext/include/scx/enum_defs.autogen.h"
require_file "$KSRC/tools/lib/bpf/bpf_helpers.h"

BPF_UAPI=${ORCHESTRA_BPF_UAPI:-}
if [ -z "$BPF_UAPI" ]; then
    if [ -f "$KSRC/tools/include/uapi/linux/bpf.h" ]; then
        BPF_UAPI="$KSRC/tools/include/uapi/linux/bpf.h"
    elif [ -f "$KSRC/include/uapi/linux/bpf.h" ]; then
        # A matching source export may omit the tools copy while still
        # shipping the exact kernel UAPI header.  bpf_doc.py consumes the
        # header contents, so no other kernel revision is imported here.
        BPF_UAPI="$KSRC/include/uapi/linux/bpf.h"
    else
        blocked "MISSING_MATCHING_BPF_UAPI:$KSRC/tools/include/uapi/linux/bpf.h"
    fi
fi
require_file "$BPF_UAPI"

BPF_DOC=${ORCHESTRA_BPF_DOC:-$KSRC/scripts/bpf_doc.py}
HELPER_DEFS=${ORCHESTRA_BPF_HELPER_DEFS:-$KSRC/tools/lib/bpf/bpf_helper_defs.h}
if [ ! -f "$HELPER_DEFS" ]; then
    require_command python3
    require_file "$BPF_DOC"
fi

case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$PWD/$BUILD_DIR" ;;
esac
BUILD_PARENT=$(dirname -- "$BUILD_DIR")
BUILD_NAME=$(basename -- "$BUILD_DIR")
orchestra_prepare_parent_dir "$BUILD_PARENT" ||
    blocked "UNSAFE_BUILD_PARENT:$BUILD_PARENT"
BUILD_DIR=$(CDPATH= cd -- "$BUILD_PARENT" && pwd)/$BUILD_NAME
case "$BUILD_DIR" in
    "$REPO_ROOT"|"$REPO_ROOT"/*)
        blocked "BUILD_DIR_INSIDE_REPOSITORY:$BUILD_DIR" ;;
esac
orchestra_ensure_private_dir "$BUILD_DIR" ||
    blocked "UNSAFE_BUILD_DIR:$BUILD_DIR"
orchestra_ensure_private_dir "$BUILD_DIR/libbpf" ||
    blocked "UNSAFE_BUILD_SUBDIR:$BUILD_DIR/libbpf"

BPF_SOURCE=${ORCHESTRA_BPF_SOURCE:-$REPO_ROOT/kernel/sched_ext/bpf/orchestra_sched.bpf.c}
require_file "$BPF_SOURCE"

case "$(uname -m)" in
    x86_64) KERNEL_ARCH_INCLUDE="$KSRC/arch/x86" ;;
    aarch64) KERNEL_ARCH_INCLUDE="$KSRC/arch/arm64" ;;
    *) KERNEL_ARCH_INCLUDE="" ;;
esac

LOG="$BUILD_DIR/build.log"
exec > >(tee "$LOG") 2>&1

echo "ORCHESTRA-OS target-matched sched_ext build"
echo "repository=$REPO_ROOT"
echo "running_kernel=$RUNNING_KERNEL"
echo "kernel_source=$KSRC"
echo "source_version=$SOURCE_VERSION"
echo "build_dir=$BUILD_DIR"
echo "bpf_uapi=$BPF_UAPI"
echo "bpf_doc=$BPF_DOC"
echo "bpf_helper_defs=$HELPER_DEFS"
echo "bpf_source=$BPF_SOURCE"
echo "architecture=$(uname -m)"

VMLINUX_H="$BUILD_DIR/vmlinux.h"
echo "Generating $VMLINUX_H from /sys/kernel/btf/vmlinux"
bpftool btf dump file /sys/kernel/btf/vmlinux format c > "$VMLINUX_H"

if [ ! -f "$HELPER_DEFS" ]; then
    echo "Generating libbpf helper definitions from selected UAPI"
    if ! python3 "$BPF_DOC" --header --file "$BPF_UAPI" \
        > "$BUILD_DIR/libbpf/bpf_helper_defs.h" \
        2> "$BUILD_DIR/libbpf-generate.log"; then
        echo "libbpf helper-generation output:" >&2
        sed -n '1,160p' "$BUILD_DIR/libbpf-generate.log" >&2 || true
        blocked "BPF_HELPER_GENERATION_FAILED:$BPF_DOC"
    fi
    HELPER_DEFS="$BUILD_DIR/libbpf/bpf_helper_defs.h"
fi
require_file "$HELPER_DEFS"

BPF_OBJECT="$BUILD_DIR/orchestra_sched.bpf.o"
BRIDGE="$BUILD_DIR/orchestra_bridge"
LOADER="$BUILD_DIR/orchestra_loader"

BPF_INCLUDES=(
    -I"$BUILD_DIR"
    -I"$BUILD_DIR/libbpf"
    -I"$REPO_ROOT/kernel/sched_ext/include"
    -I"$KSRC/tools/lib"
    -I"$KSRC/tools/include"
    -I"$KSRC/tools/include/uapi"
    -I"$KSRC/include"
    -I"$KSRC/include/uapi"
    -I"$KSRC/tools/sched_ext/include"
    -I/usr/include/bpf
)
if [ -n "$KERNEL_ARCH_INCLUDE" ]; then
    BPF_INCLUDES+=("-I$KERNEL_ARCH_INCLUDE/include")
    BPF_INCLUDES+=("-I$KERNEL_ARCH_INCLUDE/include/generated")
fi

echo "Building $BPF_OBJECT"
clang -O2 -target bpf -g -nostdinc -D__BPF__ \
    "${BPF_INCLUDES[@]}" \
    -Wno-missing-declarations -Wno-visibility \
    -Wno-address-of-packed-member \
    -c "$BPF_SOURCE" \
    -o "$BPF_OBJECT"

echo "Building $BRIDGE"
cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wformat=2 -Werror -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
    -I"$REPO_ROOT/kernel/sched_ext/include" \
    "$REPO_ROOT/kernel/sched_ext/bridge/orchestra_bridge.c" \
    -o "$BRIDGE" -Wl,-z,relro,-z,now -Wl,-z,noexecstack -pie

echo "Building $LOADER"
LIBBPF_INCLUDES=(
    -isystem "$KSRC/tools/lib"
    -isystem "$KSRC/include/uapi"
)
if [ -n "$KERNEL_ARCH_INCLUDE" ]; then
    LIBBPF_INCLUDES+=("-isystem" "$KERNEL_ARCH_INCLUDE/include/uapi")
fi
LIBBPF_DEFINES=(-D__EXPORTED_HEADERS__)
if ! printf '#include <bpf/libbpf.h>\n' | cc "${LIBBPF_DEFINES[@]}" -E \
    "${LIBBPF_INCLUDES[@]}" - \
    >/dev/null 2>"$BUILD_DIR/libbpf-header-check.log"; then
    echo "libbpf header-check output:" >&2
    sed -n '1,160p' "$BUILD_DIR/libbpf-header-check.log" >&2 || true
    blocked "MISSING_LIBBPF_HEADERS:$KSRC/tools/lib/bpf/libbpf.h"
fi

LIBBPF_SONAME=
for candidate in /usr/lib/*/libbpf.so.* /lib/*/libbpf.so.*; do
    if [ -f "$candidate" ]; then
        LIBBPF_SONAME=$(basename -- "$candidate")
        break
    fi
done
[ -n "$LIBBPF_SONAME" ] || blocked "MISSING_LIBBPF_RUNTIME_LIBRARY:libbpf.so.1"

echo "libbpf_link=$LIBBPF_SONAME"
cc -O2 -std=c11 -Wall -Wextra -Wpedantic \
    -Wconversion -Wshadow -Wformat=2 -Werror \
    -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
    "${LIBBPF_DEFINES[@]}" \
    "${LIBBPF_INCLUDES[@]}" \
    -I"$REPO_ROOT/kernel/sched_ext/include" \
    "$REPO_ROOT/kernel/sched_ext/loader/orchestra_loader.c" \
    -o "$LOADER" -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
    -Wl,-z,relro,-z,now -Wl,-z,noexecstack -pie \
    -l:"$LIBBPF_SONAME"

# Keep root-facing artifacts non-writable by group/other even when the build
# host uses a permissive umask. The installer performs the same checks before
# copying them into a privileged prefix.
chmod 0644 "$VMLINUX_H" "$BPF_OBJECT"
chmod 0755 "$BRIDGE" "$LOADER"
# Transitional artifact alias for older installed commands.
if [ -e "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ] || [ -L "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ]; then
    [ -f "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ] &&
    [ ! -L "$BUILD_DIR/orchestra_scx_stage7.bpf.o" ] &&
    [ "$(stat -c %h "$BUILD_DIR/orchestra_scx_stage7.bpf.o")" = 1 ] ||
        blocked "UNSAFE_LEGACY_ARTIFACT:$BUILD_DIR/orchestra_scx_stage7.bpf.o"
fi
cp -- "$BPF_OBJECT" "$BUILD_DIR/orchestra_scx_stage7.bpf.o"
chmod 0644 "$BUILD_DIR/orchestra_scx_stage7.bpf.o"

{
    echo "running_kernel=$RUNNING_KERNEL"
    echo "source_version=$SOURCE_VERSION"
    echo "kernel_source=$KSRC"
    echo "bpf_uapi=$BPF_UAPI"
    echo "bpf_uapi_sha256=$(sha256sum "$BPF_UAPI" | awk '{print $1}')"
    echo "bpf_doc=$BPF_DOC"
    if [ -f "$BPF_DOC" ]; then
        echo "bpf_doc_sha256=$(sha256sum "$BPF_DOC" | awk '{print $1}')"
    else
        echo "bpf_doc_sha256=not_used"
    fi
    echo "bpf_helper_defs=$HELPER_DEFS"
    echo "bpf_helper_defs_sha256=$(sha256sum "$HELPER_DEFS" | awk '{print $1}')"
    echo "libbpf_link=$LIBBPF_SONAME"
    echo "vmlinux_sha256=$(sha256sum "$VMLINUX_H" | awk '{print $1}')"
    echo "bpf_object_sha256=$(sha256sum "$BPF_OBJECT" | awk '{print $1}')"
    echo "bridge_sha256=$(sha256sum "$BRIDGE" | awk '{print $1}')"
    echo "loader_sha256=$(sha256sum "$LOADER" | awk '{print $1}')"
} > "$BUILD_DIR/build-manifest.txt"

echo "BUILD_COMPLETE"
echo "manifest=$BUILD_DIR/build-manifest.txt"
