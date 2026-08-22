/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ORCHESTRA-OS native coordination and controller ABI, version 10.
 *
 * The v8 records remain the compatibility contract used by existing bridge
 * clients.  v10 is additive: it gives the kernel a bounded coordination
 * window, a generation-safe controller publication record, and explicit
 * telemetry for the closed feedback loop.
 */
#ifndef ORCHESTRA_CONTROL_ABI_H
#define ORCHESTRA_CONTROL_ABI_H

#ifndef __BPF__
#include <stdint.h>
#else
#include <vmlinux.h>
#ifndef UINT64_C
#define UINT64_C(v) (v ## ULL)
#endif
#ifndef UINT32_C
#define UINT32_C(v) (v ## U)
#endif
#endif

#ifndef ORCHESTRA_MAP_LOCK
#ifdef __BPF__
#define ORCHESTRA_MAP_LOCK struct bpf_spin_lock
#else
struct orchestra_userspace_map_lock { uint32_t opaque; };
#define ORCHESTRA_MAP_LOCK struct orchestra_userspace_map_lock
#endif
#endif

#define ORCHESTRA_CONTROL_ABI_VERSION             10u
#define ORCHESTRA_COORD_SCHEMA_VERSION            10u
#define ORCHESTRA_CONTROLLER_SCHEMA_VERSION      10u
#define ORCHESTRA_RUNTIME_SCHEMA_VERSION         10u
#define ORCHESTRA_CONTROLLER_TELEMETRY_VERSION   10u

#define ORCHESTRA_V10_FIXED_POINT_SCALE          1000u
#define ORCHESTRA_COORD_DEFAULT_WINDOW_NS        UINT64_C(10000000)
#define ORCHESTRA_CONTROLLER_DEFAULT_PERIOD_NS   UINT64_C(100000000)
#define ORCHESTRA_CONTROLLER_MIN_HOLD_NS         UINT64_C(30000000)
#define ORCHESTRA_CONTROLLER_COOLDOWN_NS         UINT64_C(200000000)
#define ORCHESTRA_CONTROLLER_EVALUATION_NS       UINT64_C(200000000)
#define ORCHESTRA_COORD_MASS_SWITCH_WINDOW_NS    UINT64_C(2000000)
#define ORCHESTRA_COORD_MASS_SWITCH_THRESHOLD    8u
#define ORCHESTRA_COORD_STATE_CLASS_COUNT        8u

/* The arrays are deliberately bounded.  A machine with more CPU/NUMA
 * domains is represented by its bounded global/domain aggregate rather than
 * forcing an unbounded map allocation. */
#define ORCHESTRA_COORD_MAX_CPU_DOMAINS          1024u
#define ORCHESTRA_COORD_MAX_NUMA_DOMAINS         64u
#define ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT       \
    (ORCHESTRA_COORD_MAX_CPU_DOMAINS + ORCHESTRA_COORD_MAX_NUMA_DOMAINS)
#define ORCHESTRA_COORD_DOMAIN_SLOT_COUNT        \
    (ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT + 1u)
#define ORCHESTRA_COORD_WINDOW_BANK_COUNT        2u
#define ORCHESTRA_COORD_MAP_ENTRY_COUNT          \
    (ORCHESTRA_COORD_DOMAIN_SLOT_COUNT * ORCHESTRA_COORD_WINDOW_BANK_COUNT)

#define BRIDGE_COORD_V10_MAP_NAME                "orch_coord_v10"
#define BRIDGE_COORD_CPU_V10_MAP_NAME            "orch_coord_cpu"
#define BRIDGE_CONTROLLER_V10_MAP_NAME           "orch_ctrl_v10"
#define BRIDGE_CONTROLLER_TEL_V10_MAP_NAME       "orch_ctrl_tel"
#define BRIDGE_RUNTIME_V10_MAP_NAME              "orch_runtime10"
#define BRIDGE_TASK_COORD_V10_MAP_NAME           "orch_task_coord"

