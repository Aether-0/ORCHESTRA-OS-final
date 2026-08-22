#!/usr/bin/env python3
"""Non-privileged regression tests for the policy trust boundary."""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from policy_load import (  # noqa: E402
    MAX_POLICY_BYTES,
    MAX_POLICY_ENTRIES,
    _safe_bridge_path,
    load_policy_document,
    validate_policy_document,
)


def entry(index: int) -> dict[str, object]:
    return {"state_index": index, "action": "RUN"}


def expect_value_error(function) -> None:
    try:
        function()
    except ValueError:
        return
    raise AssertionError("malformed policy input was accepted")


def main() -> None:
    document = {
        "schema": "orchestra.policy.v1",
        "policy": {
            "mode": "ADAPT",
            "controller_state": "NORMAL",
            "entries": [entry(index) for index in range(MAX_POLICY_ENTRIES)],
        },
    }
    commands, commit = validate_policy_document(document, "/trusted/orchestra_bridge")
    assert len(commands) == MAX_POLICY_ENTRIES
    assert commit[-1] == "NORMAL"
    assert commands[-1][commands[-1].index("--policy-state-index") + 1] == "255"

    duplicate = {"policy": {"entries": [entry(1), entry(1)]}}
    expect_value_error(lambda: validate_policy_document(duplicate, "/bridge"))
    out_of_range = {"policy": {"entries": [entry(MAX_POLICY_ENTRIES)]}}
    expect_value_error(lambda: validate_policy_document(out_of_range, "/bridge"))
    invalid_integer = {"policy": {"entries": [{"state_index": True, "action": "RUN"}]}}
    expect_value_error(lambda: validate_policy_document(invalid_integer, "/bridge"))
    invalid_action = {"policy": {"entries": [{"state_index": 0, "action": "INVALID"}]}}
    expect_value_error(lambda: validate_policy_document(invalid_action, "/bridge"))

    with tempfile.TemporaryDirectory(prefix="orchestra-policy-security-") as directory:
        root = Path(directory)
        valid_path = root / "policy.json"
        valid_path.write_text(json.dumps(document), encoding="utf-8")
        assert load_policy_document(valid_path)["schema"] == "orchestra.policy.v1"

        symlink = root / "policy-link.json"
        symlink.symlink_to(valid_path)
        expect_value_error(lambda: load_policy_document(symlink))

        oversized = root / "oversized.json"
        oversized.write_bytes(b"{" + (b" " * MAX_POLICY_BYTES))
        expect_value_error(lambda: load_policy_document(oversized))

        malformed = root / "malformed.json"
        malformed.write_bytes(b"{\"policy\":")
        expect_value_error(lambda: load_policy_document(malformed))

        duplicate_keys = root / "duplicate.json"
        duplicate_keys.write_text(
            '{"policy":{"mode":"ADAPT","mode":"TRAIN","entries":[]}}',
            encoding="utf-8",
        )
        expect_value_error(lambda: load_policy_document(duplicate_keys))

        deeply_nested = root / "deep.json"
        deeply_nested.write_text("{" * 1200 + "}" * 1200, encoding="utf-8")
        expect_value_error(lambda: load_policy_document(deeply_nested))

        writable_parent = root / "writable-parent"
        writable_parent.mkdir()
        writable_parent.chmod(0o777)
        bridge = writable_parent / "orchestra_bridge"
        bridge.write_text("not an executable", encoding="utf-8")
        bridge.chmod(0o755)
        expect_value_error(lambda: _safe_bridge_path(bridge))

    assert os.geteuid() >= 0
    print("PASS policy-loader security regressions")


if __name__ == "__main__":
    main()
