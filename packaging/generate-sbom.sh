#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 PACKAGE OUT_JSON" >&2
    exit 2
fi
PACKAGE=$1
OUTPUT=$2
[ -f "$PACKAGE" ] || { echo "package not found: $PACKAGE" >&2; exit 1; }
mkdir -p "$(dirname -- "$OUTPUT")"

VERSION=${ORCHESTRA_VERSION:-$(tr -d '\n' < VERSION 2>/dev/null || echo unknown)}
ASSET=$(basename -- "$PACKAGE")
SHA=$(sha256sum "$PACKAGE" | awk '{print $1}')
SIZE=$(stat -c '%s' "$PACKAGE")
EPOCH=${SOURCE_DATE_EPOCH:-0}
CREATED=$(date -u -d "@$EPOCH" '+%Y-%m-%dT%H:%M:%SZ')
DOC_UUID=$(printf '%s' "$ASSET:$SHA" | sha256sum | awk '{print $1}')
cat > "$OUTPUT" <<EOF
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "ORCHESTRA-OS ${VERSION} ${ASSET}",
  "documentNamespace": "https://github.com/Aether-0/ORCHESTRA-OS-final/sbom/${DOC_UUID}",
  "creationInfo": {
    "created": "${CREATED}",
    "creators": ["Tool: ORCHESTRA-OS package SBOM generator"],
    "licenseListVersion": "3.23"
  },
  "packages": [{
    "SPDXID": "SPDXRef-Package-orchestra-os",
    "name": "orchestra-os",
    "versionInfo": "${VERSION}",
    "downloadLocation": "NOASSERTION",
    "filesAnalyzed": false,
    "licenseConcluded": "NOASSERTION",
    "licenseDeclared": "MIT AND GPL-2.0-only",
    "copyrightText": "NOASSERTION",
    "checksums": [{"algorithm": "SHA256", "checksumValue": "${SHA}"}],
    "externalRefs": [{
      "referenceCategory": "PACKAGE-MANAGER",
      "referenceType": "purl",
      "referenceLocator": "pkg:generic/orchestra-os@${VERSION}"
    }],
    "supplier": "Organization: Aether-0"
  }],
  "files": [],
  "relationships": [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": "SPDXRef-Package-orchestra-os"}],
  "annotations": [{
    "annotationDate": "${CREATED}",
    "annotationType": "OTHER",
    "annotator": "Tool: ORCHESTRA-OS package SBOM generator",
    "comment": "Asset ${ASSET}; bytes=${SIZE}; sha256=${SHA}"
  }]
}
EOF
if [ -n "${ORCHESTRA_LIBBPF_ARCHIVE:-}" ]; then
    python3 - "$OUTPUT" <<'PYLIBBPF'
import json, sys
from pathlib import Path
path=Path(sys.argv[1]); document=json.loads(path.read_text())
document['packages'].append({
    'SPDXID':'SPDXRef-Package-libbpf', 'name':'libbpf', 'versionInfo':'1.7.0',
    'downloadLocation':'https://github.com/libbpf/libbpf/archive/refs/tags/v1.7.0.tar.gz',
    'filesAnalyzed':False, 'licenseConcluded':'NOASSERTION',
    'licenseDeclared':'BSD-2-Clause OR LGPL-2.1-only', 'copyrightText':'NOASSERTION',
    'checksums':[{'algorithm':'SHA256','checksumValue':'7ab5feffbf78557f626f2e3e3204788528394494715a30fc2070fcddc2051b7b'}]})
document['relationships'].append({'spdxElementId':'SPDXRef-Package-orchestra-os','relationshipType':'STATIC_LINK','relatedSpdxElement':'SPDXRef-Package-libbpf'})
path.write_text(json.dumps(document,indent=2)+'\n')
PYLIBBPF
fi
python3 -m json.tool "$OUTPUT" >/dev/null
echo "SBOM_COMPLETE $OUTPUT"
