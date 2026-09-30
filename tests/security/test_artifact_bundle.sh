#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
. "$ROOT/scripts/realworld_artifact_bundle.sh"
WORK=$(mktemp -d)
trap 'rm -rf -- "$WORK"' EXIT
# Trust-policy checks are tested separately; these fixtures exercise bundle identity offline.
orchestra_safe_existing_dir() { [ -d "$1" ]; }
orchestra_realworld_safe_file() { [ -f "$1" ] && [ ! -L "$1" ]; }
for file in orchestra_scx_stage7.bpf.o orchestra_bridge orchestra_loader; do
    printf '%s\n' "$file" > "$WORK/$file"
done
{
    echo 'running_kernel=test-kernel'
    echo "bpf_object_sha256=$(sha256sum "$WORK/orchestra_scx_stage7.bpf.o" | cut -d' ' -f1)"
    echo "bridge_sha256=$(sha256sum "$WORK/orchestra_bridge" | cut -d' ' -f1)"
    echo "loader_sha256=$(sha256sum "$WORK/orchestra_loader" | cut -d' ' -f1)"
} > "$WORK/build-manifest.txt"
cp "$WORK/build-manifest.txt" "$WORK/original"
orchestra_realworld_bundle_matches "$WORK" test-kernel
if orchestra_realworld_bundle_matches "$WORK" wrong-kernel; then exit 1; fi
echo 'running_kernel=test-kernel' >> "$WORK/build-manifest.txt"
if orchestra_realworld_bundle_matches "$WORK" test-kernel; then exit 1; fi
cp "$WORK/original" "$WORK/build-manifest.txt"
echo 'bridge_sha256=invalid' >> "$WORK/build-manifest.txt"
if orchestra_realworld_bundle_matches "$WORK" test-kernel; then exit 1; fi
cp "$WORK/original" "$WORK/build-manifest.txt"
echo corrupted >> "$WORK/orchestra_bridge"
if orchestra_realworld_bundle_matches "$WORK" test-kernel; then exit 1; fi
echo 'PASS bundle identity: valid, wrong kernel, duplicate keys, corruption'
printf '%s\n' orchestra_bridge > "$WORK/orchestra_bridge"
orchestra_realworld_bundle_matches "$WORK" test-kernel
mv "$WORK/orchestra_loader" "$WORK/loader-real"
if orchestra_realworld_bundle_matches "$WORK" test-kernel; then exit 1; fi
ln -s "$WORK/loader-real" "$WORK/orchestra_loader"
if orchestra_realworld_bundle_matches "$WORK" test-kernel; then exit 1; fi
echo 'PASS missing and symlinked bundle member rejection'
