# Future Work — Stage 10

## Production Hardening
- Privilege drop after scheduler attach
- Symlink-safe file handling (O_NOFOLLOW)
- World-readable map prevention
- Audit logging

## NUMA Validation
- Multi-node NUMA testing on physical hardware
- Topology-aware CPU selection
- Cross-node migration measurement

## Scalability
- 8, 16, 32 worker scaling on bare metal
- Perf-based profiling of BPF hot paths
- Bridge latency optimization

## Reliability
- 1-hour + 24-hour soak tests
- Memory leak detection (valgrind, kmemleak)
- Counter overflow stress

## Benchmarking
- Bare-metal CFS vs scx_simple vs ORCHESTRA
- Phoronix Test Suite integration
- Statistical significance analysis (t-test, confidence intervals)

## Research Publication
- Academic paper submission
- Conference presentation
- Open-source release announcement
