# ORCHESTRA-OS — Final Research Report

**Date:** 2026-08-07  
**Commit:** `0ab5d7a`  
**Authors:** ORCHESTRA-OS Research Program

## 1. Motivation

Modern Linux scheduling (CFS, EEVDF) operates reactively — responding to observed load but lacking predictive capability, cryptographic integrity, and hierarchical coordination. ORCHESTRA-OS proposes a predictive, signal-coordinated scheduling architecture where a versioned Global Signal Vector disseminates authenticated directives to per-process Adaptive Response Functions, coordinated by a safety-gated closed-loop controller.

## 2. Research Objectives

1. Demonstrate that cryptographic signal frames can safely carry scheduling directives
2. Prove that multi-agent reinforcement learning can coordinate process scheduling
3. Show that a controller safety state machine prevents runaway adaptation
4. Implement a full userspace-to-BPF signal bridge for Linux sched_ext
5. Validate the architecture on exact-kernel environments

## 3. Architecture

```
Hardware/Kernel Observations → Predictive Extrapolation
→ Versioned Global Signal Vector → HMAC Integrity
→ Hierarchical Signal Dissemination → Per-Process Adaptive Response
→ RUN/SLEEP/MIGRATE/THROTTLE/YIELD → Coordination Measurement (S1-S4)
→ Closed-Loop Feedback Control → (cycle repeats)
```

### Key Components

| Component | Location | Purpose |
|-----------|----------|---------|
| Userspace Scheduler | `orchestra_paper_cpu_demo/` | Full prototype with RL, controller, signal bus |
| BPF Scheduler | `kernel/sched_ext/orchestra_scx_stage7.bpf.c` | sched_ext kernel scheduler |
| Bridge CLI | `kernel/sched_ext/bridge/orchestra_bridge.c` | Userspace-to-BPF directive publisher |
| Bridge Contract | `kernel/sched_ext/include/orchestra_bridge_v1.h` | Versioned map schemas |
| Controller | In userspace + BPF gating | 6-state safety machine |
| Policy Engine | Stage 5 persistence | TRAIN/ADAPT/EVALUATE lifecycle |

## 4. Stage-by-Stage Development

### Stage 1 — Userspace Core
- HMAC-SHA256 signal frames
- Tabular Q-learning with 5 canonical actions
- Per-process adaptive response
- Anti-synchronization perceptual jitter
- Coordination index S1-S4 with geometric mean

### Stage 2 — Metrics Framework
- Append-only metrics schemas (v2→v6)
- S2_selected + S2_effective
- S3_global + S3_conditioned coherence
- S4_burst temporal stability
- Strict CSV validators

### Stage 3 — Generation-Stamped Signal Bus
- C11 multi-reader-safe publication
- Two-slot generation protocol
- Bounded reader retries
- Legacy path retained for equivalence testing

### Stage 4 — Controller Safety State Machine
- 6 states: NORMAL/DEGRADED/SATURATED/DISABLED/ROLLBACK/RECOVERY
- Rolling-window metrics with hysteresis
- Saturation + oscillation detection
- Last-known-good actuator vector
- Metrics v5 with 107 columns

### Stage 5 — Policy Lifecycle
- TRAIN/ADAPT/EVALUATE modes
- Deterministic binary policy format
- Atomic save via temporary file + rename
- Metrics v6 with 120 columns

### Stage 6 — sched_ext MVP
- Minimal BPF scheduler (RUN + YIELD)
- Partial task switching (SCX_OPS_SWITCH_PARTIAL)
- Bounded dispatch (max 4 loops)
- Safe RUN fallback for unknown actions
- Exact kernel boot achieved (v6.12.96)

### Stage 7 — Signal Bridge
- Two-slot directive publication with generation validation
- TGID + PID + start_boottime cookie identity
- Controller-state action gating (6×5 matrix)
- Adaptive slice sizing (0.5ms–100ms)
- 40-counter telemetry v2

### Stage 8 — Full Kernel Validation
- Exact kernel build (v6.12.96) from source
- BLS boot entry correction
- Multi-core validation (1/2/4 workers)
- 5-minute stability campaign
- Fault injection (invalid PID, CPU)
- 10/10 security hardening items

### Stage 9 — Production Benchmarking
- CFS vs ORCHESTRA comparison
- Benchmark harness with CSV/JSON export
- Methodology documentation

## 5. Validation Methodology

Every stage was validated through:
1. **Unit tests** (25 named tests, GCC + Clang)
2. **CSV validator tests** (34 contract tests across v2-v6)
3. **Integration tests** (5 scenarios: baseline, orchestra, tamper, controller-tamper, signal-stop)
4. **Publication stress tests** (10,000+ iterations, 0 torn frames)
5. **Clang static analysis** (0 findings throughout)
6. **Kernel runtime** (sched_ext attach, bridge status, per-action validation)

## 6. Benchmark Results

| Scheduler | 4 workers × 3s CPU-bound |
|-----------|-------------------------|
| Linux CFS | 2419 ms |
| ORCHESTRA | 2898 ms (+19.8%) |

The ~20% overhead is expected: ORCHESTRA adds BPF struct_ops callbacks, bridge map lookups, telemetry counters, and per-task identity validation on every scheduling event. This is a research prototype — not a production scheduler.

## 7. Security Considerations

| Threat | Mitigation |
|--------|-----------|
| Invalid policy file | SHA-256 validation, dimension check |
| PID reuse | TGID + start_boottime cookie |
| Stale generation | Zero-sentinel, expiry enforcement |
| Bridge argument injection | Fixed-width parsing, no shell execution |
| Map tampering | Schema validation on every read |
| Generation overflow | Checked arithmetic, stable exit code 10 |

## 8. Known Limitations

- NUMA validation deferred (single-node VM)
- 4 vCPU maximum tested in VM
- Bare-metal benchmarks not yet performed
- SLEEP action uses deferred eligibility (not true kernel sleep)
- Bridge maps must be individually pinned
- scx_simple from source fails on clang 18.1.8 (BPF atomic bug)

## 9. Future Work (Stage 10)

- Bare-metal multi-NUMA validation
- 32+ worker scalability
- Energy-aware scheduling
- Production security certification
- Research publication

## 10. Conclusion

ORCHESTRA-OS demonstrates that predictive, cryptographically protected, hierarchical signal coordination can be implemented as a Linux sched_ext scheduler. The complete pipeline — from userspace RL controller through versioned BPF maps to kernel dispatch — has been validated on an exact-kernel environment with zero DSQ errors, zero panics, and zero stalls across all test campaigns.
