# Changelog

## Stage 1 (2026-08-02)
- Initial commit: userspace scheduler with HMAC-SHA256 signal frames
- 5 canonical actions: RUN, SLEEP, MIGRATE, THROTTLE, YIELD
- Tabular Q-learning with difference rewards

## Stage 2 (2026-08-03)
- Metrics v3: S2_selected, S2_effective, action telemetry
- Metrics v4: S4_burst temporal stability, 83-column CSV
- CSV validator with v2/v3/v4 support

## Stage 3 (2026-08-03)
- Generation-stamped signal publication (C11 atomic)
- Two-slot protocol with bounded reader retries
- Legacy byte-wise path retained for equivalence

## Stage 4 (2026-08-04)
- Controller safety state machine (6 states)
- Rolling windows, hysteresis, saturation detection
- Metrics v5 (107 columns)

## Stage 5 (2026-08-04)
- TRAIN/ADAPT/EVALUATE lifecycle modes
- Binary policy format with SHA-256 integrity
- Metrics v6 (120 columns)

## Stage 6 (2026-08-05)
- sched_ext BPF scheduler (RUN + YIELD)
- Partial task switching
- GitHub Actions CI for BPF compilation

## Stage 6B (2026-08-05)
- VirtualBox runtime validation
- scx_simple 3-cycle gate

## Stage 7 (2026-08-05)
- Versioned bridge contract v1
- Two-slot directive publication
- All 5 actions in BPF
- Adaptive slice sizing (0.5ms–100ms)

## Stage 8 (2026-08-06)
- Exact v6.12.96 kernel build and boot
- BLS boot entry correction
- Multi-core validation (1/2/4)
- 5-min stability + fault injection
- 10/10 security hardening

## Stage 9 (2026-08-07)
- CFS vs ORCHESTRA benchmark (2419ms vs 2898ms)
- Benchmark methodology documentation
