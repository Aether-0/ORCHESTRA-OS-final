#!/usr/bin/env python3
"""Validate and publish one versioned ORCHESTRA policy document."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ACTION_NAMES = {"RUN", "SLEEP", "MIGRATE", "THROTTLE", "YIELD"}
MODES = {"TRAIN": 0, "ADAPT": 1, "EVALUATE": 2}
CONTROLLERS = {"NORMAL", "DEGRADED", "SATURATED", "DISABLED", "ROLLBACK", "RECOVERY"}


def integer(value: object, field: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{field} must be an integer >= {minimum}")
    return value


def enum_value(value: object, field: str, values: set[str]) -> str:
    if not isinstance(value, str) or value.upper() not in values:
        raise ValueError(f"{field} must be one of {sorted(values)}")
    return value.upper()


def bridge_command(
    bridge: str, entry: dict[str, object], mode: str, controller: str
) -> list[str]:
    action = enum_value(entry.get("action"), "entry.action", ACTION_NAMES)
    state_index = integer(entry.get("state_index"), "entry.state_index")
    command = [
        bridge,
        "--policy-entry",
        "--policy-state-index",
        str(state_index),
        "--action",
        action,
        "--policy-mode",
        str(MODES[mode]),
        "--controller-state",
        controller,
    ]
    numeric_options = (
        ("target_cpu", "--target-cpu"),
        ("slice_ns", "--slice-ns"),
        ("not_before_ns", "--not-before-ns"),
        ("throttle_period_ns", "--throttle-period-ns"),
        ("throttle_budget_ns", "--throttle-budget-ns"),
    )
    for key, option in numeric_options:
        if key in entry:
            command.extend((option, str(integer(entry[key], f"entry.{key}"))))
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True)
    parser.add_argument("config", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    try:
        document = json.loads(args.config.read_text(encoding="utf-8"))
        if not isinstance(document, dict):
            raise ValueError("policy document must be a JSON object")
        if document.get("schema") not in (None, "orchestra.policy.v1"):
            raise ValueError("unsupported policy schema")
        policy = document.get("policy", document)
        if not isinstance(policy, dict):
            raise ValueError("policy must be an object")
        mode = enum_value(policy.get("mode", "EVALUATE"), "policy.mode", set(MODES))
        controller = enum_value(
            policy.get("controller_state", "NORMAL"),
            "policy.controller_state",
            CONTROLLERS,
        )
        entries = policy.get("entries", [])
        if not isinstance(entries, list):
            raise ValueError("policy.entries must be an array")
        if len(entries) > 64:
            raise ValueError("policy.entries exceeds the bounded 64-state policy bank")
        commands = []
        for raw_entry in entries:
            if not isinstance(raw_entry, dict):
                raise ValueError("each policy entry must be an object")
            commands.append(bridge_command(args.bridge, raw_entry, mode, controller))
        commit = [
            args.bridge,
            "--policy-commit",
            "--policy-mode",
            str(MODES[mode]),
            "--controller-state",
            controller,
        ]
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"policy validation failed: {error}", file=sys.stderr)
        return 2

    if args.dry_run:
        for command in (*commands, commit):
            print(" ".join(command))
        return 0

    for command in commands:
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            print(f"policy entry failed with status {result.returncode}", file=sys.stderr)
            return result.returncode
    result = subprocess.run(commit, check=False)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