#define ORCHESTRA_KERNEL_CAP_NATIVE_COORDINATION (1u << 28)
#define ORCHESTRA_KERNEL_CAP_RUNTIME_CONTROLLER   (1u << 29)
#define ORCHESTRA_KERNEL_CAP_HIERARCHICAL_COORD   (1u << 30)
#define ORCHESTRA_KERNEL_V10_REQUIRED_CAPS        \
    (ORCHESTRA_KERNEL_REQUIRED_CAPS |             \
     ORCHESTRA_KERNEL_CAP_NATIVE_COORDINATION |   \
     ORCHESTRA_KERNEL_CAP_RUNTIME_CONTROLLER |    \
     ORCHESTRA_KERNEL_CAP_HIERARCHICAL_COORD)

enum orchestra_coord_scope_v10 {
    ORCHESTRA_COORD_SCOPE_CPU = 0,
    ORCHESTRA_COORD_SCOPE_NUMA = 1,
    ORCHESTRA_COORD_SCOPE_GLOBAL = 2,
    ORCHESTRA_COORD_SCOPE_COUNT = 3
};

enum orchestra_deficit_class_v10 {
    ORCH_DEFICIT_NONE = 0,
    ORCH_DEFICIT_SIGNAL = 1,
    ORCH_DEFICIT_COMPLIANCE = 2,
    ORCH_DEFICIT_COHERENCE = 3,
    ORCH_DEFICIT_STABILITY = 4,
    ORCH_DEFICIT_MIXED = 5,
    ORCH_DEFICIT_COUNT = 6
};

enum orchestra_actuator_id_v10 {
    ORCH_ACTUATOR_JITTER = 0,
    ORCH_ACTUATOR_SWITCHING_PENALTY = 1,
    ORCH_ACTUATOR_CONSENSUS_BLEND = 2,
    ORCH_ACTUATOR_MIGRATION_THRESHOLD = 3,
    ORCH_ACTUATOR_YIELD_THRESHOLD = 4,
    ORCH_ACTUATOR_THROTTLE_DURATION = 5,
    ORCH_ACTUATOR_SLEEP_DEFER = 6,
    ORCH_ACTUATOR_PREDICTION_CONFIDENCE = 7,
    ORCH_ACTUATOR_PREDICTION_HORIZON = 8,
    ORCH_ACTUATOR_SIGNAL_CADENCE = 9,
    ORCH_ACTUATOR_COORDINATION_THRESHOLD = 10,
    ORCH_ACTUATOR_COUNT = 11
};

#define ORCHESTRA_COORD_F_VALID              (1u << 0)
#define ORCHESTRA_COORD_F_FINALIZED          (1u << 1)
#define ORCHESTRA_COORD_F_SIGNAL_FALLBACK   (1u << 2)
#define ORCHESTRA_COORD_F_MASS_SWITCH       (1u << 3)
#define ORCHESTRA_COORD_F_CONTROLLER_SEEN   (1u << 4)

#define ORCHESTRA_CONTROLLER_F_VALID         (1u << 0)
#define ORCHESTRA_CONTROLLER_F_STAGING      (1u << 1)
#define ORCHESTRA_CONTROLLER_F_EVALUATING   (1u << 2)
#define ORCHESTRA_CONTROLLER_F_COOLDOWN     (1u << 3)
#define ORCHESTRA_CONTROLLER_F_SATURATED    (1u << 4)
#define ORCHESTRA_CONTROLLER_F_ROLLBACK     (1u << 5)
#define ORCHESTRA_CONTROLLER_F_RECOVERING   (1u << 6)

#define ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED (1u << 0)
#define ORCHESTRA_CONTROLLER_ACTUATOR_F_UPDATED   (1u << 1)

struct orchestra_actuator_v10 {
    uint64_t minimum;
    uint64_t maximum;
    uint64_t default_value;
    uint64_t current_value;
    uint64_t previous_value;
    uint64_t rollback_value;
    uint64_t maximum_step;
    uint64_t generation;
    uint64_t last_update_epoch;
    uint32_t flags;
    uint32_t reserved;
};

/* Current-window aggregate.  Counters are monotonically updated by BPF
 * atomics; the summary fields are written while the record is locked at the
 * window boundary. */
