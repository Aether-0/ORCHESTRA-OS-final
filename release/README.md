# Historical release package

This directory contains archived stage-era reports, scripts, and manifests.
It is retained for research reproducibility and is not the authoritative
installation or runtime interface for the current product.

Use the repository root and maintained documentation instead:

- [`README.md`](../README.md) — current product entry point
- [`docs/installation/INSTALL.md`](../docs/installation/INSTALL.md) — current installation
- [`docs/usage/USAGE.md`](../docs/usage/USAGE.md) — current CLI and runtime usage
- [`docs/validation/FINAL_VALIDATION_REPORT.md`](../docs/validation/FINAL_VALIDATION_REPORT.md) — current gate status
- [`FINAL_PRODUCT_STATUS.md`](../FINAL_PRODUCT_STATUS.md) — version and claim boundary

The historical scripts may contain assumptions from older stage milestones,
including kernel paths and cleanup behavior that are not valid for every host.
Inspect them before reproducing archival evidence. Do not run a broad bpffs
cleanup script from this directory on a machine containing unrelated BPF
state.
