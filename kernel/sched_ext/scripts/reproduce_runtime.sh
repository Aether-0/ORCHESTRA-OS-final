#!/usr/bin/env bash
set -euo pipefail

# Reproduce the loader-scoped ownership and five-action runtime gate.
# Historical STAGE7_EVIDENCE_DIR remains accepted during migration.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EVIDENCE_DIR=${ORCHESTRA_RUNTIME_EVIDENCE_DIR:-${STAGE7_EVIDENCE_DIR:-"/tmp/orchestra-runtime-$(date +%Y%m%d-%H%M%S)"}}

exec "$SCRIPT_DIR/verify_ownership.sh" "$EVIDENCE_DIR"
