#!/usr/bin/env python3
"""Negative and positive tests for the strict benchmark CSV validator."""

from __future__ import annotations

import copy
import csv
import importlib.util
import math
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
HARNESS_PATH = REPOSITORY / "tools/benchmark/run_paper_cpu_benchmark.py"
V2_SCHEMA_PATH = REPOSITORY / "experiments/schemas/paper_cpu_metrics_v2.json"
V3_SCHEMA_PATH = REPOSITORY / "experiments/schemas/paper_cpu_metrics_v3.json"
V4_SCHEMA_PATH = REPOSITORY / "experiments/schemas/paper_cpu_metrics_v4.json"
V6_SCHEMA_PATH = REPOSITORY / "experiments/schemas/paper_cpu_metrics_v6.json"
V7_SCHEMA_PATH = REPOSITORY / "experiments/schemas/paper_cpu_metrics_v7.json"
V3_MANIFEST_PATH = REPOSITORY / "experiments/manifests/paper_cpu_exploratory_v3.json"
V4_MANIFEST_PATH = REPOSITORY / "experiments/manifests/paper_cpu_exploratory_v4.json"
V6_MANIFEST_PATH = REPOSITORY / "experiments/manifests/paper_cpu_exploratory_v6.json"
V7_MANIFEST_PATH = REPOSITORY / "experiments/manifests/paper_cpu_exploratory_v7.json"

