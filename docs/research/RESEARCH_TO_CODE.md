# Research-to-code mapping

The architecture study defines the algorithmic concepts. The submitted hardware
paper and its archived data describe a later, bounded implementation evaluation.
This table contains historical target-specific observations; it does not certify
the latest source release. See [the paper evidence map](../paper/EVIDENCE.md).

This table
maps each concept to the current implementation and evidence boundary.

| Research concept | Implementation component | Source boundary | Evidence class |
| --- | --- | --- | --- |
| State acquisition | Runtime state and signal records | `orchestra_scx_stage7.bpf.c`, bridge signal path | Controlled bare-metal runtime evidence; broad dynamic validation pending |
| Predictive Extrapolation Layer | Bounded prediction fields, confidence, generation, expiry, observed fallback | `orchestra_kernel_v8.h`, decision path | Contract/source validated; hardware predictor convergence not claimed |
| Signal Bus | Generation/freshness/schema-checked fixed-point map frame | `orchestra_bridge_v1.h`, bridge, BPF signal gate | Userspace and source validated; kernel-local transport is not cryptographic proof |
| Adaptive Response Function | Policy bank and canonical decision pipeline | v8 headers and `orchestra_scx_stage7.bpf.c` | Kernel-prototyped |
| `RUN` | DSQ dispatch | BPF action execution | Bare-metal ownership/forward-progress gate passed; timing remains exploratory |
| `YIELD` | Bounded relinquish/requeue with repeat guard | BPF action execution and task diagnostics | Bare-metal policy-driven effective action observed |
| `MIGRATE` | Target selection and local-on DSQ path | BPF action execution | VM and bounded current-host placement evidence; hotplug/contention pending |
| `THROTTLE` | Bounded deferred eligibility | BPF defer path | Bare-metal defer/release observed; bandwidth timing pending |
| `SLEEP/DEFER` | Deadline-ordered deferred eligibility | BPF defer timer path | Bare-metal defer/release observed; broad timing pending |
| Hybrid Safety Layer | Protected scheduling-class admission boundary | bridge and BPF eligibility path | Source contract; live RT coexistence pending |
| `S1` | Signal freshness, confidence, fidelity, generation continuity | `orchestra_coord.h` | Limited bare-metal live telemetry; population-scale validation pending |
| `S2` | Policy/directive compliance | `orchestra_coord.h` and task records | Limited bare-metal live telemetry |
| `S3` | State-conditioned action coherence | `orchestra_coord.h` histogram | Limited bare-metal live telemetry |
| `S4` | Transition, mass-switch, and oscillation stability | `orchestra_coord.h` | Limited bare-metal live telemetry |
| `Q` | Fixed-point geometric mean of S1–S4 | `orchestra_coord.h` | Limited bare-metal live Q; no performance acceptance claim |
| Multi-actuator controller | Deficit matrix, bounded actuator bank, rollback/recovery | `orchestra_controller.h` | Bounded live actuator adaptation/recovery; full matrix and rollback pending |
| Two-timescale behavior | Short coordination windows and slower controller period | v10 control ABI/controller | Source/unit contract |
| Anti-synchronization | Jitter, switching penalty, thresholds, S4 penalties | controller and coordination headers | Source/unit contract; population-scale runtime pending |
| Policy lifecycle | Inactive-bank write and generation commit | bridge v8 policy commands and BPF lookup | Bare-metal policy-driven YIELD observed |
| Hierarchical scaling | Bounded CPU/NUMA/global coordination slots | v10 maps | Single-node bare-metal global/CPU evidence; NUMA runtime pending |
| Distributed extension | None in current product | No distributed backend | Deferred/future work |

## Semantics preserved

The implementation intentionally preserves the corrected four-factor Q,
geometric-mean aggregation, directive-consistent state boundaries,
offline-calibrated prediction boundary, confidence-aware fallback,
anti-synchronization measurements, causal actuator mapping, and the
requested-versus-executed action distinction. Historical stage artifacts are
kept for evidence, but they are not alternate final pipelines.
