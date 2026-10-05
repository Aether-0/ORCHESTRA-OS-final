# Non-canonical exploratory experiments

Components under this directory are **Exploratory** only. They are isolated from
the canonical ORCHESTRA hierarchy and must not be cited as userspace-validated
or kernel-prototyped evidence for the core research path.

## Contents

| Path | Status | Why non-canonical |
| --- | --- | --- |
| `process-group-prototype/` | Exploratory | Implements process groups, group runnable budgets, and coordinator-driven restart behavior excluded from the approved architecture (see the canonical scope in `orchestra_paper_cpu_demo/README.md`). |

Canonical userspace implementation: `orchestra_paper_cpu_demo/orchestra_paper_cpu.c`.
