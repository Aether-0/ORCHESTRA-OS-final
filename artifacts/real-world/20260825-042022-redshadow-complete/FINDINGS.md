# Findings

## Fixed and verified

1. The compact deferred timer path now loads under the running kernel verifier
   and releases bounded SLEEP/THROTTLE entries without leaving tasks stuck.
2. Observation publication is split from dispatch-stack-heavy paths; live v10
   coordination windows finalize and expose S1/S2/S3/S4/Q.
3. Required signal directives reject stale frames in BPF while the bridge
   publication command remains a map publication operation.
4. Committed policy entries are applied to the active state bank and can drive
   an owned task to effective YIELD.
5. Ordinary bridge publication inherits active controller/policy generation
   instead of resetting it.
6. The live controller performs one bounded deficit-selected actuator step per
   update and returns the actuator toward its default after recovery.
7. The benchmark runner accepts an explicit `ORCHESTRA_SCX_SIMPLE` path,
   allowing the existing kernel-source scheduler to be tested without a
   system-wide install.

## Preserved failures

- Earlier fix15/fix16/fix17/fix19/fix20/fix21 attempts preserved verifier
  stack/E2BIG failures in the raw campaign logs.
- The first `scx_simple` build attempt preserved the missing-outdir and
  missing-`llvm-strip` failures; the retry with `/usr/sbin/bpftool` passed.
- The first MIGRATE attempt preserved the expected affinity rejection when
  the task was pinned only to CPU 0; the legal 0,1 affinity retry passed.
- The first ORCHESTRA stress invocation preserved the hard-coded source-bridge
  block; the explicit installed-bridge retry passed.

## Open limitations

- The kernel signal map is schema/freshness checked but not cryptographically
  authenticated in BPF.
- The current live controller path demonstrates bounded actuator adaptation
  and recovery, not the complete staged rollback/multi-actuator state machine.
- The benchmark suite is short and exploratory; memory stress is blocked by a
  missing dependency and no package installation was performed.
- NUMA, distributed scheduling, RT coexistence, hotplug, and production soak
  remain untested or not implemented on this host.
