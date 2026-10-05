# Paper companion completion audit — 1.0.4

The [1.0.3 paper companion audit](PUBLICATION_1_0_3.md) records data provenance,
numerical checks, citation identity, folder navigation, and retained limitations.
The final source-archive check also inspects every maintained Markdown link
inside the archive, rather than relying only on the full repository checkout.

That check found three links to excluded bulk historical evidence in 1.0.3.
They now target the versioned GitHub archive. Repository and source-archive
local-link checks must both pass before publication. The archive contains all
35 checksummed compact evidence files; ZIP and TAR.GZ contents must match,
and independently generated source TAR.GZ archives must match byte for byte.

The README identifies the submitted hardware paper, the separate simulation
dataset, positive and negative results, version boundaries, and reproduction
requirements. All primary folders have indexes. Agent instruction files and
the copied Linux header tree are absent. Historical measurements and failure
records remain accessible, with retired-file inventories checked against Git.

This is a documentation/evidence publication. Runtime code, tests, and
operational scripts are unchanged from 1.0.2. Earlier offline validation is
carried forward; no new kernel-runtime, performance, or deployment certification
is claimed. A final hardware article DOI and a public September hardware
supplement DOI are not asserted without verified publication metadata.
