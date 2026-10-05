# Known Limitations

## Hardware
- NUMA validation deferred: current VM has single NUMA node
- 4 vCPU maximum tested; 8+ worker scaling not yet measured
- Bare-metal benchmarks not yet performed
- VirtualBox environment only; no physical server validation

## Kernel
- Kernel v6.12.96 built from x86_64_defconfig + additions; full Fedora driver set not included
- scx_simple from source fails on clang 18.1.8 (BPF 32-bit atomic bug)
- initramfs must be regenerated for different hardware

## Bridge
- Bridge maps must be individually pinned after scheduler load
- Bridge CLI requires CAP_BPF + CAP_SYS_ADMIN
- Map pin paths use truncated bpftool names (15 chars)

## Controller
- Controller is userspace-only; BPF only enforces gating
- Recovery/degradation transitions use rolling window hysteresis
- Last-known-good vector promotion requires 5+ stable NORMAL updates

## Actions
- SLEEP uses deferred eligibility (not_before_ns); not true kernel sleep
- MIGRATE uses SCX_DSQ_LOCAL dispatch in Stage 7; targeted DSQ pending
- THROTTLE uses reduced slice; not precise bandwidth control

## Security
- Bridge maps have no cryptographic authentication (local trust only)
- Policy digest is SHA-256 for corruption detection, not authentication
- No privilege drop after scheduler attach

## Research Status
- ORCHESTRA is a **research prototype**, not production software
- No performance claims without controlled statistical evidence
- No hard real-time guarantees
- No production security certification
