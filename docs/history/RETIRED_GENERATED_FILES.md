# Retired generated and external build material

The current source publication excludes compiled host binaries, generated BTF
headers, kernel images, and a copied Linux header tree. Measurements, command
logs, failures, and build identity records are retained. Those removals do not
turn an old campaign into a complete runnable artifact bundle.

The removed Linux header snapshot is inventoried in
[retired-linux-headers.sha256](retired-linux-headers.sha256). Its 3,812 files can
be retrieved from Git revision `280687019a7d58bdcebdcd66bb2859d2250f4072` at
the paths listed in the manifest. Symlink targets are recorded separately in `retired-linux-symlinks.tsv`.
Verify regular-file hashes after extracting that revision;
the manifest is an inventory of retired files, not a checksum gate on the
current checkout. Earlier generated binaries/BTF were present in pre-cleanup
revision `1c4d10f4f15db282be88055772342d07faf33263`.

Historical campaign checksum manifests may name these retired files. Their
original inventories remain unchanged for provenance; do not describe them
as complete current-checkout integrity checks. The maintained paper tables
have a separate, complete checksum manifest under `docs/paper/`.

The retired Linux-header inventory references the companion repository
https://github.com/Aether-0/ORCHESTRA-OS/tree/280687019a7d58bdcebdcd66bb2859d2250f4072,
not a commit newly created in ORCHESTRA-OS-final. No additional runtime
binaries were removed from ORCHESTRA-OS-final during this synchronization.
