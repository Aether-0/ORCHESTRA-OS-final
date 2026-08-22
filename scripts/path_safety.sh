#!/usr/bin/env bash
# Shared shell checks for paths touched by privileged ORCHESTRA tooling.
# This file is sourced by scripts; it does not perform any operation itself.

orchestra_reject_dot_components() {
    local path=$1 part
    local -a components=()

    IFS='/' read -r -a components <<<"$path"
    for part in "${components[@]}"; do
        [ "$part" != . ] || return 1
        [ "$part" != .. ] || return 1
    done
}

orchestra_safe_existing_dir() {
    local path=$1 owner permissions

    orchestra_reject_dot_components "$path" || return 1
    [ -d "$path" ] || return 1
    [ ! -L "$path" ] || return 1
    owner=$(stat -c '%u' -- "$path" 2>/dev/null) || return 1
    [ "$owner" = "$(id -u)" ] || return 1
    permissions=$(stat -c '%a' -- "$path" 2>/dev/null) || return 1
    (( (8#$permissions & 0022) == 0 )) || return 1
}

orchestra_safe_path_chain() {
    local current=$1 owner permissions

    [[ "$current" = /* ]] || return 1
    orchestra_reject_dot_components "$current" || return 1
    while :; do
        if [ -e "$current" ] || [ -L "$current" ]; then
            [ -d "$current" ] || return 1
            [ ! -L "$current" ] || return 1
            owner=$(stat -c '%u' -- "$current" 2>/dev/null) || return 1
            permissions=$(stat -c '%a' -- "$current" 2>/dev/null) || return 1
            # /tmp and /var/tmp are acceptable only as root-owned sticky
            # parents.  A writable non-sticky parent is a symlink/TOCTOU
            # attack surface for root-facing build and install output.
            if (( (8#$permissions & 0022) != 0 )); then
                (( owner == 0 && (8#$permissions & 01000) != 0 )) || return 1
            fi
        fi
        [ "$current" = / ] && break
        current=$(dirname -- "$current")
    done
}

orchestra_ensure_private_dir() {
    local path=$1

    [[ "$path" = /* ]] || return 1
    if [ -e "$path" ] || [ -L "$path" ]; then
        orchestra_safe_existing_dir "$path"
        return $?
    fi
    orchestra_prepare_parent_dir "$(dirname -- "$path")" || return 1
    mkdir -- "$path"
    chmod 0755 -- "$path"
    orchestra_safe_existing_dir "$path"
}

orchestra_prepare_parent_dir() {
    local path=$1

    [[ "$path" = /* ]] || return 1
    if [ -e "$path" ] || [ -L "$path" ]; then
        orchestra_safe_path_chain "$path"
        return $?
    fi
    orchestra_prepare_parent_dir "$(dirname -- "$path")" || return 1
    mkdir -- "$path"
    chmod 0755 -- "$path"
    orchestra_safe_existing_dir "$path"
}

orchestra_safe_destination_file() {
    local path=$1 parent

    parent=$(dirname -- "$path")
    orchestra_safe_path_chain "$parent" || return 1
    [ ! -L "$path" ] || return 1
    if [ -e "$path" ]; then
        [ -f "$path" ] || return 1
        orchestra_safe_existing_dir "$parent" || return 1
    fi
}
