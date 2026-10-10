# Major scheduler update: fair service with bounded adaptation

Status: proposed design, not implemented or experimentally validated.
Basis: source revision `26eff36` and Linux 7.1 sched_ext documentation.
Proposed target: v2.0.0-alpha, then release candidates after the gates below.
Versioning is provisional: a major version is appropriate if scheduling
contracts or userspace/kernel ABI compatibility change. No release tag or
current version is changed by this plan.

## 1. Outcome

Make ORCHESTRA a fair, responsive scheduler whose adaptive actions improve
measured workloads without silently sacrificing other tasks. Preserve exactly
RUN, SLEEP, MIGRATE, THROTTLE, and YIELD, exact lifetime identity, coherent
publication, bounded verifier work, and scoped loader recovery.

The architecture should be:

```text
task becomes runnable
  -> validate identity, policy, signal, and controller snapshot
  -> decide one canonical action
  -> apply eligibility, affinity, and service safeguards
  -> enqueue eligible work through the fair scheduling base
  -> account actual CPU service and action outcome
  -> aggregate measurements at a slower cadence
  -> evaluate bounded controller changes or roll back
```

Fair service governs eligible tasks. An explicit, valid SLEEP deadline or
THROTTLE budget deliberately changes eligibility; it must not be counted as
ordinary queue starvation. Record policy-induced delay separately and bound
renewal behavior according to a documented admission/service contract.

## 2. Current evidence and gaps

| Area | Inspected current behavior | Design implication |
| --- | --- | --- |
| RUN fallback | `dispatch_run()` uses a fixed slice and built-in global FIFO | Introduce a fair normal-service path; RUN fallback while attached is not Linux scheduling |
| Ownership | Full-switch sched_ext | Fairness must cover unadmitted ordinary tasks as well as adaptively controlled tasks |
| Priority | No registered `set_weight` callback; limitations exclude nice/weight and cgroup CPU control semantics | Define and implement explicit weight/group contracts |
| Placement | `select_cpu()` calls the kernel default and discards the idle result | Evaluate a safe placement improvement after fair accounting is established |
| Runtime | Running/stopping timestamps and cumulative runtime exist | Reuse actual runtime; do not infer service from requested slices or dispatch counts |
| Deferred work | Deadline DSQ, bounded scan, periodic 250 microsecond timer | Measure empty-tick overhead, late release, and backlog before changing the timer |
| Controller | Existing hysteresis, cooldown, bounded actuators, evaluation, rollback | Extend guardrails and behavioral coverage rather than introduce a second controller |
| Coordination | S1/S2/S3/S4/Q and action provenance exist | Preserve the corrected metric; add latency/service objectives alongside Q |
| Validation | Local compilation, regression, verifier, and packaging gates exist | Add kernel behavioral coverage and keep build, verifier, attach, ownership, and performance claims separate |

These observations describe implementation structure, not measured bottlenecks
or proof of starvation on the current revision.

## 3. Ordered engineering work

### M0 — Establish a trustworthy starting point

1. Require green amd64 and arm64 CI on the actual starting commit. The naming
   update's initial CI failed because a new validation document contained a
   machine-specific path; correct the document without weakening the scanner.
2. Preserve the v1.1.1 baseline and all negative evidence. Use an isolated v2
   branch and external build/evidence directories.
3. Write an ADR defining eligible service, latency measurement, administrative
   throttling, fallback behavior, and the boundary between RT bypass and the
   paper's broader Hybrid Safety Layer.
4. Inventory callback/map/ABI constraints against the exact target kernel.

Exit: reproducible baseline, explicit contracts, and passing non-privileged
checks. No claim that the latest runtime fixes have been fully validated.

### M1 — Implement the fair scheduling base first

Use a custom priority DSQ with weighted virtual runtime as the first prototype.
Charge actual execution approximately as `runtime * reference_weight / weight`
using bounded integer arithmetic. Use the target kernel's supported weight
interface and handle runtime weight changes explicitly.

- Initialize new tasks relative to the current service clock; clamp sleep
  credit so repeated sleep/wake cannot create unlimited priority.
