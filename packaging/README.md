# Native Linux packages

[release-targets.json](release-targets.json) defines 35 pinned distro/architecture
targets. [ci-build-target.sh](ci-build-target.sh) runs only in disposable CI
containers. It compiles userspace tools and the bridge, builds a native package,
and runs [package-smoke.sh](package-smoke.sh) before retaining the package.
[aggregate-release.py](aggregate-release.py) records PASS/FAIL/UNKNOWN results
and refuses release assembly when no package passes.

Full source regressions run independently on x86_64 and ARM64. Native target
checks cover compilation and package lifecycle, not kernel runtime execution.
DEB dependencies are resolved from native binaries; RPM dependencies use the
native dependency generator. Packages do not activate sched_ext or ship a
kernel-independent BPF object. See the [release guide](../docs/releases/v1.1.0.md).

The SPDX SBOM records package identity and artifact checksum with filesAnalyzed
false; it is not a complete dependency inventory. Logs, checksums, source TAR/ZIP
and GitHub provenance accompany releases. Tag vVERSION triggers publication;
manual workflow dispatch performs a build-only preflight.

For older native libbpf/UAPI headers, CI builds checksum-pinned upstream
libbpf 1.7.0 as a static loader dependency. Its full source and license notices
are included in the affected packages and identified in their SBOMs. This
changes the build dependency, not the scheduler source or runtime safety gates.
A current sched_ext-capable kernel is still required for scheduler activation.