spec = importlib.util.spec_from_file_location("orchestra_benchmark", HARNESS_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load benchmark harness from {HARNESS_PATH}")
harness = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = harness
spec.loader.exec_module(harness)


def valid_rows() -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for tick in range(1, 41):
        updated = tick % harness.CONTROLLER_PERIOD_TICKS == 0
        step = tick // harness.CONTROLLER_PERIOD_TICKS
        beta = harness.CONTROLLER_BETA0 / math.sqrt(1.0 + step) if updated else 0.0
        rows.append(
            {
                "tick": str(tick),
                "mode": "orchestra",
                "cpu_now": "0.10000000",
                "cpu_pred": "0.10000000",
                "decision_cpu": "0.10000000",
                "prediction_used": "1",
                "confidence": "1.00000000",
                "forecast_error": "0.00000000",
                "frame_age_ms": "50.00000000",
                "mem": "0.10000000",
                "thermal": "0.10000000",
                "directive": "SLEEP",
                "run": "0",
                "sleep": "4",
                "migrate": "0",
                "throttle": "0",
                "yield": "0",
                "eligible_workers": "4",
                "fallback_workers": "0",
                "S1": "1.00000000",
                "S2": "1.00000000",
                "S3": "1.00000000",
                "S4": "1.00000000",
                "Q": "1.00000000",
                "jitter_sigma": "0.02000000",
                "switch_penalty": "0.00000000",
                "consensus_blend": "0.00000000",
                "next_jitter_sigma": "0.02000000",
                "next_switch_penalty": "0.00000000",
                "next_consensus_blend": "0.00000000",
                "controller_updated": "1" if updated else "0",
                "controller_step": str(step),
                "controller_reason": "NONE",
                "controller_beta": f"{beta:.8f}",
                "jitter_saturated": "1" if updated else "0",
                "switch_saturated": "1" if updated else "0",
                "consensus_saturated": "1" if updated else "0",
                "consensus_applied": "0",
                "rejected_frames": "0",
                "missed_deadlines": "0",
            }
        )
    return rows


def valid_v3_rows() -> list[dict[str, str]]:
    """Create a valid append-only v3 fixture with observable sleep outcomes."""

    rows = copy.deepcopy(valid_rows())
    for row in rows:
        row.update(
            {
                "metrics_schema": harness.V3_SCHEMA_ID,
                "S3_global": row["S3"],
                "S3_conditioned": "1.00000000",
                "S2_selected": row["S2"],
                "S2_effective": "1.00000000",
                "action_attempt_count": "4",
                "effective_action_success_count": "4",
                "action_error_count": "0",
                "migration_attempt_count": "0",
                "migration_valid_requested_cpu_count": "0",
                "migration_affinity_success_count": "0",
                "migration_observed_success_count": "0",
                "migration_observed_success_fraction": "1.00000000",
                "sleep_attempt_count": "4",
                "sleep_effective_success_count": "4",
                "sleep_effectiveness_fraction": "1.00000000",
                "requested_sleep_ns_total": "20000000",
                "observed_sleep_ns_total": "20000000",
                "yield_attempt_count": "0",
                "yield_call_success_count": "0",
                "yield_call_success_fraction": "1.00000000",
                "throttle_attempt_count": "0",
                "throttle_operation_success_count": "0",
                "throttle_operation_success_fraction": "1.00000000",
                "fallback_fraction": "0.00000000",
                "fallback_reason": "NONE",
            }
        )
    return rows


def valid_v4_rows() -> list[dict[str, str]]:
    """Create a no-change v4 fixture using documented neutral conventions."""

    rows = valid_v3_rows()
    for index, row in enumerate(rows):
        row.update(
            {
                "metrics_schema": harness.V4_SCHEMA_ID,
                "S4_burst": "1.00000000",
                "change_fraction": "0.00000000",
                "dominant_transition_fraction": "0.00000000",
                "justified_change_fraction": "0.00000000",
                "oscillation_penalty": "0.00000000",
                "dominant_old_action": "-1",
                "dominant_new_action": "-1",
                "changed_eligible_workers": "0",
                "justified_changed_workers": "0",
                "dominant_transition_count": "0",
                "rolling_window_burst_count": "0",
                "rolling_window_oscillation_count": "0",
                "current_directive_valid": "1",
                "previous_directive_valid": "0" if index == 0 else "1",
                "directive_transition_valid": "0",
                "large_burst_event": "0",
                "repeated_oscillation_event": "0",
            }
        )
    return rows


def valid_v7_rows() -> list[dict[str, str]]:
    """Create a valid v7 fixture with conditioned coordination aliases."""

    rows = valid_v4_rows()
    for row in rows:
        row.update(
            {
                "metrics_schema": harness.V7_SCHEMA_ID,
                "S3_global": "1.00000000",
                "S3_conditioned": "1.00000000",
                "S2_selected": row["S2"],
                "controller_state": "NORMAL",
                "previous_state": "NORMAL",
                "transition_reason": "NONE",
                "state_residence_time": "0",
                "valid_control_history_count": "0",
                "invalid_frame_fault_count": "0",
                "saturation_bitmask": "0",
                "saturation_direction": "0",
                "saturation_persistence": "0",
                "oscillation_score": "0.00000000",
                "oscillation_event": "0",
                "rollback_event": "0",
                "rollback_reason": "NONE",
                "recovery_progress": "0.00000000",
                "last_known_good_available": "0",
                "requested_jitter": "0.02000000",
                "applied_jitter": "0.02000000",
                "requested_switch": "0.00000000",
                "applied_switch": "0.00000000",
                "requested_consensus": "0.00000000",
                "applied_consensus": "0.00000000",
                "update_accepted": "0",
                "update_suppressed": "0",
                "suppression_reason": "0",
                "policy_mode": "TRAIN",
                "policy_schema_version": "1",
                "policy_generation": "0",
                "policy_update_allowed": "1",
                "policy_update_applied": "0",
                "policy_update_suppression_reason": "NONE",
                "policy_exploration_enabled": "1",
                "policy_train_update_count": "0",
                "policy_adapt_update_count": "0",
                "policy_load_status": "SKIP",
                "policy_save_status": "OK",
                "policy_digest_prefix": "0000000000000000",
                "policy_format_version": "1",
                "coordination_semantics_version": "7",
            }
        )
    return rows


class BenchmarkValidatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema = harness.load_metrics_schema(V2_SCHEMA_PATH, harness.V2_SCHEMA_ID)
        cls.v3_schema = harness.load_metrics_schema(V3_SCHEMA_PATH, harness.V3_SCHEMA_ID)
        cls.v4_schema = harness.load_metrics_schema(V4_SCHEMA_PATH, harness.V4_SCHEMA_ID)
        cls.v6_schema = harness.load_metrics_schema(V6_SCHEMA_PATH, harness.V6_SCHEMA_ID)
        cls.v7_schema = harness.load_metrics_schema(V7_SCHEMA_PATH, harness.V7_SCHEMA_ID)

    def validate(
        self,
        rows: list[dict[str, str]],
        schema: harness.MetricsSchema | None = None,
        expected_rows: int = 40,
    ):
        active_schema = self.schema if schema is None else schema
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=active_schema.header)
                writer.writeheader()
                writer.writerows(rows)
            return harness.validate_csv(
                path,
                active_schema,
                "orchestra",
                4,
                10,
                expected_rows,
            )

    def test_valid_contract(self) -> None:
        result = self.validate(valid_rows())
        self.assertTrue(result.valid, result.issues)

    def test_truncated_run_is_rejected(self) -> None:
        result = self.validate(valid_rows()[:1])
        self.assertFalse(result.valid)
        self.assertTrue(any("expected exactly 40" in issue for issue in result.issues))

    def test_fabricated_s3_is_rejected(self) -> None:
        rows = valid_rows()
        rows[0]["S3"] = "0.50000000"
        rows[0]["Q"] = f"{0.5 ** 0.25:.8f}"
        result = self.validate(rows)
        self.assertFalse(result.valid)
        self.assertTrue(any("action-entropy coherence" in issue for issue in result.issues))

    def test_wrong_on_cadence_actuator_is_rejected(self) -> None:
        rows = copy.deepcopy(valid_rows())
        rows[19]["next_jitter_sigma"] = "0.03000000"
        rows[20]["jitter_sigma"] = "0.03000000"
        result = self.validate(rows)
        self.assertFalse(result.valid)
        self.assertTrue(any("causal controller mapping" in issue for issue in result.issues))

    def test_valid_v3_contract_and_invocation_summary(self) -> None:
        result = self.validate(valid_v3_rows(), self.v3_schema)
        self.assertTrue(result.valid, result.issues)
        summary = harness.make_run_summary(
            "v3-run",
            "orchestra",
            1,
            17,
            result.rows_after_warmup,
            harness.V3_SCHEMA_ID,
        )
        self.assertEqual(summary["S2_selected_mean"], 1.0)
        self.assertEqual(summary["S2_effective_mean"], 1.0)
        self.assertEqual(summary["S3_global_mean"], 1.0)
        self.assertEqual(summary["S3_conditioned_mean"], 1.0)
        self.assertEqual(summary["sleep_effectiveness_fraction"], 1.0)
        self.assertEqual(summary["migration_observed_success_fraction"], 1.0)
        self.assertEqual(summary["fallback_fraction"], 0.0)
        self.assertEqual(summary["action_error_count"], 0)
        aggregate = harness.aggregate_run_summaries([summary], harness.V3_SCHEMA_ID)
        self.assertEqual(aggregate["metrics_schema_id"], harness.V3_SCHEMA_ID)
        self.assertIn("effective_action_summary_semantics", aggregate)
        mode_metrics = aggregate["modes"]["orchestra"]["metrics"]
        self.assertIn("S2_effective_mean", mode_metrics)
        self.assertIn("action_error_count", mode_metrics)

    def test_valid_v4_contract_and_burst_invocation_summary(self) -> None:
        result = self.validate(valid_v4_rows(), self.v4_schema)
        self.assertTrue(result.valid, result.issues)
        summary = harness.make_run_summary(
            "v4-run",
            "orchestra",
            1,
            23,
            result.rows_after_warmup,
            harness.V4_SCHEMA_ID,
        )
        self.assertEqual(summary["S4_mean"], 1.0)
        self.assertEqual(summary["S4_burst_mean"], 1.0)
        self.assertEqual(summary["change_fraction_mean"], 0.0)
        self.assertEqual(summary["dominant_transition_fraction_mean"], 0.0)
        self.assertEqual(summary["justified_change_fraction_mean"], 0.0)
        self.assertEqual(summary["oscillation_penalty_mean"], 0.0)
        self.assertEqual(summary["large_burst_event_count"], 0)
        self.assertEqual(summary["repeated_oscillation_event_count"], 0)
        self.assertEqual(summary["Q_mean"], 1.0)
        aggregate = harness.aggregate_run_summaries([summary], harness.V4_SCHEMA_ID)
        self.assertEqual(aggregate["metrics_schema_id"], harness.V4_SCHEMA_ID)
        self.assertIn("burst_stability_summary_semantics", aggregate)
        mode_metrics = aggregate["modes"]["orchestra"]["metrics"]
        self.assertIn("S4_burst_mean", mode_metrics)
        self.assertIn("large_burst_event_count", mode_metrics)
        self.assertIn("repeated_oscillation_event_count", mode_metrics)

    def test_v4_burst_formula_and_event_summary(self) -> None:
        rows = valid_v4_rows()
        # A same-transition, unjustified population burst has a deterministic
        # 0.8 penalty under the documented v4 formula.
        row = rows[0]
        row.update(
            {
                "S4": "0.00000000",
                "Q": "0.00000000",
                "S4_burst": "0.20000000",
                "change_fraction": "1.00000000",
                "dominant_transition_fraction": "1.00000000",
                "justified_change_fraction": "0.00000000",
                "dominant_old_action": "0",
                "dominant_new_action": "1",
                "changed_eligible_workers": "4",
                "justified_changed_workers": "0",
                "dominant_transition_count": "4",
                "rolling_window_burst_count": "1",
                "rolling_window_oscillation_count": "0",
                "large_burst_event": "1",
                "repeated_oscillation_event": "0",
            }
        )
        result = self.validate(rows, self.v4_schema)
        self.assertTrue(result.valid, result.issues)
        summary = harness.make_run_summary(
            "v4-burst",
            "orchestra",
            1,
            29,
            result.rows_after_warmup,
            harness.V4_SCHEMA_ID,
        )
        # The constructed burst is in warm-up, so counts remain invocation
        # summaries of post-warm-up observations only.
        self.assertEqual(summary["large_burst_event_count"], 0)

    def test_v4_inconsistent_burst_components_are_rejected(self) -> None:
        rows = valid_v4_rows()
        rows[0]["dominant_transition_count"] = "1"
        result = self.validate(rows, self.v4_schema)
        self.assertFalse(result.valid)
        self.assertTrue(any("dominant_transition_count" in issue for issue in result.issues))

    def test_valid_v7_contract_and_append_only_schema_resolution(self) -> None:
        result = self.validate(valid_v7_rows(), self.v7_schema)
        self.assertTrue(result.valid, result.issues)
        self.assertEqual(len(self.v6_schema.header), 120)
        self.assertEqual(len(self.v7_schema.header), 121)
        self.assertEqual(
            self.v7_schema.header[-1], "coordination_semantics_version"
        )
        summary = harness.make_run_summary(
            "v7-run",
            "orchestra",
            1,
            41,
            result.rows_after_warmup,
            harness.V7_SCHEMA_ID,
        )
        self.assertEqual(summary["S3_conditioned_mean"], 1.0)
        self.assertEqual(summary["S4_burst_mean"], 1.0)
        self.assertEqual(
            summary["policy_lifecycle"]["policy_modes"],  # type: ignore[index]
            ["TRAIN"],
        )
        aggregate = harness.aggregate_run_summaries([summary], harness.V7_SCHEMA_ID)
        self.assertEqual(aggregate["metrics_schema_id"], harness.V7_SCHEMA_ID)
        orchestra_mode = aggregate["modes"]["orchestra"]  # type: ignore[index]
        self.assertEqual(
            orchestra_mode["policy_lifecycle"]["policy_update_applied_rows"]["mean"],  # type: ignore[index]
            0.0,
        )

    def test_v7_policy_contract_rejects_inconsistent_mode_flags(self) -> None:
        rows = valid_v7_rows()
        rows[0]["policy_mode"] = "EVALUATE"
        result = self.validate(rows, self.v7_schema)
        self.assertFalse(result.valid)
        self.assertTrue(
            any("policy_exploration_enabled" in issue for issue in result.issues)
        )

    def test_v6_and_v7_manifests_declare_exact_column_contract(self) -> None:
        v6 = harness.load_manifest(V6_MANIFEST_PATH, enforce_host=False)
        v7 = harness.load_manifest(V7_MANIFEST_PATH, enforce_host=False)
        self.assertEqual(v6.expected_csv_column_count, 120)
        self.assertEqual(v7.expected_csv_column_count, 121)
        self.assertEqual(v6.metrics_schema_id, harness.V6_SCHEMA_ID)
        self.assertEqual(v7.metrics_schema_id, harness.V7_SCHEMA_ID)

    def test_v4_no_change_sentinels_are_required(self) -> None:
        rows = valid_v4_rows()
        rows[0]["dominant_new_action"] = "0"
        result = self.validate(rows, self.v4_schema)
        self.assertFalse(result.valid)
        self.assertTrue(any("sentinels" in issue for issue in result.issues))

    def test_v3_effective_action_identity_is_rejected_when_inconsistent(self) -> None:
        rows = valid_v3_rows()
        rows[0]["effective_action_success_count"] = "3"
        result = self.validate(rows, self.v3_schema)
        self.assertFalse(result.valid)
        self.assertTrue(any("S2_effective" in issue for issue in result.issues))

    def test_v3_external_validator_is_executed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=self.v3_schema.header)
                writer.writeheader()
                writer.writerows(valid_v3_rows())
            result = harness.run_v3_integration_validator(
                REPOSITORY,
                path,
                "orchestra",
                40,
                harness.V3_SCHEMA_ID,
            )
        self.assertTrue(result["required"])
        self.assertTrue(result["executed"])
        self.assertTrue(result["valid"], result["stderr"])
        self.assertEqual(result["return_code"], 0)

    def test_v4_external_validator_is_executed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=self.v4_schema.header)
                writer.writeheader()
                writer.writerows(valid_v4_rows())
            result = harness.run_integration_validator(
                REPOSITORY,
                path,
                "orchestra",
                40,
                harness.V4_SCHEMA_ID,
            )
        self.assertTrue(result["required"])
        self.assertTrue(result["executed"])
        self.assertTrue(result["valid"], result["stderr"])
        self.assertEqual(result["return_code"], 0)

    def test_v2_external_validator_is_explicitly_not_required(self) -> None:
        result = harness.run_v3_integration_validator(
            REPOSITORY,
            V2_SCHEMA_PATH,
            "orchestra",
            40,
            harness.V2_SCHEMA_ID,
        )
        self.assertFalse(result["required"])
        self.assertTrue(result["valid"])

    def test_manifest_schema_mismatch_is_rejected(self) -> None:
        with self.assertRaisesRegex(harness.BenchmarkError, "schema_id mismatch"):
            harness.load_metrics_schema(V2_SCHEMA_PATH, harness.V3_SCHEMA_ID)

    def test_v3_manifest_declares_the_exact_column_contract(self) -> None:
        manifest = harness.load_manifest(V3_MANIFEST_PATH, enforce_host=False)
        self.assertEqual(manifest.metrics_schema_id, harness.V3_SCHEMA_ID)
        self.assertEqual(manifest.expected_csv_column_count, len(harness.V3_EXPECTED_HEADER))

    def test_v4_manifest_declares_the_exact_column_contract(self) -> None:
        manifest = harness.load_manifest(V4_MANIFEST_PATH, enforce_host=False)
        self.assertEqual(manifest.metrics_schema_id, harness.V4_SCHEMA_ID)
        self.assertEqual(manifest.expected_csv_column_count, 83)
        self.assertEqual(manifest.expected_csv_column_count, len(harness.V4_EXPECTED_HEADER))

    def test_v2_csv_cannot_be_validated_as_v3(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=harness.V2_EXPECTED_HEADER)
                writer.writeheader()
                writer.writerows(valid_rows())
            result = harness.validate_csv(path, self.v3_schema, "orchestra", 4, 10, 40)
        self.assertFalse(result.valid)
        self.assertTrue(any("does not match strict" in issue for issue in result.issues))

    def test_v3_csv_cannot_be_validated_as_v4(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=harness.V3_EXPECTED_HEADER)
                writer.writeheader()
                writer.writerows(valid_v3_rows())
            result = harness.validate_csv(path, self.v4_schema, "orchestra", 4, 10, 40)
        self.assertFalse(result.valid)
        self.assertTrue(any("does not match strict" in issue for issue in result.issues))

    def test_v4_csv_cannot_be_validated_as_v3(self) -> None:
        with tempfile.TemporaryDirectory(prefix="orchestra-validator-") as directory:
            path = Path(directory) / "metrics.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=harness.V4_EXPECTED_HEADER)
                writer.writeheader()
                writer.writerows(valid_v4_rows())
            result = harness.validate_csv(path, self.v3_schema, "orchestra", 4, 10, 40)
        self.assertFalse(result.valid)
        self.assertTrue(any("does not match strict" in issue for issue in result.issues))

    def test_summary_aggregation_rejects_mixed_schema_inputs(self) -> None:
        result = self.validate(valid_v3_rows(), self.v3_schema)
        self.assertTrue(result.valid, result.issues)
        summary = harness.make_run_summary(
            "v3-run",
            "orchestra",
            1,
            17,
            result.rows_after_warmup,
            harness.V3_SCHEMA_ID,
        )
        with self.assertRaisesRegex(AssertionError, "different metrics schemas"):
            harness.aggregate_run_summaries([summary], harness.V2_SCHEMA_ID)

    def test_v4_summary_aggregation_rejects_v3_inputs(self) -> None:
        v3_result = self.validate(valid_v3_rows(), self.v3_schema)
        v4_result = self.validate(valid_v4_rows(), self.v4_schema)
        self.assertTrue(v3_result.valid, v3_result.issues)
        self.assertTrue(v4_result.valid, v4_result.issues)
        v3_summary = harness.make_run_summary(
            "v3-run", "orchestra", 1, 31, v3_result.rows_after_warmup, harness.V3_SCHEMA_ID
        )
        v4_summary = harness.make_run_summary(
            "v4-run", "orchestra", 1, 37, v4_result.rows_after_warmup, harness.V4_SCHEMA_ID
        )
        with self.assertRaisesRegex(AssertionError, "different metrics schemas"):
            harness.aggregate_run_summaries([v3_summary, v4_summary], harness.V4_SCHEMA_ID)


if __name__ == "__main__":
    unittest.main(verbosity=2)
