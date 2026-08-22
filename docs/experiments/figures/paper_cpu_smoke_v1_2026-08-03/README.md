# Paper CPU smoke v1 figures - 2026-08-03

These figures were generated from the completed, validated repository artifact `artifacts/test-results/2026-08-03/benchmark/authoritative-c` using `tools/plotting/plot_paper_cpu_smoke.py`. The plotting script reads the processed tables and provenance only; it does not modify raw benchmark data.

## Interpretation boundary

- Maturity class: **userspace-validated**.
- Comparison design: `descriptive-unpaired-endogenous`.
- Artifact claim boundary: Descriptive userspace observations only; do not infer a paired mode effect, kernel scheduling behavior, or general performance.
- Processed-summary interpretation: Mode summaries are descriptive and unpaired because each mode changes the endogenous host CPU signal; no causal performance difference is computed.
- The baseline is an instrumented, observed-state reactive **contract reference**, not a causal control group. Error bars and horizontal placement must not be interpreted as a treatment-effect estimate.
- Each plotted run statistic uses one validated invocation after the declared warm-up. There are three invocations per mode.

## Captions

1. **Coordination means and 95% CI.** Across-run means for S1-S4 and the corrected coordination index `Q = (S1 × S2 × S3 × S4)^(1/4)`. Error bars are the processed two-sided 95% t intervals over three invocation means per mode. Baseline values characterize the contract reference; the figure does not estimate a mode effect.
2. **Per-run coordination variability.** S1-S4 and Q invocation means for each repetition and declared seed. Points are independent invocation summaries; their horizontal placement identifies the run and does not encode a sequence, trend, pairing, or causal comparison.
3. **Action mix by run and mode.** Post-warm-up fractions of canonical `RUN`, `SLEEP`, `MIGRATE`, `THROTTLE`, and `YIELD` actions for each validated invocation. The bars report observed policy behavior, not Linux kernel dispatch shares.

## Source integrity

- Experiment ID: `paper_cpu_smoke_v1`
- Benchmark source SHA-256: `d5ec68c28a2cfcc20475dba44cb4b3c36b46b08acb4206647b874d2e2edf8713`
- Benchmark binary SHA-256: `f2f14020ef6f4caf7cc1f9dda3b392451367209b9d9f79e27415680c5e678b4b`
- Plot generator SHA-256: `192b386dd251af9c47b74502a0a7b1a17e85bfaf856ba1533fbfdd2015d76343`
- `benchmark_result.json` SHA-256: `3afc1219025cd0111bf7ae48924d02528be815849f54038e1a3ddf6b8938ddd5`
- `manifest.input.json` SHA-256: `40844535648f4a7dc25131141035d9cfb88886c20f0f8b9fb4bbbc8ffef02b5a`
- `metrics_schema.input.json` SHA-256: `04cb97e55d66bcf3e03e810372865c25368c89abb5de4e09e43d952d6f2d09c2`
- `processed/run_summaries.csv` SHA-256: `89dfcd5039b903a3051bc05dbf53fe70271a804c9b9784049e46b3990d459324`
- `processed/summary.json` SHA-256: `a9b1cc25045fd4e5060626671861d0072eb7e0978ec3ab2a3239d105fa0b7188`

## Generated files

- `action_mix_per_run.png` SHA-256: `b90a40d437e0ed52a881747d61405ee089f7ec22bbbc32b37c9999fcfe9a3072`
- `action_mix_per_run.svg` SHA-256: `80640c53d9e3eceb17d8eff7a47269d46f0f7f44f773a527fd53dbb10bd16ab4`
- `coordination_means_ci95.png` SHA-256: `edc3003193d2925e09683898489581ed340f9cce070e36ee9d64ed3edcffa9d3`
- `coordination_means_ci95.svg` SHA-256: `1c8bde83f2a7be4016079fc43034eb01b4531b89ed17fe62010bc0f3a9dae89e`
- `coordination_per_run.png` SHA-256: `57297f00c8167f4e3bb1776c1e7a58f45a99392cc06f5928e8f9d198c88a1a0f`
- `coordination_per_run.svg` SHA-256: `51400a45554e52e4d5388148d49bd7d2e307e16b99918ee9902144e76230597a`

Generated with Matplotlib `3.10.7+dfsg1` using a fixed SVG hash salt and fixed metadata date for deterministic output.
Pinned visualization/report dependencies are listed in `tools/requirements-visualization.txt`.
