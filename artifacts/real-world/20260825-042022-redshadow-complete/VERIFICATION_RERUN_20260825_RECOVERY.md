# Verification Rerun — Recovery — 2026-08-25

An exact-TID normal worker was admitted and dispatched through ORCHESTRA while
it remained active. The existing loader was then unloaded before the worker
finished. The worker completed successfully after unload, the unload returned
`0`, and `/sys/kernel/sched_ext/state` returned `disabled`.

Evidence:

- Result: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/unload-with-work/result.txt`
- Owned status before unload: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/unload-with-work/status-before.stdout`
- Commands and return codes: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/COMMANDS.log`

This validates scoped unload and forward progress for the exercised normal
worker. It does not validate panic recovery, boot fallback, CPU hotplug,
filesystem recovery, or every fault-injection path.
