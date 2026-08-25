# ORCHESTRA-OS Real-World Campaign

- Campaign: `20260825-042022-redshadow-complete`
- Host: `redshadow`
- Kernel: `7.0.12+kali-amd64`
- Architecture: `x86_64`
- CPUs: `8`
- Final artifact: `fix34`
- Final scheduler state: `disabled`
- Final bpffs state: only `/sys/fs/bpf`; no ORCHESTRA pins
- Raw evidence: `/tmp/orchestra-realworld-20250825-042022-redshadow-complete`

## Completion

The fix34 target-matched BPF object, bridge, and loader built successfully,
passed the verifier, attached, passed the exact-TID ownership gate, exercised
the runtime probes, and unloaded cleanly. Userspace build, checks, unit,
integration, security, and source-safety gates all pass.

The campaign proves bounded bare-metal runtime behavior for RUN, SLEEP,
THROTTLE, YIELD, and legal MIGRATE placement, generation/freshness rejection,
policy-driven action selection, live S1/S2/S3/S4/Q telemetry, and bounded
controller actuator adaptation/recovery.

It does not claim deployment readiness. Memory pressure is blocked by the
missing existing `stress` tool. RT coexistence, cryptographic kernel signal
authentication, online predictor convergence, full actuator rollback,
NUMA/distributed scheduling, and long-duration soak remain open gates.

## Final hashes

See the installed manifest at `/usr/local/lib/orchestra-os/build/build-manifest.txt`
and `recovery/final-fix34/hashes.txt` in the raw campaign directory.
