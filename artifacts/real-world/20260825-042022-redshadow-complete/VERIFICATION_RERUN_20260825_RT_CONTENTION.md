# Verification Rerun — RT Contention — 2026-08-25

An actual `SCHED_FIFO` priority-1 task was pinned to CPU 0 while four normal
tasks were pinned to CPUs 1–4, admitted to ORCHESTRA, and published with RUN.
All four normal workers completed successfully and each recorded positive
accepted, dispatched, running, and effective counters while the FIFO task was
running.

Evidence:

- RT policy and process tree: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/rt-contention/rt.ps` and `rt.policy`
- Normal ownership snapshots: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/rt-contention/ownership-final.tsv`
- Worker completion: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/rt-contention/worker-results.txt`
- Commands and return codes: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/COMMANDS.log`

The first probe wrapper incorrectly marked its loader as unloaded before the
process-exit trap ran. That left the scheduler enabled temporarily; the
existing loader was immediately invoked separately and returned `0`, restoring
`sched_ext=disabled` with only the bpffs root remaining. This harness defect is
preserved in the command log and is not treated as a clean first-pass result.

This is limited RT coexistence/forward-progress evidence. It does not prove
fairness, priority inversion absence, starvation freedom, deadlock freedom,
or long-duration mixed-class safety.
