#!/usr/bin/env python3
"""Publish only packages that completed native smoke tests; preserve all failures."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import stat
import time
import tarfile
import zipfile

root = Path(sys.argv[1])
version = os.environ['RELEASE_VERSION']
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
expected = json.loads(Path('packaging/release-targets.json').read_text())['include']
results = []
for target in expected:
    path = root / (target['name'] + '-result.json')
    result = json.loads(path.read_text()) if path.exists() else {'target': target['name'], 'status': 'UNKNOWN'}
    if result.get('source_commit', commit) != commit:
        raise SystemExit('Result belongs to a different source revision')
    results.append(result)
passed = [result for result in results if result['status'] == 'PASS']
if not passed:
    raise SystemExit('No native package passed; refusing to assemble release')
packages = [p for p in root.iterdir() if p.name.endswith(('.deb', '.rpm', '.apk', '.pkg.tar.zst'))]
if len(packages) != len(passed):
    raise SystemExit('Passing targets and package count disagree')
for package in packages:
    sbom = Path(str(package) + '.spdx.json')
    if not sbom.exists():
        raise SystemExit('Missing package SBOM')
    json.loads(sbom.read_text())
archive = root / f'orchestra-os-{version}-source.tar.gz'
with tarfile.open(archive) as source, zipfile.ZipFile(root / f'orchestra-os-{version}-source.zip', 'w', compression=zipfile.ZIP_DEFLATED) as output:
    for member in source.getmembers():
        if member.isfile():
            entry = zipfile.ZipInfo(member.name, time.gmtime(max(member.mtime, 315532800))[:6])
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | member.mode) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(entry, source.extractfile(member).read())
        elif member.issym():
            entry = zipfile.ZipInfo(member.name, time.gmtime(max(member.mtime, 315532800))[:6])
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | member.mode) << 16
            output.writestr(entry, member.linkname)
lines = [f'# ORCHESTRA-OS v{version} Linux package results', '', f'Source commit: `{commit}`', '', f'{len(passed)}/{len(expected)} native targets passed compilation and package lifecycle smoke tests.', '', '| Target | Result |', '| --- | --- |']
lines += [f"| {r['target']} | {r['status']} |" for r in results]
lines += ['', 'Only PASS targets have downloadable native packages. FAIL/UNKNOWN targets are not supported by this release. Logs and result JSON files preserve the build evidence.', '', 'These results validate userspace packaging, not sched_ext loading or hardware performance.', '']
(root / 'PLATFORM_RESULTS.md').write_text('\n'.join(lines))
(root / 'PLATFORM_RESULTS.json').write_text(json.dumps(results, indent=2) + '\n')
assets = [{'name': p.name, 'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(root.iterdir()) if p.is_file() and p.name not in ('SHA256SUMS', 'RELEASE_MANIFEST.json')]
(root / 'RELEASE_MANIFEST.json').write_text(json.dumps({'product': 'ORCHESTRA-OS', 'version': version, 'source_commit': commit, 'targets': results, 'kernel_artifacts': 'target-specific-and-not-shipped', 'scheduler_install': 'disabled-by-default', 'assets': assets}, indent=2) + '\n')
