# ORCHESTRA-OS Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────┐
│                    Userspace (C)                         │
│  ┌──────────┐  ┌──────────┐  ┌───────────────────────┐ │
│  │Predictor │→ │Controller│→ │Policy Engine (Q-table) │ │
│  └──────────┘  └──────────┘  └───────────────────────┘ │
│        ↓              ↓                ↓                │
│  ┌──────────────────────────────────────────────────┐   │
│  │         Generation-Stamped Signal Bus            │   │
│  │    (C11 atomic, two-slot, bounded retry)         │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │ HMAC-SHA256 frames
┌──────────────────────▼──────────────────────────────────┐
│               Bridge CLI (Userspace)                     │
│  ┌──────────────────────────────────────────────────┐   │
│  │  Two-slot directive publication                  │   │
│  │  Generation validation + read-back               │   │
│  │  TGID + PID + boottime cookie identity           │   │
│  └──────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │ BPF maps
┌──────────────────────▼──────────────────────────────────┐
│              BPF Scheduler (Kernel)                      │
│  ┌──────────┐  ┌──────────┐  ┌────────────────────┐    │
│  │ ops.init │  │ops.enable│  │ops.dispatch        │    │
│  │ops.exit  │  │ops.disable│ │ops.enqueue         │    │
│  └──────────┘  └──────────┘  └────────────────────┘    │
│                                                         │
│  Bridge maps: control, directive[2], task_state, tel    │
│  SCX_OPS_SWITCH_PARTIAL (only opted-in tasks)           │
└─────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Acquisition:** `/proc/stat`, `/proc/meminfo` → CPU utilization, memory pressure
2. **Prediction:** Fixed-gain exponential smoothing → short-horizon CPU forecast
3. **Signal:** Serialized 128-byte payload + 32-byte HMAC → atomic map publication
4. **Directive:** Bridge CLI validates policy, task identity → publishes to BPF maps
5. **Dispatch:** BPF `ops.enqueue` reads active directive → `scx_bpf_dispatch()`
6. **Telemetry:** 40 counters in bridge_telemetry map → userspace polling

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Geometric mean for Q | Prevents AND-like collapse when any factor is zero |
| Two-slot publication | Race-free C11 atomic with generation validation |
| SCX_OPS_SWITCH_PARTIAL | Only opted-in tasks use ORCHESTRA |
| TGID+PID+cookie identity | Mitigates PID reuse |
| Userspace-only controller | BPF has no floating point, no blocking |
| Packed bridge structs | Deterministic sizes across userspace/BPF |
