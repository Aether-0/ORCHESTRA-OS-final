# Security policy

ORCHESTRA-OS is a research-grade sched_ext product candidate. Read the full
[security model](../docs/security/SECURITY.md) before enabling it on a host.
The current release does not claim cryptographic signal authentication,
signed release provenance, or privileged runtime validation on every supported
kernel.

Report suspected vulnerabilities privately through GitHub's security advisory
mechanism for this private repository. Do not include secrets, private keys,
kernel dumps, or unrelated machine data in reports. Include the affected
commit, host/kernel family, reproduction steps, impact, and whether the issue
can cause loss of scheduling ownership or bypass a safety/fallback gate.
