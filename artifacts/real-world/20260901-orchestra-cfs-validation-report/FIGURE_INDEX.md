# Figure index and evidence scope

All figures in the report are native TikZ/PGFPlots vector graphics. The
figure number, purpose, evidence class, and source are listed below.

| Figure | Purpose | Evidence class | Source |
|---:|---|---|---|
| 1 | Evidence chain from design through field-fit claim | mixed; labels are explicit in the figure | `PROVENANCE.md`, primary source documents, included tables |
| 2 | Corrected simulation coordination index across simulation rounds | `SIMULATED` | primary simulation study |
| 3 | Matched fixed-work ORCHESTRA/CFS completion-time ratio | exploratory, ownership-proven | `data/fixed_work_summary.csv` |
| 4 | Held-out foreground completion means | `EXPERIMENTALLY-VALIDATED (bounded)` | `data/photo_archival_pairs.csv` |
| 5 | Foreground improvement alongside background-service cost | `EXPERIMENTALLY-VALIDATED (bounded)` | `data/photo_archival_pairs.csv` |
| 6 | Source-supported early RUN-to-THROTTLE accounting diagnosis | diagnosis, not a repair claim | `data/run_to_throttle_diagnosis.md` |
| 7 | Decision flow for appropriate use and non-use | recommendation grounded in included evidence | report Sections 5 and 8 |

Figures 3--5 deliberately show the negative control and the service trade-off
together. They should not be read as a universal throughput ranking: the
fixed-work rows are exploratory, while the strongest positive result is a
scoped foreground-protection experiment that reduced background CPU service.
