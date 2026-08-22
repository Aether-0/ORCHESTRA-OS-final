# Userspace components

The portable research and observer implementation remains in
`orchestra_paper_cpu_demo/`; it contains the paper-oriented simulation and
userspace validation path. The product control plane is in `scripts/`, and
the kernel bridge/loader are in `kernel/sched_ext/`.

This directory is a stable documentation boundary for future userspace
components such as offline calibration, telemetry export, and policy
management. It does not duplicate the existing inference implementation.
