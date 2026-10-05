# ORCHESTRA-OS research artifacts

This directory stores bounded research evidence requested for local retention.
It is not, by its presence, evidence that ORCHESTRA-OS is experimentally
validated, kernel-prototyped, production-secure, or deployment-ready.

## Handling rules

- Treat copied raw benchmark CSV, JSON, and logs as immutable.
- Put new analysis beside raw evidence; never rewrite the raw files.
- Classify authoritative, superseded, failed, excluded, and exploratory runs
  separately.
- Retain a content-hash manifest and the command/environment record for each
  captured campaign.
- Do not add production keys, credentials, session material, private data, or
  unrelated machine dumps.
- Do not cite superseded or development-smoke artifacts as final evidence.
- Large future traces should use approved external storage, with durable
  checksums and retrieval information retained here.

The available 2026-08-03 evidence is indexed in
[test-results/2026-08-03/README.md](test-results/2026-08-03/README.md).

The 2026-08-05 capture adds git-provenanced regression evidence and the first
retained signal-publication microbenchmark campaign; see
[test-results/2026-08-05/](test-results/2026-08-05/).


## Publication evidence map

The compact [paper evidence tables](../docs/paper/README.md) provide a checksummed
entry point for reviewers. Existing `test-results/` contains userspace/VM
campaigns; `real-world/` and `real-machine-*` retain hardware preparation,
runtime observations, and failure records. These earlier archives are scoped
to their recorded environment and revision.

Some campaign inventories list generated binaries, BTF headers, or copied
Linux headers removed from the current tree. See the [retired material
record](../docs/history/RETIRED_GENERATED_FILES.md) before checking an old
manifest. Negative results and blocked coverage are not removed.
