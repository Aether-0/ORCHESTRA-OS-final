# Benchmark Results — Stage 9 Multi-Angle Evaluation

## Test Configuration
- **Environment:** Fedora 40 guest (VirtualBox 7.2.8), 4 vCPUs, 8 GB RAM
- **Kernel:** Linux 6.12.96 (Stage 8 exact build)
- **ORCHESTRA Commit:** `0ab5d7a`
- **Data Location:** `artifacts/test-results/multi_angle_results.json`

---

## 1. CPU-Bound Workload (4 Workers × 3 Seconds)

| Scheduler | Time (ms) | Relative |
|-----------|-----------|----------|
| Linux CFS | 2419 ms | 1.00× (Baseline) |
| ORCHESTRA | 2898 ms | 1.20× (+19.8%) |

---

## 2. Yield Throughput (`sched_yield()` ops/sec)

| Sched | Workers | Throughput (yields/sec) | Overhead vs CFS |
|-------|---------|-------------------------|-----------------|
| CFS | 1 | 2,925,644 | Baseline |
| ORCHESTRA | 1 | 2,198,821 | -24.8% |
| CFS | 2 | 5,336,744 | Baseline |
| ORCHESTRA | 2 | 4,948,653 | -7.2% |
| CFS | 4 | 8,347,674 | Baseline |
| ORCHESTRA | 4 | 7,753,349 | -7.1% |

---

## 3. Interactive Wakeup Latency (`nanosleep(1ms)` excess delay)

| Sched | Workers | Avg Wakeup Delay (µs) | Delta vs CFS |
|-------|---------|----------------------|--------------|
| CFS | 1 | 411.04 µs | Baseline |
| ORCHESTRA | 1 | **406.81 µs** | **-4.23 µs (-1.0% faster)** |
| CFS | 2 | 161.66 µs | Baseline |
| ORCHESTRA | 2 | 337.46 µs | +175.80 µs |
| CFS | 4 | 209.60 µs | Baseline |
| ORCHESTRA 4 | 4 | 312.91 µs | +103.31 µs |

---

## 4. Bridge Directive Modulation

| Active Directive | Throughput (yields/sec) | Delta vs RUN |
|------------------|-------------------------|--------------|
| RUN | 6,416,841 | Baseline |
| **YIELD** | **7,032,099** | **+9.6% higher yield frequency** |
| THROTTLE | 6,628,402 | +3.3% higher yield frequency |

---

## Sources of Overhead

ORCHESTRA's scheduling overhead is attributable to:
1. BPF `struct_ops` callbacks per scheduling event
2. Bridge map lookups (`bridge_control_`, `bridge_directiv`, `bridge_task_map`)
3. Task identity validation (TGID + PID + start_boottime cookie)
4. Controller-state gating check
5. Telemetry counter atomic operations
