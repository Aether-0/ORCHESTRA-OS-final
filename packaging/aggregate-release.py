#!/usr/bin/env python3
"""Validate native results and assemble concise, independently verifiable assets."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tarfile
import time
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_name(target, version):
    distro, arch = target['distro'], target['package_arch']
    return {
        'deb': f'orchestra-os_{version}-1_{distro}_{arch}.deb',
        'rpm': f'orchestra-os-{version}-1.{distro}.{arch}.rpm',
        'apk': f'orchestra-os-{version}-r0.{distro}.{arch}.apk',
        'pacman': f'orchestra-os-{version}-1.{distro}-{arch}.pkg.tar.zst',
    }[target['format']]


def zip_entry(name, data, epoch, mode=0o644):
    entry = zipfile.ZipInfo(name, time.gmtime(max(epoch, 315532800))[:6])
    entry.create_system = 3
    entry.external_attr = (stat.S_IFREG | mode) << 16
    entry.compress_type = zipfile.ZIP_DEFLATED
    return entry, data


def bundle(root, name, paths, description, epoch):
    """Check the completed archive before removing individual release-page files."""
    payload = {path.name: path.read_bytes() for path in paths}
    payload['README.md'] = description.encode()
    payload['SHA256SUMS'] = ''.join(
        f'{hashlib.sha256(data).hexdigest()}  {filename}\n'
        for filename, data in sorted(payload.items())
    ).encode()
    archive = root / name
    with zipfile.ZipFile(archive, 'w') as output:
        for filename, data in sorted(payload.items()):
            entry, data = zip_entry(filename, data, epoch)
            output.writestr(entry, data)
    with zipfile.ZipFile(archive) as output:
        if output.testzip() is not None:
            raise SystemExit(f'Corrupted bundle: {name}')
        for filename, data in payload.items():
            if output.read(filename) != data:
                raise SystemExit(f'Bundle changed evidence: {filename}')
    for path in paths:
        path.unlink()


def main():
    root = Path(sys.argv[1])
    version = os.environ['RELEASE_VERSION']
    epoch = int(os.environ['SOURCE_DATE_EPOCH'])
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    expected = json.loads(Path('packaging/release-targets.json').read_text())['include']
    if len({target['name'] for target in expected}) != len(expected):
        raise SystemExit('Duplicate native target identity')
    results, packages = [], []
    for target in expected:
        path = root / (target['name'] + '-result.json')
        result = json.loads(path.read_text()) if path.exists() else {
            'target': target['name'], 'status': 'UNKNOWN', 'source_commit': commit,
        }
        if path.exists():
            identity = {'target': target['name'], 'distro': target['distro'],
                        'image': target['image'], 'format': target['format'],
                        'package_arch': target['package_arch'], 'source_commit': commit}
            if any(result.get(key) != value for key, value in identity.items()):
                raise SystemExit(f'Result identity mismatch: {target["name"]}')
        if result.get('status') not in ('PASS', 'FAIL', 'UNKNOWN'):
            raise SystemExit('Invalid result classification')
        if result['status'] == 'PASS':
            if result.get('build_status') != '0':
                raise SystemExit('PASS result has a failing return code')
            package = root / package_name(target, version)
            sbom = Path(str(package) + '.spdx.json')
            if not package.is_file() or not sbom.is_file():
                raise SystemExit(f'Missing passing package or SBOM: {target["name"]}')
            document = json.loads(sbom.read_text())
            package_record = document['packages'][0]
            expected_checksum = {'algorithm': 'SHA256', 'checksumValue': digest(package)}
            if package_record.get('versionInfo') != version or expected_checksum not in package_record.get('checksums', []):
                raise SystemExit('SBOM does not identify the built package')
            result['package'] = package.name
            packages.append(package)
        results.append(result)
    passed = [result for result in results if result['status'] == 'PASS']
    retained = {path for path in root.iterdir() if path.name.endswith(('.deb', '.rpm', '.apk', '.pkg.tar.zst'))}
    if not passed or retained != set(packages):
        raise SystemExit('Passing target identities and retained native packages disagree')

    archive = root / f'orchestra-os-{version}-source.tar.gz'
    with tarfile.open(archive) as source, zipfile.ZipFile(root / f'orchestra-os-{version}-source.zip', 'w') as output:
        for member in source.getmembers():
            if member.isfile():
                entry, data = zip_entry(member.name, source.extractfile(member).read(), member.mtime, member.mode)
                output.writestr(entry, data)
            elif member.issym():
                entry, data = zip_entry(member.name, member.linkname.encode(), member.mtime, member.mode)
                entry.external_attr = (stat.S_IFLNK | member.mode) << 16
                output.writestr(entry, data)

    lines = [f'# ORCHESTRA-OS v{version} Linux packages', '',
             f'{len(passed)}/{len(expected)} native targets passed build and package lifecycle smoke tests.', '',
             '## Choose a download', '',
             '- Installation: download the native package matching your distribution version and CPU architecture.',
             '- Source: choose the source ZIP or TAR.GZ.',
             '- Verification: SHA256SUMS covers every release download; GitHub build provenance covers every asset.',
             '- Research review: build-evidence.zip contains logs and detailed JSON results.',
             '- Dependency review: package-sboms.zip contains each native package\'s SPDX document.', '',
             f'Source commit: `{commit}`', '', '| Target | Result |', '| --- | --- |']
    lines += [f'| {result["target"]} | {result["status"]} |' for result in results]
    lines += ['', 'Only PASS targets have native packages. Failed or missing evidence remains explicitly classified.', '',
              'Package smoke tests validate userspace packaging, not kernel scheduler loading or performance.',
              'Installation does not enable sched_ext. Kernel BPF artifacts require a target-matched build and are not shipped.', '']
    (root / 'PLATFORM_RESULTS.md').write_text('\n'.join(lines))
    (root / 'PLATFORM_RESULTS.json').write_text(json.dumps(results, indent=2) + '\n')
    evidence = sorted([*root.glob('*.log'), *root.glob('*-result.json'), root / 'PLATFORM_RESULTS.json'])
    bundle(root, 'build-evidence.zip', evidence,
           f'# Build evidence for ORCHESTRA-OS {version}\n\nSource commit: {commit}\n\n'
           'Contains original per-target logs, available result records, and the complete PASS/FAIL/UNKNOWN matrix.\n'
           'After extraction, run `sha256sum -c SHA256SUMS` in this directory.\n'
           'These are userspace packaging results; they do not establish kernel runtime compatibility.\n', epoch)
    bundle(root, 'package-sboms.zip', sorted(root.glob('*.spdx.json')),
           f'# Package SPDX documents for ORCHESTRA-OS {version}\n\nSource commit: {commit}\n\n'
           'Each document identifies its native package checksum and bundled libbpf where applicable.\n'
           'These documents do not enumerate every transitive dependency.\n'
           'After extraction, run `sha256sum -c SHA256SUMS` in this directory.\n', epoch)
    assets = [{'name': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path)}
              for path in sorted(root.iterdir()) if path.is_file() and path.name not in ('SHA256SUMS', 'RELEASE_MANIFEST.json')]
    (root / 'RELEASE_MANIFEST.json').write_text(json.dumps({
        'product': 'ORCHESTRA-OS', 'version': version, 'source_commit': commit,
        'targets': results, 'kernel_artifacts': 'target-specific-and-not-shipped',
        'scheduler_install': 'disabled-by-default', 'assets': assets,
        'evidence_bundle': 'build-evidence.zip', 'sbom_bundle': 'package-sboms.zip',
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
