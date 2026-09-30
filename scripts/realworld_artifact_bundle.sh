# Shared target-artifact discovery for the privileged real-machine demos.
#
# The caller must source path_safety.sh first and run as root.  This helper
# never rebuilds, installs, or modifies an artifact; it only selects a
# complete bundle whose manifest and hashes match the running kernel.

orchestra_realworld_manifest_value() {
    local manifest=$1 key=$2
    awk -F= -v wanted="$key" '
        $1 == wanted { count++; value = substr($0, index($0, "=") + 1) }
        END { if (count != 1 || value == "") exit 1; print value }
    ' "$manifest"
}

orchestra_realworld_safe_file() {
    local path=$1 permissions owner

    [ -f "$path" ] || return 1
    [ ! -L "$path" ] || return 1
    orchestra_safe_path_chain "$(dirname -- "$path")" || return 1
    owner=$(stat -c '%u' -- "$path" 2>/dev/null) || return 1
    [ "$owner" = 0 ] || return 1
    permissions=$(stat -c '%a' -- "$path" 2>/dev/null) || return 1
    (( (8#$permissions & 0022) == 0 ))
}

orchestra_realworld_bundle_matches() {
    local directory=$1 expected_kernel=$2 manifest key filename expected actual

    orchestra_safe_existing_dir "$directory" || return 1
    manifest="$directory/build-manifest.txt"
    orchestra_realworld_safe_file "$manifest" || return 1
    [ "$(orchestra_realworld_manifest_value "$manifest" running_kernel)" = \
        "$expected_kernel" ] || return 1

    for key in bpf_object_sha256 bridge_sha256 loader_sha256; do
        case "$key" in
            bpf_object_sha256) filename=orchestra_scx_stage7.bpf.o ;;
            bridge_sha256) filename=orchestra_bridge ;;
            loader_sha256) filename=orchestra_loader ;;
        esac
        orchestra_realworld_safe_file "$directory/$filename" || return 1
        expected=$(orchestra_realworld_manifest_value "$manifest" "$key") || return 1
        [[ "$expected" =~ ^[[:xdigit:]]{64}$ ]] || return 1
        actual=$(sha256sum -- "$directory/$filename" | awk '{print $1}')
        [ "$actual" = "${expected,,}" ] || return 1
    done
}

orchestra_realworld_resolve_bundle() {
    local expected_kernel=$1 preferred_directory=${2:-}
    local candidate manifest recorded_kernel
    local -a candidates=()
    local -a search_roots=(/var/tmp /var/lib/orchestra-os /usr/local/lib/orchestra-os)
    local search_root
    declare -A seen=()

    add_candidate() {
        local directory=$1
        [ -n "$directory" ] || return 0
        [ -z "${seen[$directory]+present}" ] || return 0
        seen[$directory]=1
        candidates+=("$directory")
    }

    add_candidate "$preferred_directory"
    add_candidate "/var/tmp/orchestra-os-build-$(id -u)"
    add_candidate /var/lib/orchestra-os/build
    add_candidate /usr/local/lib/orchestra-os/build
    for search_root in "${search_roots[@]}"; do
        [ -d "$search_root" ] || continue
        while IFS= read -r -d '' manifest; do
            add_candidate "${manifest%/build-manifest.txt}"
        done < <(find "$search_root" -mindepth 2 -maxdepth 3 \
            -type f -name build-manifest.txt -print0 2>/dev/null)
    done

    for candidate in "${candidates[@]}"; do
        if orchestra_realworld_bundle_matches "$candidate" "$expected_kernel"; then
            BPF="$candidate/orchestra_scx_stage7.bpf.o"
            BRIDGE="$candidate/orchestra_bridge"
            LOADER="$candidate/orchestra_loader"
            MANIFEST="$candidate/build-manifest.txt"
            printf 'real-world demo: selected target-matched bundle: %s\n' \
                "$candidate" >&2
            return 0
        fi
    done

    printf 'real-world demo: no complete target-matched artifact bundle found for running kernel %s\n' \
        "$expected_kernel" >&2
    for candidate in "${candidates[@]}"; do
        manifest="$candidate/build-manifest.txt"
        if orchestra_realworld_safe_file "$manifest"; then
            recorded_kernel=$(orchestra_realworld_manifest_value "$manifest" running_kernel)
            printf '  candidate=%s manifest_kernel=%s\n' "$candidate" \
                "${recorded_kernel:-missing}" >&2
        fi
    done
    printf '%s\n' \
        'Build with the running kernel and rerun, or pass --build-dir/--manifest for an exact bundle.' >&2
    return 1
}

# Direct invocation selects exactly the supplied bundle; no fallback for explicit input.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -euo pipefail
    if [ "$#" -ne 1 ]; then
        echo "usage: $0 BUILD_DIR" >&2
        exit 2
    fi
    . "$(dirname -- "${BASH_SOURCE[0]}")/path_safety.sh"
    if ! orchestra_realworld_bundle_matches "$1" "$(uname -r)"; then
        echo "bundle rejected: check kernel, manifest, hashes, ownership and permissions" >&2
        exit 2
    fi
    printf 'BUNDLE_VERIFIED=%s\n' "$1"
fi
