# 17. Final Product

> Historical report. It is not the current security or runtime status
> document. Use `FINAL_PRODUCT_STATUS.md`, `SECURITY_AUDIT.md`, and the
> maintained installation/usage guides for current claims and commands.

## ORCHESTRA-OS Complete System

The ORCHESTRA-OS research program delivers a complete, validated, end-to-end predictive scheduling architecture spanning nine research stages.

## Subsystems

| Subsystem | Lines | Language | Location |
|-----------|-------|----------|----------|
| Userspace scheduler | 3384 | C11 | `orchestra_paper_cpu_demo/` |
| BPF scheduler (Stage 7) | 476 | C (BPF) | `kernel/sched_ext/orchestra_scx_stage7.bpf.c` |
| Bridge CLI | 425 | C | `kernel/sched_ext/bridge/orchestra_bridge.c` |
| Bridge contract | 184 | C header | `kernel/sched_ext/include/orchestra_bridge_v1.h` |
| CSV validator | ~900 | Python 3 | `tests/integration/validate_paper_cpu_csv.py` |
| Benchmark runner | ~2500 | Python 3 | `tools/benchmark/run_paper_cpu_benchmark.py` |

## Repository Layout
```
ORCHESTRA-OS/
├── orchestra_paper_cpu_demo/     Userspace prototype (3384 lines C)
├── kernel/sched_ext/             BPF scheduler + bridge (1085 lines C)
├── benchmarks/                   Stage 8+9 harnesses
├── experiments/                  JSON manifests + schemas
├── tests/                        25 unit + 34 validator + 5 integration
├── docs/                         12 ADRs + architecture + kernel + security
├── tools/                        Benchmark runner + reporting
├── artifacts/                    Test results + kernel images
├── release/                      Submission package (20 documents)
└── Makefile                      One-command build + test
```

## Runtime Workflow
1. `make clean && make && make test` → userspace baseline
2. Build kernel (optional, requires Linux source)
3. Build BPF scheduler + bridge CLI
4. `sudo orchestra enable` (or the validated loader) → load ORCHESTRA
5. `sudo ./bridge/orchestra_bridge --status` → verify
6. `sudo ./bridge/orchestra_bridge --publish --action RUN --target-pid <pid>` → dispatch
7. `sudo orchestra disable` → detach only the ORCHESTRA-owned scheduler

## Research Contributions
1. First end-to-end cryptographic signal coordination for Linux scheduling
2. Generation-stamped C11 atomic publication protocol for sched_ext
3. 6-state controller safety machine with formal state transitions
4. Append-only metrics framework (v2→v6, 120 columns)
5. Exact-kernel reproducibility: source, BTF, headers aligned
6. Research overhead measurement: ~20% vs CFS on VM
