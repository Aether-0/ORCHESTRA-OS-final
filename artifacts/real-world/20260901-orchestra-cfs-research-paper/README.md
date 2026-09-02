# ORCHESTRA-OS versus Linux CFS: publication package

This directory contains the publication-ready manuscript and the compact,
machine-readable evidence set used to produce it.

Author: **S.W. ZAW**. The real-world validation records and companion evidence
supplement are the author's project archives; exact provenance and source
hashes are recorded in `PROVENANCE.md`.

## Deliverables

- `ORCHESTRA_OS_CFS_COMPARATIVE_RESEARCH_PAPER.pdf` — rendered manuscript.
- `ORCHESTRA_OS_CFS_COMPARATIVE_RESEARCH_PAPER.tex` — reproducible LaTeX source.
- `PROVENANCE.md` — source identity, evidence classes, and claim boundaries.
- `DATA_DICTIONARY.md` — definitions for the included evidence tables.
- `EVIDENCE_CALCULATIONS.md` — formulas and independently auditable derived
  quantities used in the manuscript.
- `data/parameter_inventory.csv` — source-backed ORCHESTRA parameters, host CFS
  baseline observations, and the evidence boundary for each comparison.
- `FIGURE_INDEX.md` — figure purposes, evidence classes, and source mapping.
- `FINAL_AUDIT.md` — final render, numerical, provenance, and integrity checks.
- `CHECKSUMS.sha256` — integrity hashes for the non-transient package files.
- `data/` — byte-preserved result tables and compact summaries copied from the
  repository's real-machine evidence store, including per-run fixed-work,
  action, stress, RT, integrity, and checklist evidence.

## Central conclusion

The current prototype is not a general replacement for Linux CFS. Its strongest
demonstrated use is an opt-in foreground-protection policy: known foreground
tasks remain runnable while known, deferrable background tasks receive an
explicit service budget. In the held-out photo/archive experiment, foreground
completion improved in all 20 pairs, while background CPU service was reduced by
97.69%. That is a controlled service-reallocation result, not free total
acceleration.

In matched fixed-work trials, ORCHESTRA was slower than the CFS baseline for
pure CPU work and directionally faster in the small mixed CPU/I/O sample. The
paper reports both results and does not claim a universal speedup.

## Reproduction boundary

The paper uses the primary simulation study and the author-controlled
real-machine validation archive as source documents, plus the later
ownership-gated campaign tables preserved in this repository. The raw
boot-journal fixture remains in the author-controlled real-world archive; its
dimensions and digest are recorded in `PROVENANCE.md` and the evidence package.

No ORCHESTRA implementation, kernel/BPF source, test, or benchmark script was
modified to create this package.
