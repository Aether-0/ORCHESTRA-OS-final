# v1.1.0 release validation

## Scope

The public ORCHESTRA-OS-final repository now uses the paper companion layout.
Scheduler implementation, Makefile and executable tests retain upstream
revision 4c1fb8a; changes concern documentation, repository organization,
packaging dependencies and CI. Archived paper results remain historical.
No new privileged scheduler or heavy hardware benchmark campaign was run.
The supplied manuscript's hash and repository citation are recorded in the
[paper guide](../paper/README.md).

## Preserved preflight evidence

- [Initial workflow run](https://github.com/Aether-0/ORCHESTRA-OS-final/actions/runs/37375065508): both architecture source regressions passed; matrix generation failed because of a Python expression error. The workflow now writes and validates matrix JSON directly.
- [35-target preflight](https://github.com/Aether-0/ORCHESTRA-OS-final/actions/runs/37375496773): both source regressions passed; 30 native targets passed. Ubuntu 22.04 and Debian 12 on both architectures lacked required libbpf/UAPI definitions. Arch compiled but rejected archive metadata names prefixed with `./`.

Older-header packaging now builds checksum-pinned upstream libbpf 1.7.0
as a static dependency, retains the full upstream source/license notices,
and identifies the component in the package SBOM. External dependency headers
use the compiler's system-header search path; project warning flags are
unchanged. Arch metadata now has the native archive paths expected by pacman.

The final tagged workflow repeats source gates and native package checks.
Its authoritative PASS/FAIL/UNKNOWN records, source commit, pinned image
identities and asset hashes are published in PLATFORM_RESULTS.json and
RELEASE_MANIFEST.json. Only passing native targets receive packages.
Source ZIP preserves executable permissions and symlinks.

## Evidence boundary

Package installation/removal and CLI smoke tests validate packaging. They
are not kernel verifier, attach, scheduler ownership, effective-action,
performance or deployment validation. Kernel BPF objects are target-specific
and are not shipped. Existing releases and negative research results remain
available. Package SPDX documents provide artifact identity/checksums and
bundled libbpf identity where applicable, not full transitive inventories.
