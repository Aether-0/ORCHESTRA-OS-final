#!/usr/bin/env python3
"""Run bounded, provenance-rich ORCHESTRA paper CPU userspace experiments.

This runner intentionally produces descriptive, unpaired, endogenous userspace
measurements. It does not make kernel-scheduler or comparative-performance
claims. Only Python's standard library is used.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import pwd
import re
import resource
import shlex
import shutil
import signal
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Final, TypeAlias


V2_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v2"
V3_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v3"

V4_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v4"
V5_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v5"
V6_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v6"
V7_SCHEMA_ID: Final = "orchestra.paper_cpu.metrics/v7"

# Retain the historical public names for v2-focused callers and tests.
SCHEMA_ID: Final = V2_SCHEMA_ID
SUPPORTED_SCHEMA_IDS: Final = (
    V2_SCHEMA_ID,
    V3_SCHEMA_ID,
    V4_SCHEMA_ID,
    V5_SCHEMA_ID,
    V6_SCHEMA_ID,
    V7_SCHEMA_ID,
)
MANIFEST_ID: Final = "orchestra.paper_cpu.benchmark_manifest/v1"
COMPARISON_DESIGN: Final = "descriptive-unpaired-endogenous"
MATURITY_CLASS: Final = "userspace-validated"
EXPLORATORY_MATURITY_CLASS: Final = "exploratory"
CONTROLLER_PERIOD_TICKS: Final = 20
CONTROLLER_THRESHOLD: Final = 0.82
CONTROLLER_BETA0: Final = 0.02

V2_EXPECTED_HEADER: Final = (
    "tick",
    "mode",
    "cpu_now",
    "cpu_pred",
    "decision_cpu",
    "prediction_used",
    "confidence",
    "forecast_error",
    "frame_age_ms",
    "mem",
    "thermal",
    "directive",
    "run",
    "sleep",
    "migrate",
    "throttle",
    "yield",
    "eligible_workers",
    "fallback_workers",
    "S1",
    "S2",
    "S3",
    "S4",
    "Q",
    "jitter_sigma",
    "switch_penalty",
    "consensus_blend",
    "next_jitter_sigma",
    "next_switch_penalty",
    "next_consensus_blend",
    "controller_updated",
    "controller_step",
    "controller_reason",
    "controller_beta",
    "jitter_saturated",
    "switch_saturated",
    "consensus_saturated",
    "consensus_applied",
    "rejected_frames",
    "missed_deadlines",
)

# v3 is append-only: the first forty cells are byte-for-byte the v2 CSV
# contract, followed by the prior coherence extension and effective-action
# aggregates.  Keep this compiled-in order so a malformed external schema
# cannot silently change the benchmark's accepted raw-data format.
V3_EXPECTED_HEADER: Final = (
    *V2_EXPECTED_HEADER,
    "metrics_schema",
    "S3_global",
    "S3_conditioned",
    "S2_selected",
    "S2_effective",
    "action_attempt_count",
    "effective_action_success_count",
    "action_error_count",
    "migration_attempt_count",
    "migration_valid_requested_cpu_count",
    "migration_affinity_success_count",
    "migration_observed_success_count",
    "migration_observed_success_fraction",
    "sleep_attempt_count",
    "sleep_effective_success_count",
    "sleep_effectiveness_fraction",
    "requested_sleep_ns_total",
    "observed_sleep_ns_total",
    "yield_attempt_count",
    "yield_call_success_count",
    "yield_call_success_fraction",
    "throttle_attempt_count",
    "throttle_operation_success_count",
    "throttle_operation_success_fraction",
    "fallback_fraction",
    "fallback_reason",
)

# v4 remains strictly append-only relative to v3.  It preserves historical S4
# and Q, while exposing an experimental burst-sensitive temporal-stability
# diagnostic.  The compiled-in order prevents a schema file from silently
# changing the raw-data format accepted by the benchmark runner.
V4_EXPECTED_HEADER: Final = (
    *V3_EXPECTED_HEADER,
    "S4_burst",
    "change_fraction",
    "dominant_transition_fraction",
    "justified_change_fraction",
    "oscillation_penalty",
    "dominant_old_action",
    "dominant_new_action",
    "changed_eligible_workers",
    "justified_changed_workers",
    "dominant_transition_count",
    "rolling_window_burst_count",
    "rolling_window_oscillation_count",
    "current_directive_valid",
    "previous_directive_valid",
    "directive_transition_valid",
    "large_burst_event",
    "repeated_oscillation_event",
)
# Retain the historical public name for callers that validate v2 artifacts.
EXPECTED_HEADER: Final = V2_EXPECTED_HEADER
V5_EXPECTED_HEADER: Final = (
    *V4_EXPECTED_HEADER,
    "controller_state",
    "previous_state",
    "transition_reason",
    "state_residence_time",
    "valid_control_history_count",
    "invalid_frame_fault_count",
    "saturation_bitmask",
    "saturation_direction",
    "saturation_persistence",
    "oscillation_score",
    "oscillation_event",
    "rollback_event",
    "rollback_reason",
    "recovery_progress",
    "last_known_good_available",
    "requested_jitter",
    "applied_jitter",
    "requested_switch",
    "applied_switch",
    "requested_consensus",
    "applied_consensus",
    "update_accepted",
    "update_suppressed",
    "suppression_reason",
)
V6_EXPECTED_HEADER: Final = (
    *V5_EXPECTED_HEADER,
    "policy_mode",
    "policy_schema_version",
    "policy_generation",
    "policy_update_allowed",
    "policy_update_applied",
    "policy_update_suppression_reason",
    "policy_exploration_enabled",
    "policy_train_update_count",
    "policy_adapt_update_count",
    "policy_load_status",
    "policy_save_status",
    "policy_digest_prefix",
    "policy_format_version",
)
V7_EXPECTED_HEADER: Final = (*V6_EXPECTED_HEADER, "coordination_semantics_version")
EXPECTED_HEADERS_BY_SCHEMA: Final = {
    V2_SCHEMA_ID: V2_EXPECTED_HEADER,
    V3_SCHEMA_ID: V3_EXPECTED_HEADER,
    V4_SCHEMA_ID: V4_EXPECTED_HEADER,
    V5_SCHEMA_ID: V5_EXPECTED_HEADER,
    V6_SCHEMA_ID: V6_EXPECTED_HEADER,
    V7_SCHEMA_ID: V7_EXPECTED_HEADER,
}

ACTION_COLUMNS: Final = ("run", "sleep", "migrate", "throttle", "yield")
DIRECTIVES: Final = ("RUN", "SLEEP", "MIGRATE", "THROTTLE", "YIELD")
MODES: Final = ("baseline", "orchestra")
CONTROLLER_REASONS: Final = ("NONE", "S3", "S4", "S3+S4")
POLICY_MODES: Final = ("TRAIN", "ADAPT", "EVALUATE")
POLICY_SUPPRESSION_REASONS: Final = (
    "NONE",
    "MODE",
    "STATE",
    "FRAME_INVALID",
    "CADENCE",
    "DELTA",
    "EXPLORATION_DISABLED",
)
POLICY_LOAD_STATUSES: Final = (
    "OK",
    "SKIP",
    "MAGIC",
    "VERSION",
    "SCHEMA",
    "TRUNCATED",
    "OVERSIZED",
    "OVERFLOW",
    "STATE_COUNT",
    "ACTION_COUNT",
    "DIMENSIONS",
    "NON_FINITE",
    "DIGEST",
    "TRAILING",
    "MISSING",
)
POLICY_SAVE_STATUSES: Final = (
    "OK",
    "WRITE_ERROR",
    "RENAME_ERROR",
    "TEMP_FAILED",
    "VALIDATION_FAILED",
)
PARAMETER_COLUMNS: Final = (
    "jitter_sigma",
    "switch_penalty",
    "consensus_blend",
)
NEXT_PARAMETER_COLUMNS: Final = (
    "next_jitter_sigma",
    "next_switch_penalty",
    "next_consensus_blend",
)

V2_RUN_MEAN_COLUMNS: Final = (
    "cpu_now",
    "cpu_pred",
    "decision_cpu",
    "confidence",
    "forecast_error",
    "frame_age_ms",
    "mem",
    "thermal",
    "S1",
    "S2",
    "S3",
    "S4",
    "Q",
    "jitter_sigma",
    "switch_penalty",
    "consensus_blend",
    "fallback_workers",
)

V3_RUN_MEAN_COLUMNS: Final = (
    *V2_RUN_MEAN_COLUMNS,
    "S2_selected",
    "S2_effective",
    "S3_global",
    "S3_conditioned",
)

V4_RUN_MEAN_COLUMNS: Final = (
    *V3_RUN_MEAN_COLUMNS,
    "S4_burst",
    "change_fraction",
    "dominant_transition_fraction",
    "justified_change_fraction",
    "oscillation_penalty",
)

V2_AGGREGATE_METRICS: Final = tuple(f"{name}_mean" for name in V2_RUN_MEAN_COLUMNS) + (
    "prediction_used_fraction",
    "fallback_fraction",
    "run_action_fraction",
    "sleep_action_fraction",
    "migrate_action_fraction",
    "throttle_action_fraction",
    "yield_action_fraction",
    "controller_updates",
    "rejected_frames_final",
    "missed_deadlines_final",
)
V3_AGGREGATE_METRICS: Final = tuple(f"{name}_mean" for name in V3_RUN_MEAN_COLUMNS) + (
    "prediction_used_fraction",
    "fallback_fraction",
    "run_action_fraction",
    "sleep_action_fraction",
    "migrate_action_fraction",
    "throttle_action_fraction",
    "yield_action_fraction",
    "migration_observed_success_fraction",
    "sleep_effectiveness_fraction",
    "yield_call_success_fraction",
    "throttle_operation_success_fraction",
    "action_error_count",
    "controller_updates",
    "rejected_frames_final",
    "missed_deadlines_final",
)
V4_AGGREGATE_METRICS: Final = tuple(f"{name}_mean" for name in V4_RUN_MEAN_COLUMNS) + (
    "prediction_used_fraction",
    "fallback_fraction",
    "run_action_fraction",
    "sleep_action_fraction",
    "migrate_action_fraction",
    "throttle_action_fraction",
    "yield_action_fraction",
    "migration_observed_success_fraction",
    "sleep_effectiveness_fraction",
    "yield_call_success_fraction",
    "throttle_operation_success_fraction",
    "action_error_count",
    "large_burst_event_count",
    "repeated_oscillation_event_count",
    "controller_updates",
    "rejected_frames_final",
    "missed_deadlines_final",
)
# Retain v2 constants for existing imports.  Schema-aware helper functions
# below select append-only v3-v7 extensions only for an explicit manifest.
RUN_MEAN_COLUMNS: Final = V2_RUN_MEAN_COLUMNS
AGGREGATE_METRICS: Final = V2_AGGREGATE_METRICS

Scalar: TypeAlias = int | float | str
ParsedRow: TypeAlias = dict[str, Scalar]
JsonObject: TypeAlias = dict[str, object]


class BenchmarkError(RuntimeError):
    """Raised when a benchmark cannot start safely."""


@dataclass(frozen=True)
class Manifest:
    """Validated experiment controls loaded from a manifest."""

    manifest_schema_id: str
    experiment_id: str
    metrics_schema_id: str
    expected_csv_column_count: int | None
    maturity_class: str
    comparison_design: str
    modes: tuple[str, ...]
    workers: int
    rt_exempt: int
    duration_sec: int
    interval_ms: int
    calibration_sec: int
    repetitions: int
    warmup_ticks: int
    seeds: tuple[int, ...]
    timeout_sec: int
    timeout_grace_sec: int
    nice_increment: int


@dataclass(frozen=True)
class CsvColumn:
    """One strict CSV column declaration."""

    name: str
    kind: str
    minimum: float | None
    maximum: float | None
    enum: tuple[str, ...]


@dataclass(frozen=True)
class MetricsSchema:
    """Validated external description of one fixed versioned CSV format."""

    schema_id: str
    columns: tuple[CsvColumn, ...]
    header: tuple[str, ...]
    q_absolute_tolerance: float


@dataclass(frozen=True)
class ProcessResult:
    """Lifecycle result for one child invocation."""

    return_code: int | None
    timed_out: bool
    interrupted: bool
    utc_start: str
    utc_end: str
    wall_duration_sec: float
    user_cpu_sec: float
    system_cpu_sec: float
    voluntary_context_switches: int
    involuntary_context_switches: int
    minor_faults: int
    major_faults: int


@dataclass(frozen=True)
class ValidationResult:
    """Strict validation result for one raw CSV file."""

    valid: bool
    issues: tuple[str, ...]
    rows: tuple[ParsedRow, ...]
    rows_after_warmup: tuple[ParsedRow, ...]


def utc_now() -> str:
    """Return an ISO-8601 UTC timestamp with an explicit Z suffix."""

    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    """Hash a file without loading it fully into memory."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json_object(path: Path) -> JsonObject:
    """Load a JSON object and reject non-object top-level values."""

    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BenchmarkError(f"cannot read JSON object {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise BenchmarkError(f"{path} must contain a JSON object")
    return value


def require_str(obj: JsonObject, key: str) -> str:
    """Read one required string field."""

    value = obj.get(key)
    if not isinstance(value, str) or not value:
        raise BenchmarkError(f"manifest field {key!r} must be a non-empty string")
    return value


def require_int(obj: JsonObject, key: str) -> int:
    """Read one required integer while rejecting JSON booleans."""

    value = obj.get(key)
    if type(value) is not int:
        raise BenchmarkError(f"manifest field {key!r} must be an integer")
    return value


def optional_int(obj: JsonObject, key: str) -> int | None:
    """Read an optional integer while rejecting JSON booleans and other types."""

    value = obj.get(key)
    if value is None:
        return None
    if type(value) is not int:
        raise BenchmarkError(f"manifest field {key!r} must be an integer when present")
    return value


def require_string_list(obj: JsonObject, key: str) -> tuple[str, ...]:
    """Read one required list of strings."""

    value = obj.get(key)
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise BenchmarkError(f"manifest field {key!r} must be a list of strings")
    return tuple(value)


def require_int_list(obj: JsonObject, key: str) -> tuple[int, ...]:
    """Read one required list of integers while rejecting booleans."""

    value = obj.get(key)
    if not isinstance(value, list) or not all(type(item) is int for item in value):
        raise BenchmarkError(f"manifest field {key!r} must be a list of integers")
    return tuple(value)


def require_range(name: str, value: int, minimum: int, maximum: int) -> None:
    """Reject an integer outside an inclusive safety range."""

    if value < minimum or value > maximum:
        raise BenchmarkError(
            f"{name}={value} is outside safe range {minimum}..{maximum}"
        )


def available_processor_count() -> int:
    """Return the CPUs available to this process, honoring Linux affinity."""

    try:
        return len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        return os.cpu_count() or 1


def load_manifest(path: Path, *, enforce_host: bool = True) -> Manifest:
    """Load bounded controls, optionally including the live host gate.

    Production experiment execution keeps ``enforce_host=True``. Contract
    tests can set it to ``False`` when they are validating only the manifest
    schema and must run on a small CI runner.
    """

    obj = read_json_object(path)
    manifest = Manifest(
        manifest_schema_id=require_str(obj, "manifest_schema_id"),
        experiment_id=require_str(obj, "experiment_id"),
        metrics_schema_id=require_str(obj, "metrics_schema_id"),
        expected_csv_column_count=optional_int(obj, "expected_csv_column_count"),
        maturity_class=require_str(obj, "maturity_class"),
        comparison_design=require_str(obj, "comparison_design"),
        modes=require_string_list(obj, "modes"),
        workers=require_int(obj, "workers"),
        rt_exempt=require_int(obj, "rt_exempt"),
        duration_sec=require_int(obj, "duration_sec"),
        interval_ms=require_int(obj, "interval_ms"),
        calibration_sec=require_int(obj, "calibration_sec"),
        repetitions=require_int(obj, "repetitions"),
        warmup_ticks=require_int(obj, "warmup_ticks"),
        seeds=require_int_list(obj, "seeds"),
        timeout_sec=require_int(obj, "timeout_sec"),
        timeout_grace_sec=require_int(obj, "timeout_grace_sec"),
        nice_increment=require_int(obj, "nice_increment"),
    )

    if manifest.manifest_schema_id != MANIFEST_ID:
        raise BenchmarkError(
            f"manifest_schema_id must be {MANIFEST_ID!r}, got {manifest.manifest_schema_id!r}"
        )
    if manifest.metrics_schema_id not in SUPPORTED_SCHEMA_IDS:
        raise BenchmarkError(
            "metrics_schema_id must be one of "
            f"{list(SUPPORTED_SCHEMA_IDS)!r}, got {manifest.metrics_schema_id!r}"
        )
    expected_columns = len(EXPECTED_HEADERS_BY_SCHEMA[manifest.metrics_schema_id])
    if manifest.metrics_schema_id != V2_SCHEMA_ID and manifest.expected_csv_column_count is None:
        raise BenchmarkError(
            "append-only manifests must declare expected_csv_column_count"
        )
    if (
        manifest.expected_csv_column_count is not None
        and manifest.expected_csv_column_count != expected_columns
    ):
        raise BenchmarkError(
            "expected_csv_column_count does not match the explicit schema contract: "
            f"expected {expected_columns}, got {manifest.expected_csv_column_count}"
        )
    expected_maturity = (
        (MATURITY_CLASS,)
        if manifest.metrics_schema_id == V2_SCHEMA_ID
        else (MATURITY_CLASS, EXPLORATORY_MATURITY_CLASS)
    )
    if manifest.maturity_class not in expected_maturity:
        raise BenchmarkError(
            f"maturity_class must be one of {list(expected_maturity)!r} for "
            f"{manifest.metrics_schema_id!r}"
        )
    if manifest.comparison_design != COMPARISON_DESIGN:
        raise BenchmarkError(f"comparison_design must be {COMPARISON_DESIGN!r}")
    if manifest.modes != MODES:
        raise BenchmarkError(f"modes must be exactly {list(MODES)!r} in that order")

    if enforce_host:
        nproc = available_processor_count()
        safe_worker_max = min(8, nproc)
        if safe_worker_max < 4:
            raise BenchmarkError(
                f"host exposes only {nproc} CPUs; the prototype requires at least 4 workers"
            )
        require_range("workers", manifest.workers, 4, safe_worker_max)
    else:
        require_range("workers", manifest.workers, 4, 8)
    if manifest.rt_exempt != 0:
        raise BenchmarkError(
            "rt_exempt must be 0; the bounded harness never attempts SCHED_FIFO"
        )
    require_range("duration_sec", manifest.duration_sec, 1, 30)
    require_range("interval_ms", manifest.interval_ms, 50, 2000)
    require_range("calibration_sec", manifest.calibration_sec, 1, 10)
    require_range("repetitions", manifest.repetitions, 2, 10)
    require_range("warmup_ticks", manifest.warmup_ticks, 0, 100_000)
    require_range("timeout_grace_sec", manifest.timeout_grace_sec, 1, 10)
    require_range("nice_increment", manifest.nice_increment, 1, 19)

    minimum_timeout = manifest.duration_sec + manifest.calibration_sec + 2
    maximum_timeout = manifest.duration_sec + manifest.calibration_sec + 30
    require_range("timeout_sec", manifest.timeout_sec, minimum_timeout, maximum_timeout)
    if len(manifest.seeds) != manifest.repetitions:
        raise BenchmarkError("seeds must contain exactly one seed per repetition")
    if len(set(manifest.seeds)) != len(manifest.seeds):
        raise BenchmarkError("seeds must be unique across repetitions")
    if any(seed < 1 or seed > 9_223_372_036_854_775_807 for seed in manifest.seeds):
        raise BenchmarkError("every seed must be in the inclusive range 1..2^63-1")
    return manifest


def optional_number(value: object, name: str) -> float | None:
    """Validate an optional finite schema bound."""

    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BenchmarkError(f"schema field {name!r} must be numeric or null")
    result = float(value)
    if not math.isfinite(result):
        raise BenchmarkError(f"schema field {name!r} must be finite")
    return result


def _parse_schema_columns(raw_columns: object) -> list[CsvColumn]:
    """Parse the compact column declarations used by a metrics schema."""

    if not isinstance(raw_columns, list):
        raise BenchmarkError("schema columns must be a list")

    columns: list[CsvColumn] = []
    for index, raw_column in enumerate(raw_columns):
        if not isinstance(raw_column, dict):
            raise BenchmarkError(f"schema column {index} must be an object")
        name = raw_column.get("name")
        kind = raw_column.get("type")
        if not isinstance(name, str) or kind not in {
            "integer",
            "number",
            "string",
            "boolean01",
        }:
            raise BenchmarkError(f"schema column {index} has invalid name or type")
        raw_enum = raw_column.get("enum", [])
        if not isinstance(raw_enum, list) or not all(
            isinstance(item, str) for item in raw_enum
        ):
            raise BenchmarkError(f"schema column {name!r} enum must be a string list")
        columns.append(
            CsvColumn(
                name=name,
                kind=kind,
                minimum=optional_number(raw_column.get("minimum"), f"{name}.minimum"),
                maximum=optional_number(raw_column.get("maximum"), f"{name}.maximum"),
                enum=tuple(raw_enum),
            )
        )
    return columns


def _load_append_only_columns(path: Path, expected_id: str, obj: JsonObject) -> list[CsvColumn]:
    """Resolve a local append-only schema extension without weakening its ABI.

    v6 and v7 intentionally keep their large historical prefix in the
    already-reviewed v5 schema.  The child file declares the exact local base
    filename and only adds new, typed columns.  This keeps the JSON contracts
    readable while the compiled-in header below remains the final order gate.
    """

    base_name = obj.get("base_schema")
    raw_append = obj.get("append_columns")
    if not isinstance(base_name, str) or not base_name:
        raise BenchmarkError("append-only schema requires a non-empty base_schema")
    if not isinstance(raw_append, list):
        raise BenchmarkError("append-only schema requires append_columns list")
    if expected_id == V6_SCHEMA_ID:
        base_id = V5_SCHEMA_ID
    elif expected_id == V7_SCHEMA_ID:
        base_id = V6_SCHEMA_ID
    else:
        raise BenchmarkError(f"append-only schema is not supported for {expected_id!r}")
    base_path = (path.parent / base_name).resolve()
    if base_path.parent != path.parent.resolve():
        raise BenchmarkError("append-only schema base must remain in its schema directory")
    if not base_path.is_file():
        raise BenchmarkError(f"append-only schema base is missing: {base_path}")
    base = load_metrics_schema(base_path, base_id)
    columns = [
        CsvColumn(
            name=column.name,
            kind=column.kind,
            minimum=column.minimum,
            maximum=column.maximum,
            enum=(expected_id,) if column.name == "metrics_schema" else column.enum,
        )
        for column in base.columns
    ]
    return columns + _parse_schema_columns(raw_append)


def load_metrics_schema(path: Path, expected_id: str) -> MetricsSchema:
    """Load one fixed schema and reject a mismatched or weakened contract."""

    obj = read_json_object(path)
    schema_id = obj.get("schema_id")
    if expected_id not in SUPPORTED_SCHEMA_IDS:
        raise BenchmarkError(f"unsupported expected schema_id {expected_id!r}")
    if schema_id != expected_id:
        raise BenchmarkError(
            f"schema_id mismatch: manifest requires {expected_id!r}, schema declares {schema_id!r}"
        )
    if "columns" in obj:
        columns = _parse_schema_columns(obj["columns"])
    else:
        columns = _load_append_only_columns(path, expected_id, obj)

    header = tuple(column.name for column in columns)
    expected_header = EXPECTED_HEADERS_BY_SCHEMA[expected_id]
    if header != expected_header:
        raise BenchmarkError(
            f"{expected_id} schema header differs from its compiled-in "
            f"{len(expected_header)}-column contract: expected {list(expected_header)!r}, "
            f"got {list(header)!r}"
        )
    tolerance = optional_number(obj.get("q_absolute_tolerance"), "q_absolute_tolerance")
    if tolerance is None or tolerance <= 0.0 or tolerance > 1e-6:
        raise BenchmarkError("q_absolute_tolerance must be in (0, 1e-6]")
    return MetricsSchema(
        schema_id=schema_id,
        columns=tuple(columns),
        header=header,
        q_absolute_tolerance=tolerance,
    )


def run_mean_columns_for_schema(schema_id: str) -> tuple[str, ...]:
    """Return the row-level metrics summarized for one explicit CSV version."""

    if schema_id == V2_SCHEMA_ID:
        return V2_RUN_MEAN_COLUMNS
    if schema_id == V3_SCHEMA_ID:
        return V3_RUN_MEAN_COLUMNS
    if schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID):
        return V4_RUN_MEAN_COLUMNS
    raise AssertionError(f"unsupported validated schema {schema_id!r}")


