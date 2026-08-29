#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

if [ "$#" -gt 1 ]; then
    echo "usage: $0 [OUTPUT_TARBALL]" >&2
    exit 2
fi

VERSION=$(tr -d '\n' < "$ROOT/VERSION")
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
case "$SOURCE_DATE_EPOCH" in
    ''|*[!0-9]*) echo "SOURCE_DATE_EPOCH must be an integer" >&2; exit 2 ;;
esac
OUTPUT=${1:-"$ROOT/orchestra-os-${VERSION}-source.tar.gz"}
mkdir -p -- "$(dirname -- "$OUTPUT")"

TEMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-os-source.XXXXXX")
trap 'rm -rf -- "$TEMP_ROOT"' EXIT
EXPORT_TREE="$TEMP_ROOT/tree"
"$ROOT/scripts/public-export.sh" "$EXPORT_TREE"
ARCHIVE_ROOT="$TEMP_ROOT/orchestra-os-${VERSION}"
mv -- "$EXPORT_TREE" "$ARCHIVE_ROOT"

# GNU tar's sorted entries, fixed metadata, and gzip -n make the archive
# byte-for-byte reproducible for the same source tree and epoch.
tar \
    --sort=name \
    --mtime="@${SOURCE_DATE_EPOCH}" \
    --owner=0 --group=0 --numeric-owner \
    -I 'gzip -n' \
    -C "$TEMP_ROOT" -cf "$OUTPUT" "$(basename -- "$ARCHIVE_ROOT")"

sha256sum -- "$OUTPUT"
echo "SOURCE_TARBALL_COMPLETE $OUTPUT"
