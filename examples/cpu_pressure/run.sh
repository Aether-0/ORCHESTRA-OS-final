#!/usr/bin/env bash
set -euo pipefail
DURATION=${1:-10}
case "$DURATION" in
    ''|*[!0-9]*) echo "duration must be a non-negative integer" >&2; exit 2 ;;
esac
tmp_file=$(mktemp)
cleanup() { rm -f -- "$tmp_file"; }
trap cleanup EXIT
dd if=/dev/zero of="$tmp_file" bs=1M count=8 status=none
end=$((SECONDS + DURATION))
while [ "$SECONDS" -lt "$end" ]; do
    sha256sum "$tmp_file" >/dev/null
done
echo "CPU_PRESSURE_COMPLETE duration_s=$DURATION"
