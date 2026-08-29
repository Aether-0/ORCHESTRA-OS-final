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
  "documentNamespace": "https://github.com/Aether-0/ORCHESTRA-OS-Public/sbom/${DOC_UUID}",
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
    "licenseDeclared": "MIT",
    "copyrightText": "NOASSERTION",
    "packageVerificationCode": {
      "packageVerificationCodeValue": "${SHA}"
    },
    "externalRefs": [{
      "referenceCategory": "PACKAGE-MANAGER",
      "referenceType": "purl",
      "referenceLocator": "pkg:generic/orchestra-os@${VERSION}"
    }],
    "supplier": "Organization: Aether-0"
  }],
  "files": [],
  "relationships": [],
  "annotations": [{
    "annotationDate": "${CREATED}",
    "annotationType": "OTHER",
    "annotator": "Tool: ORCHESTRA-OS package SBOM generator",
    "comment": "Asset ${ASSET}; bytes=${SIZE}; sha256=${SHA}"
  }]
}
EOF
python3 -m json.tool "$OUTPUT" >/dev/null
echo "SBOM_COMPLETE $OUTPUT"
