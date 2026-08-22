# Canonical sched_ext entry point

`orchestra_sched.bpf.c` is the stable product entry point for the single
ORCHESTRA sched_ext implementation. It includes
`../orchestra_scx_stage7.bpf.c`, which is retained as a compatibility filename
for existing scripts and historical evidence.

There must be one scheduler implementation and one ABI contract. Do not add a
second callback implementation under a versioned stage directory. Build the
entry point with `scripts/build.sh --kernel` or the target-matched builder
documented in [INSTALL.md](../../docs/installation/INSTALL.md).
