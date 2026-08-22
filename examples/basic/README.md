# Basic product smoke test

This example exercises the non-privileged control plane. It does not claim
that a kernel scheduler is attached.

Run from the repository root:

```bash
./examples/basic/run.sh
```

Expected result: the product version is printed, the capability report is
available, and status reports either observer mode or an explicitly detected
kernel capability state.
