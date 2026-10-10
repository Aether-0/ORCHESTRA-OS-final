/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ORCHESTRA-OS kernel-resident adaptive scheduling contract, ABI v8.
 *
 * This header is deliberately separate from orchestra_bridge_abi_v2.h.  The
 * bridge records remain stable for v6/v7 compatibility; v8 adds bounded
 * kernel-owned state and a generation-published policy bank.  Every record is
 * fixed-width, contains an explicit value size/schema, and is safe to carry
 * through a BPF map without pointers or variable-length data.
 */
#ifndef ORCHESTRA_KERNEL_V8_H
#define ORCHESTRA_KERNEL_V8_H

#define ORCHESTRA_KERNEL_ABI_VERSION             8u
#define ORCHESTRA_KERNEL_STATE_SCHEMA_VERSION    8u
#define ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION   8u
#define ORCHESTRA_KERNEL_PREDICTION_SCHEMA_VERSION 8u
#define ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION 8u
#define ORCHESTRA_KERNEL_TASK_SCHEMA_VERSION     8u
#define ORCHESTRA_KERNEL_TELEMETRY_SCHEMA_VERSION 8u

#define ORCHESTRA_KERNEL_MAX_POLICY_STATES       256u
#define ORCHESTRA_KERNEL_POLICY_BANK_COUNT       2u
#define ORCHESTRA_KERNEL_POLICY_ENTRY_COUNT \
    (ORCHESTRA_KERNEL_MAX_POLICY_STATES * ORCHESTRA_KERNEL_POLICY_BANK_COUNT)

/* Names are kept within Linux's BPF_OBJ_NAME_LEN - 1 limit. */
#define BRIDGE_RUNTIME_V8_MAP_NAME "orch_runtime_v8"
#define BRIDGE_POLICY_META_V8_MAP_NAME "orch_meta_v8"
#define BRIDGE_POLICY_ENTRY_V8_MAP_NAME "orch_entry_v8"
#define BRIDGE_TASK_V8_MAP_NAME "orch_task_v8"
#define BRIDGE_DIAG_V8_MAP_NAME "orch_diag_v8"
#define BRIDGE_TEL_V8_MAP_NAME "orch_tel_v8"

#define ORCHESTRA_V8_FIXED_POINT_SCALE 1000u
#define ORCHESTRA_V8_DEFAULT_SLICE_NS UINT64_C(5000000)
#define ORCHESTRA_V8_DEFAULT_THROTTLE_PERIOD_NS UINT64_C(10000000)
#define ORCHESTRA_V8_DEFAULT_THROTTLE_BUDGET_NS UINT64_C(2000000)

/* Runtime state flags. */
#define ORCHESTRA_RUNTIME_V8_F_SIGNAL_VALID       (1u << 0)
#define ORCHESTRA_RUNTIME_V8_F_PREDICTION_VALID   (1u << 1)
#define ORCHESTRA_RUNTIME_V8_F_PREDICTION_FALLBACK (1u << 2)
#define ORCHESTRA_RUNTIME_V8_F_COORDINATION_VALID (1u << 3)
#define ORCHESTRA_RUNTIME_V8_F_CONTROLLER_VALID   (1u << 4)
#define ORCHESTRA_RUNTIME_V8_F_POLICY_VALID       (1u << 5)

/* Policy records are written to the inactive bank before the meta flip. */
#define ORCHESTRA_POLICY_V8_F_VALID               (1u << 0)
#define ORCHESTRA_POLICY_V8_F_TARGET_CPU          (1u << 1)
#define ORCHESTRA_POLICY_V8_F_DEFERRED            (1u << 2)
#define ORCHESTRA_POLICY_V8_F_THROTTLE_BUDGET    (1u << 3)

#define ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID  (1u << 0)
#define ORCHESTRA_POLICY_META_V8_F_TRAIN        (1u << 1)
#define ORCHESTRA_POLICY_META_V8_F_ADAPT        (1u << 2)
#define ORCHESTRA_POLICY_META_V8_F_EVALUATE     (1u << 3)
#define ORCHESTRA_POLICY_META_V8_F_ROLLBACK     (1u << 4)
#define ORCHESTRA_POLICY_META_V8_F_RECOVERY     (1u << 5)