struct orchestra_coordination_state_v10 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t schema_version;
    uint32_t scope;
    uint32_t domain_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t window_generation;
    uint64_t window_start_ns;
    uint64_t window_end_ns;
    uint64_t eligible_observations;
    uint64_t executed_observations;
    uint64_t prediction_observations;
    uint64_t prediction_valid_count;
    uint64_t prediction_stale_count;
    uint64_t prediction_invalid_count;
    uint64_t signal_freshness_sum;
    uint64_t prediction_confidence_sum;
    uint64_t prediction_error_sum;
    uint64_t generation_continuity_sum;
    uint64_t policy_action_count[ORCHESTRA_ACTION_COUNT];
    uint64_t controller_action_count[ORCHESTRA_ACTION_COUNT];
    uint64_t capability_action_count[ORCHESTRA_ACTION_COUNT];
    uint64_t actual_action_count[ORCHESTRA_ACTION_COUNT];
    uint64_t policy_compliance_count;
    uint64_t directive_compliance_count;
    uint64_t fallback_count;
    uint64_t controller_override_count;
    uint64_t capability_adjustment_count;
    uint64_t action_transition_count;
    uint64_t synchronized_mass_switch_count;
    uint64_t repeated_migration_count;
    uint64_t run_yield_oscillation_count;
    uint64_t throttle_cycle_count;
    uint64_t sleep_cycle_count;
    uint64_t policy_state_switch_count;
    uint64_t signal_generation_break_count;
    uint64_t last_signal_generation;
    uint64_t coherence_hist[ORCHESTRA_COORD_STATE_CLASS_COUNT]
                          [ORCHESTRA_ACTION_COUNT];
    uint32_t s1_permille;
    uint32_t s2_permille;
    uint32_t s3_permille;
    uint32_t s4_permille;
    uint32_t q_permille;
    uint32_t deficit_class;
    uint32_t primary_deficit;
    uint32_t secondary_deficit;
    uint32_t deficit_severity;
    uint32_t deficit_persistence;
    uint64_t finalized_ns;
};

/* A compact copy used when a finalized window is handed to the controller.
 * Keeping this separate avoids copying the counter-heavy map record on the
 * BPF stack. */
struct orchestra_coordination_metrics_v10 {
    uint32_t scope;
    uint32_t domain_id;
    uint64_t window_generation;
    uint64_t window_start_ns;
    uint64_t window_end_ns;
    uint64_t eligible_observations;
    uint64_t executed_observations;
    uint32_t s1_permille;
    uint32_t s2_permille;
    uint32_t s3_permille;
    uint32_t s4_permille;
    uint32_t q_permille;
    uint32_t deficit_class;
    uint32_t primary_deficit;
    uint32_t secondary_deficit;
    uint32_t deficit_severity;
    uint32_t deficit_persistence;
    uint64_t action_transition_count;
    uint64_t synchronized_mass_switch_count;
    uint64_t fallback_count;
    uint64_t controller_override_count;
    uint64_t capability_adjustment_count;
};

/* Per-CPU scratch is intentionally small.  It detects transitions and local
 * mass-switch bursts without walking the task population. */
struct orchestra_coord_cpu_v10 {
    uint64_t window_generation;
    uint64_t burst_start_ns;
    uint64_t last_transition_ns;
    uint64_t last_migration_ns;
    uint32_t burst_transition_count;
    uint32_t last_action;
    uint32_t last_policy_state;
    uint32_t last_migration_target;
    uint32_t reserved;
};

/* Lifetime-stable per-task coordination state. */
struct orchestra_task_coord_v10 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t schema_version;
    uint32_t tgid;
    uint32_t tid;
    uint64_t start_boottime_ns;
    uint64_t window_generation;
    uint32_t policy_state;
    uint32_t previous_policy_state;
    uint32_t policy_selected_action;
    uint32_t controller_adjusted_action;
    uint32_t capability_adjusted_action;
    uint32_t actual_executed_action;
    uint32_t previous_action;
    uint32_t current_action;
    uint32_t last_fallback_reason;
    uint32_t flags;
    uint64_t last_decision_ns;
    uint64_t last_execution_ns;
    uint64_t last_migration_ns;
    uint32_t last_migration_target;
    uint32_t reserved;
};

struct orchestra_controller_bank_v10 {
    uint32_t state;
    uint32_t primary_deficit;
    uint32_t secondary_deficit;
    uint32_t severity;
    uint32_t persistence;
    uint32_t flags;
    uint64_t generation;
    uint64_t last_window_generation;
    uint64_t last_q_permille;
    struct orchestra_actuator_v10 actuators[ORCH_ACTUATOR_COUNT];
};

