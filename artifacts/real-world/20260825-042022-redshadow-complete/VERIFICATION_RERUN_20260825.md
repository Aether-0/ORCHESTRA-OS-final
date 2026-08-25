# Verification Rerun — 2026-08-25

This rerun validates the installed fix34 artifacts on bare-metal `redshadow`.
Raw command output is preserved under
`/tmp/orchestra-realworld-20260825-verify-redshadow`.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| `git diff --check` | PASS | return code `0` |
| `make check` | PASS | return code `0` |
| `make test` | PASS | 30/30 named unit tests plus integration/security suites |
| `orchestra check-system --strict` | PASS | return code `0`; kernel/config/tool capability gate passed |
| P0 ownership/action retest | PASS | zero failures; exact-TID ownership and all four non-RUN probes completed |
| Stage 8 short validation | PASS | P0 plus owned CFS/ORCHESTRA CPU comparison; `scx_simple` blocked because it is not installed |
| Invalid PID rejection | PASS | bridge returned `6` and reported no current identity |
| Invalid migration CPU rejection | PASS | bridge returned `7` and rejected CPU `999` |
| RT transition protection | PASS, limited | admitted task was removed from the kernel identity map after `SCHED_FIFO` transition; full RT coexistence remains unclaimed |
| Clean runtime state | PASS | `sched_ext=disabled`; `/sys/fs/bpf` contains no ORCHESTRA pins |

The installed artifacts were unchanged during the rerun:

```text
bpf_object_sha256=7fb7bd36bad1e78b0d93714b409677b2b0ab5ad3c19c52b75ae423451f5c7853
bridge_sha256=30cb0b3834b9c11014d8c729a7b02aa8201d0afb3ad5e533fbe630e0a4e7b59f
loader_sha256=d6b35436598722d17d8b446e38ab1cb31aaa1402b2a0e6e56dd20aee7f08c2bf
```

## Boundary

This closes the current-host build, verifier, attach, ownership, action, and
cleanup gates. It does not make the release deployment-ready. Kernel-side
cryptographic signal authentication, online predictor convergence, complete
multi-actuator rollback validation, memory pressure coverage, NUMA behavior,
distributed scheduling, hotplug/fault matrices, fairness under RT load, and
long-duration soak remain open or out of scope.
