#!/usr/bin/env python3
"""Source-level regressions for safety properties that need runtime proof later."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BPF = (ROOT / "kernel/sched_ext/orchestra_scx_stage7.bpf.c").read_text()
COMPAT_BPF = (ROOT / "kernel/sched_ext/orchestra_scx.bpf.c").read_text()
STABLE_BPF = (ROOT / "kernel/sched_ext/bpf/orchestra_sched.bpf.c").read_text()
ABI = (ROOT / "kernel/sched_ext/include/orchestra_bridge_v1.h").read_text()
PRODUCT_ABI = (ROOT / "kernel/sched_ext/include/orchestra_product_abi.h").read_text()
KERNEL_V8 = (ROOT / "kernel/sched_ext/include/orchestra_kernel_v8.h").read_text()
CONTROL_V10 = (ROOT / "kernel/sched_ext/include/orchestra_control_abi.h").read_text()
COORD_V10 = (ROOT / "kernel/sched_ext/include/orchestra_coord.h").read_text()
CONTROLLER_V10 = (ROOT / "kernel/sched_ext/include/orchestra_controller.h").read_text()
BRIDGE = (ROOT / "kernel/sched_ext/bridge/orchestra_bridge.c").read_text()
LOADER = (ROOT / "kernel/sched_ext/bridge/orchestra_loader.c").read_text()
POLICY = (ROOT / "scripts/policy_load.py").read_text()
PATH_SAFETY = (ROOT / "scripts/path_safety.sh").read_text()
LEGACY_LOADER = (ROOT / "kernel/sched_ext/orchestra_scx.c").read_text()
ENGINE = (ROOT / "orchestra_paper_cpu_demo/orchestra_paper_cpu.c").read_text()
CLI = (ROOT / "scripts/orchestra").read_text()
INSTALL = (ROOT / "scripts/install.sh").read_text()
UNINSTALL = (ROOT / "scripts/uninstall.sh").read_text()
SYSTEMD = (ROOT / "config/systemd/orchestra.service").read_text()
KERNEL_CI = (ROOT / ".github/workflows/build-sched-ext.yml").read_text()
SECURITY_RUN = (ROOT / "tests/security/run.sh").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require("SCX_OPS_SWITCH_PARTIAL" not in BPF,
            "partial-switch starvation mode must not return")
    require('include "orchestra_scx_stage7.bpf.c"' in COMPAT_BPF,
            "the historical BPF filename must build the canonical scheduler")
    require('include "../orchestra_scx_stage7.bpf.c"' in STABLE_BPF,
            "the stable product BPF entry point must select one canonical implementation")
    require("ORCHESTRA_PRODUCT_ABI_MAJOR 1u" in PRODUCT_ABI and
            "ORCHESTRA_PRODUCT_BRIDGE_ABI_VERSION" in PRODUCT_ABI and
            "ORCHESTRA_PRODUCT_CONTROL_ABI_VERSION" in PRODUCT_ABI,
            "the product ABI bundle must declare current component versions")
    require("syscall(" not in LEGACY_LOADER,
            "the retired loader must not change task policy")
    require("dir_cache" not in BPF,
            "cross-CPU mutable directive cache must not return")
    require("BPF_MAP_TYPE_HASH" in BPF and "orch_directives" in BPF,
            "directives must remain per-task hash entries")
    require("start_boottime_ns" in BPF and "p->start_boottime" in BPF,
            "task lifetime identity must remain enforced")
    require("bpf_spin_lock(&dir->lock)" in BPF,
            "directive snapshot must remain lock coherent")
    require("control_snapshot_equal" in BPF and "attempt < 2" in BPF,
            "control/directive publication must retain bounded validation")
    require("BRIDGE_DEFERRED_DSQ" in BPF and "deferred_timerfn" in BPF,
            "SLEEP/THROTTLE deferred eligibility must remain implemented")
    require("retire_released_sleep_directive" in BPF and
            "dir->action = ORCHESTRA_ACTION_RUN" in BPF and
            "dir->generation == generation" in BPF,
            "released SLEEP generations must retire without overwriting newer directives")
    require("deferred_timer_tick_count" in BPF and
            "deferred_release_failure_count" in BPF,
            "deferred progress diagnostics must remain observable")
    require("deferred_directive_is_current" in BPF and
            "load_directive(p, &current" in BPF and
            "BRIDGE_FALLBACK_UNSTABLE_PUBLICATION" in BPF and
            "SCX_DSQ_GLOBAL" in BPF,
            "deferred release must revalidate the current directive and fail safe to RUN")
    require("BRIDGE_DEFER_MAP_NAME" in ABI and
            '"--defer-timer-map-id"' in BRIDGE,
            "the deferred timer map must remain in the exact pin contract")
    require("BRIDGE_SIGNAL_MAP_NAME" in ABI and
            "orch_signal" in BPF and "struct bridge_signal_frame" in BRIDGE,
            "the fixed-point kernel signal frame map must remain in the ABI")
    require("BRIDGE_DIRECTIVE_F_REQUIRE_SIGNAL" in BPF and
            "snapshot_signal" in BPF and "signal_is_valid" in BPF and
            "--signal-publish" in BRIDGE and
            "BRIDGE_STREAM_F_PUBLISH_SIGNAL" in BRIDGE and
            "BRIDGE_STREAM_F_REQUIRE_SIGNAL" in BRIDGE,
            "signal-required directives must have bounded kernel freshness validation")
    require("signal_permille" in ENGINE and
            "signal_sequence" in ENGINE and
            "publish_signal" in ENGINE and
            "require_signal" in ENGINE,
            "the userspace kernel bridge must publish and require the fixed-point signal")
    require("scheduling_policy_is_rt" in BRIDGE and
            "sched_getscheduler" in BRIDGE and
            "refusing adaptive directive for RT policy" in BRIDGE,
            "bridge directives must fail closed for RT scheduling policies")
    require("orchestra_bridge must run as root" in BRIDGE and
            "if (geteuid() != 0)" in BRIDGE,
            "direct bridge invocation must enforce its privileged boundary")
    require("if (!text || !action)" in BRIDGE,
            "bridge parser helpers must reject null output/input pointers")
    require("refresh_policy_bank_generation" in BRIDGE and
            "next_policy_generation" in BRIDGE and
            "meta.policy_generation = next_generation" in BRIDGE and
            "intentionally rewinds the active generation" not in BRIDGE,
            "policy rollback must relabel the retained bank without generation replay")
    require("publication_status = BRIDGE_PUB_MAP_ERROR" in BRIDGE and
            "POLICY_PUBLISH_TIMEOUT_SECONDS" in POLICY and
            "duplicate JSON object key" in POLICY and
            "BRIDGE_COMMAND_TIMEOUT_SECONDS" in POLICY,
            "policy and publication controls must reject ambiguity and bound control-plane waits")
    require("Invalidate readers before exposing the new control generation" in BRIDGE and
            "Signal readers must remain fail-closed" in BRIDGE and
            "Invalidate scheduler readers before flipping the policy bank" in BRIDGE and
            "final control publication readback failed" in BRIDGE,
            "multi-map publication must stage as non-OK until payload readback succeeds")
    require(LOADER.index("bpf_map__pin") <
            LOADER.index("bpf_map__attach_struct_ops"),
            "the loader must pin maps before struct_ops attach initializes timers")
    require("deferred timer map was not pinned before attach" in BRIDGE,
            "late bridge pinning must not accept an already-cancelled timer")
    require("throttle_budget_ns" in BPF and "runtime_used_ns" in BPF,
            "THROTTLE must retain runtime budget accounting")
    require("struct bridge_task_state {\n    ORCHESTRA_MAP_LOCK lock" in ABI and
            "bpf_spin_lock(&state->lock)" in BPF,
            "per-task timer/callback state must remain synchronized")
    require("bpf_cpumask_test_cpu(cpu, p->cpus_ptr)" in BPF and
            "scx_bpf_get_online_cpumask" in BPF,
            "MIGRATE must check affinity and online masks")
    require("SCX_DSQ_LOCAL_ON | dir->target_cpu" in BPF,
            "MIGRATE re-enqueues must retain their validated target CPU")
    require("SCX_OPS_KEEP_BUILTIN_IDLE" in BPF,
            "built-in idle tracking must remain enabled")
    require("scx_bpf_consume(SCX_DSQ_GLOBAL)" not in BPF,
            "dispatch must not consume the reserved global DSQ")
    require("migrate_running_target_count" in BPF and "actual_cpu" in BPF,
            "migration telemetry must correlate requested and observed CPUs")
    require("select_cpu_direct_insert_count" in ABI and
            "enqueue_callback_count" in ABI,
            "callback/direct-insertion telemetry must remain unambiguous")
    require("last_generation = 0" in BPF and
            "Never reset last_generation" in BRIDGE,
            "generation may initialize only at a new scheduler epoch")
    require("bpf_map_get_next_id" not in BRIDGE,
            "global prefix map discovery must not return")
    require("info.value_size != spec->value_size" in BRIDGE,
            "bridge map schema validation must remain exact")
    require("struct orchestra_task_identity target_identity" in BRIDGE and
            "memcmp(&next, &target_identity, sizeof(next)) == 0" in BRIDGE,
            "targeted status must filter the full live task identity")
    require("ORCHESTRA_ACTION_SLEEP = 1" in
            (ROOT / "kernel/sched_ext/include/orchestra_abi.h").read_text(),
            "canonical/wire action ABI drift")
    require("sizeof(struct orchestra_task_identity) == 16" in ABI and
            "sizeof(struct bridge_signal_frame) == 152" in ABI and
            "sizeof(struct bridge_stream_request) == 176" in ABI,
            "shared task identity size assertion missing")
    require("canonical_action_to_wire" in ENGINE and
            "typedef enum orchestra_action_id action_t" in ENGINE,
            "canonical engine-to-wire action translation must remain explicit")
    require("ORCHESTRA_KERNEL_ABI_VERSION             8u" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_STATE_SCHEMA_VERSION" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION" in KERNEL_V8,
            "kernel adaptive ABI/schema versions must be explicit")
    require("ORCHESTRA_KERNEL_POLICY_BANK_COUNT       2u" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_MAX_POLICY_STATES" in KERNEL_V8 and
            "active_bank" in KERNEL_V8 and "policy_generation" in KERNEL_V8,
            "policy publication must use bounded double-buffer generation state")
    require("struct orchestra_runtime_state_v8" in KERNEL_V8 and
            "prediction_generation" in KERNEL_V8 and
            "prediction_expires_ns" in KERNEL_V8 and
            "s1_permille" in KERNEL_V8 and "q_permille" in KERNEL_V8,
            "runtime/prediction/coordination state contract missing")
    require("struct orchestra_task_hot_v8" in KERNEL_V8 and
            "struct orchestra_task_diag_v8" in KERNEL_V8 and
            "controller_override_action" in KERNEL_V8 and
            "throttle_deadline_ns" in KERNEL_V8,
            "hot and diagnostic per-task adaptive state missing")
    require("ORCHESTRA_KERNEL_CAP_ADAPTIVE_SLICE" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_CAP_STATE_CPU_SELECTION" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_CAP_SLEEP_DEFER_COMPAT" in KERNEL_V8 and
            "ORCHESTRA_KERNEL_CAP_THROTTLE_DEFER_COMPAT" in KERNEL_V8 and
            "ORCHESTRA_MIGRATE_V8_ALREADY_LOCAL" in KERNEL_V8,
            "v8 action backend capabilities/outcomes missing")
    require("struct orchestra_telemetry_v8" in KERNEL_V8 and
            "policy_cache_hit_count" in KERNEL_V8 and
            "unsupported_action_count" in KERNEL_V8 and
            "prediction_fallback_count" in KERNEL_V8 and
            "state_generation_change_count" in KERNEL_V8 and
            "signal_generation_change_count" in KERNEL_V8,
            "v8 policy/action/fallback telemetry missing")
    require("orch_runtime_v8" in BPF and "orch_meta_v8" in BPF and
            "orch_entry_v8" in BPF and "orch_task_v8" in BPF and
            "orch_diag_v8" in BPF and "orch_tel_v8" in BPF,
            "kernel v8 maps must be part of the canonical BPF object")
    require("orchestra_read_runtime_state" in BPF and
            "orchestra_build_state" in BPF and
            "orchestra_policy_lookup" in BPF and
            "orchestra_controller_gate" in BPF and
            "orchestra_validate_action" in BPF and
            "orchestra_decide" in BPF and
            "orchestra_execute_action" in BPF and
            "orchestra_record_result" in BPF,
            "canonical kernel decision pipeline is incomplete")
    require(BPF.index("orchestra_read_runtime_state") <
            BPF.index("orchestra_build_state") <
            BPF.index("orchestra_policy_lookup") <
            BPF.index("orchestra_controller_gate") <
            BPF.index("orchestra_validate_action") <
            BPF.index("orchestra_execute_action"),
            "kernel decision stages must remain ordered in source")
    require("policy_default_v8" in BPF and
            "BRIDGE_FALLBACK_UNSUPPORTED_ACTION" in BPF and
            "ORCHESTRA_CTRL_ROLLBACK" in BPF and
            "ORCHESTRA_CTRL_RECOVERY" in BPF and
            "orchestra_record_defer_v8" in BPF and
            "cpu_is_online" in BPF,
            "safe controller/action fallback semantics missing")
    require("BRIDGE_RUNTIME_V8_MAP_NAME" in LOADER and
            "BRIDGE_POLICY_ENTRY_V8_MAP_NAME" in LOADER and
            "ORCHESTRA_KERNEL_POLICY_ENTRY_COUNT" in LOADER and
            "--policy-commit" in BRIDGE and
            "EVALUATE freezes the active policy" in BRIDGE,
            "loader/control plane must expose v8 policy maps and lifecycle")
    require("m.s3 = m.s3_conditioned" in ENGINE and
            "if (m.s4_burst < m.s4) m.s4 = m.s4_burst" in ENGINE,
            "corrected conditioned-S3/burst-S4 semantics must remain canonical")
    require("calibrated_prediction_confidence" in ENGINE and
            "calibrate_fixed_gain" in ENGINE and "kalman" not in ENGINE.lower(),
            "offline fixed-gain predictor must not regress to adaptive Kalman")
    require("ORCHESTRA_CONTROL_ABI_VERSION" in CONTROL_V10 and
            "ORCHESTRA_COORD_SCHEMA_VERSION" in CONTROL_V10 and
            "ORCHESTRA_CONTROLLER_SCHEMA_VERSION" in CONTROL_V10 and
            "ORCHESTRA_RUNTIME_SCHEMA_VERSION" in CONTROL_V10,
            "native v10 ABI/schema versions must be explicit")
    require("ORCHESTRA_COORD_WINDOW_BANK_COUNT        2u" in CONTROL_V10 and
            "ORCHESTRA_COORD_MAP_ENTRY_COUNT" in CONTROL_V10 and
            "ORCHESTRA_COORD_MAX_CPU_DOMAINS" in CONTROL_V10 and
            "ORCHESTRA_COORD_MAX_NUMA_DOMAINS" in CONTROL_V10,
            "native coordination storage must remain bounded and double-buffered")
    require("struct orchestra_coordination_state_v10" in CONTROL_V10 and
            "s1_permille" in CONTROL_V10 and
            "s2_permille" in CONTROL_V10 and
            "s3_permille" in CONTROL_V10 and
            "s4_permille" in CONTROL_V10 and
            "q_permille" in CONTROL_V10 and
            "coherence_hist" in CONTROL_V10,
            "native S1/S2/S3/S4/Q state contract is missing")
    require("ORCH_DEFICIT_SIGNAL" in CONTROL_V10 and
            "ORCH_DEFICIT_COMPLIANCE" in CONTROL_V10 and
            "ORCH_DEFICIT_COHERENCE" in CONTROL_V10 and
            "ORCH_DEFICIT_STABILITY" in CONTROL_V10 and
            "ORCH_DEFICIT_MIXED" in CONTROL_V10 and
            "ORCH_ACTUATOR_COORDINATION_THRESHOLD" in CONTROL_V10,
            "deficit classes and bounded actuator IDs must remain canonical")
    require("orchestra_coord_geomean4" in COORD_V10 and
            "orchestra_classify_deficit_v10" in COORD_V10 and
            "orchestra_deficit_actuator_mask" in COORD_V10 and
            "orchestra_coord_record_signal" in COORD_V10 and
            "orchestra_coord_record_action" in COORD_V10 and
            "orchestra_coord_record_execution" in COORD_V10,
            "native coordination-window measurement path is incomplete")
    require("ORCHESTRA_CTRL_NORMAL" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_DEGRADED" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_SATURATED" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_DISABLED" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_ROLLBACK" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_RECOVERY" in CONTROLLER_V10 and
            "orchestra_controller_publish_staging" in CONTROLLER_V10 and
            "orchestra_controller_rollback_locked" in CONTROLLER_V10 and
            "minimum_hold_ns" in CONTROLLER_V10 and
            "cooldown_until_ns" in CONTROLLER_V10,
            "two-timescale controller state machine and anti-oscillation safeguards are missing")
    require("ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED" in CONTROLLER_V10 and
            "maximum_step" in CONTROL_V10 and
            "previous_good" in CONTROL_V10 and
            "evaluation_until_ns" in CONTROL_V10,
            "bounded actuator publication and rollback state is missing")
    require("fuzz_abi_state.c" in SECURITY_RUN and
            "-fsanitize=address,undefined" in SECURITY_RUN and
            "fuzz_abi_state" in SECURITY_RUN,
            "ABI/state mutation target must remain part of the sanitizer security gate")
    require("actuator->previous_value < actuator->minimum" in CONTROLLER_V10 and
            "actuator->rollback_value < actuator->minimum" in CONTROLLER_V10 and
            "controller->active_generation == controller->active.generation" in
                CONTROLLER_V10 and
            "controller->active_deficit_class < ORCH_DEFICIT_COUNT" in
                CONTROLLER_V10 and
            "if (!controller)" in CONTROLLER_V10,
            "controller validation must cover malformed state, actuator history, and bank generations")
    require("orch_coord_v10" in BPF and "orch_coord_cpu" in BPF and
            "orch_ctrl_v10" in BPF and "orch_ctrl_tel_v10" in BPF and
            "orch_runtime10" in BPF and "orch_task_coord" in BPF,
            "v10 coordination/controller maps must be part of the canonical BPF object")
    require("publish_runtime_state_v10" in BPF and
            "orchestra_controller_init_v10" in BPF and
            "controller_generation" in BPF and
            "actual_executed_action" in BPF,
            "v10 runtime publication and action provenance are not integrated")
    require("orchestra_controller_update_from_coord" in COORD_V10 and
            "orchestra_controller_update_from_coord" in CONTROLLER_V10,
            "coordination finalization must feed the native controller")
    require("ORCHESTRA_KERNEL_V10_REQUIRED_CAPS" in BPF and
            "ORCHESTRA_KERNEL_CAP_NATIVE_COORDINATION" in CONTROL_V10 and
            "ORCHESTRA_KERNEL_CAP_RUNTIME_CONTROLLER" in CONTROL_V10,
            "v10 capability negotiation is missing")
    require("ORCHESTRA_ACTION_RUN" in CONTROLLER_V10 and
            "ORCHESTRA_CTRL_DISABLED" in BPF and
            "ORCHESTRA_ACTION_RUN" in BPF and
            "orchestra_controller_should_fallback_v10" in CONTROLLER_V10,
            "controller failure must retain a safe RUN fallback")
    require("scheduler_owned" in LOADER and
            "scheduler_disabled" in LOADER and
            "root_safe_directory_chain" in LOADER and
            "validate_existing_pins" in LOADER and
            "pinned_link_matches" in LOADER and
            "bpf_link_get_info_by_fd" in LOADER and
            "BPF_LINK_TYPE_STRUCT_OPS" in LOADER,
            "loader unload must verify scheduler ownership and its pinned link/schema")
    require("safe_root_artifact" in LOADER and "pin_path_absent" in LOADER and
            "link_pinned" in LOADER and "detach_link_fd" in LOADER and
            "Keep both the pinned link" in LOADER and
            "clean up only a complete, schema-validated" in LOADER and
            "cleanup_pins" in LOADER,
            "loader must reject untrusted artifacts and pre-existing pin paths")
    require("ORCHESTRA_OPS_NAME=orchestra_scx_v8" in CLI and
            "require_scheduler_ownership" in CLI and
            "run_scheduler" in CLI and
            "monitor_scheduler" in CLI,
            "the user-facing lifecycle must have explicit ownership and foreground monitoring")
    require("require_kernel_artifact_set" in CLI and
            "manifest_hash_matches" in CLI and
            "root_safe_artifact" in CLI,
            "privileged lifecycle must reject untrusted or tampered kernel artifacts")
    require("with_kernel" in INSTALL and
            "build-manifest.txt" in INSTALL and
            "safe_source_artifact" in INSTALL and
            "cp -a" not in INSTALL,
            "installation must use explicit target artifacts and root-safe file copying")
    require("path_safety.sh" in INSTALL and
            "ORCHESTRA_INSTALL_MANIFEST_V1" in INSTALL and
            "safe_install_file" in INSTALL and
            "orchestra_safe_destination_file" in INSTALL,
            "installer must constrain destinations and record ownership")
    require("preserving non-matching or symlinked systemd unit" in UNINSTALL and
            "cmp -s" in UNINSTALL and
            "ORCHESTRA_INSTALL_MANIFEST_V1" in UNINSTALL and
            "--remove-config" in UNINSTALL,
            "uninstall must verify ownership and default to preserving configuration")
    require("MAX_POLICY_ENTRIES = 256" in POLICY and
            "MAX_POLICY_BYTES" in POLICY and
            "duplicate policy state_index" in POLICY and
            "O_NOFOLLOW" in POLICY and
            "_safe_privileged_path_chain" in POLICY and
            "writable path component" in POLICY,
            "policy loading must use the kernel-sized bound, duplicate rejection, and bounded privileged path input")
    require("orchestra_ensure_private_dir" in PATH_SAFETY and
            "orchestra_safe_path_chain" in PATH_SAFETY and
            "sticky" in PATH_SAFETY,
            "build/install path checks must reject symlink and writable-parent attacks")
    require("Root-owned sticky directories such as /var/tmp" in CLI and
            "non-sticky writable" in CLI,
            "root runtime artifact checks must permit only kernel-protected sticky parents")
    require("BRIDGE_FALLBACK_SIGNAL_REPLAY" in ABI and
            "runtime_signal_generation" in BPF and
            "signal.sequence < runtime_signal_generation" in BPF and
            "state->signal_generation < previous_signal_generation" in BPF,
            "kernel signal consumption must reject sequence rollback and preserve newer state")
    require("publication_status" in BPF and
            "ctl->publication_status == BRIDGE_PUB_OK" in BPF and
            "restore_inactive_policy_bank" in BRIDGE and
            "--policy-abort" in BRIDGE,
            "failed publications must fail closed and staged policy writes must have an abort path")
    deferred_section = BPF[BPF.index("deferred_timerfn"):BPF.index("s32 BPF_STRUCT_OPS_SLEEPABLE(orchestra_sched_init)")]
    require("SCX_DSQ_GLOBAL" in deferred_section,
            "expired deferred tasks must have a global RUN promotion fallback")
    require("orchestra_controller_next_generation_v10" in CONTROLLER_V10 and
            "orchestra_controller_deadline_v10" in CONTROLLER_V10 and
            "controller->active_state = ORCHESTRA_CTRL_DISABLED" in CONTROLLER_V10,
            "controller generation/time overflow must fail closed")
    require("ExecStart=/usr/local/bin/orchestra run" in SYSTEMD and
            "ExecStop=/usr/local/bin/orchestra disable" in SYSTEMD,
            "the optional service must own a foreground scheduler lifecycle")
    require("self-hosted" in KERNEL_CI and
            "ORCHESTRA_KERNEL_SRC" in KERNEL_CI and
            "--strict" in KERNEL_CI,
            "kernel CI must be target-matched and explicit about the privileged host")
    print("PASS sched_ext source safety invariants")


if __name__ == "__main__":
    main()
