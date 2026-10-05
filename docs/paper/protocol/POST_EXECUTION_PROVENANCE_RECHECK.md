# Post-execution provenance recheck

This recheck confirms that the source snapshot and the target-matched runtime
artifacts used for the current 7.1.5 campaign remain unchanged after the
recorded live tests.

## Source snapshot

`sha256sum -c raw-campaign/raw/source_hashes.sha256`, run from the repository
root, verified all six captured paths:

- `kernel/sched_ext/bpf/orchestra_sched.bpf.c`
- `kernel/sched_ext/orchestra_scx_stage7.bpf.c`
- `kernel/sched_ext/bridge/orchestra_bridge.c`
- `kernel/sched_ext/bridge/orchestra_loader.c`
- `scripts/build.sh`
- `scripts/demo_realworld_zstd_cfs_vs_orchestra.sh`

The captured staged patch SHA-256 and the current `git diff --cached` SHA-256
both equal `f18b738c74f13dd4770cd4b2bc3dee2bcacd09d21fbf2aa4b49bfefc59d62de3`.

## Runtime artifact snapshot

The out-of-tree build manifest and current files agree:

| Item | SHA-256 |
|---|---|
| BPF object | `8f3dbdc53c6789c62a522489b285fb51a6985496f8f4b86688786f0e778e647d` |
| Bridge | `4d62221709d461b757985bd1b8959175862b2229c011ebd8b4444e686d205e0d` |
| Loader | `af1aa651804a483a3952798e0e1f6f05f35545a2b8400fc2f733f32da22939d2` |
| Running-BTF-derived `vmlinux.h` | `114e71f27d2dd23175d3752087872cc035e9923cf36c770c335251c428230e29` |

This supports reproducible attribution of the current runtime observations.
It does not make a changed working tree clean, prove behavior outside the
tested paths, or replace a target-matched rebuild if any captured source or
kernel input changes.
