# ORCHESTRA-OS 1.0.1 publication validation

Validated on 6 October 2026 (Asia/Kolkata). The implementation and tests match
reviewed upstream commit `4c1fb8a` (resolve `4c1fb8a` in the upstream repository).
Publication changes update the public layout, version, repository URLs,
source-release documentation, and manual legacy-package workflow.

| Command | Return code | Outcome |
| --- | --- | --- |
| `make clean` | 0 | PASS |
| `make` | 0 | PASS |
| `make check` | 0 | PASS |
| `make test-unit` | 0 | PASS |
| `make test-integration` | 0 | PASS |
| `make security-test` | 2 | FAIL — see diagnosis below |
| `make security-test` | 0 | PASS |
| `bash scripts/build.sh --userspace --bridge` | 0 | PASS |

Additional gates: tracked-content security scan, documentation links (16
maintained documents), packaging shell syntax, and staged whitespace checks
PASS. Implementation and test sources in `kernel/`, `userspace/`, `tests/`,
and `orchestra_paper_cpu_demo/` match the reviewed source. Unit output reports
31/31 named core unit tests plus the compiler, ABI, publication, and Python
regressions. Integration and security suites complete successfully.

## Preserved failures and diagnosis

The initial publication scan failed on an embedded font string in a decorative
HTML demo and a machine-specific home path in an archived report. The generated
demo was removed and the home path was redacted; the historical observations
remain. The unchanged scan subsequently passed.

The first security-suite invocation inherited `ORCHESTRA_BUILD_DIR` from the
outer build campaign. An installation fixture therefore looked for a product
binary in the external directory before it was built. No test or implementation
was changed. Rerunning the suite with that variable unset passed. Both runs
and their return codes are included in the downloadable validation logs.

## Scope and limitations

This is a research source release. No scheduler load/unload, new performance
comparison, kernel stress test, or deployment certification was performed.
The target-specific sched_ext verifier, ownership, THROTTLE efficacy, and
runtime/concurrency limitations remain visible in the release documentation.
No native packages, cryptographic signatures, or hosted build attestations
are included. Historical measurements and negative results are retained;
generated host binaries and BTF headers are removed from the current tree.