- Preserve service debt through MIGRATE, YIELD, deferred release, and policy
  renewal. None of these transitions should reset virtual runtime.
- Route ordinary RUN, eligible THROTTLE, and released SLEEP through the same
  service accounting. Avoid local/direct dispatch paths that evade fairness.
- Handle dequeue, property changes, exit, and CPU-offline races for custom
  queues. Document lifecycle/locking before implementation.
- Define SCHED_NORMAL, SCHED_BATCH, and SCHED_IDLE behavior explicitly; a
  single weight formula must not silently erase their policy distinctions.
- Cover all host-owned tasks. Adaptive map capacity is not permission to lose
  accounting for unadmitted tasks; evaluate task-local storage against the
  target kernel and define failure behavior explicitly.
- Preserve a bounded emergency progress path. Count degraded fallback and
  exclude affected intervals from claims that the fair contract held.

Linux's built-in global/local DSQs cannot implement priority ordering. The
upstream example demonstrates custom-DSQ virtual-time ordering; it is a design
reference, not a ready-made ORCHESTRA implementation. [Sources A, B]

Exit: deterministic accounting tests plus dedicated-host equal/unequal-weight,
dynamic-nice, sleep/wake, fork/exit, affinity, migration, and YIELD coverage.
Measure service while tasks are actually eligible and contending.

### M2 — Bound adaptation and improve latency

- Add per-task eligibility/queue-delay timestamps and bounded histograms for
  p50/p95/p99 delay. Track voluntary waits, policy deferral, and runnable queue
  delay separately; never infer application response time from queue delay.
- Derive bounded slices from observed contention and service debt; retain
  documented minimum/maximum values and test sensitivity before selecting defaults.
- Give waking tasks bounded latency relief that is charged back into service
  accounting. Avoid an unrestricted interactive-priority bypass.
- Apply a documented maximum adaptive duty/deferral contract to cumulative
  SLEEP/THROTTLE renewals. Reject or recover from a policy that exceeds it;
  administrative hard limits need a distinct explicit authorization contract.
- Evaluate controller changes against service and latency guardrails as well
  as Q. Use existing cooldown, rollback, and saturation mechanisms.
- Add staggered action timing and bounded migration/action-change budgets to
  resist synchronized population-wide switching, with S4 retained.
- If predicted signals are used, measure prediction error/confidence and
  compare against an otherwise identical reactive policy. Disable predictive
  influence when freshness or confidence is inadequate.

Exit: foreground latency improves in selected scenarios without violating
the specified background service contract. No universal speedup claim.

### M3 — Improve placement and timer efficiency

- Prefer allowed idle CPUs, then cache-local choices, then bounded load-aware
  alternatives. Introduce one placement rule at a time and retain provenance.
- Add migration cooldown/cost accounting without blocking urgent affinity or
  CPU-offline recovery. Carry service debt across every move.
- Prototype a fair shared queue before per-CPU/cache-domain queues. If queues
  are split, specify stealing and cross-domain service normalization first.
- Measure timer idle cost, deadline lateness, scan backlog, lock contention,
  map access, and callback cost. Prototype earliest-deadline or adaptive timer
  rearming only with a race-safe wakeup/arming design and bounded recovery.
- Keep map and loop bounds. Larger machines require negotiated capacity and
  evidence, not simply increased constants.

Exit: measured improvement over M1/M2 with no loss of service, ownership,
affinity correctness, or deadline progress. NUMA policy remains experimental
until tested on a multi-node machine.

### M4 — Define cgroup CPU control and compatibility

Design hierarchical group weights and task weights together, including group
creation/removal, task moves, nested groups, and runnable population changes.
Evaluate upstream `scx_flatcg` as a reference. Group shares must not be multiplied
incorrectly by a group's thread count. [Source A]

Explicitly separate support for CPU weights, maximum bandwidth, minimum
protection, cpusets, and accounting. Implement and test each claimed interface;
support for one does not imply Linux cgroup CPU-controller parity. Unsupported
controls must be documented and reported before activation, with a strict mode
refusing incompatible configurations.

