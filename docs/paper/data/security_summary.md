# Security comparison summary (neutral evidence digest)

This digest preserves the claim boundary from the focused security comparison.

- The unchanged security wrapper passed with the Linux default scheduler.
- Direct policy-loader, mutation, ABI-sanitizer, and source-security components
  passed while the target scheduler was attached; the loader detached cleanly.
- Userspace tamper cases were rejected (`hmac=201` and `hmac=42` in the focused
  rerun; earlier maintained cases recorded `hmac=176` and `hmac=35`).
- Kernel-local replay/freshness checks rejected a duplicate sequence and reported
  stale required-signal telemetry.
- The kernel signal map is local-trust IPC and has no kernel HMAC verifier.
- The observer-only installer/uninstaller wrapper has no trusted kernel loader;
  its refusal to disable an externally attached scheduler is a harness boundary,
  not evidence of a cryptographic failure.

Therefore ORCHESTRA adds an integrity/control surface but is not intrinsically
safer than CFS. Userspace HMAC validation is experimentally validated in its
own scope; kernel cryptographic authenticity and production key management are
not implemented.
