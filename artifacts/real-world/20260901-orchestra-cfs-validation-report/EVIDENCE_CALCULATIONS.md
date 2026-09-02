# Evidence calculations and audit trail

This file records the derived quantities used in the report. The source
CSV/JSON files remain the authoritative raw evidence included in `data/`.

## Parameter register

`data/parameter_inventory.csv` is a source-backed register rather than a new
benchmark. It combines implementation constants, explicit experiment settings,
and a read-only host snapshot of exposed CFS knobs. The CFS entries are
baseline observations and are not asserted to be semantic equivalents of the
ORCHESTRA bridge parameters.

## Fixed-work comparison (FW3)

For each workload/worker row, the report gives the arithmetic mean and
sample standard deviation of three CFS and three ownership-proven ORCHESTRA
runs. The displayed ratio is:

```text
ORCHESTRA mean completion time / CFS mean completion time
```

The six rows are:

| Workload | Workers | CFS mean +/- SD (ms) | ORCHESTRA mean +/- SD (ms) | Ratio |
|---|---:|---:|---:|---:|
| CPU | 1 | 208.67 +/- 1.53 | 330.00 +/- 32.05 | 1.58x |
| CPU | 2 | 208.00 +/- 1.73 | 418.67 +/- 57.05 | 2.01x |
| CPU | 4 | 225.67 +/- 30.60 | 461.67 +/- 16.17 | 2.05x |
| CPU | 8 | 295.00 +/- 75.03 | 511.00 +/- 31.95 | 1.73x |
| Mixed CPU/I/O | 1 | 1632.33 +/- 286.88 | 1398.00 +/- 336.63 | 0.86x |
| Mixed CPU/I/O | 2 | 1958.67 +/- 21.83 | 1843.00 +/- 81.84 | 0.94x |
| Mixed CPU/I/O | 4 | 1955.00 +/- 27.84 | 1759.67 +/- 74.78 | 0.90x |

These rows support a negative general-purpose performance result for pure CPU
work and only a directional observation for the small mixed sample.

## Held-out foreground-protection experiment (P20)

The paired reduction for each pair is calculated as:

```text
100 * (CFS elapsed_ms - ORCHESTRA elapsed_ms) / CFS elapsed_ms
```

Across 20 counterbalanced pairs:

- CFS mean: 7987.800 ms
- ORCHESTRA mean: 2036.600 ms
- mean paired reduction: 74.2405%
- descriptive paired 95% t interval for reduction: 72.7217%--75.7592%
- wins: 20/20
- mean background CPU-tick change: 4872.850 (CFS) to 112.800 (ORCHESTRA),
  a 97.6851% reduction
- ORCHESTRA throttle deferrals: 24--32 per phase; 20/20 phases had effective
  deferral and clean unload

The background-service change is part of the treatment, so the foreground
result is a service-reallocation result rather than free total acceleration.

## Controlled log/archive screen (LOG5)

The five-pair means are 13172.8 ms for CFS and 5138.6 ms for ORCHESTRA, with
60.4678% paired reduction and 5/5 ORCHESTRA wins. All output and ownership
gates passed. CFS phases reached 96--97 degrees C against the reported 100
degrees C critical point, so this result is retained as exploratory and
thermally confounded.

## Checklist and integrity counts

The included 47-row checklist normalizes to 38 PASS, 2 preserved FAIL, and 7
BLOCKED, with 0 INCONCLUSIVE and 0 N/A. The userspace integrity summary records
12/12 publication runs, 12,000 successful publications, 27,161 verified reads,
and zero HMAC/invalid/stale failures. These counts do not establish a kernel
cryptographic verifier; the included kernel-local checks are separately
labelled.
