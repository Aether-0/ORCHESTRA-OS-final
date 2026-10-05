# v1.1.1 publication validation

This patch reorganizes documentation and release downloads. Scheduler source,
Makefile and executable unit/integration/security tests remain unchanged from
v1.1.0. Historical measurements and their recorded checksums are retained.

Required gates:

- Check every maintained Markdown document in the checkout and source export;
  historical snapshots and raw evidence retain their original provenance.
- Verify all 35 compact paper evidence files against their recorded checksums.
- Validate each native target's image, distribution, architecture, source commit,
  package filename and corresponding SPDX package checksum before aggregation.
- Verify evidence/SBOM ZIP contents byte-for-byte before bundling replaces
  individual files; include internal checksums for every retained member.
- Preserve executable permissions and source file bytes in source ZIP/TAR.
- Normalize public source permissions for reproducibility across checkouts.
- Run existing source regressions on x86_64 and ARM64, and build/check/install/
  remove/reinstall native packages for all 35 target profiles in CI.
- Verify public download checksums, provenance subjects, latest-release state
  and source identity after publication.

The release PLATFORM_RESULTS.md and RELEASE_MANIFEST.json provide the actual
results. Detailed logs and records are in build-evidence.zip; package SPDX
records are in package-sboms.zip. No failed measurement is converted to a
passing result through this reorganization. The earlier preflight history is
recorded in [v1.1.0 validation](PUBLIC_RELEASE_1_1_0.md).

These gates validate publication and userspace packaging. They do not establish
kernel runtime compatibility, performance superiority or deployment readiness.
