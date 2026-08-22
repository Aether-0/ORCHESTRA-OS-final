#!/usr/bin/env python3
"""Validate and publish one versioned ORCHESTRA policy document.

The policy loader is a privileged control-plane client.  It deliberately
keeps parsing, bounds checking, and command construction separate from the
bridge so malformed documents cannot reach the BPF maps.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ACTION_NAMES = {"RUN", "SLEEP", "MIGRATE", "THROTTLE", "YIELD"}
MODES = {"TRAIN": 0, "ADAPT": 1, "EVALUATE": 2}
CONTROLLERS = {"NORMAL", "DEGRADED", "SATURATED", "DISABLED", "ROLLBACK", "RECOVERY"}

# The kernel policy ARRAY has two banks of 256 entries.  The former loader
# limit of 64 was an accidental implementation restriction, not a safety
# boundary.  The byte bound remains a resource-protection limit.
MAX_POLICY_ENTRIES = 256
MAX_POLICY_BYTES = 1024 * 1024
MAX_U64 = (1 << 64) - 1
BRIDGE_COMMAND_TIMEOUT_SECONDS = 10
POLICY_PUBLISH_TIMEOUT_SECONDS = 300


def integer(value: object, field: str, minimum: int = 0, maximum: int = MAX_U64) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < minimum
        or value > maximum
    ):
        raise ValueError(f"{field} must be an integer in [{minimum}, {maximum}]")
    return value


def enum_value(value: object, field: str, values: set[str]) -> str:
    if not isinstance(value, str) or value.upper() not in values:
        raise ValueError(f"{field} must be one of {sorted(values)}")
    return value.upper()


def _safe_path_chain(path: Path) -> None:
    """Reject symlink path components before a privileged open/exec."""

    if any(component in {".", ".."} for component in path.parts):
        raise ValueError(f"refusing dot path component: {path}")
    target = path if path.is_absolute() else Path.cwd() / path
    current = target
    while True:
        try:
            info = current.lstat()
        except FileNotFoundError:
            current = current.parent
            if current == current.parent:
                return
            continue
        if stat.S_ISLNK(info.st_mode):
            raise ValueError(f"refusing symlink path component: {current}")
        if current == current.parent:
            return
        current = current.parent


def _safe_privileged_path_chain(path: Path) -> None:
    """Require non-writable parents for a root-facing open or exec.

    A symlink check alone is not enough: a caller could replace a checked
    filename inside a group/world-writable directory between validation and
    ``open``/``exec``.  Root-owned sticky directories such as ``/tmp`` retain
    their kernel-enforced replacement protection; other writable parents are
    rejected.  Root additionally requires every existing path component to
    be root-owned.
    """

    _safe_path_chain(path)
    target = path if path.is_absolute() else Path.cwd() / path
    current = target
    running_as_root = os.geteuid() == 0
    while True:
        try:
            info = current.lstat()
        except FileNotFoundError:
            current = current.parent
            if current == current.parent:
                return
            continue
        if stat.S_ISLNK(info.st_mode):
            raise ValueError(f"refusing symlink path component: {current}")
        # The target file's ownership/mode is checked by the caller after a
        # descriptor is opened.  Only parent components participate in the
        # pre-open replacement/TOCTOU check.
        if current != target and info.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
            if not (info.st_uid == 0 and info.st_mode & stat.S_ISVTX):
                raise ValueError(f"refusing writable path component: {current}")
        if running_as_root and info.st_uid != 0:
            raise ValueError(f"root path component is not root-owned: {current}")
        if current == current.parent:
            return
        current = current.parent


def _read_policy_bytes(path: Path) -> bytes:
    _safe_privileged_path_chain(path)
    try:
        descriptor = os.open(
            path,
            os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_CLOEXEC", 0),
        )
    except OSError as error:
        raise ValueError(f"cannot open policy file: {error}") from error
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("policy file must be a regular file")
        if os.geteuid() == 0 and (
            info.st_uid != 0 or info.st_mode & (stat.S_IWGRP | stat.S_IWOTH)
        ):
            raise ValueError("root policy input must be root-owned and non-writable")
        if info.st_size > MAX_POLICY_BYTES:
            raise ValueError(
                f"policy file exceeds the bounded {MAX_POLICY_BYTES}-byte input limit"
            )
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            payload = stream.read(MAX_POLICY_BYTES + 1)
        if len(payload) > MAX_POLICY_BYTES:
            raise ValueError(
                f"policy file exceeds the bounded {MAX_POLICY_BYTES}-byte input limit"
            )
        return payload
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    document: dict[str, Any] = {}
    for key, value in pairs:
        if key in document:
            raise ValueError(f"duplicate JSON object key: {key}")
        document[key] = value
    return document


def load_policy_document(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(
            _read_policy_bytes(path).decode("utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
        )
    except UnicodeDecodeError as error:
        raise ValueError(f"policy file is not UTF-8: {error}") from error
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON: {error}") from error
    except RecursionError as error:
        raise ValueError("policy JSON nesting exceeds the parser safety limit") from error
    if not isinstance(document, dict):
        raise ValueError("policy document must be a JSON object")
    return document


def _safe_bridge_path(path: Path) -> None:
    _safe_privileged_path_chain(path)
    try:
        info = path.lstat()
    except FileNotFoundError as error:
        raise ValueError(f"bridge does not exist: {path}") from error
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise ValueError(f"bridge must be a regular non-symlink file: {path}")
    if info.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
        raise ValueError(f"bridge is group/world writable: {path}")
    if not info.st_mode & stat.S_IXUSR:
        raise ValueError(f"bridge is not executable: {path}")
    if os.geteuid() == 0 and info.st_uid != 0:
        raise ValueError(f"bridge is not root-owned: {path}")


def bridge_command(
    bridge: str, entry: dict[str, object], mode: str, controller: str
) -> list[str]:
    action = enum_value(entry.get("action"), "entry.action", ACTION_NAMES)
    state_index = integer(
        entry.get("state_index"), "entry.state_index", 0, MAX_POLICY_ENTRIES - 1
    )
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


def validate_policy_document(
    document: dict[str, Any], bridge: str
) -> tuple[list[list[str]], list[str]]:
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
    if len(entries) > MAX_POLICY_ENTRIES:
        raise ValueError(
            f"policy.entries exceeds the {MAX_POLICY_ENTRIES}-state policy bank"
        )
    commands: list[list[str]] = []
    seen_indices: set[int] = set()
    for raw_entry in entries:
        if not isinstance(raw_entry, dict):
            raise ValueError("each policy entry must be an object")
        state_index = integer(
            raw_entry.get("state_index"),
            "entry.state_index",
            0,
            MAX_POLICY_ENTRIES - 1,
        )
        if state_index in seen_indices:
            raise ValueError(f"duplicate policy state_index: {state_index}")
        seen_indices.add(state_index)
        commands.append(bridge_command(bridge, raw_entry, mode, controller))
    commit = [
        bridge,
        "--policy-commit",
        "--policy-mode",
        str(MODES[mode]),
        "--controller-state",
        controller,
    ]
    return commands, commit


def run_bridge(command: list[str], timeout: float = BRIDGE_COMMAND_TIMEOUT_SECONDS) -> int:
    """Run one bounded bridge operation; never wait forever on a bad map path."""

    try:
        return subprocess.run(command, check=False, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        print(
            f"bridge command timed out after {timeout:.0f}s: {shlex.join(command)}",
            file=sys.stderr,
        )
        return 124


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True)
    parser.add_argument("config", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    try:
        if not args.dry_run and os.geteuid() != 0:
            raise ValueError("publishing a policy requires root")
        if not args.dry_run:
            _safe_bridge_path(Path(args.bridge))
        document = load_policy_document(args.config)
        commands, commit = validate_policy_document(document, args.bridge)
    except (OSError, ValueError) as error:
        print(f"policy validation failed: {error}", file=sys.stderr)
        return 2

    if args.dry_run:
        for command in (*commands, commit):
            print(shlex.join(command))
        return 0

    deadline = time.monotonic() + POLICY_PUBLISH_TIMEOUT_SECONDS
    for command in commands:
        remaining = deadline - time.monotonic()
        result_code = run_bridge(
            command,
            timeout=min(BRIDGE_COMMAND_TIMEOUT_SECONDS, max(0.1, remaining)),
        )
        if result_code != 0:
            abort_remaining = deadline - time.monotonic()
            abort_result_code = run_bridge(
                [args.bridge, "--policy-abort"],
                timeout=min(BRIDGE_COMMAND_TIMEOUT_SECONDS, max(0.1, abort_remaining)),
            )
            if abort_result_code != 0:
                print(
                    f"policy abort also failed with status {abort_result_code}; "
                    "the inactive bank requires operator review",
                    file=sys.stderr,
                )
            print(f"policy entry failed with status {result_code}", file=sys.stderr)
            return result_code
    remaining = deadline - time.monotonic()
    result_code = run_bridge(
        commit,
        timeout=min(BRIDGE_COMMAND_TIMEOUT_SECONDS, max(0.1, remaining)),
    )
    if result_code != 0:
        abort_remaining = deadline - time.monotonic()
        abort_result_code = run_bridge(
            [args.bridge, "--policy-abort"],
            timeout=min(BRIDGE_COMMAND_TIMEOUT_SECONDS, max(0.1, abort_remaining)),
        )
        if abort_result_code != 0:
            print(
                f"policy abort also failed with status {abort_result_code}; "
                "the inactive bank requires operator review",
                file=sys.stderr,
            )
    return result_code


if __name__ == "__main__":
    raise SystemExit(main())
