# ORCHESTRA-OS — Final Research Package

**Version:** Stage 9
**Date:** 2026-08-07
**Repository:** `Aether-0/ORCHESTRA-OS`
**Commit:** `0ab5d7a`

## Overview

ORCHESTRA-OS is a predictive, cryptographically protected, hierarchical, signal-coordinated scheduling architecture for Linux. This package contains the complete userspace prototype, sched_ext BPF kernel scheduler, signal bridge, controller safety state machine, policy lifecycle system, and benchmark framework developed across nine research stages.

## Research Stages

| Stage | Description | Status |
|-------|-------------|--------|
| 1 | Userspace Core Architecture | Complete |
| 2 | Metrics Framework (v2–v6) | Complete |
| 3 | Generation-stamped Signal Bus | Complete |
| 4 | Controller Safety State Machine | Complete |
| 5 | Policy Lifecycle & Persistence | Complete |
| 6 | sched_ext MVP (BPF Scheduler) | Complete |
| 6B | Runtime Validation (VirtualBox) | Complete |
| 7 | Userspace ↔ Kernel Signal Bridge | Complete |
| 8 | Full Kernel Validation | Complete |
| 9 | Production Benchmarking | Initial |

## Quick Start

```bash
make clean && make && make test
```

## Directory Structure

```
ORCHESTRA-OS/
├── orchestra_paper_cpu_demo/    # Userspace prototype (C, Linux)
├── kernel/sched_ext/            # sched_ext BPF scheduler + bridge
│   ├── include/                 # Bridge contract, scheduler headers
│   ├── bridge/                  # Userspace bridge CLI
│   └── scripts/                 # Build, validation, reproduction
├── benchmarks/                  # Stage 8+9 benchmark harnesses
├── experiments/                 # Manifests, schemas, configs
├── tests/                       # Unit, integration, validator
├── docs/                        # ADRs, architecture, kernel, security
├── tools/                       # Benchmark runner, reporting
├── artifacts/                   # Test results, kernel images
└── release/                     # This package
```

## Key Documents

- `release/FINAL_REPORT.md` — Complete research summary
- `release/ARCHITECTURE.md` — System design
- `release/REPRODUCTION_GUIDE.md` — How to reproduce all experiments
- `release/BENCHMARK_RESULTS.md` — Performance data
- `release/KNOWN_LIMITATIONS.md` — Honest limitations

## Prerequisites

- Linux 6.12+ with CONFIG_SCHED_CLASS_EXT=y
- GCC 14+ or Clang 18+
- libbpf, bpftool
- VirtualBox (for kernel validation)

## License

MIT