def aggregate_metrics_for_schema(schema_id: str) -> tuple[str, ...]:
    """Return invocation-level summary names for one explicit CSV version."""

    if schema_id == V2_SCHEMA_ID:
        return V2_AGGREGATE_METRICS
    if schema_id == V3_SCHEMA_ID:
        return V3_AGGREGATE_METRICS
    if schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID):
        return V4_AGGREGATE_METRICS
    raise AssertionError(f"unsupported validated schema {schema_id!r}")


def safe_read_text(path: Path, maximum_chars: int = 1_000_000) -> str | None:
    """Read a bounded diagnostic file, returning null when unavailable."""

    try:
        return path.read_text(encoding="utf-8", errors="replace")[:maximum_chars]
    except OSError:
        return None


def capture_command(command: list[str], timeout_sec: float = 5.0) -> JsonObject:
    """Capture an environmental diagnostic without making it fatal."""

    executable = shutil.which(command[0])
    if executable is None:
        return {
            "command": command,
            "available": False,
            "return_code": None,
            "stdout": "",
            "stderr": f"{command[0]} not found",
        }
    resolved = [executable, *command[1:]]
    try:
        completed = subprocess.run(
            resolved,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
            check=False,
        )
        return {
            "command": resolved,
            "available": True,
            "return_code": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {
            "command": resolved,
            "available": True,
            "return_code": None,
            "stdout": "",
            "stderr": str(exc),
        }


def capture_git(repository_root: Path) -> JsonObject:
    """Capture revision state or explicitly record that no Git worktree exists."""

    probe = capture_command(
        ["git", "-C", str(repository_root), "rev-parse", "--show-toplevel"]
    )
    if probe.get("return_code") != 0:
        return {
            "available": False,
            "status": "not-a-git-worktree",
            "commit": None,
            "working_tree_status": None,
            "probe": probe,
        }
    commit = capture_command(["git", "-C", str(repository_root), "rev-parse", "HEAD"])
    status = capture_command(
        ["git", "-C", str(repository_root), "status", "--porcelain=v1"]
    )
    return {
        "available": True,
        "status": "git-worktree",
        "commit": str(commit.get("stdout", "")).strip() or None,
        "working_tree_status": str(status.get("stdout", "")),
        "probe": probe,
    }


def capture_memory() -> JsonObject:
    """Capture host memory totals from procfs."""

    selected: JsonObject = {}
    text = safe_read_text(Path("/proc/meminfo"))
    if text is None:
        return {"available": False, "fields": selected}
    wanted = {"MemTotal", "MemAvailable", "SwapTotal", "SwapFree"}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        if key in wanted:
            selected[key] = value.strip()
    return {"available": True, "fields": selected}


def capture_governors() -> JsonObject:
    """Capture per-CPU frequency governors and common turbo controls."""

    governors: JsonObject = {}
    for path in sorted(
        Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor")
    ):
        governors[path.parent.parent.name] = safe_read_text(path, 256)
    turbo_paths = (
        Path("/sys/devices/system/cpu/intel_pstate/no_turbo"),
        Path("/sys/devices/system/cpu/cpufreq/boost"),
    )
    turbo: JsonObject = {str(path): safe_read_text(path, 256) for path in turbo_paths}
    return {"governors": governors, "turbo_controls": turbo}


def capture_thermal_state() -> JsonObject:
    """Capture available thermal-zone labels and temperatures."""

    zones: JsonObject = {}
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        zones[zone.name] = {
            "type": safe_read_text(zone / "type", 256),
            "temp_millidegrees": safe_read_text(zone / "temp", 256),
        }
    return {"available": bool(zones), "zones": zones}


def capture_kernel_config() -> JsonObject:
    """Hash an available kernel configuration without copying it into results."""

    candidates = (
        Path("/proc/config.gz"),
        Path(f"/boot/config-{platform.release()}"),
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.R_OK):
            try:
                return {
                    "available": True,
                    "path": str(candidate),
                    "sha256": sha256_file(candidate),
                    "size_bytes": candidate.stat().st_size,
                }
            except OSError:
                continue
    return {"available": False, "path": None, "sha256": None, "size_bytes": None}


def capture_privileges() -> JsonObject:
    """Capture identity and Linux capability masks without exposing secrets."""

    status_fields: JsonObject = {}
    status = safe_read_text(Path("/proc/self/status"))
    if status is not None:
        wanted = {"CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb", "NoNewPrivs"}
        for line in status.splitlines():
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            if key in wanted:
                status_fields[key] = value.strip()
    uid = os.getuid()
    try:
        user = pwd.getpwuid(uid).pw_name
    except KeyError:
        user = None
    return {
        "uid": uid,
        "effective_uid": os.geteuid(),
        "gid": os.getgid(),
        "effective_gid": os.getegid(),
        "supplementary_groups": os.getgroups(),
        "user": user,
        "is_root": os.geteuid() == 0,
        "proc_status": status_fields,
    }


def capture_background_state() -> JsonObject:
    """Capture lightweight host-load context around an invocation."""

    try:
        load_average: object = list(os.getloadavg())
    except OSError:
        load_average = None
    return {
        "utc": utc_now(),
        "load_average_1m_5m_15m": load_average,
        "proc_loadavg": safe_read_text(Path("/proc/loadavg"), 1024),
        "proc_uptime": safe_read_text(Path("/proc/uptime"), 1024),
        "process_snapshot": capture_command(
            ["ps", "-eo", "pid,ppid,ni,stat,comm", "--sort=pid"], timeout_sec=3.0
        ),
    }


def capture_environment(repository_root: Path) -> JsonObject:
    """Capture the declared environment and provenance envelope."""

    uname = platform.uname()
    return {
        "captured_utc": utc_now(),
        "python": {
            "version": platform.python_version(),
            "implementation": platform.python_implementation(),
            "executable": sys.executable,
        },
        "platform_uname": {
            "system": uname.system,
            "node": uname.node,
            "release": uname.release,
            "version": uname.version,
            "machine": uname.machine,
            "processor": uname.processor,
        },
        "uname_command": capture_command(["uname", "-a"]),
        "lscpu": capture_command(["lscpu"]),
        "memory": capture_memory(),
        "frequency": capture_governors(),
        "thermal_environment": capture_thermal_state(),
        "virtualization": capture_command(["systemd-detect-virt"]),
        "container_cgroup": safe_read_text(Path("/proc/1/cgroup")),
        "privileges": capture_privileges(),
        "kernel_command_line": safe_read_text(Path("/proc/cmdline")),
        "kernel_config": capture_kernel_config(),
        "configured_processors": os.cpu_count(),
        "available_processors": available_processor_count(),
        "git": capture_git(repository_root),
        "background_initial": capture_background_state(),
    }


def write_json(path: Path, value: object) -> None:
    """Write deterministic, finite JSON."""

    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def parse_csv_value(text: str, column: CsvColumn, row_number: int) -> Scalar:
    """Parse and range-check one strict CSV cell."""

    location = f"row {row_number} column {column.name}"
    if text == "":
        raise ValueError(f"{location} is empty")
    if column.kind in {"integer", "boolean01"}:
        integer_pattern = r"[0-9]+" if column.kind == "boolean01" else r"-?[0-9]+"
        if re.fullmatch(integer_pattern, text) is None:
            expected = "unsigned" if column.kind == "boolean01" else "signed"
            raise ValueError(f"{location} must be a {expected} decimal integer")
        value: Scalar = int(text, 10)
        if column.kind == "boolean01" and value not in {0, 1}:
            raise ValueError(f"{location} must be 0 or 1")
    elif column.kind == "number":
        numeric = float(text)
        if not math.isfinite(numeric):
            raise ValueError(f"{location} must be finite")
        value = numeric
    else:
        value = text
        if column.enum and text not in column.enum:
            raise ValueError(f"{location} must be one of {list(column.enum)!r}")

    if isinstance(value, (int, float)):
        if column.minimum is not None and value < column.minimum:
            raise ValueError(f"{location} is below {column.minimum}")
        if column.maximum is not None and value > column.maximum:
            raise ValueError(f"{location} is above {column.maximum}")
    return value


def float_value(row: ParsedRow, name: str) -> float:
    """Convert a validated numeric field to float."""

    value = row[name]
    if not isinstance(value, (int, float)):
        raise AssertionError(f"validated field {name} is not numeric")
    return float(value)


def int_value(row: ParsedRow, name: str) -> int:
    """Return a validated integer field."""

    value = row[name]
    if type(value) is not int:
        raise AssertionError(f"validated field {name} is not an integer")
    return value


def expected_controller_reasons(row: ParsedRow, tolerance: float) -> set[str]:
    """Derive acceptable diagnoses, allowing only CSV threshold-rounding ambiguity."""

    def possible_deficit(value: float) -> set[bool]:
        if math.isclose(value, CONTROLLER_THRESHOLD, rel_tol=0.0, abs_tol=tolerance):
            return {False, True}
        return {value < CONTROLLER_THRESHOLD}

    reasons: set[str] = set()
    for low_s3 in possible_deficit(float_value(row, "S3")):
        for low_s4 in possible_deficit(float_value(row, "S4")):
            if low_s3 and low_s4:
                reasons.add("S3+S4")
            elif low_s3:
                reasons.add("S3")
            elif low_s4:
                reasons.add("S4")
            else:
                reasons.add("NONE")
    return reasons


def success_fraction(successes: int, attempts: int) -> float:
    """Return the explicit v3-v7 zero-attempt convention for action outcomes."""

    # Counter ordering is reported as a validation issue by the caller.  Keep
    # this arithmetic total so malformed raw data cannot turn validation into
    # an assertion failure.
    return successes / attempts if attempts else 1.0


def validate_effective_action_semantics(
    row: ParsedRow,
    row_number: int,
    eligible: int,
    fallback: int,
    tolerance: float,
    schema_id: str,
) -> list[str]:
    """Validate append-only v3-v7 observable action-outcome aggregates.

    These checks intentionally validate only bounded userspace observations.
    They do not infer Linux dispatch state or kernel scheduler compliance.
    """

    issues: list[str] = []
    prefix = f"row {row_number}"

    if schema_id not in {
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        raise AssertionError(f"unsupported effective-action schema {schema_id!r}")
    if str(row["metrics_schema"]) != schema_id:
        issues.append(f"{prefix}: metrics_schema is not {schema_id!r}")
    if not math.isclose(
        float_value(row, "S2_selected"),
        float_value(row, "S2"),
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: S2_selected must be the explicit alias of historical S2"
        )
    canonical_s3 = "S3_conditioned" if schema_id == V7_SCHEMA_ID else "S3_global"
    if not math.isclose(
        float_value(row, canonical_s3),
        float_value(row, "S3"),
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: {canonical_s3} must be the explicit alias of historical S3"
        )

    attempted = int_value(row, "action_attempt_count")
    successful = int_value(row, "effective_action_success_count")
    errors = int_value(row, "action_error_count")
    if attempted > eligible:
        issues.append(f"{prefix}: action_attempt_count exceeds eligible_workers")
    if successful + errors > attempted:
        issues.append(
            f"{prefix}: effective successes plus action errors exceed action attempts"
        )
    expected_effective = successful / eligible if eligible else 1.0
    if not math.isclose(
        float_value(row, "S2_effective"),
        expected_effective,
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: S2_effective={float_value(row, 'S2_effective'):.10g}, "
            f"observable effectiveness={expected_effective:.10g}"
        )

    migration_attempts = int_value(row, "migration_attempt_count")
    migration_valid_cpu = int_value(row, "migration_valid_requested_cpu_count")
    migration_affinity_success = int_value(row, "migration_affinity_success_count")
    migration_observed_success = int_value(row, "migration_observed_success_count")
    sleep_attempts = int_value(row, "sleep_attempt_count")
    sleep_success = int_value(row, "sleep_effective_success_count")
    yield_attempts = int_value(row, "yield_attempt_count")
    yield_success = int_value(row, "yield_call_success_count")
    throttle_attempts = int_value(row, "throttle_attempt_count")
    throttle_success = int_value(row, "throttle_operation_success_count")
    specific_attempts = (
        migration_attempts + sleep_attempts + yield_attempts + throttle_attempts
    )
    if specific_attempts > attempted:
        issues.append(f"{prefix}: action-specific attempts exceed action_attempt_count")
    if migration_attempts > int_value(row, "migrate"):
        issues.append(f"{prefix}: migration attempts exceed selected MIGRATE actions")
    if sleep_attempts > int_value(row, "sleep"):
        issues.append(f"{prefix}: sleep attempts exceed selected SLEEP actions")
    if yield_attempts > int_value(row, "yield"):
        issues.append(f"{prefix}: yield attempts exceed selected YIELD actions")
    if throttle_attempts > int_value(row, "throttle"):
        issues.append(f"{prefix}: throttle attempts exceed selected THROTTLE actions")
    if not (
        migration_observed_success
        <= migration_affinity_success
        <= migration_valid_cpu
        <= migration_attempts
    ):
        issues.append(
            f"{prefix}: migration counters must satisfy observed <= affinity <= valid CPU <= attempts"
        )
    for label, successes, attempts in (
        ("sleep", sleep_success, sleep_attempts),
        ("yield", yield_success, yield_attempts),
        ("throttle", throttle_success, throttle_attempts),
    ):
        if successes > attempts:
            issues.append(f"{prefix}: {label} successes exceed attempts")

    expected_fractions = (
        (
            "migration_observed_success_fraction",
            success_fraction(migration_observed_success, migration_attempts),
        ),
        (
            "sleep_effectiveness_fraction",
            success_fraction(sleep_success, sleep_attempts),
        ),
        (
            "yield_call_success_fraction",
            success_fraction(yield_success, yield_attempts),
        ),
        (
            "throttle_operation_success_fraction",
            success_fraction(throttle_success, throttle_attempts),
        ),
    )
    for name, expected in expected_fractions:
        if not math.isclose(
            float_value(row, name), expected, rel_tol=0.0, abs_tol=tolerance
        ):
            issues.append(
                f"{prefix}: {name}={float_value(row, name):.10g}, expected {expected:.10g}"
            )

    requested_sleep_ns = int_value(row, "requested_sleep_ns_total")
    observed_sleep_ns = int_value(row, "observed_sleep_ns_total")
    if sleep_attempts == 0 and (requested_sleep_ns != 0 or observed_sleep_ns != 0):
        issues.append(f"{prefix}: sleep totals require a recorded sleep attempt")

    expected_fallback_fraction = fallback / eligible if eligible else 0.0
    if not math.isclose(
        float_value(row, "fallback_fraction"),
        expected_fallback_fraction,
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: fallback_fraction={float_value(row, 'fallback_fraction'):.10g}, "
            f"expected {expected_fallback_fraction:.10g}"
        )
    fallback_reason = str(row["fallback_reason"])
    if fallback == 0 and fallback_reason != "NONE":
        issues.append(
            f"{prefix}: fallback_reason must be NONE without fallback workers"
        )
    if fallback > 0 and fallback_reason == "NONE":
        issues.append(f"{prefix}: fallback workers require a non-NONE fallback_reason")
    return issues


def validate_v4_burst_semantics(
    row: ParsedRow,
    row_number: int,
    eligible: int,
    tolerance: float,
) -> list[str]:
    """Validate the bounded, experimental v4-v7 burst-metric aggregates.

    The runner retains historical S4 and Q validation elsewhere.  This check
    only verifies the append-only burst diagnostics and deliberately does not
    infer kernel dispatch behavior from userspace observations.
    """

    issues: list[str] = []
    prefix = f"row {row_number}"
    if str(row["metrics_schema"]) not in {
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        issues.append(f"{prefix}: metrics_schema is not an accepted burst schema")

    changed = int_value(row, "changed_eligible_workers")
    justified = int_value(row, "justified_changed_workers")
    dominant_count = int_value(row, "dominant_transition_count")
    if changed > eligible:
        issues.append(f"{prefix}: changed_eligible_workers exceeds eligible_workers")
    if justified > changed:
        issues.append(f"{prefix}: justified_changed_workers exceeds changed workers")
    if dominant_count > changed:
        issues.append(f"{prefix}: dominant_transition_count exceeds changed workers")

    expected_change_fraction = changed / eligible if eligible else 0.0
    expected_dominant_fraction = dominant_count / changed if changed else 0.0
    expected_justified_fraction = justified / changed if changed else 0.0
    fractions = (
        ("change_fraction", expected_change_fraction),
        ("dominant_transition_fraction", expected_dominant_fraction),
        ("justified_change_fraction", expected_justified_fraction),
    )
    for name, expected in fractions:
        if not math.isclose(
            float_value(row, name), expected, rel_tol=0.0, abs_tol=tolerance
        ):
            issues.append(
                f"{prefix}: {name}={float_value(row, name):.10g}, expected {expected:.10g}"
            )

    old_action = int_value(row, "dominant_old_action")
    new_action = int_value(row, "dominant_new_action")
    if changed == 0:
        if dominant_count != 0:
            issues.append(f"{prefix}: no changes require dominant_transition_count=0")
        if old_action != -1 or new_action != -1:
            issues.append(f"{prefix}: no changes require -1 dominant action sentinels")
    else:
        if dominant_count == 0:
            issues.append(f"{prefix}: changed workers require a dominant transition")
        if old_action not in range(len(ACTION_COLUMNS)) or new_action not in range(
            len(ACTION_COLUMNS)
        ):
            issues.append(
                f"{prefix}: changed workers require valid dominant action IDs"
            )
        elif old_action == new_action:
            issues.append(f"{prefix}: dominant transition must change action")
        elif dominant_count > int_value(row, ACTION_COLUMNS[new_action]):
            issues.append(
                f"{prefix}: dominant transition exceeds selected new-action count"
            )

    current_valid = int_value(row, "current_directive_valid")
    previous_valid = int_value(row, "previous_directive_valid")
    directive_transition_valid = int_value(row, "directive_transition_valid")
    if directive_transition_valid and not (current_valid and previous_valid):
        issues.append(
            f"{prefix}: directive_transition_valid requires both directives to be valid"
        )
    if not directive_transition_valid and justified != 0:
        issues.append(
            f"{prefix}: directive-justified changes require a valid directive transition"
        )
    if current_valid and eligible == 0:
        issues.append(f"{prefix}: zero eligible workers cannot accept a directive")

    rolling_bursts = int_value(row, "rolling_window_burst_count")
    rolling_oscillations = int_value(row, "rolling_window_oscillation_count")
    if rolling_bursts > 5:
        issues.append(f"{prefix}: rolling_window_burst_count exceeds bounded window")
    if rolling_oscillations > 4:
        issues.append(
            f"{prefix}: rolling_window_oscillation_count exceeds bounded window"
        )
    if rolling_oscillations > rolling_bursts:
        issues.append(f"{prefix}: rolling oscillations exceed rolling bursts")
    expected_oscillation_penalty = min(0.5, 0.15 * rolling_oscillations)
    if not math.isclose(
        float_value(row, "oscillation_penalty"),
        expected_oscillation_penalty,
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: oscillation_penalty={float_value(row, 'oscillation_penalty'):.10g}, "
            f"expected {expected_oscillation_penalty:.10g}"
        )

    large = int_value(row, "large_burst_event")
    repeated = int_value(row, "repeated_oscillation_event")
    expected_large = int(
        changed > 1
        and expected_change_fraction >= 0.5 - tolerance
        and expected_dominant_fraction >= 0.75 - tolerance
    )
    if large != expected_large:
        issues.append(f"{prefix}: large_burst_event={large}, expected {expected_large}")
    expected_repeated = int(bool(large) and rolling_oscillations > 0)
    if repeated != expected_repeated:
        issues.append(
            f"{prefix}: repeated_oscillation_event={repeated}, expected {expected_repeated}"
        )
    if not large and rolling_oscillations != 0:
        issues.append(f"{prefix}: non-burst row cannot report rolling oscillations")

    # The population-scale term gives a single isolated change no burst
    # penalty, while retaining a full scale for a population-wide switch.
    population_scale = (
        (changed - 1) / (eligible - 1) if eligible > 1 and changed > 1 else 0.0
    )
    burst_penalty = (
        0.8
        * expected_change_fraction
        * population_scale
        * expected_dominant_fraction
        * (0.20 + 0.80 * (1.0 - expected_justified_fraction))
    )
    expected_s4_burst = min(
        1.0,
        max(0.0, 1.0 - burst_penalty - expected_oscillation_penalty),
    )
    if not math.isclose(
        float_value(row, "S4_burst"),
        expected_s4_burst,
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: S4_burst={float_value(row, 'S4_burst'):.10g}, "
            f"expected {expected_s4_burst:.10g}"
        )
    expected_s4 = 1.0 - expected_change_fraction
    if str(row["metrics_schema"]) == V7_SCHEMA_ID:
        expected_s4 = min(expected_s4, float_value(row, "S4_burst"))
    if not math.isclose(
        float_value(row, "S4"), expected_s4, rel_tol=0.0, abs_tol=tolerance
    ):
        issues.append(
            f"{prefix}: historical S4={float_value(row, 'S4'):.10g}, "
            f"expected {expected_s4:.10g}"
        )
    return issues


def validate_policy_lifecycle_semantics(
    row: ParsedRow, row_number: int, schema_id: str
) -> list[str]:
    """Validate the append-only v6 policy lifecycle fields."""

    if schema_id not in {V6_SCHEMA_ID, V7_SCHEMA_ID}:
        return []
    issues: list[str] = []
    prefix = f"row {row_number}"
    policy_mode = str(row["policy_mode"])
    if policy_mode not in POLICY_MODES:
        issues.append(f"{prefix}: invalid policy_mode {policy_mode!r}")
    if int_value(row, "policy_schema_version") != 1:
        issues.append(f"{prefix}: policy_schema_version must be 1")
    if int_value(row, "policy_format_version") != 1:
        issues.append(f"{prefix}: policy_format_version must be 1")
    for field in (
        "policy_update_allowed",
        "policy_update_applied",
        "policy_exploration_enabled",
    ):
        if int_value(row, field) not in {0, 1}:
            issues.append(f"{prefix}: {field} must be boolean01")
    if int_value(row, "policy_update_applied") and not int_value(
        row, "policy_update_allowed"
    ):
        issues.append(f"{prefix}: policy update cannot be applied when disallowed")
    expected_exploration = int(policy_mode == "TRAIN")
    if int_value(row, "policy_exploration_enabled") != expected_exploration:
        issues.append(
            f"{prefix}: policy_exploration_enabled does not match {policy_mode} mode"
        )
    if policy_mode == "EVALUATE" and int_value(row, "policy_update_allowed"):
        issues.append("{}: EVALUATE mode cannot allow policy updates".format(prefix))
    if str(row["policy_update_suppression_reason"]) not in POLICY_SUPPRESSION_REASONS:
        issues.append(f"{prefix}: invalid policy suppression reason")
    if str(row["policy_load_status"]) not in POLICY_LOAD_STATUSES:
        issues.append(f"{prefix}: invalid policy load status")
    if str(row["policy_save_status"]) not in POLICY_SAVE_STATUSES:
        issues.append(f"{prefix}: invalid policy save status")
    if schema_id == V7_SCHEMA_ID and int_value(
        row, "coordination_semantics_version"
    ) != 7:
        issues.append(f"{prefix}: coordination_semantics_version must be 7")
    return issues


def validate_v4_burst_chronology(rows: list[ParsedRow]) -> list[str]:
    """Validate the fixed sequence-distance history behind v4 diagnostics.

    This mirrors the bounded parent-side tracker for reproducible offline
    validation.  It is deliberately analysis-side code; the C metric hot path
    remains fixed-storage and allocation-free.
    """

    issues: list[str] = []
    last_valid_directive: str | None = None
    accepted_large_bursts: list[tuple[int, int, int]] = []
    previous_rejections = 0
    for row in rows:
        tick = int_value(row, "tick")
        prefix = f"tick {tick}"
        current_valid = int_value(row, "current_directive_valid")
        previous_valid = int_value(row, "previous_directive_valid")
        transition_valid = int_value(row, "directive_transition_valid")
        expected_previous_valid = int(last_valid_directive is not None)
        if previous_valid != expected_previous_valid:
            issues.append(
                f"{prefix}: previous directive validity does not match accepted-frame tracker"
            )
        expected_transition_valid = int(
            bool(
                current_valid
                and previous_valid
                and last_valid_directive is not None
                and str(row["directive"]) != last_valid_directive
            )
        )
        if transition_valid != expected_transition_valid:
            issues.append(f"{prefix}: directive transition validity mismatch")

        rejected = int_value(row, "rejected_frames")
        justified = int_value(row, "justified_changed_workers")
        if rejected > previous_rejections and (
            current_valid or transition_valid or justified
        ):
            issues.append(
                f"{prefix}: rejected frame cannot be valid or justify an action transition"
            )

        large = int_value(row, "large_burst_event")
        old_action = int_value(row, "dominant_old_action")
        new_action = int_value(row, "dominant_new_action")
        prior_large_bursts = [
            entry for entry in accepted_large_bursts if tick - entry[0] <= 4
        ]
        # Rolling diagnostics are event-local: a non-large current row emits
        # zero rather than carrying prior-window counts into a quiet row.
        expected_rolling_bursts = len(prior_large_bursts) + 1 if large else 0
        if int_value(row, "rolling_window_burst_count") != expected_rolling_bursts:
            issues.append(f"{prefix}: rolling burst diagnostic mismatch")
        reverse_count = 0
        if large:
            reverse_count = sum(
                int(prior_old == new_action and prior_new == old_action)
                for _, prior_old, prior_new in prior_large_bursts
            )
        if int_value(row, "rolling_window_oscillation_count") != reverse_count:
            issues.append(f"{prefix}: rolling oscillation diagnostic mismatch")

        if current_valid:
            if large:
                accepted_large_bursts.append((tick, old_action, new_action))
            # Only the four immediately preceding sequence values can affect
            # a subsequent row, so bounded retention is sufficient here too.
            accepted_large_bursts = [
                entry for entry in accepted_large_bursts if tick - entry[0] <= 4
            ]
            last_valid_directive = str(row["directive"])
        previous_rejections = rejected
    return issues


def validate_row_semantics(
    row: ParsedRow,
    row_number: int,
    expected_mode: str,
    expected_eligible: int,
    tolerance: float,
    schema_id: str = V2_SCHEMA_ID,
) -> list[str]:
    """Validate shared invariants and the explicit append-only extensions."""

    issues: list[str] = []
    prefix = f"row {row_number}"
    if row["mode"] != expected_mode:
        issues.append(
            f"{prefix}: mode {row['mode']!r} does not match invocation {expected_mode!r}"
        )

    eligible = int_value(row, "eligible_workers")
    if eligible != expected_eligible:
        issues.append(
            f"{prefix}: eligible_workers={eligible}, expected {expected_eligible}"
        )
    action_sum = sum(int_value(row, name) for name in ACTION_COLUMNS)
    if action_sum != eligible:
        issues.append(
            f"{prefix}: action counts sum to {action_sum}, eligible_workers={eligible}"
        )
    fallback = int_value(row, "fallback_workers")
    if fallback > eligible:
        issues.append(
            f"{prefix}: fallback_workers={fallback} exceeds eligible_workers={eligible}"
        )

    directive = str(row["directive"])
    action_column = directive.lower()
    compliant = int_value(row, action_column)
    expected_s2 = (
        float_value(row, "S2_selected")
        if schema_id == V7_SCHEMA_ID
        else (compliant / eligible if eligible else 1.0)
    )
    if not math.isclose(
        float_value(row, "S2"), expected_s2, rel_tol=0.0, abs_tol=tolerance
    ):
        issues.append(
            f"{prefix}: S2={float_value(row, 'S2'):.10g}, exact compliance={expected_s2:.10g}"
        )

    if schema_id == V7_SCHEMA_ID:
        expected_s3 = float_value(row, "S3_conditioned")
    elif eligible <= 1:
        expected_s3 = 1.0
    else:
        entropy = 0.0
        for name in ACTION_COLUMNS:
            count = int_value(row, name)
            if count:
                probability = count / eligible
                entropy -= probability * math.log(probability)
        expected_s3 = 1.0 - entropy / math.log(len(ACTION_COLUMNS))
    if not math.isclose(
        float_value(row, "S3"), expected_s3, rel_tol=0.0, abs_tol=tolerance
    ):
        issues.append(
            f"{prefix}: S3={float_value(row, 'S3'):.10g}, "
            f"action-entropy coherence={expected_s3:.10g}"
        )

    factors = [float_value(row, name) for name in ("S1", "S2", "S3", "S4")]
    expected_q = math.prod(factors) ** 0.25
    reported_q = float_value(row, "Q")
    if expected_q == 0.0:
        if reported_q != 0.0:
            issues.append(
                f"{prefix}: zero coordination factor requires exact Q=0, got {reported_q}"
            )
    elif not math.isclose(reported_q, expected_q, rel_tol=0.0, abs_tol=tolerance):
        issues.append(
            f"{prefix}: Q={reported_q:.10g}, geometric mean={expected_q:.10g}"
        )

    prediction_used = int_value(row, "prediction_used")
    expected_decision = (
        float_value(row, "cpu_pred") if prediction_used else float_value(row, "cpu_now")
    )
    if not math.isclose(
        float_value(row, "decision_cpu"),
        expected_decision,
        rel_tol=0.0,
        abs_tol=tolerance,
    ):
        issues.append(
            f"{prefix}: decision_cpu does not match prediction_used={prediction_used}"
        )
    if expected_mode == "baseline" and prediction_used != 0:
        issues.append(
            f"{prefix}: reactive baseline must use observed CPU, not prediction"
        )

    tick = int_value(row, "tick")
    updated = int_value(row, "controller_updated")
    expected_updated = int(
        expected_mode == "orchestra" and tick % CONTROLLER_PERIOD_TICKS == 0
    )
    if updated != expected_updated:
        issues.append(
            f"{prefix}: controller_updated={updated}, expected {expected_updated} at tick {tick}"
        )
    expected_step = (
        tick // CONTROLLER_PERIOD_TICKS if expected_mode == "orchestra" else 0
    )
    if int_value(row, "controller_step") != expected_step:
        issues.append(
            f"{prefix}: controller_step={int_value(row, 'controller_step')}, expected {expected_step}"
        )
    reason = str(row["controller_reason"])
    expected_reasons = (
        expected_controller_reasons(row, tolerance) if updated else {"NONE"}
    )
    if reason not in expected_reasons:
        issues.append(
            f"{prefix}: controller_reason={reason!r}, expected one of {sorted(expected_reasons)!r}"
        )
    beta = float_value(row, "controller_beta")
    expected_beta = (
        CONTROLLER_BETA0 / math.sqrt(1.0 + expected_step) if updated else 0.0
    )
    if not math.isclose(beta, expected_beta, rel_tol=0.0, abs_tol=tolerance):
        issues.append(
            f"{prefix}: controller_beta={beta:.10g}, expected {expected_beta:.10g}"
        )

    if not updated:
        for current_name, next_name in zip(
            PARAMETER_COLUMNS, NEXT_PARAMETER_COLUMNS, strict=True
        ):
            if not math.isclose(
                float_value(row, current_name),
                float_value(row, next_name),
                rel_tol=0.0,
                abs_tol=tolerance,
            ):
                issues.append(
                    f"{prefix}: {next_name} changed without a controller update"
                )
    if expected_mode == "baseline":
        if int_value(row, "consensus_applied") != 0:
            issues.append(f"{prefix}: baseline cannot apply Q-table consensus")
        for flag in ("jitter_saturated", "switch_saturated", "consensus_saturated"):
            if int_value(row, flag) != 0:
                issues.append(f"{prefix}: baseline unexpectedly reports {flag}=1")
    if schema_id in {
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        issues.extend(
            validate_effective_action_semantics(
                row,
                row_number,
                eligible,
                fallback,
                tolerance,
                schema_id,
            )
        )
        if schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID):
            issues.extend(
                validate_v4_burst_semantics(row, row_number, eligible, tolerance)
            )
    if schema_id in (V6_SCHEMA_ID, V7_SCHEMA_ID):
        issues.extend(validate_policy_lifecycle_semantics(row, row_number, schema_id))
    if schema_id not in {
        V2_SCHEMA_ID,
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        raise AssertionError(f"unsupported validated schema {schema_id!r}")
    return issues


def validate_csv(
    path: Path,
    schema: MetricsSchema,
    expected_mode: str,
    expected_eligible: int,
    warmup_ticks: int,
    expected_rows: int,
) -> ValidationResult:
    """Validate syntax, schema, metric invariants, and controller chronology."""

    issues: list[str] = []
    rows: list[ParsedRow] = []
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        return ValidationResult(False, (f"cannot open raw CSV: {exc}",), (), ())

    with stream:
        reader = csv.DictReader(stream)
        actual_header = tuple(reader.fieldnames or ())
        if actual_header != schema.header:
            return ValidationResult(
                False,
                (
                    f"CSV header does not match strict {schema.schema_id} schema: "
                    f"expected {list(schema.header)!r}, got {list(actual_header)!r}",
                ),
                (),
                (),
            )
        for csv_row_number, raw_row in enumerate(reader, start=2):
            if None in raw_row or any(value is None for value in raw_row.values()):
                issues.append(f"row {csv_row_number}: malformed field count")
                continue
            parsed: ParsedRow = {}
            row_failed = False
            for column in schema.columns:
                try:
                    parsed[column.name] = parse_csv_value(
                        str(raw_row[column.name]), column, csv_row_number
                    )
                except (KeyError, ValueError) as exc:
                    issues.append(str(exc))
                    row_failed = True
            if row_failed:
                continue
            issues.extend(
                validate_row_semantics(
                    parsed,
                    csv_row_number,
                    expected_mode,
                    expected_eligible,
                    schema.q_absolute_tolerance,
                    schema.schema_id,
                )
            )
            rows.append(parsed)

    if schema.schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID) and rows:
        issues.extend(validate_v4_burst_chronology(rows))
    if not rows:
        issues.append("raw CSV contains no parseable data rows")
    if len(rows) != expected_rows:
        issues.append(
            f"raw CSV contains {len(rows)} parsed rows, expected exactly {expected_rows} "
            "for the declared duration and interval"
        )
    previous_tick = 0
    previous_rejections = 0
    previous_missed = 0
    previous_next: tuple[float, float, float] | None = None
    jitter_floor = float_value(rows[0], "jitter_sigma") if rows else 0.0
    for data_index, row in enumerate(rows, start=1):
        tick = int_value(row, "tick")
        if tick != data_index:
            issues.append(
                f"tick sequence is not contiguous at row {data_index}: got {tick}"
            )
        if tick <= previous_tick:
            issues.append(f"tick is not strictly increasing at tick {tick}")
        previous_tick = tick

        rejections = int_value(row, "rejected_frames")
        missed = int_value(row, "missed_deadlines")
        if rejections < previous_rejections:
            issues.append(f"rejected_frames decreases at tick {tick}")
        if missed < previous_missed:
            issues.append(f"missed_deadlines decreases at tick {tick}")
        previous_rejections = rejections
        previous_missed = missed

        current = tuple(float_value(row, name) for name in PARAMETER_COLUMNS)
        if previous_next is not None:
            for parameter_name, actual, expected in zip(
                PARAMETER_COLUMNS, current, previous_next, strict=True
            ):
                if not math.isclose(
                    actual,
                    expected,
                    rel_tol=0.0,
                    abs_tol=schema.q_absolute_tolerance,
                ):
                    issues.append(
                        f"tick {tick}: applied {parameter_name}={actual:.10g}, "
                        f"previous row declared next value {expected:.10g}"
                    )
        previous_next = tuple(float_value(row, name) for name in NEXT_PARAMETER_COLUMNS)

        if int_value(row, "controller_updated"):
            reason = str(row["controller_reason"])
            beta = float_value(row, "controller_beta")
            current_jitter, current_switch, current_consensus = current
            jitter_can_increase = "S4" in reason and (
                schema.schema_id != V7_SCHEMA_ID
                or float_value(row, "S2") >= CONTROLLER_THRESHOLD
            )
            raw_jitter = current_jitter + (
                0.20 * beta if jitter_can_increase else -0.05 * beta
            )
            raw_switch = current_switch + (
                0.15 * beta if reason != "NONE" else -0.04 * beta
            )
            raw_consensus = current_consensus + (
                0.10 * beta if "S3" in reason else -0.03 * beta
            )
            expected_next = (
                min(0.20, max(jitter_floor, raw_jitter)),
                min(0.30, max(0.0, raw_switch)),
                min(0.15, max(0.0, raw_consensus)),
            )
            for parameter_name, actual, expected in zip(
                NEXT_PARAMETER_COLUMNS, previous_next, expected_next, strict=True
            ):
                if not math.isclose(
                    actual,
                    expected,
                    rel_tol=0.0,
                    abs_tol=schema.q_absolute_tolerance,
                ):
                    issues.append(
                        f"tick {tick}: {parameter_name}={actual:.10g}, "
                        f"causal controller mapping requires {expected:.10g}"
                    )

            saturation_cases = (
                ("jitter_saturated", raw_jitter, jitter_floor, 0.20),
                ("switch_saturated", raw_switch, 0.0, 0.30),
                ("consensus_saturated", raw_consensus, 0.0, 0.15),
            )
            for flag_name, raw_value, lower, upper in saturation_cases:
                reported = int_value(row, flag_name)
                if (
                    raw_value < lower - schema.q_absolute_tolerance
                    or raw_value > upper + schema.q_absolute_tolerance
                ):
                    expected_flag: int | None = 1
                elif (
                    lower + schema.q_absolute_tolerance
                    < raw_value
                    < upper - schema.q_absolute_tolerance
                ):
                    expected_flag = 0
                else:
                    expected_flag = None
                if expected_flag is not None and reported != expected_flag:
                    issues.append(
                        f"tick {tick}: {flag_name}={reported}, expected {expected_flag} "
                        f"for raw request {raw_value:.10g}"
                    )

            if int_value(row, "consensus_applied") and expected_next[2] <= 0.0:
                issues.append(
                    f"tick {tick}: consensus_applied=1 with non-positive next blend"
                )

    rows_after_warmup = tuple(
        row for row in rows if int_value(row, "tick") > warmup_ticks
    )
    if rows and not rows_after_warmup:
        issues.append(
            f"warm-up exclusion of {warmup_ticks} ticks leaves no observations for analysis"
        )
    return ValidationResult(not issues, tuple(issues), tuple(rows), rows_after_warmup)


def expected_row_count(manifest: Manifest) -> int:
    """Calculate the documented bounded CSV row count for one invocation."""

    interval_ns = manifest.interval_ms * 1_000_000
    observed_duration_ns = max(
        0,
        manifest.duration_sec * 1_000_000_000 - interval_ns // 2,
    )
    return (observed_duration_ns + interval_ns - 1) // interval_ns


def run_integration_validator(
    repository_root: Path,
    csv_path: Path,
    mode: str,
    expected_rows: int,
    schema_id: str,
) -> JsonObject:
    """Run the repository's independent v3-v7 CSV validator with a timeout.

    v2 remains readable through the runner's embedded historical contract.
    v3-v7 invocations additionally execute the independent validator so their
    inline schema markers and version-specific aggregate invariants are checked
    before a row contributes to an invocation-level summary.
    """

    if schema_id == V2_SCHEMA_ID:
        return {
            "required": False,
            "executed": False,
            "valid": True,
            "reason": "v2_historical_contract_uses_embedded_validator",
            "command_argv": [],
            "stdout": "",
            "stderr": "",
            "return_code": None,
        }
    if schema_id not in {
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        raise AssertionError(f"unsupported validated schema {schema_id!r}")

    validator = repository_root / "tests/integration/validate_paper_cpu_csv.py"
    command = [
        sys.executable,
        str(validator),
        str(csv_path),
        "--mode",
        mode,
        "--schema",
        schema_id,
        "--min-rows",
        str(expected_rows),
        "--max-rows",
        str(expected_rows),
    ]
    if not validator.is_file():
        return {
            "required": True,
            "executed": False,
            "valid": False,
            "reason": "validator_script_missing",
            "command_argv": command,
            "stdout": "",
            "stderr": f"missing validator script: {validator}",
            "return_code": None,
        }
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=10.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {
            "required": True,
            "executed": False,
            "valid": False,
            "reason": "validator_execution_failed",
            "command_argv": command,
            "stdout": "",
            "stderr": str(exc),
            "return_code": None,
        }
    return {
        "required": True,
        "executed": True,
        "valid": completed.returncode == 0,
        "reason": "pass" if completed.returncode == 0 else "validator_rejected_csv",
        "command_argv": command,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "return_code": completed.returncode,
    }


def run_v3_integration_validator(
    repository_root: Path,
    csv_path: Path,
    mode: str,
    expected_rows: int,
    schema_id: str,
) -> JsonObject:
    """Backward-compatible name for callers of the versioned validator hook.

    The function now accepts v3-v7 explicitly; its historical name is retained
    because external test harnesses imported it before metrics v4.
    """

    return run_integration_validator(
        repository_root, csv_path, mode, expected_rows, schema_id
    )


def terminate_process_group(
    process: subprocess.Popen[bytes], grace_sec: int
) -> int | None:
    """Stop the benchmark process group with a bounded graceful interval."""

    try:
        os.killpg(process.pid, signal.SIGINT)
    except ProcessLookupError:
        return process.poll()
    try:
        return process.wait(timeout=grace_sec)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            return process.wait(timeout=grace_sec)
        except subprocess.TimeoutExpired:
            return process.poll()


def execute_run(
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
    timeout_sec: int,
    grace_sec: int,
) -> ProcessResult:
    """Execute one low-priority invocation with an external process-group timeout."""

    start_utc = utc_now()
    start_mono = time.monotonic()
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    timed_out = False
    interrupted = False
    return_code: int | None = None
    with (
        stdout_path.open("wb") as stdout_stream,
        stderr_path.open("wb") as stderr_stream,
    ):
        try:
            process = subprocess.Popen(
                command,
                stdout=stdout_stream,
                stderr=stderr_stream,
                start_new_session=True,
            )
        except OSError as exc:
            stderr_stream.write(f"harness could not start process: {exc}\n".encode())
        else:
            try:
                return_code = process.wait(timeout=timeout_sec)
            except subprocess.TimeoutExpired:
                timed_out = True
                return_code = terminate_process_group(process, grace_sec)
            except KeyboardInterrupt:
                interrupted = True
                return_code = terminate_process_group(process, grace_sec)
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return ProcessResult(
        return_code=return_code,
        timed_out=timed_out,
        interrupted=interrupted,
        utc_start=start_utc,
        utc_end=utc_now(),
        wall_duration_sec=time.monotonic() - start_mono,
        user_cpu_sec=max(0.0, usage_after.ru_utime - usage_before.ru_utime),
        system_cpu_sec=max(0.0, usage_after.ru_stime - usage_before.ru_stime),
        voluntary_context_switches=max(0, usage_after.ru_nvcsw - usage_before.ru_nvcsw),
        involuntary_context_switches=max(
            0, usage_after.ru_nivcsw - usage_before.ru_nivcsw
        ),
        minor_faults=max(0, usage_after.ru_minflt - usage_before.ru_minflt),
        major_faults=max(0, usage_after.ru_majflt - usage_before.ru_majflt),
    )


def make_run_summary(
    run_id: str,
    mode: str,
    repetition: int,
    seed: int,
    rows: tuple[ParsedRow, ...],
    schema_id: str = V2_SCHEMA_ID,
) -> dict[str, object]:
    """Reduce one post-warm-up run to one independent statistical observation."""

    if not rows:
        raise AssertionError(
            "validated run summaries require at least one post-warm-up row"
        )
    summary: dict[str, object] = {
        "run_id": run_id,
        "metrics_schema_id": schema_id,
        "mode": mode,
        "repetition": repetition,
        "seed": seed,
        "rows_after_warmup": len(rows),
    }
    for name in run_mean_columns_for_schema(schema_id):
        summary[f"{name}_mean"] = statistics.fmean(
            float_value(row, name) for row in rows
        )

    eligible_total = sum(int_value(row, "eligible_workers") for row in rows)
    prediction_count = sum(int_value(row, "prediction_used") for row in rows)
    fallback_total = sum(int_value(row, "fallback_workers") for row in rows)
    summary["prediction_used_fraction"] = prediction_count / len(rows)
    summary["fallback_fraction"] = (
        fallback_total / eligible_total if eligible_total else 0.0
    )
    for action in ACTION_COLUMNS:
        action_total = sum(int_value(row, action) for row in rows)
        summary[f"{action}_action_fraction"] = (
            action_total / eligible_total if eligible_total else 0.0
        )
    summary["controller_updates"] = sum(
        int_value(row, "controller_updated") for row in rows
    )
    summary["rejected_frames_final"] = int_value(rows[-1], "rejected_frames")
    summary["missed_deadlines_final"] = int_value(rows[-1], "missed_deadlines")
    if schema_id in {
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        action_outcome_fractions = (
            (
                "migration_observed_success_fraction",
                "migration_observed_success_count",
                "migration_attempt_count",
            ),
            (
                "sleep_effectiveness_fraction",
                "sleep_effective_success_count",
                "sleep_attempt_count",
            ),
            (
                "yield_call_success_fraction",
                "yield_call_success_count",
                "yield_attempt_count",
            ),
            (
                "throttle_operation_success_fraction",
                "throttle_operation_success_count",
                "throttle_attempt_count",
            ),
        )
        for metric, success_name, attempt_name in action_outcome_fractions:
            successes = sum(int_value(row, success_name) for row in rows)
            attempts = sum(int_value(row, attempt_name) for row in rows)
            summary[metric] = success_fraction(successes, attempts)
        summary["action_error_count"] = sum(
            int_value(row, "action_error_count") for row in rows
        )
        if schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID):
            summary["large_burst_event_count"] = sum(
                int_value(row, "large_burst_event") for row in rows
            )
            summary["repeated_oscillation_event_count"] = sum(
                int_value(row, "repeated_oscillation_event") for row in rows
            )
        if schema_id in (V6_SCHEMA_ID, V7_SCHEMA_ID):
            summary["policy_lifecycle"] = make_policy_lifecycle_run_summary(
                rows, schema_id
            )
    elif schema_id not in (V2_SCHEMA_ID,):
        raise AssertionError(f"unsupported validated schema {schema_id!r}")
    return summary


def make_policy_lifecycle_run_summary(
    rows: tuple[ParsedRow, ...], schema_id: str
) -> JsonObject:
    """Summarize policy lifecycle fields without treating ticks as runs."""

    if schema_id not in (V6_SCHEMA_ID, V7_SCHEMA_ID):
        raise AssertionError(f"unsupported policy lifecycle schema {schema_id!r}")
    policy_modes = sorted({str(row["policy_mode"]) for row in rows})
    policy_load_statuses = sorted({str(row["policy_load_status"]) for row in rows})
    policy_save_statuses = sorted({str(row["policy_save_status"]) for row in rows})
    policy_digest_prefixes = sorted({str(row["policy_digest_prefix"]) for row in rows})
    lifecycle: JsonObject = {
        "policy_modes": policy_modes,
        "policy_schema_versions": sorted(
            {int_value(row, "policy_schema_version") for row in rows}
        ),
        "policy_format_versions": sorted(
            {int_value(row, "policy_format_version") for row in rows}
        ),
        "policy_load_statuses": policy_load_statuses,
        "policy_save_statuses": policy_save_statuses,
        "policy_digest_prefixes": policy_digest_prefixes,
        "policy_generation_initial": int_value(rows[0], "policy_generation"),
        "policy_generation_final": int_value(rows[-1], "policy_generation"),
        "policy_update_allowed_fraction": sum(
            int_value(row, "policy_update_allowed") for row in rows
        )
        / len(rows),
        "policy_update_applied_rows": sum(
            int_value(row, "policy_update_applied") for row in rows
        ),
        "policy_update_suppressed_rows": sum(
            int(str(row["policy_update_suppression_reason"]) != "NONE")
            for row in rows
        ),
        "policy_exploration_enabled_fraction": sum(
            int_value(row, "policy_exploration_enabled") for row in rows
        )
        / len(rows),
        "policy_train_update_count_final": int_value(
            rows[-1], "policy_train_update_count"
        ),
        "policy_adapt_update_count_final": int_value(
            rows[-1], "policy_adapt_update_count"
        ),
    }
    if schema_id == V7_SCHEMA_ID:
        lifecycle["coordination_semantics_versions"] = sorted(
            {int_value(row, "coordination_semantics_version") for row in rows}
        )
    return lifecycle


def t_critical_95(degrees_of_freedom: int) -> float:
    """Return the two-sided 95% Student-t critical value for df 1..9."""

    values = {
        1: 12.706,
        2: 4.303,
        3: 3.182,
        4: 2.776,
        5: 2.571,
        6: 2.447,
        7: 2.365,
        8: 2.306,
        9: 2.262,
    }
    try:
        return values[degrees_of_freedom]
    except KeyError as exc:
        raise AssertionError(
            "manifest bounds guarantee 1..9 degrees of freedom"
        ) from exc


def summarize_values(values: list[float]) -> JsonObject:
    """Compute mean, sample standard deviation, and a run-level t interval."""

    n = len(values)
    if n == 0:
        return {
            "n": 0,
            "mean": None,
            "stdev": None,
            "ci95_lower": None,
            "ci95_upper": None,
        }
    mean = statistics.fmean(values)
    if n == 1:
        return {
            "n": 1,
            "mean": mean,
            "stdev": None,
            "ci95_lower": None,
            "ci95_upper": None,
        }
    stdev = statistics.stdev(values)
    half_width = t_critical_95(n - 1) * stdev / math.sqrt(n)
    return {
        "n": n,
        "mean": mean,
        "stdev": stdev,
        "ci95_lower": mean - half_width,
        "ci95_upper": mean + half_width,
    }


def aggregate_run_summaries(
    run_summaries: list[dict[str, object]], schema_id: str = V2_SCHEMA_ID
) -> JsonObject:
    """Aggregate only across independent validated invocations, never ticks."""

    aggregate_metrics = aggregate_metrics_for_schema(schema_id)
    mismatched_summaries = [
        summary.get("metrics_schema_id")
        for summary in run_summaries
        if summary.get("metrics_schema_id") != schema_id
    ]
    if mismatched_summaries:
        raise AssertionError(
            "cannot aggregate summaries from different metrics schemas: "
            f"expected {schema_id!r}, found {mismatched_summaries!r}"
        )
    modes: JsonObject = {}
    for mode in MODES:
        mode_runs = [summary for summary in run_summaries if summary["mode"] == mode]
        metrics: JsonObject = {}
        for metric in aggregate_metrics:
            values = [float(summary[metric]) for summary in mode_runs]
            metrics[metric] = summarize_values(values)
        modes[mode] = {
            "independent_run_count": len(mode_runs),
            "statistical_unit": "one validated invocation after declared warm-up",
            "metrics": metrics,
        }
    aggregate: JsonObject = {
        "metrics_schema_id": schema_id,
        "comparison_design": COMPARISON_DESIGN,
        "interpretation": (
            "Mode summaries are descriptive and unpaired because each mode changes the endogenous "
            "host CPU signal; no causal performance difference is computed."
        ),
        "modes": modes,
    }
    if schema_id in (V6_SCHEMA_ID, V7_SCHEMA_ID):
        for mode in MODES:
            mode_runs = [summary for summary in run_summaries if summary["mode"] == mode]
            lifecycle_runs: list[JsonObject] = []
            for summary in mode_runs:
                lifecycle = summary.get("policy_lifecycle")
                if not isinstance(lifecycle, dict):
                    raise AssertionError("v6/v7 run summary lacks policy lifecycle data")
                lifecycle_runs.append(lifecycle)
            numeric_fields = (
                "policy_generation_initial",
                "policy_generation_final",
                "policy_update_allowed_fraction",
                "policy_update_applied_rows",
                "policy_update_suppressed_rows",
                "policy_exploration_enabled_fraction",
                "policy_train_update_count_final",
                "policy_adapt_update_count_final",
            )
            lifecycle_aggregate: JsonObject = {
                field: summarize_values(
                    [float(entry[field]) for entry in lifecycle_runs]
                )
                for field in numeric_fields
            }
            lifecycle_aggregate["policy_modes"] = sorted(
                {
                    mode_name
                    for entry in lifecycle_runs
                    for mode_name in entry["policy_modes"]
                }
            )
            lifecycle_aggregate["policy_load_statuses"] = sorted(
                {
                    status
                    for entry in lifecycle_runs
                    for status in entry["policy_load_statuses"]
                }
            )
            lifecycle_aggregate["policy_save_statuses"] = sorted(
                {
                    status
                    for entry in lifecycle_runs
                    for status in entry["policy_save_statuses"]
                }
            )
            lifecycle_aggregate["policy_digest_prefixes"] = sorted(
                {
                    digest
                    for entry in lifecycle_runs
                    for digest in entry["policy_digest_prefixes"]
                }
            )
            if schema_id == V7_SCHEMA_ID:
                lifecycle_aggregate["coordination_semantics_versions"] = sorted(
                    {
                        version
                        for entry in lifecycle_runs
                        for version in entry["coordination_semantics_versions"]
                    }
                )
            mode_value = modes[mode]
            if not isinstance(mode_value, dict):
                raise AssertionError("aggregate mode must be an object")
            mode_value["policy_lifecycle"] = lifecycle_aggregate
        aggregate["policy_lifecycle_summary_semantics"] = {
            "statistical_unit": "one validated invocation after declared warm-up",
            "numeric_fields": (
                "Policy lifecycle numeric fields are summarized across validated "
                "invocations; raw per-tick values remain in validated_rows.csv."
            ),
            "claim_limitation": (
                "These are userspace policy lifecycle observations, not kernel "
                "controller state or scheduler evidence."
            ),
        }
    if schema_id in {
        V3_SCHEMA_ID,
        V4_SCHEMA_ID,
        V5_SCHEMA_ID,
        V6_SCHEMA_ID,
        V7_SCHEMA_ID,
    }:
        aggregate["effective_action_summary_semantics"] = {
            "statistical_unit": "one validated invocation after declared warm-up",
            "fractions": (
                "Per-action effectiveness fractions are recomputed from summed current-window "
                "success and attempt counters within each invocation; rows are not treated as "
                "independent repetitions."
            ),
            "action_error_count": (
                "Sum of published current-window error counts after warm-up, not a deduplicated "
                "kernel error-event trace."
            ),
            "claim_limitation": (
                "All effective-action statistics are observable userspace operation outcomes, "
                "not Linux scheduler compliance or scheduler-performance evidence."
            ),
        }
    if schema_id in (V4_SCHEMA_ID, V5_SCHEMA_ID, V6_SCHEMA_ID, V7_SCHEMA_ID):
        aggregate["burst_stability_summary_semantics"] = {
            "statistical_unit": "one validated invocation after declared warm-up",
            "historical_s4": (
                "Historical S4 remains a mean of per-window change fractions and is not "
                "reinterpreted by the v4 burst diagnostic."
            ),
            "S4_burst": (
                "Experimental userspace population-action stability diagnostic; it is not "
                "included in historical Q and does not establish kernel scheduling behavior."
            ),
            "event_counts": (
                "Large-burst and repeated-oscillation event counts are sums across repeated "
                "rows within one invocation, not independent experimental repetitions."
            ),
        }
    return aggregate


def write_run_summaries_csv(
    path: Path, summaries: list[dict[str, object]], schema_id: str = V2_SCHEMA_ID
) -> None:
    """Write one row per validated independent invocation."""

    fieldnames = [
        "run_id",
        "metrics_schema_id",
        "mode",
        "repetition",
        "seed",
        "rows_after_warmup",
        *aggregate_metrics_for_schema(schema_id),
    ]
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(summaries)


def write_mode_summary_csv(
    path: Path, aggregate: JsonObject, schema_id: str = V2_SCHEMA_ID
) -> None:
    """Write long-form run-level statistics for analysis tools."""

    fieldnames = ("mode", "metric", "n", "mean", "stdev", "ci95_lower", "ci95_upper")
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        raw_modes = aggregate["modes"]
        if not isinstance(raw_modes, dict):
            raise AssertionError("aggregate modes must be an object")
        for mode in MODES:
            mode_value = raw_modes[mode]
            if not isinstance(mode_value, dict) or not isinstance(
                mode_value.get("metrics"), dict
            ):
                raise AssertionError("aggregate mode metrics must be an object")
            metrics = mode_value["metrics"]
            for metric in aggregate_metrics_for_schema(schema_id):
                statistic = metrics[metric]
                if not isinstance(statistic, dict):
                    raise AssertionError("aggregate statistic must be an object")
                writer.writerow({"mode": mode, "metric": metric, **statistic})


def write_validated_rows_csv(
    path: Path,
    included_runs: list[tuple[str, int, int, tuple[ParsedRow, ...]]],
    schema_id: str = V2_SCHEMA_ID,
) -> None:
    """Write post-warm-up rows with explicit run identity, leaving raw data untouched."""

    fieldnames = (
        "run_id",
        "repetition",
        "seed",
        *EXPECTED_HEADERS_BY_SCHEMA[schema_id],
    )
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        for run_id, repetition, seed, rows in included_runs:
            for row in rows:
                writer.writerow(
                    {"run_id": run_id, "repetition": repetition, "seed": seed, **row}
                )


def build_command(
    nice_path: str,
    binary: Path,
    manifest: Manifest,
    mode: str,
    seed: int,
) -> list[str]:
    """Construct the fully captured, bounded child command."""

    return [
        nice_path,
        "-n",
        str(manifest.nice_increment),
        str(binary),
        "--workers",
        str(manifest.workers),
        "--rt-exempt",
        str(manifest.rt_exempt),
        "--duration",
        str(manifest.duration_sec),
        "--interval-ms",
        str(manifest.interval_ms),
        "--calibration",
        str(manifest.calibration_sec),
        "--mode",
        mode,
        "--seed",
        str(seed),
    ]


def parse_args(argv: list[str]) -> argparse.Namespace:
    """Parse the explicit benchmark inputs."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest", required=True, type=Path, help="bounded v1 manifest"
    )
    parser.add_argument(
        "--schema",
        required=True,
        type=Path,
        help="strict metrics schema; its explicit identifier must match the manifest",
    )
    parser.add_argument(
        "--binary", required=True, type=Path, help="prebuilt executable to run"
    )
    parser.add_argument(
        "--source", required=True, type=Path, help="C source corresponding to binary"
    )
    parser.add_argument(
        "--repository-root",
        required=True,
        type=Path,
        help="repository root used for revision provenance",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        type=Path,
        help="new output directory; existing paths are never overwritten",
    )
    return parser.parse_args(argv)


def resolve_input_file(path: Path, label: str, executable: bool = False) -> Path:
    """Resolve and validate one caller-supplied input path."""

    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise BenchmarkError(f"{label} is not a regular file: {resolved}")
    if executable and not os.access(resolved, os.X_OK):
        raise BenchmarkError(f"{label} is not executable: {resolved}")
    return resolved


def preflight_binary(binary: Path) -> JsonObject:
    """Verify the passed binary advertises the required deterministic seed option."""

    try:
        completed = subprocess.run(
            [str(binary), "--help"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise BenchmarkError(f"cannot execute binary --help: {exc}") from exc
    combined = completed.stdout + completed.stderr
    if completed.returncode != 0 or "--seed" not in combined:
        raise BenchmarkError(
            "passed binary must exit successfully and advertise --seed in --help"
        )
    return {
        "command": [str(binary), "--help"],
        "return_code": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def run_benchmark(args: argparse.Namespace) -> int:
    """Run all bounded invocations and materialize provenance plus summaries."""

    if not sys.platform.startswith("linux"):
        raise BenchmarkError("the userspace prototype and this harness require Linux")
    manifest_path = resolve_input_file(args.manifest, "manifest")
    schema_path = resolve_input_file(args.schema, "schema")
    binary_path = resolve_input_file(args.binary, "binary", executable=True)
    source_path = resolve_input_file(args.source, "source")
    repository_root = args.repository_root.expanduser().resolve()
    if not repository_root.is_dir():
        raise BenchmarkError(f"repository root is not a directory: {repository_root}")

    output_dir = args.output_dir.expanduser().resolve()
    if output_dir.exists():
        raise BenchmarkError(
            f"output directory already exists; refusing to overwrite: {output_dir}"
        )
    nice_path = shutil.which("nice")
    if nice_path is None:
        raise BenchmarkError("required low-priority launcher 'nice' is not available")

    manifest = load_manifest(manifest_path)
    schema = load_metrics_schema(schema_path, manifest.metrics_schema_id)
    invocation_expected_rows = expected_row_count(manifest)
    binary_help = preflight_binary(binary_path)
    source_hash_start = sha256_file(source_path)
    binary_hash_start = sha256_file(binary_path)
    manifest_hash = sha256_file(manifest_path)
    schema_hash = sha256_file(schema_path)

    output_dir.mkdir(parents=True, exist_ok=False)
    runs_dir = output_dir / "runs"
    processed_dir = output_dir / "processed"
    runs_dir.mkdir()
    processed_dir.mkdir()
    shutil.copy2(manifest_path, output_dir / "manifest.input.json")
    shutil.copy2(schema_path, output_dir / "metrics_schema.input.json")

    benchmark_start = utc_now()
    environment = capture_environment(repository_root)
    provenance: JsonObject = {
        "experiment_id": manifest.experiment_id,
        "metrics_schema_id": schema.schema_id,
        "maturity_class": manifest.maturity_class,
        "comparison_design": COMPARISON_DESIGN,
        "claim_boundary": (
            "Descriptive userspace observations only; modes are unpaired and endogenous. "
            "No kernel-scheduler or comparative-performance claim is authorized."
        ),
        "benchmark_start_utc": benchmark_start,
        "repository_root": str(repository_root),
        "output_directory": str(output_dir),
        "inputs": {
            "manifest": {"path": str(manifest_path), "sha256": manifest_hash},
            "metrics_schema": {"path": str(schema_path), "sha256": schema_hash},
            "source": {"path": str(source_path), "sha256": source_hash_start},
            "binary": {
                "path": str(binary_path),
                "sha256": binary_hash_start,
                "build_configuration": "not supplied; caller passed a prebuilt binary",
                "source_association": (
                    "caller-declared; hashes are recorded, but the harness cannot prove that the "
                    "binary was built from the supplied source"
                ),
            },
        },
        "manifest": asdict(manifest),
        "expected_rows_per_invocation": invocation_expected_rows,
        "binary_help": binary_help,
        "environment": environment,
        "execution_order": "for each seed: baseline, then orchestra; never concurrent",
    }
    write_json(output_dir / "provenance.json", provenance)

    run_records: list[JsonObject] = []
    run_summaries: list[dict[str, object]] = []
    included_runs: list[tuple[str, int, int, tuple[ParsedRow, ...]]] = []
    stop_requested = False
    execution_index = 0

    for repetition, seed in enumerate(manifest.seeds, start=1):
        for mode in manifest.modes:
            execution_index += 1
            run_id = f"rep_{repetition:02d}_seed_{seed}_{mode}"
            stdout_path = runs_dir / f"{run_id}.csv"
            stderr_path = runs_dir / f"{run_id}.stderr.log"
            record_path = runs_dir / f"{run_id}.json"
            command = build_command(nice_path, binary_path, manifest, mode, seed)
            background_before = capture_background_state()
            process = execute_run(
                command,
                stdout_path,
                stderr_path,
                manifest.timeout_sec,
                manifest.timeout_grace_sec,
            )
            background_after = capture_background_state()
            validation = validate_csv(
                stdout_path,
                schema,
                mode,
                manifest.workers - manifest.rt_exempt,
                manifest.warmup_ticks,
                invocation_expected_rows,
            )
            external_validation = run_integration_validator(
                repository_root,
                stdout_path,
                mode,
                invocation_expected_rows,
                schema.schema_id,
            )
            binary_hash_after = sha256_file(binary_path)
            source_hash_after = sha256_file(source_path)
            hash_stable = (
                binary_hash_after == binary_hash_start
                and source_hash_after == source_hash_start
            )

            minimum_expected_wall_sec = 0.90 * (
                manifest.calibration_sec + manifest.duration_sec
            )
            runtime_complete = process.wall_duration_sec >= minimum_expected_wall_sec
            run_ok = (
                process.return_code == 0
                and not process.timed_out
                and not process.interrupted
                and runtime_complete
                and validation.valid
                and external_validation.get("valid") is True
                and hash_stable
            )
            exclusion_reasons: list[str] = []
            if process.return_code != 0:
                exclusion_reasons.append(f"return_code={process.return_code}")
            if process.timed_out:
                exclusion_reasons.append("external_timeout")
            if process.interrupted:
                exclusion_reasons.append("operator_interrupt")
            if not runtime_complete:
                exclusion_reasons.append(
                    f"wall_duration_below_{minimum_expected_wall_sec:.3f}_seconds"
                )
            if not validation.valid:
                exclusion_reasons.append("csv_validation_failed")
            if external_validation.get("valid") is not True:
                exclusion_reasons.append("integration_csv_validation_failed")
            if not hash_stable:
                exclusion_reasons.append("source_or_binary_changed_during_benchmark")

            record: JsonObject = {
                "run_id": run_id,
                "execution_index": execution_index,
                "mode": mode,
                "repetition": repetition,
                "seed": seed,
                "command_argv": command,
                "command_shell_escaped": shlex.join(command),
                "process": asdict(process),
                "minimum_expected_wall_sec": minimum_expected_wall_sec,
                "runtime_complete": runtime_complete,
                "background_before": background_before,
                "background_after": background_after,
                "raw_stdout": {
                    "path": str(stdout_path.relative_to(output_dir)),
                    "sha256": sha256_file(stdout_path),
                },
                "raw_stderr": {
                    "path": str(stderr_path.relative_to(output_dir)),
                    "sha256": sha256_file(stderr_path),
                },
                "validation": {
                    "schema_id": schema.schema_id,
                    "valid": validation.valid,
                    "issues": list(validation.issues),
                    "rows_total": len(validation.rows),
                    "warmup_ticks_excluded": manifest.warmup_ticks,
                    "rows_after_warmup": len(validation.rows_after_warmup),
                },
                "integration_validation": external_validation,
                "source_sha256_after": source_hash_after,
                "binary_sha256_after": binary_hash_after,
                "source_and_binary_stable": hash_stable,
                "included_in_processed_summary": run_ok,
                "predeclared_exclusion_reasons": exclusion_reasons,
            }
            write_json(record_path, record)
            run_records.append(record)

            if run_ok:
                summary = make_run_summary(
                    run_id,
                    mode,
                    repetition,
                    seed,
                    validation.rows_after_warmup,
                    schema.schema_id,
                )
                run_summaries.append(summary)
                included_runs.append(
                    (run_id, repetition, seed, validation.rows_after_warmup)
                )
            if process.interrupted:
                stop_requested = True
                break
        if stop_requested:
            break

    aggregate = aggregate_run_summaries(run_summaries, schema.schema_id)
    write_run_summaries_csv(
        processed_dir / "run_summaries.csv", run_summaries, schema.schema_id
    )
    write_mode_summary_csv(
        processed_dir / "mode_summary.csv", aggregate, schema.schema_id
    )
    write_validated_rows_csv(
        processed_dir / "validated_rows.csv", included_runs, schema.schema_id
    )
    write_json(processed_dir / "summary.json", aggregate)

    failures = sum(
        1
        for record in run_records
        if record.get("included_in_processed_summary") is not True
    )
    expected_runs = manifest.repetitions * len(manifest.modes)
    result: JsonObject = {
        "experiment_id": manifest.experiment_id,
        "status": "complete"
        if failures == 0 and len(run_records) == expected_runs
        else "complete_with_failures",
        "metrics_schema_id": schema.schema_id,
        "maturity_class": manifest.maturity_class,
        "comparison_design": COMPARISON_DESIGN,
        "claim_boundary": (
            "Descriptive userspace observations only; do not infer a paired mode effect, "
            "kernel scheduling behavior, or general performance."
        ),
        "benchmark_start_utc": benchmark_start,
        "benchmark_end_utc": utc_now(),
        "expected_run_count": expected_runs,
        "expected_rows_per_invocation": invocation_expected_rows,
        "attempted_run_count": len(run_records),
        "validated_run_count": len(run_summaries),
        "failed_or_excluded_run_count": failures,
        "operator_interrupted": stop_requested,
        "source_sha256_start": source_hash_start,
        "source_sha256_end": sha256_file(source_path),
        "binary_sha256_start": binary_hash_start,
        "binary_sha256_end": sha256_file(binary_path),
        "background_final": capture_background_state(),
        "run_record_paths": [
            str((runs_dir / f"{record['run_id']}.json").relative_to(output_dir))
            for record in run_records
        ],
        "processed_outputs": {
            "run_summaries_csv": "processed/run_summaries.csv",
            "mode_summary_csv": "processed/mode_summary.csv",
            "validated_rows_csv": "processed/validated_rows.csv",
            "summary_json": "processed/summary.json",
        },
    }
    write_json(output_dir / "benchmark_result.json", result)
    return 0 if result["status"] == "complete" else 1


def main(argv: list[str] | None = None) -> int:
    """CLI entry point with a distinct configuration-error status."""

    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return run_benchmark(args)
    except BenchmarkError as exc:
        print(f"benchmark configuration error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