/* Capability negotiation is distinct from action validity. */
#define ORCHESTRA_KERNEL_CAP_POLICY_LOOKUP        (1u << 16)
#define ORCHESTRA_KERNEL_CAP_STATE_DERIVED       (1u << 17)
#define ORCHESTRA_KERNEL_CAP_CONTROLLER_GATE     (1u << 18)
#define ORCHESTRA_KERNEL_CAP_ADAPTIVE_TASK_STATE (1u << 19)
#define ORCHESTRA_KERNEL_CAP_PREDICTION_RECORD   (1u << 20)
#define ORCHESTRA_KERNEL_CAP_COORDINATION        (1u << 21)
#define ORCHESTRA_KERNEL_CAP_POLICY_BANK         (1u << 22)
#define ORCHESTRA_KERNEL_CAP_ACTION_FALLBACK     (1u << 23)
#define ORCHESTRA_KERNEL_CAP_ADAPTIVE_SLICE      (1u << 24)
#define ORCHESTRA_KERNEL_CAP_STATE_CPU_SELECTION (1u << 25)
#define ORCHESTRA_KERNEL_CAP_SLEEP_DEFER_COMPAT  (1u << 26)
#define ORCHESTRA_KERNEL_CAP_THROTTLE_DEFER_COMPAT (1u << 27)

#define ORCHESTRA_KERNEL_REQUIRED_CAPS \
    (ORCHESTRA_KERNEL_CAP_POLICY_LOOKUP | \
     ORCHESTRA_KERNEL_CAP_STATE_DERIVED | \
     ORCHESTRA_KERNEL_CAP_CONTROLLER_GATE | \
     ORCHESTRA_KERNEL_CAP_ADAPTIVE_TASK_STATE | \
     ORCHESTRA_KERNEL_CAP_PREDICTION_RECORD | \
     ORCHESTRA_KERNEL_CAP_COORDINATION | \
     ORCHESTRA_KERNEL_CAP_POLICY_BANK | \
     ORCHESTRA_KERNEL_CAP_ACTION_FALLBACK | \
     ORCHESTRA_KERNEL_CAP_ADAPTIVE_SLICE | \
     ORCHESTRA_KERNEL_CAP_STATE_CPU_SELECTION | \
     ORCHESTRA_KERNEL_CAP_SLEEP_DEFER_COMPAT | \
     ORCHESTRA_KERNEL_CAP_THROTTLE_DEFER_COMPAT)

enum orchestra_migrate_outcome_v8 {
    ORCHESTRA_MIGRATE_V8_NONE = 0,
    ORCHESTRA_MIGRATE_V8_SELECTED = 1,
    ORCHESTRA_MIGRATE_V8_ALREADY_LOCAL = 2,
    ORCHESTRA_MIGRATE_V8_AFFINITY_REJECT = 3,
    ORCHESTRA_MIGRATE_V8_CPU_OFFLINE = 4,
    ORCHESTRA_MIGRATE_V8_NO_TARGET = 5,
    ORCHESTRA_MIGRATE_V8_FALLBACK = 6
};

/* The state index is intentionally a bounded 8-bit fixed-point projection. */
struct orchestra_runtime_state_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t state_schema_version;
    uint32_t flags;
    uint32_t cpu_now_permille;
    uint32_t cpu_pred_permille;
    uint32_t queue_pressure_permille;
    uint32_t memory_pressure_permille;
    uint32_t thermal_permille;
    uint32_t prediction_confidence_permille;
    uint32_t observed_cpu_permille;
    uint32_t predicted_cpu_permille;
    uint32_t prediction_fallback_reason;
    uint64_t scheduler_epoch;
    uint64_t state_generation;
    uint64_t signal_generation;
    uint64_t prediction_generation;
    uint64_t prediction_published_ns;
    uint64_t prediction_expires_ns;
    uint32_t prediction_model_version;
    uint32_t state_index;
    uint32_t s1_permille;
    uint32_t s2_permille;
    uint32_t s3_permille;
    uint32_t s4_permille;
    uint32_t q_permille;
    uint64_t coordination_generation;
    uint32_t deficit_class;
    uint32_t controller_state;
    uint32_t controller_schema_version;
    uint64_t controller_generation;
    uint32_t policy_mode;
    uint32_t policy_schema_version;
    uint64_t policy_generation;
    uint64_t published_ns;
    uint64_t expires_ns;
};

struct orchestra_policy_meta_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t policy_schema_version;
    uint32_t active_bank;
    uint32_t entry_count;
    uint32_t policy_mode;
    uint32_t controller_state;
    uint32_t controller_schema_version;
    uint32_t flags;
    uint64_t scheduler_epoch;
    uint64_t policy_generation;
    uint64_t previous_generation;
    uint64_t published_ns;
    uint32_t capability_flags;
    uint32_t reserved;
};

struct orchestra_policy_entry_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t policy_schema_version;
    uint32_t state_index;
    uint32_t action;
    uint32_t flags;
    uint32_t controller_state;
    uint32_t capability_mask;
    uint32_t target_cpu;
    uint32_t reserved;
    uint64_t slice_ns;
    uint64_t not_before_ns;
    uint64_t throttle_period_ns;
    uint64_t throttle_budget_ns;
    uint64_t policy_generation;
};