Version capability records/map schemas when layouts change. Ensure the bridge
and loader reject mismatched bundles, and test supported upgrade/rollback paths.
Keep kernel adapters narrow: sched_ext provides no stable cross-version API.
[Source A]

Exit: nested-group and dynamic task-move tests demonstrate the exact declared
contract. Do not declare deployment readiness while ordinary host service
controls remain unsupported or silent.

### M5 — Validate and release

Progress from deterministic userspace tests to target compilation/verifier,
dedicated-host ownership/action/recovery checks, short comparisons, medium
stability, then longer runs. Runtime testing must pass the existing safety
gate; no heavy campaign is needed to approve this design plan.

Compare:

1. the host's conventional Linux fair scheduler (record the exact kernel);
2. v1.1.1 from an identified commit;
3. the new fair base with adaptation disabled;
4. the fair base plus reactive adaptation;
5. the predictive/controller configuration, when independently observable.

Keep workload, workers, affinity, duration, and collection method identical.
Randomize or balance run order. Preserve all trials. Use at least five paired
repetitions for the planned release comparison; report individual values,
effect sizes and uncertainty rather than treating five runs as automatic proof.

## 4. Proposed acceptance budgets

These are initial engineering targets, not measured achievements or kernel
guarantees. Confirm feasibility against the baseline before implementation;
freeze the protocol and budgets before collecting release results.

| Property | Proposed criterion |
| --- | --- |
| Safety/correctness | No unexplained task loss, corruption, verifier/attach failure, stalled eligible task, or unclean scoped unload in the declared suite |
| Equal-share service | Jain's index at least 0.98 over sufficiently long steady-state eligible CPU service windows; publish inputs and window lengths |
| Weighted service | Per-task normalized share within 10% relative error of its declared expected share in controlled steady-state tests |
| Throughput regression | No more than 5% loss against the new fair base in predeclared scenarios unless explicitly accepted and documented |
| Latency objective | At least 10% p99 improvement in one predeclared mixed-load scenario while meeting service and throughput budgets |
| Deferred progress | No lost wakeup; report worst and p99 deadline lateness under the specified backlog, CPU count, and load |
| Recovery | Bound and publish detach/lease-expiry/revocation recovery under tested conditions; define the numerical deadline in M0 |
| Scaling | Publish CPU cost, memory, queue delay, fairness, and migration rate at each tested worker/CPU count |

For Jain's index use `(sum(x))^2 / (n * sum(x^2))`, with positive aggregate
service and an explicitly defined eligible population. For weighted tests,
state the expected share and available CPU/affinity constraints. Exclude RT
tasks and report policy-deferred intervals separately. Guardrails require both
per-task results and aggregate measures so an average cannot hide starvation.

## 5. Commit and release sequence

1. Baseline contracts, metrics, and behavioral tests.
2. Fair queue/accounting implementation with adaptation disabled.
3. Integration of all five actions with service/latency safeguards.
4. Placement and timer optimization, each with an ablation comparison.
5. Group control, capability negotiation, and compatibility checks.
6. Runtime evidence and documented limitations.
7. Alpha, release candidate, then stable after the declared gates pass.

Do not combine the fair-queue change, controller redesign, timer redesign,
and topology changes into one unreviewable patch. Cryptographic kernel signal
authentication, online learning, distributed coordination, and energy claims
need their own designs and evidence; they are not prerequisites for the first
fairness prototype and are not claimed by this roadmap.

## Sources

- **A:** [Linux 7.1 sched_ext documentation](https://docs.kernel.org/7.1/scheduler/sched-ext.html):
  queue/lifecycle semantics, ownership, cgroup example, and API instability.
- **B:** [Linux 7.1 scx_simple source](https://github.com/torvalds/linux/blob/v7.1/tools/sched_ext/scx_simple.bpf.c):
  custom shared queue, virtual runtime, bounded sleep credit, and weight charging.
- Repository basis: `kernel/sched_ext/bpf/orchestra_sched.bpf.c`,
  `kernel/sched_ext/include/orchestra_controller.h`, `LIMITATIONS.md`,
  `docs/development/NEXT_VERSION.md`, and `.github/workflows/build-sched-ext.yml`.
