# ORCHESTRA-OS Project Timeline

## Stage 1 — Userspace Core Architecture
- **Goal:** Implement paper-aligned real-CPU userspace prototype
- **Key commits:** `a2e14d0`, `b956ce2`
- **Key ADRs:** 0001 (userspace contract), 0002 (signal frame)
- **Validation:** 25 unit tests, publication stress (10k+ iterations)
- **Result:** HMAC-SHA256 signal bus, 5-action RL scheduler, coordination metrics

## Stage 2 — Metrics Framework  
- **Goal:** Establish strict append-only metrics contracts
- **Key commits:** `0edbeda`, `8160a09`
- **Key ADRs:** 0003 (conditioned coherence), 0004 (effective action)
- **Validation:** 34 CSV validator tests across v2-v4
- **Result:** S2_selected, S2_effective, S3_conditioned, S4_burst

## Stage 3 — Generation-Stamped Signal Bus
- **Goal:** Replace byte-wise atomic publication with C11-safe transport
- **Key commits:** `46dfbe2`
- **Key ADRs:** 0006 (generation-stamped publication)
- **Validation:** Multi-reader stress test, 0 torn frames
- **Result:** Two-slot generation protocol, bounded retry readers

## Stage 4 — Controller Safety State Machine
- **Goal:** Convert controller to explicitly stateful fail-safe subsystem
- **Key commits:** `a2e14d0`, `f7c7f39`
- **Key ADRs:** 0007 (controller safety)
- **Validation:** 25 unit tests, metrics v5 (107 columns)
- **Result:** 6 states, hysteresis, saturation detection, rollback/recovery

## Stage 5 — Policy Lifecycle & Persistence
- **Goal:** Separate TRAIN/ADAPT/EVALUATE, add versioned policy save/load
- **Key commits:** `8160a09`, `0edbeda`
- **Key ADRs:** 0008 (policy lifecycle)
- **Validation:** Round-trip validation, corruption rejection
- **Result:** Binary policy format, atomic temp-file save, metrics v6 (120 columns)

## Stage 6 — sched_ext MVP
- **Goal:** Boot sched_ext kernel, load minimal ORCHESTRA scheduler
- **Key commits:** `f7c7f39`, `4629a7f`
- **Key ADRs:** 0009 (sched_ext MVP)
- **Validation:** BPF verifier, struct_ops attach, 5 load/unload cycles
- **Result:** RUN + YIELD on SCX_DSQ_LOCAL, partial task switching

## Stage 6B — Runtime Validation
- **Goal:** Prove scheduler on live kernel in VirtualBox
- **Key commits:** `23ae89c`, `f4643db`
- **Validation:** scx_simple 3-cycle, ORCHESTRA enabled, clean disable
- **Result:** Fedora 6.12.15 validated as compatible environment

## Stage 7 — Signal Bridge
- **Goal:** Userspace-to-BPF directive publication with generation safety
- **Key commits:** `11b1ab3`, `dc889d5`, `8f7316a`
- **Key ADRs:** 0010 (signal bridge)
- **Validation:** 5 actions × NORMAL, 2-slot publication, gen 1→6
- **Result:** Bridge CLI, TGID+PID+cookie identity, 40-counter telemetry v2

## Stage 8 — Full Kernel Validation
- **Goal:** Exact same-revision kernel environment
- **Key commits:** `68abf77`, `844ce86`, `c4be571`
- **Key ADRs:** 0011 (full kernel validation)
- **Validation:** bzImage build, BLS boot fix, sched_ext/ORCHESTRA on exact v6.12.96
- **Result:** Multi-core (1/2/4), 5-min stability, fault injection, 10/10 hardening

## Stage 9 — Production Benchmarking
- **Goal:** CFS vs ORCHESTRA comparison
- **Key commits:** `0ab5d7a`
- **Key ADRs:** 0012 (production validation)
- **Validation:** Benchmark harness, CSV/JSON export
- **Result:** CFS 2419ms vs ORCHESTRA 2898ms (+20% expected overhead)
