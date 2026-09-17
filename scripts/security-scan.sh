#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

found=0
if matches=$(git grep -nI -E \
    -e 'AKIA[0-9A-Z]{16}' \
    -e 'gh[pousr]_[A-Za-z0-9_]{20,}' \
    -e 'github_pat_[A-Za-z0-9_]{20,}' \
    -e 'BEGIN (RSA|OPENSSH|EC|DSA|PRIVATE) KEY' \
    -e 'password[[:space:]]*=[[:space:]]*["'"'][^"'"']+["'"']' \
    -e 'api[_-]?key[[:space:]]*=[[:space:]]*["'"'][^"'"']+["'"']' \
    -- . ':!*.png' ':!*.svg' ':!*.bpf.o' ':!*.bin' 2>/dev/null); then
    echo "secret-pattern: $matches"
    found=1
fi

if matches=$(git grep -nI -E -e '/home/[A-Za-z0-9_.-]+' -- . \
    ':!artifacts/**' \
    ':!*.png' ':!*.svg' ':!*.bpf.o' ':!*.bin' ':!*.csv' ':!*.log' 2>/dev/null); then
    echo "machine-path: $matches"
    found=1
fi

if [ "$found" -ne 0 ]; then
    echo "SECURITY_SCAN_FAIL" >&2
    exit 1
fi

if tracked_runtime=$(git ls-files | grep -En \
    '(^|/)(vmlinux\.h|orchestra_(bridge|loader)|fixed_work|.*\.bpf\.o|.*\.skel\.h)$' \
    2>/dev/null); then
    echo "tracked-runtime-artifact: $tracked_runtime" >&2
    echo "SECURITY_SCAN_FAIL" >&2
    exit 1
fi

if tracked_secret_file=$(git ls-files | grep -En \
    '(^|/)(\.env($|\.)|.*\.(pem|key|p12|pfx|kdbx)$|id_(rsa|ed25519)(\.pub)?$)' \
    2>/dev/null); then
    echo "tracked-secret-file: $tracked_secret_file" >&2
    echo "SECURITY_SCAN_FAIL" >&2
    exit 1
fi

# External actions execute third-party code in CI. Require an immutable full
# commit SHA; local actions under ./ remain tied to this repository revision.
workflow_uses=$(git grep -nI -E \
    'uses:[[:space:]]*[^[:space:]#]+' -- \
    '.github/*.yml' '.github/*.yaml' '.github/**/*.yml' '.github/**/*.yaml' \
    2>/dev/null || true)
if [ -n "$workflow_uses" ]; then
    unpinned_actions=$(printf '%s\n' "$workflow_uses" | grep -Ev \
        'uses:[[:space:]]*(\./[^[:space:]#]+|[^[:space:]#]+@[0-9a-fA-F]{40})([[:space:]]|#|$)' \
        || true)
    if [ -n "$unpinned_actions" ]; then
        echo "unpinned-workflow-action: $unpinned_actions" >&2
        echo "SECURITY_SCAN_FAIL" >&2
        exit 1
    fi
fi

echo "SECURITY_SCAN_PASS"
