#!/usr/bin/env bash
set -euo pipefail

# Safe Stage 8 compatibility entry point.
#
# The former script performed a global bpffs wipe, detached every visible
# struct_ops link, used a VirtualBox path, and read telemetry fields from an
# older ABI. This replacement composes the maintained loader-scoped P0/action
# gate with the location-independent comparison runner. The historical
# 30-minute/fault-injection claims are not silently represented as passed.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
# shellcheck source=/dev/null
. "$REPO_ROOT/scripts/path_safety.sh"
EVIDENCE_DIR=${STAGE8_EVIDENCE_DIR:-"/tmp/stage8-validation-$(date +%Y%m%d-%H%M%S)"}
P0_DIR="$EVIDENCE_DIR/p0"
COMPARE_DIR="$EVIDENCE_DIR/comparison"
orchestra_ensure_private_dir "$EVIDENCE_DIR" || {
    echo "refusing unsafe evidence directory: $EVIDENCE_DIR" >&2
    exit 2
}

echo "Stage 8 safe validation"
echo "evidence=$EVIDENCE_DIR"

ORCHESTRA_WORK_ITERATIONS=${ORCHESTRA_WORK_ITERATIONS:-80000000} \
    ORCHESTRA_FORWARD_TIMEOUT=${ORCHESTRA_FORWARD_TIMEOUT:-30} \
    bash "$SCRIPT_DIR/p0_ownership_retest.sh" "$P0_DIR"

BPF=${ORCHESTRA_BPF:-$P0_DIR/build/orchestra_scx_stage7.bpf.o}
BRIDGE=${ORCHESTRA_BRIDGE:-$P0_DIR/build/orchestra_bridge}
LOADER=${ORCHESTRA_LOADER:-$P0_DIR/build/orchestra_loader}

ORCHESTRA_BENCH_OUTDIR="$COMPARE_DIR" \
ORCHESTRA_BPF="$BPF" \
ORCHESTRA_BRIDGE="$BRIDGE" \
ORCHESTRA_LOADER="$LOADER" \
ORCHESTRA_WORKERS="${STAGE8_WORKERS:-1 2 4}" \
BENCH_SECS="${STAGE8_BENCH_SECS:-8}" \
ORCHESTRA_WORKLOAD_MODE=cpu \
    bash "$REPO_ROOT/benchmarks/real-machine/benchmark_suite.sh"

cat > "$EVIDENCE_DIR/NOT_RUN.md" <<'EOF'
# Stage 8 scope boundary

The historical 30-minute stability and broad fault-injection phases were not
run by this compatibility entry point. They require a separate exact-TID
lifecycle protocol and dedicated fault-injection interfaces. Their absence is
reported as untested, not as a pass.
EOF

echo "SAFE_STAGE8_COMPLETE"
echo "evidence=$EVIDENCE_DIR"
