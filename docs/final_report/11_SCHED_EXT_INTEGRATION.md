# 11. sched_ext Integration

> Historical report. The current product uses the target-matched loader and
> the lifecycle documented in `docs/installation/INSTALL.md`. The older raw
> registration examples below are retained only as research history and must
> not be used for current deployment.

## Overview

Linux sched_ext (available since kernel 6.12) allows BPF programs to implement custom scheduling policies. ORCHESTRA uses this mechanism to dispatch kernel tasks according to bridge directives.

## Kernel Environment

| Item | Value |
|------|-------|
| Kernel | Linux 6.12.96 (Stage 8 exact build) |
| CONFIG_SCHED_CLASS_EXT | y |
| CONFIG_DEBUG_INFO_BTF | y |
| CONFIG_BPF_SYSCALL | y |
| CONFIG_BPF_JIT_ALWAYS_ON | y |

## BPF Program Structure

```
orchestra_scx_stage7.bpf.c
├── SEC("license") GPL
├── bridge_control_map    ARRAY (1 entry, bridge_control)
├── bridge_directive_map   ARRAY (2 entries, bridge_directive)
├── bridge_task_map        HASH (128 entries, bridge_task_state)
├── bridge_telemetry_map   ARRAY (1 entry, bridge_telemetry, 40 fields)
├── orchestra_sched_init()    → increment load_count
├── orchestra_sched_exit()    → increment unload_count + exit reason
├── orchestra_sched_enable()  → increment task_enable_count
├── orchestra_sched_disable() → increment task_disable_count, clean task map
├── orchestra_sched_select_cpu() → return prev_cpu (simple)
├── orchestra_sched_enqueue() → read bridge directive, validate identity, dispatch
├── orchestra_sched_dispatch() → increment dispatch_count
├── orchestra_sched_running()  → no-op
├── orchestra_sched_stopping() → no-op
├── orchestra_sched_update_idle() → no-op
└── SCX_OPS_DEFINE with SWITCH_PARTIAL
```

## Enqueue Flow
1. Read bridge_control → validate magic, schema, generation
2. Read bridge_directive[active_slot] → validate generation match
3. Validate task identity: TGID + PID + cookie + expiry
4. Validate controller-state gating
5. Clamp adaptive slice to [0.5ms, 100ms]
6. Dispatch via `scx_bpf_dispatch(p, SCX_DSQ_LOCAL, slice, flags)`
7. Record action in bridge_task_map + telemetry

## Partial Switching

`SCX_OPS_SWITCH_PARTIAL` ensures only tasks that explicitly request `SCHED_EXT` scheduling policy use the ORCHESTRA scheduler. All other tasks remain on CFS/EEVDF.

## Build

```bash
clang -O2 -target bpf -g -nostdinc -D__BPF__ \
  -I include -I $KSRC/tools/lib \
  -I $KSRC/include -I $KSRC/include/uapi \
  -I $KSRC/arch/x86/include \
  -I $KSRC/tools/sched_ext/include -I /usr/include/bpf \
  -Wno-missing-declarations -Wno-visibility \
  -c orchestra_scx_stage7.bpf.c -o orchestra_scx_stage7.bpf.o

sudo ./kernel/sched_ext/bridge/orchestra_loader --load \
  /var/tmp/orchestra-os-build-$(id -u)/orchestra_scx_stage7.bpf.o
```

## Load/Unload Cycle

1. `orchestra_loader --load` → map/schema checks → verifier → struct_ops attaches
2. `/sys/kernel/sched_ext/state` transitions: disabled → enabled
3. Scheduler runs; only SCHED_EXT tasks affected
4. `orchestra_loader --unload` → detach → state transitions: enabled → disabled → scoped unpin
5. All tasks return to CFS
