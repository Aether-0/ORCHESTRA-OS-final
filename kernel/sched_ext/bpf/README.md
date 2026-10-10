# Canonical sched_ext implementation

`orchestra_sched.bpf.c` contains the single ORCHESTRA sched_ext implementation.
The historical `../orchestra_scx_stage7.bpf.c` and `../orchestra_scx.bpf.c`
filenames are compatibility wrappers that include this file.

Build with `scripts/build.sh --kernel` from the repository root, using the
target-matched kernel source described in
[INSTALL.md](../../../docs/installation/INSTALL.md).
