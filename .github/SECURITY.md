# Security policy

ORCHESTRA-OS is a research software release with an
experimental sched_ext path. Read the full
[security model](../docs/security/SECURITY.md) before enabling it on a host.
Release assets include GitHub build-provenance attestations. These do not
establish kernel-side cryptographic signal authentication or privileged runtime
validation on every supported kernel.

Report suspected vulnerabilities privately through GitHub Security Advisories
for the public repository. Do not include secrets, private keys, kernel dumps,
or unrelated machine data in reports. Include the affected commit, release
asset or package, host/kernel family, reproduction steps, impact, and whether
the issue can cause loss of scheduling ownership or bypass a safety/fallback
gate. If private reporting is unavailable, contact the maintainer before
publishing technical details.