/* Hot scheduling fields are kept separate from diagnostic counters. */
struct orchestra_task_hot_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t task_schema_version;
    uint32_t tgid;
    uint32_t tid;
    uint64_t start_boottime_ns;
    uint64_t scheduler_epoch;
    uint64_t signal_generation;
    uint64_t policy_generation;
    uint64_t controller_generation;
    uint32_t policy_index;
    uint32_t current_action;
    uint32_t previous_action;
    uint32_t controller_override_action;
    uint32_t target_cpu;
    uint32_t flags;
    uint64_t slice_ns;
    uint64_t throttle_deadline_ns;
    uint64_t sleep_deadline_ns;
    uint64_t last_enqueue_ns;
    uint64_t last_running_ns;
    uint64_t running_since_ns;
    uint64_t state_transition_ns;
};

#define ORCHESTRA_TASK_V8_F_ENQUEUED       (1u << 0)
#define ORCHESTRA_TASK_V8_F_RUNNING       (1u << 1)
#define ORCHESTRA_TASK_V8_F_DEFERRED      (1u << 2)
#define ORCHESTRA_TASK_V8_F_THROTTLED     (1u << 3)
#define ORCHESTRA_TASK_V8_F_FALLBACK      (1u << 4)
#define ORCHESTRA_TASK_V8_F_CONTROLLER_OVERRIDE (1u << 5)
#define ORCHESTRA_TASK_V8_F_PROGRESS_GUARD (1u << 6)
#define ORCHESTRA_TASK_V8_F_POLICY_CACHE_VALID (1u << 7)

struct orchestra_task_diag_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t task_schema_version;
    uint64_t action_transition_count;
    uint64_t enqueue_count;
    uint64_t running_count;
    uint64_t migration_request_count;
    uint64_t migration_success_count;
    uint64_t migration_fallback_count;
    uint64_t yield_count;
    uint64_t repeated_yield_guard_count;
    uint64_t throttle_request_count;
    uint64_t throttle_defer_count;
    uint64_t sleep_request_count;
    uint64_t sleep_defer_count;
    uint64_t controller_override_count;
    uint64_t fallback_count;
    uint64_t invalid_policy_count;
    uint64_t unsupported_action_count;
    uint32_t last_fallback_reason;
    uint32_t last_migration_outcome;
    uint32_t last_error;
};

struct orchestra_telemetry_v8 {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t telemetry_schema_version;
    uint64_t policy_lookup_count;
    uint64_t policy_cache_hit_count;
    uint64_t policy_cache_miss_count;
    uint64_t policy_generation_mismatch_count;
    uint64_t state_generation_change_count;
    uint64_t policy_generation_change_count;
    uint64_t signal_generation_change_count;
    uint64_t invalid_policy_index_count;
    uint64_t invalid_policy_action_count;
    uint64_t unsupported_action_count;
    uint64_t policy_fallback_count;
    uint64_t controller_override_count;
    uint64_t controller_rollback_count;
    uint64_t controller_recovery_count;
    uint64_t prediction_fallback_count;
    uint64_t signal_fallback_count;
    uint64_t action_requested_count[ORCHESTRA_ACTION_COUNT];
    uint64_t action_executed_count[ORCHESTRA_ACTION_COUNT];
    uint64_t action_fallback_count[ORCHESTRA_ACTION_COUNT];
    uint64_t task_state_create_count;
    uint64_t task_state_update_count;
    uint64_t telemetry_drop_count;
    uint64_t policy_commit_count;
    uint64_t policy_rollback_count;
    uint64_t lifecycle_train_count;
    uint64_t lifecycle_adapt_count;
    uint64_t lifecycle_evaluate_count;
};

#ifndef __BPF__
_Static_assert(sizeof(struct orchestra_runtime_state_v8) == 208,
               "v8 runtime state ABI drift");
_Static_assert(sizeof(struct orchestra_policy_meta_v8) == 88,
               "v8 policy meta ABI drift");
_Static_assert(sizeof(struct orchestra_policy_entry_v8) == 88,
               "v8 policy entry ABI drift");
_Static_assert(sizeof(struct orchestra_task_hot_v8) == 152,
               "v8 hot task ABI drift");
_Static_assert(sizeof(struct orchestra_task_diag_v8) == 168,
               "v8 diagnostic task ABI drift");
_Static_assert(sizeof(struct orchestra_telemetry_v8) == 336,
               "v8 telemetry ABI drift");
_Static_assert(ORCHESTRA_KERNEL_MAX_POLICY_STATES <= 256u,
               "v8 policy state index must remain bounded");
#endif

#endif /* ORCHESTRA_KERNEL_V8_H */
