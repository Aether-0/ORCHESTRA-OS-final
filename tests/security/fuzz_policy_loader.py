#!/usr/bin/env python3
"""Deterministic mutation/property target for policy validation.

This target uses only the standard library so it runs in CI without pulling
an external fuzzing dependency.  It is intentionally bounded; campaigns can
increase ``--iterations`` on an isolated runner.
"""

from __future__ import annotations

import argparse
import copy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from policy_load import validate_policy_document  # noqa: E402


def valid_document() -> dict[str, object]:
    return {
        "schema": "orchestra.policy.v1",
        "policy": {
            "mode": "ADAPT",
            "controller_state": "NORMAL",
            "entries": [{"state_index": 0, "action": "RUN"}],
        },
    }


def mutations(iterations: int):
    for index in range(iterations):
        document = copy.deepcopy(valid_document())
        policy = document["policy"]
        assert isinstance(policy, dict)
        entries = policy["entries"]
        assert isinstance(entries, list)
        selector = index % 10
        if selector == 0:
            document["schema"] = "orchestra.policy.v999"
        elif selector == 1:
            policy["entries"] = "not-an-array"
        elif selector == 2:
            policy["entries"] = [{"state_index": -1, "action": "RUN"}]
        elif selector == 3:
            policy["entries"] = [{"state_index": 256, "action": "RUN"}]
        elif selector == 4:
            policy["entries"] = [
                {"state_index": 0, "action": "RUN"},
                {"state_index": 0, "action": "YIELD"},
            ]
        elif selector == 5:
            policy["entries"] = [{"state_index": 0, "action": None}]
        elif selector == 6:
            policy["mode"] = {"unexpected": "object"}
        elif selector == 7:
            policy["controller_state"] = "UNKNOWN"
        elif selector == 8:
            policy["entries"] = [{"state_index": 0, "action": "RUN", "slice_ns": 1 << 80}]
        else:
            policy["entries"] = [
                {"state_index": item, "action": "RUN"} for item in range(256)
            ]
        yield document


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=1000)
    args = parser.parse_args()
    if args.iterations < 1 or args.iterations > 100000:
        raise SystemExit("iterations must be in [1, 100000]")
    accepted = 0
    rejected = 0
    for document in mutations(args.iterations):
        try:
            validate_policy_document(document, "/trusted/orchestra_bridge")
        except (TypeError, ValueError, KeyError):
            rejected += 1
        else:
            accepted += 1
    assert rejected > 0
    assert accepted > 0
    print(f"PASS policy-loader mutation target iterations={args.iterations} accepted={accepted} rejected={rejected}")


if __name__ == "__main__":
    main()
