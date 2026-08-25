# Verification Rerun — Protocol Gates — 2026-08-25

## Signal Publication Microbenchmark

The documented `run_signal_publication_microbenchmark.py` protocol completed
12/12 validated invocations: two publication modes, two repetitions, and one,
two, and four readers. Both modes reported 100% publisher success in the
validated summaries; generation-stamped publication recorded bounded reader
retry/contention observations at higher reader counts without retry
exhaustion. These are local userspace publication/reader API measurements, not
kernel scheduler or cryptographic-authentication evidence.

Evidence:

- Result contract: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/signal-microbenchmark/benchmark_result.json`
- Invocation summaries: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/signal-microbenchmark/processed/invocation_summaries.csv`
- Commands and return codes: `/tmp/orchestra-realworld-20250825-verify-redshadow/continued/COMMANDS.log`

## Exact Paper Gate

The repository contains only bounded v2/v7 userspace manifests. The existing
runner rejects nonzero `rt_exempt`, caps the manifest duration at 30 seconds,
and does not expose the paper's 3,000-tick/500-warm-up contract. The binary
also exposes seconds and userspace worker emulation rather than a canonical
paper tick/exemption protocol. Therefore the exact N=40, four-exempt,
3,000-tick, 500-warm-up, seed-42/five-seed gate remains correctly classified
as `BLOCKED_NO_CANONICAL_RUNNER`; the bounded v7 campaign is not silently
substituted for it.

## Current Classification

- Signal publication overhead: `EXPERIMENTALLY_VALIDATED`, userspace-only.
- Exact paper protocol: `BLOCKED_NO_CANONICAL_RUNNER`.
- Kernel HMAC/authentication overhead: `NOT_IMPLEMENTED`, not inferred from
  the userspace microbenchmark.

## Legacy Schema Compatibility

The existing v3, v4, v5, and v6 exploratory manifests were each attempted
with six invocations. All 24 invocations completed their child processes but
validated zero rows because the current binary emits the append-only v7
121-column header while each legacy schema requires its older ordered header.
These failures are preserved under
`/tmp/orchestra-realworld-20250825-verify-redshadow/continued/paper-v{3,4,5,6}`.
The v7 manifest remains the only current manifest compatible with the current
binary; no legacy result was silently relabeled as a pass.
