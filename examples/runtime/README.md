# Foreground runtime lifecycle

This example mirrors the foreground lifecycle used by sched_ext/scx-style
tools. It is only for a dedicated, authorized test host with a
target-matched, installed kernel artifact set.

## Command

```bash
sudo orchestra check-system --strict
sudo orchestra run --interval 5
```

While running, the command prints the timestamp, sched_ext state, active ops
name, and bridge telemetry when the installed bridge is available. Start a
separate workload only after the ownership gate is visible. Press Ctrl-C to
run bounded cleanup and return the machine to conventional scheduling.

For an already-running instance, use the read-only monitor:

```bash
sudo orchestra monitor --interval 5
```

## Expected evidence

- `state=enabled` and `ops=orchestra_scx_v8` while ORCHESTRA owns sched_ext;
- telemetry records that distinguish requested, accepted, dispatched,
  effective, and fallback actions;
- after Ctrl-C, `/sys/kernel/sched_ext/state` reports `disabled` when this
  command performed the attach;
- no unrelated `/sys/fs/bpf` objects are removed.

If ownership, artifact integrity, or cleanup cannot be proven, the command
fails closed. Preserve the first error and follow the troubleshooting guide;
do not broad-delete bpffs state.
