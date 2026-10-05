# Paper companion publication audit — 1.0.3

This release changes documentation, navigation, citation metadata, and the
selection of historical evidence. Runtime implementation, regression tests,
operational scripts, experiment manifests/schemas, and Makefile are unchanged
from 1.0.2; earlier [offline build and test results](PUBLIC_RELEASE_1_0_1.md)
are carried forward. No new scheduler or performance experiment is performed.

| Publication requirement | Verification |
| --- | --- |
| README explains the submitted hardware paper separately from the simulation study | Manuscript title and authors checked against the supplied Draft1; the final article DOI is not asserted. |
| Results are bounded and adverse outcomes visible | P20 and all seven FW3 summary rows independently recomputed; thermal/service confounding, CPU slowdown, action uncertainty and unrun cgroup comparison remain explicit. |
| Public evidence is preserved | All copied data bytes match the original prepared hardware package; 35 copied files pass their complete checksum manifest. |
| Directory tree is navigable | Every primary source/documentation directory has an index; maintained links were inspected across 114 Markdown files with zero missing local targets at the recorded audit. Historical records are explicitly exempt from current-layout claims. |
| Citation and release identity are consistent | CFF YAML parsed and required citation fields checked; software author matches the public maintainer name, license list follows component notices, version matches VERSION. |
| Large copied kernel snapshot is removed without losing its identity | 3,808 regular-file hashes and four symlink targets verified against v1.0.2; retired inventories preserve retrieval information. Logs and measured results remain. |
| No coding-agent instructions are restored | Tracked filenames and release archive contents are checked for the removed instruction-file names. |
| Publication checks pass | Existing security scan, maintained-document checker and whitespace checks pass; curated source export and release checksums are verified. |

Detailed recorded checks are under [paper/audit](../paper/audit). The earlier
[Figshare simulation dataset](https://doi.org/10.6084/m9.figshare.32925431.v1)
was verified through its public Figshare metadata. It is not relabeled as the
submitted hardware paper's DOI or the unpublished supplement's deposit.

The local submitted manuscript is not uploaded as a publisher-approved paper.
The repository provides the paper's title, author list, data, reproduction
boundaries and software source. Missing full historical fixtures are declared
in the reproduction guide; current-source validation is not an exact hardware
replication. Source archives omit bulk historical campaigns, retained in Git,
and include the compact paper tables and checksum manifest.
