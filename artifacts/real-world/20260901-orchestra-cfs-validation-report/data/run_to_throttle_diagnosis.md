# Source-supported RUN-to-THROTTLE diagnosis

This is a read-only diagnosis. No implementation or test source was changed.

Source snapshot:
`raw/source-snapshot/kernel/sched_ext/orchestra_scx_stage7.bpf.c`

SHA-256:
`58f1de0d9f872eb9c1e1cf51e1c01d88dc07fa152f2645799597d2223a461d32`

## Relevant control flow

Line numbers refer to the preserved snapshot and repository revision
`94664aeb001d8b3552245aedfd78254fbb5f13b8`.

1. Lines 2171--2187, `record_accepted_legacy`, write the new directive generation
   and action to task telemetry.
2. Lines 2316--2345, `orchestra_enqueue_bridge`, call the deferred-action helper
   for SLEEP/THROTTLE and return immediately when that helper reports success.
3. Lines 2525--2554, the THROTTLE helper records acceptance, reads
   `runtime_used_ns`, and—when usage is below budget—inserts a limited slice and
   returns. That below-budget path does not write the new generation/action to the
   task-state object.
4. Lines 2536--2546 show the contrasting already-over-budget path. `defer_task`
   receives the new generation/action and updates state before deferral.
5. Lines 3374--3406, `orchestra_sched_running`, read task-state and telemetry
   generations and return immediately when they differ.
6. Lines 3422--3425 set the running timestamp/flag only after that equality gate.
7. Lines 3464--3508, `orchestra_sched_stopping`, accumulate `runtime_used_ns` only
   when the running flag was set.

## Reproduced behavior

```text
early RUN -> THROTTLE transition
  ownership: yes
  accepted THROTTLE events: many
  deferrals: zero
  BPF runtime: near zero relative to /proc CPU ticks

short RUN warm-up inside active period -> THROTTLE
  ownership: yes
  accepted THROTTLE events: yes
  deferrals: 48
  deferred releases: 40
  BPF runtime: 1.6859 s
  /proc background service: 1.64 aggregate CPU-s
```

The controlled-v6 five-pair protocol reproduced effective action in every ORCHESTRA
phase after using the within-period warm-up: 248 deferrals and 208 releases total.

## Confidence and boundary

The diagnosis is high confidence because timing-controlled before/after behavior and
source control flow agree. It remains a diagnosis rather than proof of a repaired
defect. A fix must be implemented separately and validated with a regression matrix
covering:

- RUN-to-THROTTLE before budget consumption;
- RUN-to-THROTTLE after budget consumption;
- transition at/after the period reset;
- repeated generations;
- multiple exact TIDs;
- output correctness, deferral/release telemetry, and clean unload.
