# Preparing a source publication

Local manuscript copies, evidence-package duplicates, print exports, root
diagnostic logs, analyzer reports, and new artifacts are ignored by default.
They remain on disk. Source files, tests, scripts, and curated documentation
remain eligible for Git tracking.

Existing tracked research evidence stays versioned because repository reports
cite it. Ignore rules do not remove tracked files or erase historical commits.
Keep failed and blocked results when selecting evidence for publication.

Before adding new evidence, review its content for credentials, session data,
private host information, provenance, and claim scope. Prefer a separate
versioned evidence deposit for large bundles. Add selected evidence explicitly
only after review, and retain its checksums and environment record. A local
archive or planned deposit is not a verified public reference.

Review staged and unstaged changes separately before committing. Run the
existing security scan with its output saved privately if it may contain
matches. A successful scan is a check of configured patterns in the current
tree, not a full audit of Git history or assurance that all private information
has been removed.

Publication cleanup does not establish scheduler correctness or readiness.
Keep the candidate's validation results and known limitations with the release.
