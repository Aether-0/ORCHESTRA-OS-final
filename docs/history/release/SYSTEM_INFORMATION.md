# System Information

## Host
- OS: Debian/Kali Linux (7.0.12+kali-amd64)
- VirtualBox: 7.2.8
- CPU: Host processor (Intel/AMD x86_64)
- RAM: Host memory

## Guest (orchestra-scx-lab)
- OS: Fedora 40 (Forty)
- vCPU: 4
- RAM: 8 GB
- Disk: 64 GB dynamically allocated
- Storage: SATA AHCI
- Network: NAT (e1000)

## Kernel (Stage 8 exact build)
- Version: 6.12.96
- Build: x86_64_defconfig + SCHED_CLASS_EXT + DEBUG_INFO_BTF + XFS + E1000 + SATA_AHCI
- BTF SHA-256: `7af6be54b981c04e7e57c2043f3cb0247e05e564ff65271138197fd8a91ca458`
- bzImage SHA-256: `d3d50d26b3d1a91196318a2c99d1c71a859c3171b69276bd26414ae6dfa9e912`

## Toolchain
- GCC: 14.2.1 (Red Hat 14.2.1-3)
- Clang: 18.1.8 (Fedora 18.1.8-2.fc40)
- bpftool: 7.5.0 (libbpf 1.5)
- cmake: 3.30.8
- python3: (Fedora 40 default)

## ORCHESTRA Source
- Repository: https://github.com/Aether-0/ORCHESTRA-OS
- Commit: `0ab5d7a`
- Branch: `feature/stage9-production-validation`
- Userspace SHA-256: `5fc21224846a2a17db0f02ce02efe8ebbe1067e40b0ad78e82419839d73657b9`
