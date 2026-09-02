# Final publication audit

Audit date: 1 September 2026. Working directory: this package directory.

## Artifact checks

| Check | Result | Evidence |
|---|---|---|
| LaTeX build | PASS | Two `pdflatex -interaction=nonstopmode -halt-on-error -file-line-error` passes returned zero |
| PDF structure | PASS | Seven pages, letter format, unencrypted, non-empty |
| Authorship | PASS | Title page and PDF metadata identify `S.W. ZAW` |
| PDF text extraction | PASS | Abstract, quantitative tables, seven figures, conclusion, and references present |
| Font embedding | PASS | `pdffonts` reports embedded fonts for the manuscript |
| Cross-references/citations | PASS | No undefined-reference or undefined-citation diagnostics |
| Layout | PASS | Rendered pages visually inspected; no clipped figures, tables, or flowchart nodes |
| Numerical audit | PASS | P20, LOG5, FW3, checklist, and longrun counts recomputed from included data |
| Source provenance | PASS | Primary document hashes, repository revision, host, kernel, and target-build hashes recorded |
| Host-label hygiene | PASS | No disallowed hostname text appears in the package or extracted PDF text |
| Parameter register | PASS | Source-backed ORCHESTRA contracts, host CFS observations, and claim boundaries are included in `data/parameter_inventory.csv` |
| Package integrity | PASS | 37 non-transient files covered by `CHECKSUMS.sha256`; all checksums verify |

## Recomputed headline values

- P20: 40 rows / 20 pairs, 20/20 wins, 74.2405% mean paired reduction, 20/20
  ownership gates, 20/20 effective-deferral gates, and 97.6851% lower mean
  background CPU ticks.
- LOG5: five paired reductions average 60.4678%; the reduction of the two
  arithmetic means is 60.9908%. The manuscript reports the paired value and
  marks the result thermally confounded and exploratory.
- FW3: pure CPU ratios are 1.58x, 2.01x, 2.05x, and 1.73x at 1, 2, 4, and 8
  workers; mixed ratios are 0.86x, 0.94x, and 0.90x at 1, 2, and 4 workers.
- Checklist: 47 rows normalize to 38 PASS, 2 preserved FAIL, 7 BLOCKED, 0
  INCONCLUSIVE, and 0 N/A.
- Soak: three 600-second CPU/I/O/mixed phases passed ownership with zero
  errors; the memory row remains blocked because ownership was not proven.

The audit confirms that the paper's positive claim is a bounded service-policy
result and that the negative, blocked, and unproven findings remain visible.
