# Kernel prototype

The maintained scheduler lives in [sched_ext](sched_ext/README.md). Its stable
BPF entry point is [orchestra_sched.bpf.c](sched_ext/bpf/orchestra_sched.bpf.c),
with bridge/loader source and shared ABI headers beside it. Target-matched
build, verifier acceptance, task ownership, effective actions, and safe unload
are separate gates. See the [paper evidence map](../docs/paper/EVIDENCE.md)
and [installation guide](../docs/installation/INSTALL.md).
