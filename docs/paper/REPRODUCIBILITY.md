# Reproduction guide

## Verify the paper tables

From the repository root, run:

```bash
(cd docs/paper && sha256sum -c CHECKSUMS.sha256)
```

The compact evidence was copied without modifying its contents. The source
package is the prepared 18 September 2026 supplement, report directory
`20260901-orchestra-cfs-research-paper`. The original provenance and build
hashes are retained here. An independently verified public DOI for that
hardware supplement is not available in the supplied publication metadata.
The earlier Figshare simulation DOI must not be substituted for it.

For P20, group `photo_archival_pairs.csv` by `rep`, requiring one `cfs` and
one `orchestra` row per pair. Calculate each reduction as
`100 * (cfs_elapsed_ms - orchestra_elapsed_ms) / cfs_elapsed_ms`.
Report the mean of those 20 pair-level reductions; its confidence interval
uses their sample standard deviation and the Student-t critical value for
19 degrees of freedom. Background service reduction uses the two mode means
of `bg_cpu_ticks`. The original statistics file records ownership, exit,
output-validation, thermal, and unload outcomes.

For FW3, use `fixed_work_raw/` to recompute the arithmetic mean and sample
standard deviation of three repetitions for each mode/workload/worker count.
The ratio is `ORCHESTRA mean / default Linux mean`. Preserve all negative,
blocked, and excluded outcomes.

## Validate the current source

On Linux, use a C compiler, make, and Python 3. Clang enables the additional
compiler/static-check passes. libbpf development headers are required for the
optional loader build; a missing prerequisite must be reported as blocked.
From a writable source checkout:

```bash
make
make check
make test
python3 scripts/check-doc-links.py
./scripts/check-system.sh --json
```

These are portable userspace/source checks. They do not load a scheduler.
The separate [userspace smoke protocol](../experiments/paper_cpu_smoke_v1.md)
produces new descriptive smoke data, not a reproduction of the paper's
kernel comparison. Never substitute its results for P20 or FW3.

## Historical hardware experiments

Exact replication needs the recorded kernel/BTF, matching object and bridge,
original workload fixtures, CPU placement, experiment protocol, ownership
telemetry, and environment controls. The current source ZIP is not a frozen
copy of the paper's tested runtime bundle. Removed generated headers and
binaries remain accessible in the pre-cleanup Git revision
`1c4d10f4f15db282be88055772342d07faf33263`; that old snapshot does not contain
all September supplement records.

The [three-condition protocol](protocol/PREREGISTERED_THREE_ARM_PROTOCOL.md)
is archived and unexecuted. Its [schedule](protocol/RANDOMIZATION_SCHEDULE.csv)
and [target manifest](protocol/TARGET_BUILD_MANIFEST.txt) identify its frozen
inputs; the protocol describes additional required fixtures and quota
calibration. Changing those inputs requires a new registration. Kernel testing
requires a dedicated host and the safety/ownership gates in the maintained
[installation guide](../installation/INSTALL.md).
