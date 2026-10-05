# Regression tests

From the repository root, `make test` runs static checks, [unit](unit/run.sh),
[integration](integration/run.sh), and [security](security/run.sh) regressions.
Individual entry points are `make check`, `make test-unit`,
`make test-integration`, and `make security-test`. Test fixtures use temporary
paths and do not attach the kernel scheduler. Preserve the first failure and
its environment when reporting results.

These gates cover userspace contracts, publication, parsers, lifecycle fixtures,
and source invariants. They do not replace kernel ownership, action timing,
performance comparisons, or deployment validation. See the
[software validation record](../docs/validation/PUBLIC_RELEASE_1_0_1.md)
and [paper evidence guide](../docs/paper/README.md).
