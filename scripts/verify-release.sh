#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 ARCHIVE.tar.gz [AGGREGATE_SHA256SUMS]"
}

if [ "$#" -eq 1 ] && { [ "$1" = "--help" ] || [ "$1" = "-h" ]; }; then
    usage
    exit 0
fi
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 2
fi

ARCHIVE=$1
ARCHIVE_DIR=$(CDPATH= cd -- "$(dirname -- "$ARCHIVE")" && pwd)
ARCHIVE_NAME=$(basename -- "$ARCHIVE")
case "$ARCHIVE_NAME" in
    *.tar.gz) ;;
    *) echo "archive must have a .tar.gz suffix: $ARCHIVE" >&2; exit 2 ;;
esac
ARCHIVE="$ARCHIVE_DIR/$ARCHIVE_NAME"
if [ ! -f "$ARCHIVE" ] || [ -L "$ARCHIVE" ]; then
    echo "archive is missing, not a regular file, or a symlink: $ARCHIVE" >&2
    exit 1
fi

if [ "$#" -eq 2 ]; then
    CHECKSUMS=$2
else
    CHECKSUMS="$ARCHIVE_DIR/SHA256SUMS"
fi
CHECKSUMS_DIR=$(CDPATH= cd -- "$(dirname -- "$CHECKSUMS")" && pwd)
CHECKSUMS_NAME=$(basename -- "$CHECKSUMS")
CHECKSUMS="$CHECKSUMS_DIR/$CHECKSUMS_NAME"
if [ ! -f "$CHECKSUMS" ] || [ -L "$CHECKSUMS" ]; then
    echo "aggregate checksum manifest is missing or unsafe: $CHECKSUMS" >&2
    exit 1
fi

for command_name in sha256sum tar python3; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "required command is missing: $command_name" >&2
        exit 1
    }
done

validate_manifest_paths() {
    local manifest=$1 root=$2 label=$3
    python3 - "$manifest" "$root" "$label" <<'PY'
import os
from pathlib import Path, PurePosixPath
import re
import sys

manifest, root, label = sys.argv[1:]
root_path = Path(root).resolve()
digest_line = re.compile(r"^([0-9a-fA-F]{64})  (.+)$")
with open(manifest, encoding="utf-8", errors="strict") as stream:
    for number, raw_line in enumerate(stream, 1):
        line = raw_line.rstrip("\n")
        if not line:
            continue
        match = digest_line.fullmatch(line)
        if match is None:
            raise SystemExit(f"{label} checksum manifest has malformed line {number}")
        name = match.group(2)
        if "\\" in name or "\x00" in name:
            raise SystemExit(f"{label} checksum manifest has unsafe path: {name}")
        relative = name[2:] if name.startswith("./") else name
        path = PurePosixPath(relative)
        if path.is_absolute() or not relative or ".." in path.parts or "." in path.parts:
            raise SystemExit(f"{label} checksum manifest has unsafe path: {name}")
        raw_candidate = root_path / Path(*path.parts)
        if raw_candidate.is_symlink() or not raw_candidate.is_file():
            raise SystemExit(f"{label} checksum manifest references a non-regular file: {name}")
        candidate = raw_candidate.resolve()
        try:
            candidate.relative_to(root_path)
        except ValueError as error:
            raise SystemExit(f"{label} checksum manifest escapes its root: {name}") from error
        if not candidate.is_file():
            raise SystemExit(f"{label} checksum manifest references a non-regular file: {name}")
PY
}

validate_manifest_paths "$CHECKSUMS" "$CHECKSUMS_DIR" aggregate

(
    cd "$CHECKSUMS_DIR"
    sha256sum --quiet -c "$CHECKSUMS_NAME"
)

manifest_digest() {
    local name=$1
    awk -v expected="./$name" -v plain="$name" \
        '$2 == expected || $2 == plain { print $1; exit }' "$CHECKSUMS"
}

