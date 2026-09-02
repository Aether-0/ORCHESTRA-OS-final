# Provenance and evidence boundary

## Machine and software identity

| Field | Value |
|---|---|
| OS | Kali GNU/Linux Rolling x86_64 |
| Host | 20S1S9CY14 (ThinkPad T14 Gen 1) |
| CPU | Intel Core i5-10310U, 4 physical cores / 8 logical CPUs |
| NUMA | 1 node, CPUs 0--7 |
| RAM | approximately 31 GiB |
| Kernel | Linux 7.0.12+kali-amd64 |
| ORCHESTRA revision | `94664aeb001d8b3552245aedfd78254fbb5f13b8` |
| Repository state | dirty at the recorded campaign; source and documentation changes were not overwritten |
| Final scheduler state | `sched_ext=disabled` |

## Authorship and archive ownership

The report author is **S.W. ZAW**. The real-world validation records and
the companion evidence supplement are treated as the author's project archives.
This report uses descriptive archive citations so the references remain readable;
exact source hashes and the reproducible file map are retained here.

The host inventory and runtime campaign identify the same Kali kernel and
hardware. This report is a synthesis of the completed campaign archive;
the tool, thermal, topology, and storage boundaries recorded during testing
remain part of the evidence interpretation.

## Primary source documents

The two primary source documents are identified here by neutral descriptive
names and SHA-256 rather than by their original filenames.

| ID | Document used | Pages | SHA-256 | Role |
|---|---|---:|---|---|
| SIM | Original ORCHESTRA-OS TDPS research paper, 6 Jul 2026 | 12 | `9af8f45412e2f7d8be89f108965372caa2c420f917c7b612c3b4a86ab626561d` | simulation design, corrected coordination metric, negative predictor result, design principles |
| VAL | S.W. ZAW, real-machine validation archive, version 2, 27 Aug 2026 | 9 | `9183288c560183d84f40134fd5d5f561cb0de07727d99f4941dc4fa63df0ebaa` | prior real-machine synthesis, field-fit protocol, 20-pair result, log workload, bottleneck diagnosis |

The report does not promote the simulation paper's results to hardware
results. Simulation values are labelled `SIMULATED`; hardware values are
labelled `EXPERIMENTALLY VALIDATED` only within their stated host, workload,
and protocol envelope.

## Evidence IDs used in this report

| ID | Included table or source | Main evidence |
|---|---|---|
| P20 | `data/photo_archival_pairs.csv`, `data/photo_archival_statistics.txt` | 20 held-out paired ImageMagick foreground / bzip2 background phases; 20/20 wins; output and ownership gates |
| LOG5 | `data/log_archival_pairs.csv` and `data/log_screen_pairs.csv` | fixed boot-journal transform versus archive compression; five controlled pairs plus the earlier screen |
| FW3 | `data/fixed_work_summary.csv` and the per-run tables summarized in the source campaign | three matched CFS/ORCHESTRA repetitions for 1/2/4/8-worker CPU and 1/2/4-worker mixed work |
| ACT | `data/runtime_action_matrix.csv` | exact-TID attach, generation coherence, action, fallback, and clean-unload assertions; 81/81 assertions passed |
| STRESS | `data/longrun_results.csv` and `data/checklist_status.csv` | 600-second CPU/I/O/mixed phases, exact ownership, health scan, and missing-tool memory block |
| INT | `data/signal_microbenchmark_summary.json` and the checklist/test tables | 12/12 userspace publication runs; 12,000 publications; 27,161 verified reads; zero invalid/HMAC/stale failures |
| SEC | `data/security_summary.md` plus the checklist/test tables | CFS full-wrapper baseline, direct ORCHESTRA security components, kernel-local replay/freshness checks, and the preserved harness boundary |
| DIAG | `data/run_to_throttle_diagnosis.md` | source-supported early RUN-to-THROTTLE accounting diagnosis and reproduced before/after behavior |
| PARAM | `data/parameter_inventory.csv` | source-backed control values, host CFS knobs, experiment settings, and claim boundaries |

The `data/fixed_work_raw/` directory contains the six per-run CSVs behind the
fixed-work summary, and `data/stress/` contains the 5-second and 30-second
CPU/I/O/mixed result tables. The RT and kernel-local integrity matrices are
included as `data/rt_policy_matrix.txt`, `data/rt_contention_matrix.txt`, and
`data/kernel_integrity_matrix.txt`. The parameter register records the exact
ORCHESTRA contracts used in the comparison and the CFS runtime knobs observed
on the target host; it does not treat unlike scheduler controls as equivalent.

## Exact source hashes for included data

| Included object | SHA-256 |
|---|---|
| `data/photo_archival_pairs.csv` | `b5c4bfd7d4805ba116532da2826983ddb059f0fcf54e43efcf93c2a5e90a99eb` |
| `data/log_archival_pairs.csv` | `825ec64ffebf4cd897bc06053234c5587bb9d515f6d4368d13a28178fe730757` |
| `data/log_screen_pairs.csv` | `24cd8d888f4f45dc9a74b7dcdea468b35f989c77da02ed078543dee9cc1fafd9` |
| `data/fixed_work_summary.csv` | `4ac1416503a61e3fe58b85e4a0f0c099854392d7ec35e867c1b10b63741df838` |
| `data/runtime_action_matrix.csv` | generated evidence copy; verify with `CHECKSUMS.sha256` |
| `data/checklist_status.csv` | `94c1ccbd4610485aa703d512d2283c751ea9e04ff7a5889d3e6c27989dfae259` |
| `data/test_results.csv` | `9a79598e22298fe8da978d246dd15333b9744825e86815143da0926a7533853f` |

The associated 47-row checklist normalizes to PASS 38, FAIL 2 (preserved
harness/attempt failures), BLOCKED 7, INCONCLUSIVE 0, and N/A 0. The two
preserved failures are not silently removed from that count.

The author-controlled journal fixture had 104,353 records and 133,520,770
bytes; the four-repeat background input had 417,412 records and 534,083,080
bytes. The raw payload remains in the real-world archive. The expected
foreground TSV and output digest are preserved in the evidence package.

## Claim classes

- `SIMULATED`: results from the pre-kernel discrete-event study.
- `USERSPACE_VALIDATED`: API, schema, integrity, or control-plane evidence
  without kernel ownership.
- `KERNEL_PROTOTYPED`: target-matched build, verifier, attach, or source
  contract evidence.
- `EXPERIMENTALLY_VALIDATED (bounded)`: repeated real-machine observations
  with the required ownership/correctness/recovery gates, restricted to the
  tested protocol.
- `BLOCKED` / `NOT_IMPLEMENTED`: no valid result is substituted for a missing
  tool, unsupported topology, or absent implementation.

## Negative evidence retained

The report retains the pure-CPU slowdown, the incomplete `scx_simple` comparison,
the missing `stress`/memory row, the one-node NUMA limit, the absent distributed
backend, the lack of a kernel HMAC verifier, the thermally confounded log screen,
and the early RUN-to-THROTTLE transition diagnosis. No unsuccessful observation
was deleted or reclassified as a success.
