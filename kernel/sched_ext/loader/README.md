# Loader boundary

The supported loader is `../bridge/orchestra_loader.c`, built as
`orchestra_loader`. It owns the lifecycle boundary:

1. create the loader-owned `/sys/fs/bpf/orchestra` directory;
2. validate every map name, type, key size, value size, and capacity;
3. pin maps before struct-ops attach so the deferred timer is valid;
4. attach one struct-ops link;
5. remove only the loader-owned pins on disable.

The loader never performs a broad bpffs cleanup. A verifier, schema, or attach
failure rolls back only the pins created by that invocation.
