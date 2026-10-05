# Operational and publication tools

| Entry point | Purpose |
| --- | --- |
| [orchestra](orchestra) | Version, status, capability, build and scoped scheduler lifecycle CLI |
| [build.sh](build.sh) | Userspace/bridge or target-matched kernel build outside the source tree |
| [check-system.sh](check-system.sh) | Read-only host capability report; strict kernel gate is explicit |
| [policy_load.py](policy_load.py) | Policy validation, dry run and privileged publication |
| [install.sh](install.sh), [uninstall.sh](uninstall.sh) | Manifest-tracked installation and removal |
| [source-tarball.sh](source-tarball.sh), [public-export.sh](public-export.sh) | Curated source-release archives |
| [security-scan.sh](security-scan.sh), [check-doc-links.py](check-doc-links.py) | Publication and maintained-document checks |

Start with the [usage guide](../docs/usage/USAGE.md). Kernel lifecycle actions
require explicit administrator activation and a target-matched verified bundle.
The [paper reproduction guide](../docs/paper/REPRODUCIBILITY.md) separates these
operations from archived-data analysis.