for required_file in "$ARCHIVE_NAME" "$ARCHIVE_NAME.cdx.json"; do
    required_path="$ARCHIVE_DIR/$required_file"
    required_digest=$(manifest_digest "$required_file")
    if [[ ! "$required_digest" =~ ^[[:xdigit:]]{64}$ ]] ||
       [ ! -f "$required_path" ] || [ -L "$required_path" ] ||
       [ "$(sha256sum -- "$required_path" | awk '{print $1}')" != "$required_digest" ]; then
        echo "aggregate checksum manifest does not cover: $required_file" >&2
        exit 1
    fi
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-release-verify.XXXXXX")
cleanup() {
    rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT HUP INT TERM

# Inspect before extraction. Reject absolute paths, traversal, symlinks, and
# hardlinks so a verification run never executes an archive-controlled path.
python3 - "$ARCHIVE" "$WORK_DIR" <<'PY'
import os
import posixpath
import sys
import tarfile

archive, destination = sys.argv[1:]
with tarfile.open(archive, "r:gz") as stream:
    members = stream.getmembers()
    if not members:
        raise SystemExit("archive is empty")
    top_levels = {member.name.split("/", 1)[0] for member in members}
    if len(top_levels) != 1 or not next(iter(top_levels)):
        raise SystemExit("archive must contain exactly one top-level directory")
    top = next(iter(top_levels))
    seen = set()
    for member in members:
        if member.name in seen:
            raise SystemExit(f"duplicate archive member: {member.name}")
        seen.add(member.name)
        name = member.name
        if name.startswith("/") or "\\" in name:
            raise SystemExit(f"unsafe archive path: {name}")
        normalized = posixpath.normpath(name)
        if normalized != name or normalized == "." or (
            name != top and not name.startswith(top + "/")
        ):
            raise SystemExit(f"unsafe archive path: {name}")
        if member.issym() or member.islnk():
            raise SystemExit(f"archive links are not accepted: {name}")
        if member.isdev() or member.isfifo():
            raise SystemExit(f"archive special files are not accepted: {name}")
    stream.extractall(destination)
    package_dir = os.path.join(destination, top)
    if not os.path.isdir(package_dir) or os.path.islink(package_dir):
        raise SystemExit("archive top-level entry is not a directory")
PY

PACKAGE_DIR=$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d -print -quit)
if [ -z "$PACKAGE_DIR" ] || [ "$(find "$WORK_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l)" -ne 1 ]; then
    echo "archive extraction did not produce one package directory" >&2
    exit 1
fi
if find "$PACKAGE_DIR" \( -type l -o -type b -o -type c -o -type p \) \
    -print -quit | grep -q .; then
    echo "extracted package contains an unsafe special file" >&2
    exit 1
fi

if [ ! -f "$PACKAGE_DIR/SHA256SUMS" ] || [ -L "$PACKAGE_DIR/SHA256SUMS" ]; then
    echo "inner package checksum manifest is missing or unsafe" >&2
    exit 1
fi
validate_manifest_paths "$PACKAGE_DIR/SHA256SUMS" "$PACKAGE_DIR" package
(
    cd "$PACKAGE_DIR"
    sha256sum --quiet -c SHA256SUMS
    ./bin/orchestra version
    ./bin/orchestra paper-cpu --help >/dev/null 2>&1
)

SBOM="$ARCHIVE_DIR/$ARCHIVE_NAME.cdx.json"
if [ ! -f "$SBOM" ] || [ -L "$SBOM" ]; then
    echo "package SBOM is missing or unsafe: $SBOM" >&2
    exit 1
fi
python3 - "$SBOM" "$PACKAGE_DIR" "$ARCHIVE_NAME" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

sbom_path, package_dir, archive_name = sys.argv[1:]
package_root = Path(package_dir).resolve()
extraction_root = package_root.parent
with open(sbom_path, encoding="utf-8") as stream:
    document = json.load(stream)
assert document.get("bomFormat") == "CycloneDX"
assert document.get("specVersion")
root = document.get("metadata", {}).get("component", {})
assert root.get("name") == archive_name
build_info = Path(package_dir) / "BUILD_INFO"
assert build_info.is_file() and not build_info.is_symlink()
build_fields = {}
for line in build_info.read_text(encoding="utf-8").splitlines():
    if "=" in line:
        key, value = line.split("=", 1)
        build_fields[key] = value
source_commit = build_fields.get("source_commit", "")
assert len(source_commit) == 40 and all(
    character in "0123456789abcdefABCDEF" for character in source_commit
)
assert root.get("version") == source_commit
components = document.get("components")
assert isinstance(components, list) and components
inventory = [
    component for component in components
    if any(
        prop.get("name") == "orchestra:inventory-scope"
        and prop.get("value") == "packaged-executable"
        for prop in component.get("properties", [])
    )
]
assert inventory
names = set()
for component in inventory:
    relative = component.get("name")
    assert isinstance(relative, str) and not relative.startswith("/")
    path = (extraction_root / relative).resolve()
    try:
        path.relative_to(package_root)
    except ValueError as error:
        raise AssertionError("SBOM component escapes package root") from error
    assert path.is_file() and not path.is_symlink()
    hashes = component.get("hashes", [])
    sha256 = next(
        item.get("content") for item in hashes if item.get("alg") == "SHA-256"
    )
    assert hashlib.sha256(path.read_bytes()).hexdigest() == sha256
    names.add(relative)
assert any(name.endswith("/orchestra_paper_cpu") for name in names)
assert any(name.endswith("/orchestra_bridge") for name in names)
print(f"RELEASE_VERIFICATION_PASS sbom_components={len(inventory)}")
PY

echo "RELEASE_VERIFICATION_PASS archive=$ARCHIVE_NAME"
