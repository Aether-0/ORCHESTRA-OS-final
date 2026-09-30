#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/orchestra-release-verifier-security.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT HUP INT TERM

# A checksum-valid archive must still be rejected before extraction if it
# contains a link controlled by the archive author.
python3 - "$WORK/malicious.tar.gz" <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], "w:gz") as archive:
    root = tarfile.TarInfo("malicious/")
    root.type = tarfile.DIRTYPE
    archive.addfile(root)
    link = tarfile.TarInfo("malicious/bin")
    link.type = tarfile.SYMTYPE
    link.linkname = "/etc"
    archive.addfile(link)
PY
(cd "$WORK" && printf '%s\n' '{"bomFormat":"CycloneDX","specVersion":"1.7"}' > malicious.tar.gz.cdx.json &&
    sha256sum malicious.tar.gz malicious.tar.gz.cdx.json > SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/malicious.tar.gz" \
    >"$WORK/stdout" 2>"$WORK/stderr"; then
    echo "release verifier accepted an archive symlink" >&2
    exit 1
fi
grep -q 'archive links are not accepted' "$WORK/stderr"

# Even a checksum-valid aggregate manifest must not be allowed to reference
# files outside the release directory.
(cd "$WORK" && {
    sha256sum malicious.tar.gz malicious.tar.gz.cdx.json
    sha256sum /etc/hosts
} > unsafe-path-SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/malicious.tar.gz" \
    "$WORK/unsafe-path-SHA256SUMS" >"$WORK/unsafe-path-out" \
    2>"$WORK/unsafe-path-err"; then
    echo "release verifier accepted an aggregate manifest path escape" >&2
    exit 1
fi
grep -q 'aggregate checksum manifest has unsafe path' "$WORK/unsafe-path-err"

ln -s /etc/hosts "$WORK/release-link"
(cd "$WORK" && {
    sha256sum malicious.tar.gz malicious.tar.gz.cdx.json release-link
} > symlink-SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/malicious.tar.gz" \
    "$WORK/symlink-SHA256SUMS" >"$WORK/symlink-out" \
    2>"$WORK/symlink-err"; then
    echo "release verifier accepted an aggregate manifest symlink" >&2
    exit 1
fi
grep -q 'aggregate checksum manifest references a non-regular file' \
    "$WORK/symlink-err"

# A manifest that omits the sibling SBOM must not be accepted as a partial
# release verification.
(cd "$WORK" && sha256sum malicious.tar.gz > truncated-SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/malicious.tar.gz" \
    "$WORK/truncated-SHA256SUMS" >"$WORK/truncated-out" \
    2>"$WORK/truncated-err"; then
    echo "release verifier accepted an incomplete aggregate manifest" >&2
    exit 1
fi
grep -q 'manifest does not cover' "$WORK/truncated-err"

# Device nodes and FIFOs are rejected during archive inspection, before
# extraction can create an archive-controlled special inode.
python3 - "$WORK/special.tar.gz" <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], "w:gz") as archive:
    root = tarfile.TarInfo("special/")
    root.type = tarfile.DIRTYPE
    archive.addfile(root)
    fifo = tarfile.TarInfo("special/pipe")
    fifo.type = tarfile.FIFOTYPE
    archive.addfile(fifo)
PY
(cd "$WORK" && printf '%s\n' '{"bomFormat":"CycloneDX","specVersion":"1.7"}' > special.tar.gz.cdx.json &&
    sha256sum special.tar.gz special.tar.gz.cdx.json > special-SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/special.tar.gz" \
    "$WORK/special-SHA256SUMS" >"$WORK/special-out" \
    2>"$WORK/special-err"; then
    echo "release verifier accepted an archive special file" >&2
    exit 1
fi
grep -q 'archive special files are not accepted' "$WORK/special-err"

echo "PASS release verifier archive safety regression"

# Duplicate regular members are ambiguous even when outer hashes are valid.
python3 - "$WORK/duplicate.tar.gz" <<'PY'
import io, sys, tarfile
with tarfile.open(sys.argv[1], 'w:gz') as archive:
    for payload in (b'first', b'second'):
        member = tarfile.TarInfo('package/file')
        member.size = len(payload)
        archive.addfile(member, io.BytesIO(payload))
PY
printf '{}' > "$WORK/duplicate.tar.gz.cdx.json"
(cd "$WORK" && sha256sum duplicate.tar.gz duplicate.tar.gz.cdx.json > duplicate-SHA256SUMS)
if bash "$ROOT/scripts/verify-release.sh" "$WORK/duplicate.tar.gz" "$WORK/duplicate-SHA256SUMS" > "$WORK/duplicate.out" 2> "$WORK/duplicate.err"; then
    echo 'duplicate archive accepted' >&2
    exit 1
fi
grep -q 'duplicate archive member' "$WORK/duplicate.err"
echo 'PASS duplicate archive rejection'