/* One locked record contains active, staging, and previous-known-good banks.
 * Readers copy only the compact view below while holding the lock, so a
 * scheduler callback never observes a partially published bank. */
struct orchestra_controller_state_v10 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t active_state;
    uint32_t active_primary_deficit;
    uint32_t active_secondary_deficit;
    uint32_t active_deficit_class;
    uint32_t active_severity;
    uint32_t active_persistence;
    uint32_t reserved;
    uint64_t scheduler_epoch;
    uint64_t active_generation;
    uint64_t staging_generation;
    uint64_t previous_good_generation;
    uint64_t controller_epoch;
    uint64_t last_update_ns;
    uint64_t next_update_ns;
    uint64_t update_period_ns;
    uint64_t minimum_hold_ns;
    uint64_t cooldown_until_ns;
    uint64_t evaluation_until_ns;
    uint64_t evaluation_baseline_q_permille;
    uint64_t last_q_permille;
    uint64_t best_q_permille;
    uint64_t saturation_count;
    uint64_t rollback_count;
    uint64_t recovery_count;
    uint64_t override_count;
    uint64_t no_op_count;
    struct orchestra_controller_bank_v10 active;
    struct orchestra_controller_bank_v10 staging;
    struct orchestra_controller_bank_v10 previous_good;
};

struct orchestra_controller_view_v10 {
    uint32_t state;
    uint32_t primary_deficit;
    uint32_t secondary_deficit;
    uint32_t severity;
    uint32_t persistence;
    uint32_t flags;
    uint64_t generation;
    uint64_t window_generation;
    uint64_t jitter;
    uint64_t switching_penalty;
    uint64_t consensus_blend;
    uint64_t migration_threshold;
    uint64_t yield_threshold;
    uint64_t throttle_duration_ns;
    uint64_t sleep_defer_ns;
    uint64_t prediction_confidence_threshold;
    uint64_t prediction_horizon_ns;
    uint64_t signal_cadence_ns;
    uint64_t coordination_threshold;
};

struct orchestra_controller_telemetry_v10 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t schema_version;
    uint64_t update_count;
    uint64_t no_op_count;
    uint64_t saturation_count;
    uint64_t rollback_count;
    uint64_t recovery_count;
    uint64_t override_count;
    uint64_t disabled_count;
    uint64_t state_transition_count;
    uint64_t last_update_epoch;
    uint64_t last_window_generation;
    uint64_t last_q_permille;
    uint32_t last_state;
    uint32_t last_primary_deficit;
    uint32_t last_secondary_deficit;
    uint32_t last_actuator;
    uint32_t last_update_direction;
    uint32_t reserved;
    uint64_t actuator_update_count[ORCH_ACTUATOR_COUNT];
    uint64_t deficit_count[ORCH_DEFICIT_COUNT];
};

struct orchestra_runtime_state_v10 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t scope;
    uint32_t domain_id;
    uint64_t scheduler_epoch;
    uint64_t window_generation;
    uint64_t window_start_ns;
    uint64_t window_end_ns;
    uint64_t eligible_observations;
    uint64_t executed_observations;
    uint32_t s1_permille;
    uint32_t s2_permille;
    uint32_t s3_permille;
    uint32_t s4_permille;
    uint32_t q_permille;
    uint32_t deficit_class;
    uint32_t primary_deficit;
    uint32_t secondary_deficit;
    uint32_t deficit_severity;
    uint32_t deficit_persistence;
    uint32_t controller_state;
    uint32_t controller_flags;
    uint64_t controller_generation;
    uint64_t policy_generation;
    uint64_t signal_generation;
    uint64_t prediction_generation;
    uint64_t published_ns;
};

#ifndef __BPF__
_Static_assert(ORCHESTRA_COORD_SCOPE_COUNT == 3,
               "v10 coordination scopes must remain bounded");
_Static_assert(ORCH_ACTUATOR_COUNT == 11,
               "v10 actuator matrix drift");
_Static_assert(ORCH_DEFICIT_MIXED == 5,
               "v10 deficit ABI drift");
_Static_assert(sizeof(struct orchestra_coord_cpu_v10) == 56,
               "v10 per-CPU coordination ABI drift");
_Static_assert(sizeof(struct orchestra_task_coord_v10) == 120,
               "v10 task coordination ABI drift");
#endif

#endif /* ORCHESTRA_CONTROL_ABI_H */
