/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ORCHESTRA-OS coherent userspace <-> sched_ext bridge ABI v2.
 *
 * Despite the historical filename, this is ABI v2.  Values containing an
 * orchestra_map_lock must be accessed with BPF_F_LOCK from userspace and with
 * bpf_spin_lock() from BPF.  The lock is intentionally the first field and all
 * records use natural alignment; native packed structs are not a protocol.
 */
#ifndef ORCHESTRA_BRIDGE_V1_H
#define ORCHESTRA_BRIDGE_V1_H

#include "orchestra_abi.h"

#ifdef __BPF__
#include <vmlinux.h>
typedef __u32 uint32_t;
typedef __u64 uint64_t;
typedef __s32 int32_t;
#define UINT64_C(v) (v ## ULL)
#define ORCHESTRA_MAP_LOCK struct bpf_spin_lock
#else
#include <stddef.h>
#include <stdint.h>
struct orchestra_userspace_map_lock { uint32_t opaque; };
#define ORCHESTRA_MAP_LOCK struct orchestra_userspace_map_lock
#endif

#include "orchestra_kernel_v8.h"
#include "orchestra_control_abi.h"

/* Exact BPF object names. Linux BPF names are limited to 15 characters. */
#define BRIDGE_CTL_MAP_NAME       "orch_control"
#define BRIDGE_DIR_MAP_NAME       "orch_directives"
#define BRIDGE_ID_MAP_NAME        "orch_identity"
#define BRIDGE_TASK_MAP_NAME      "orch_task_state"
#define BRIDGE_TASK_TEL_MAP_NAME  "orch_task_tel"
#define BRIDGE_TEL_MAP_NAME       "orch_telemetry"
#define BRIDGE_DEFER_MAP_NAME     "orch_defer_tmr"
#define BRIDGE_SIGNAL_MAP_NAME    "orch_signal"

#define BRIDGE_MAX_TASKS          4096u
#define BRIDGE_DEFERRED_DSQ        UINT64_C(0x4f52434800000001)
#define BRIDGE_DEFER_SCAN_MAX      64u
#define BRIDGE_DEFER_TICK_NS       UINT64_C(250000)

#define BRIDGE_SLICE_MIN_NS        UINT64_C(100000)
#define BRIDGE_SLICE_RUN_NS        UINT64_C(5000000)
#define BRIDGE_SLICE_MAX_NS        UINT64_C(100000000)
#define BRIDGE_THROTTLE_MAX_NS     UINT64_C(1000000000)
#define BRIDGE_SLEEP_MAX_NS        UINT64_C(5000000000)
#define BRIDGE_EXPIRY_MAX_NS       UINT64_C(60000000000)
#define BRIDGE_LEASE_MAX_NS        UINT64_C(60000000000)
#define BRIDGE_STREAM_MAGIC         0x4f525153u /* "ORQS" */
#define BRIDGE_SIGNAL_SCALE         1000u
#define BRIDGE_SIGNAL_MAX_AGE_NS    UINT64_C(5000000000)

#define BRIDGE_SIGNAL_F_PREDICTION_VALID (1u << 0)
#define BRIDGE_SIGNAL_F_METRICS_VALID    (1u << 1)
#define BRIDGE_SIGNAL_F_CONTROLLER_VALID (1u << 2)

/* Stream records may carry one signal frame before their directive. */
#define BRIDGE_STREAM_F_PUBLISH_SIGNAL  (1u << 0)
#define BRIDGE_STREAM_F_REQUIRE_SIGNAL  (1u << 1)

/* A directive carrying this flag is fail-closed until a current signal frame
 * with the same scheduler/controller epoch is available. */
#define BRIDGE_DIRECTIVE_F_REQUIRE_SIGNAL (1u << 0)

#define BRIDGE_CAP_RUN              (1u << ORCHESTRA_ACTION_RUN)
#define BRIDGE_CAP_SLEEP            (1u << ORCHESTRA_ACTION_SLEEP)
#define BRIDGE_CAP_MIGRATE          (1u << ORCHESTRA_ACTION_MIGRATE)
#define BRIDGE_CAP_THROTTLE         (1u << ORCHESTRA_ACTION_THROTTLE)
#define BRIDGE_CAP_YIELD            (1u << ORCHESTRA_ACTION_YIELD)
#define BRIDGE_CAP_PER_TASK         (1u << 8)
#define BRIDGE_CAP_STRONG_IDENTITY  (1u << 9)
#define BRIDGE_CAP_DEFERRED_DSQ     (1u << 10)
#define BRIDGE_CAP_RUNTIME_BUDGET   (1u << 11)
#define BRIDGE_CAP_SIGNAL_FRAME     (1u << 12)
#define BRIDGE_CAP_SIGNAL_FRESHNESS (1u << 13)
#define BRIDGE_CAP_KERNEL_POLICY_LOOKUP ORCHESTRA_KERNEL_CAP_POLICY_LOOKUP
#define BRIDGE_CAP_KERNEL_STATE_DERIVED ORCHESTRA_KERNEL_CAP_STATE_DERIVED
#define BRIDGE_CAP_KERNEL_CONTROLLER_GATE ORCHESTRA_KERNEL_CAP_CONTROLLER_GATE
#define BRIDGE_CAP_KERNEL_ADAPTIVE_TASK_STATE ORCHESTRA_KERNEL_CAP_ADAPTIVE_TASK_STATE
#define BRIDGE_CAP_KERNEL_PREDICTION ORCHESTRA_KERNEL_CAP_PREDICTION_RECORD
#define BRIDGE_CAP_KERNEL_COORDINATION ORCHESTRA_KERNEL_CAP_COORDINATION
#define BRIDGE_CAP_KERNEL_POLICY_BANK ORCHESTRA_KERNEL_CAP_POLICY_BANK
#define BRIDGE_CAP_KERNEL_ACTION_FALLBACK ORCHESTRA_KERNEL_CAP_ACTION_FALLBACK
#define BRIDGE_CAP_KERNEL_ADAPTIVE_SLICE ORCHESTRA_KERNEL_CAP_ADAPTIVE_SLICE
#define BRIDGE_CAP_KERNEL_STATE_CPU_SELECTION \
    ORCHESTRA_KERNEL_CAP_STATE_CPU_SELECTION
#define BRIDGE_CAP_KERNEL_SLEEP_DEFER_COMPAT \
    ORCHESTRA_KERNEL_CAP_SLEEP_DEFER_COMPAT
#define BRIDGE_CAP_KERNEL_THROTTLE_DEFER_COMPAT \
    ORCHESTRA_KERNEL_CAP_THROTTLE_DEFER_COMPAT
#define BRIDGE_LEGACY_REQUIRED_CAPS \
    (BRIDGE_CAP_RUN | BRIDGE_CAP_SLEEP | BRIDGE_CAP_MIGRATE | \
     BRIDGE_CAP_THROTTLE | BRIDGE_CAP_YIELD | BRIDGE_CAP_PER_TASK | \
     BRIDGE_CAP_STRONG_IDENTITY | BRIDGE_CAP_DEFERRED_DSQ | \
     BRIDGE_CAP_RUNTIME_BUDGET | BRIDGE_CAP_SIGNAL_FRAME | \
     BRIDGE_CAP_SIGNAL_FRESHNESS)
#define BRIDGE_V8_REQUIRED_CAPS \
    (BRIDGE_CAP_KERNEL_POLICY_LOOKUP | \
     BRIDGE_CAP_KERNEL_STATE_DERIVED | BRIDGE_CAP_KERNEL_CONTROLLER_GATE | \
     BRIDGE_CAP_KERNEL_ADAPTIVE_TASK_STATE | BRIDGE_CAP_KERNEL_PREDICTION | \
     BRIDGE_CAP_KERNEL_COORDINATION | BRIDGE_CAP_KERNEL_POLICY_BANK | \
     BRIDGE_CAP_KERNEL_ACTION_FALLBACK | BRIDGE_CAP_KERNEL_ADAPTIVE_SLICE | \
     BRIDGE_CAP_KERNEL_STATE_CPU_SELECTION | \
     BRIDGE_CAP_KERNEL_SLEEP_DEFER_COMPAT | \
     BRIDGE_CAP_KERNEL_THROTTLE_DEFER_COMPAT)
#define BRIDGE_KERNEL_REQUIRED_CAPS \
    (BRIDGE_LEGACY_REQUIRED_CAPS | BRIDGE_V8_REQUIRED_CAPS)
/* Existing v1/v2 bridge clients continue to validate against this name. */
#define BRIDGE_REQUIRED_CAPS BRIDGE_LEGACY_REQUIRED_CAPS

enum bridge_publication_status {
    BRIDGE_PUB_OK = 0,
    BRIDGE_PUB_MAP_ERROR = 1,
    BRIDGE_PUB_READBACK_FAIL = 2,
    BRIDGE_PUB_GENERATION_OVERFLOW = 3,
    BRIDGE_PUB_IDENTITY_UNKNOWN = 4,
    BRIDGE_PUB_MAP_FULL = 5
};

enum bridge_fallback_reason {
    BRIDGE_FALLBACK_NONE = 0,
    BRIDGE_FALLBACK_NO_DIRECTIVE = 1,
    BRIDGE_FALLBACK_BAD_ABI = 2,
    BRIDGE_FALLBACK_BAD_IDENTITY = 3,
    BRIDGE_FALLBACK_EXPIRED = 4,
    BRIDGE_FALLBACK_STALE_LEASE = 5,
    BRIDGE_FALLBACK_BAD_ACTION = 6,
    BRIDGE_FALLBACK_CONTROLLER = 7,
    BRIDGE_FALLBACK_BAD_CPU = 8,
    BRIDGE_FALLBACK_BAD_PARAMETERS = 9,
    BRIDGE_FALLBACK_MAP_ERROR = 10,
    BRIDGE_FALLBACK_UNSTABLE_PUBLICATION = 11,
    BRIDGE_FALLBACK_SIGNAL_INVALID = 12,
    BRIDGE_FALLBACK_SIGNAL_STALE = 13,
    BRIDGE_FALLBACK_POLICY_MISSING = 14,
    BRIDGE_FALLBACK_POLICY_GENERATION = 15,
    BRIDGE_FALLBACK_UNSUPPORTED_ACTION = 16,
    BRIDGE_FALLBACK_CONTROLLER_OVERRIDE = 17,
    BRIDGE_FALLBACK_PROGRESS_GUARD = 18,
    /* A validly shaped frame older than the last accepted sequence. */
    BRIDGE_FALLBACK_SIGNAL_REPLAY = 19
};

struct orchestra_task_identity {
    uint32_t tgid;
    uint32_t tid;
    uint64_t start_boottime_ns;
};

/* A pinned userspace reference to this map value keeps its BPF timer alive. */
struct bridge_defer_timer {
#ifdef __BPF__
    struct bpf_timer timer;
#else
    uint64_t opaque[2];
#endif
};

struct orchestra_pid_key {
    uint32_t tgid;
    uint32_t tid;
};

struct bridge_identity_record {
    uint64_t start_boottime_ns;
    uint64_t scheduler_epoch;
};

/*
 * ARRAY[1] fixed-point signal frame.  The frame is deliberately bounded and
 * contains no pointers, floating point, or variable-length data.  Values
 * ending in _permille are in [0, BRIDGE_SIGNAL_SCALE].  The bridge validates
 * the frame before publication; BPF validates schema, epoch, freshness,
 * sequence, bounds, and controller coherence before a directive may require
 * it.  This is a trusted local map transport, not a kernel HMAC verifier.
 */
struct bridge_signal_frame {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t flags;
    uint32_t tier;
    uint32_t source_id;
    uint64_t scheduler_epoch;
    uint64_t sequence;
    uint64_t published_ns;
    uint64_t expires_ns;
    uint32_t key_epoch;
    uint32_t directive;
    uint32_t state_schema_version;
    uint32_t prediction_used;
    uint32_t confidence_permille;
    uint32_t cpu_now_permille;
    uint32_t cpu_pred_permille;
    uint32_t decision_cpu_permille;
    uint32_t memory_pressure_permille;
    uint32_t thermal_permille;
    uint32_t s1_permille;
    uint32_t s2_permille;
    uint32_t s3_permille;
    uint32_t s4_permille;
    uint32_t q_permille;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint64_t policy_generation;
    uint64_t reserved;
};

/* ARRAY[1]. scheduler_epoch never changes for one attached BPF instance. */
struct bridge_control {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t capability_flags;
    uint64_t scheduler_epoch;
    uint64_t last_generation;
    uint64_t publisher_heartbeat_ns;
    uint64_t publisher_lease_ns;
    uint64_t policy_generation;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint32_t publication_status;
    uint32_t scx_api_version;
};

/* HASH keyed by orchestra_task_identity. One coherent value per task. */
struct bridge_directive {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t flags;
    uint64_t scheduler_epoch;
    uint64_t generation;
    struct orchestra_task_identity identity;
    uint32_t action;
    uint32_t target_cpu;
    uint64_t slice_ns;
    uint64_t not_before_ns;
    uint64_t throttle_period_ns;
    uint64_t throttle_budget_ns;
    uint64_t expiry_ns;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint64_t policy_generation;
};

/* HASH keyed by orchestra_task_identity; deleted in ops.disable(). */
struct bridge_task_state {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t reserved_lock_pad;
    uint64_t generation;
    uint64_t period_start_ns;
    uint64_t runtime_used_ns;
    uint64_t running_since_ns;
    uint64_t eligible_ns;
    uint64_t last_enqueue_ns;
    uint32_t action;
    uint32_t requested_cpu;
    uint32_t dispatched_cpu;
    uint32_t flags;
};

#define BRIDGE_TASK_F_DEFERRED       (1u << 0)
#define BRIDGE_TASK_F_RUNNING        (1u << 1)
#define BRIDGE_TASK_F_THROTTLED      (1u << 2)

/* Correlated last outcome, keyed by the same lifetime-stable identity. */
struct bridge_task_telemetry {
    ORCHESTRA_MAP_LOCK lock;
    uint32_t reserved_lock_pad;
    uint64_t generation;
    uint64_t accepted_ns;
    uint64_t dispatched_ns;
    uint64_t running_ns;
    uint64_t stopped_ns;
    uint64_t runtime_ns;
    uint32_t action;
    uint32_t requested_cpu;
    int32_t dispatched_cpu;
    int32_t actual_cpu;
    uint32_t accepted_count;
    uint32_t dispatched_count;
    uint32_t running_count;
    uint32_t effective_count;
    uint32_t fallback_count;
    uint32_t error_count;
    uint32_t fallback_reason;
    uint32_t reserved;
};

/* Global aggregate telemetry. Names describe the event actually observed. */
struct bridge_telemetry {
    uint64_t load_count;
    uint64_t unload_count;
    uint64_t task_enable_count;
    uint64_t task_disable_count;
    uint64_t select_cpu_count;
    uint64_t enqueue_callback_count;
    uint64_t placement_count;
    uint64_t select_cpu_direct_insert_count;
    uint64_t dispatch_callback_count;
    uint64_t accepted_directive_count;
    uint64_t dispatched_action_count;
    uint64_t running_count;
    uint64_t stopping_count;
    uint64_t fallback_count;
    uint64_t invalid_action_count;
    uint64_t invalid_identity_count;
    uint64_t invalid_cpu_count;
    uint64_t expired_directive_count;
    uint64_t stale_lease_count;
    uint64_t map_error_count;
    uint64_t deferred_count;
    uint64_t deferred_release_count;
    uint64_t run_dispatched_count;
    uint64_t yield_dispatched_count;
    uint64_t migrate_accepted_count;
    uint64_t migrate_dispatched_count;
    uint64_t migrate_running_target_count;
    uint64_t migrate_running_other_count;
    uint64_t throttle_accepted_count;
    uint64_t throttle_deferred_count;
    uint64_t sleep_accepted_count;
    uint64_t sleep_deferred_count;
    uint64_t scheduler_error_count;
    uint64_t deferred_timer_tick_count;
    uint64_t deferred_timer_scanned_count;
    uint64_t deferred_timer_future_count;
    uint64_t deferred_release_failure_count;
    uint64_t deferred_cpu_failure_count;
    uint64_t signal_accepted_count;
    uint64_t signal_invalid_count;
    uint64_t signal_stale_count;
};

/* Fixed records for the canonical engine -> privileged bridge stream. */
struct bridge_stream_request {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    uint32_t action;
    uint64_t sequence;
    uint32_t target_tid;
    uint32_t target_cpu;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint64_t policy_generation;
    uint64_t slice_ns;
    uint64_t not_before_ns;
    uint64_t throttle_period_ns;
    uint64_t throttle_budget_ns;
    uint64_t expiry_duration_ns;
    uint64_t signal_sequence;
    uint64_t signal_max_age_ns;
    uint32_t stream_flags;
    uint32_t signal_tier;
    uint32_t signal_source_id;
    uint32_t signal_key_epoch;
    uint32_t signal_directive;
    uint32_t signal_state_schema_version;
    uint32_t signal_prediction_used;
    uint32_t signal_confidence_permille;
    uint32_t signal_cpu_now_permille;
    uint32_t signal_cpu_pred_permille;
    uint32_t signal_decision_cpu_permille;
    uint32_t signal_memory_pressure_permille;
    uint32_t signal_thermal_permille;
    uint32_t signal_s1_permille;
    uint32_t signal_s2_permille;
    uint32_t signal_s3_permille;
    uint32_t signal_s4_permille;
    uint32_t signal_q_permille;
};

struct bridge_stream_response {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t value_size;
    int32_t status;
    uint64_t sequence;
    uint64_t generation;
};

#ifndef __BPF__
_Static_assert(sizeof(struct orchestra_task_identity) == 16,
               "task identity ABI drift");
_Static_assert(sizeof(struct orchestra_pid_key) == 8,
               "PID key ABI drift");
_Static_assert(sizeof(struct bridge_identity_record) == 16,
               "identity record ABI drift");
_Static_assert(sizeof(struct bridge_signal_frame) == 152,
               "signal frame ABI drift");
_Static_assert(sizeof(struct bridge_control) == 80,
               "control ABI drift");
_Static_assert(sizeof(struct bridge_directive) == 112,
               "directive ABI drift");
_Static_assert(sizeof(struct bridge_task_state) == 72,
               "task state ABI drift");
_Static_assert(sizeof(struct bridge_task_telemetry) == 104,
               "task telemetry ABI drift");
_Static_assert(sizeof(struct bridge_telemetry) == 328,
               "global telemetry ABI drift");
_Static_assert(sizeof(struct bridge_defer_timer) == 16,
               "deferred timer map ABI drift");
_Static_assert(sizeof(struct bridge_stream_request) == 176,
               "stream request ABI drift");
_Static_assert(sizeof(struct bridge_stream_response) == 32,
               "stream response ABI drift");
_Static_assert(offsetof(struct bridge_control, scheduler_epoch) % 8 == 0,
               "control u64 alignment drift");
_Static_assert(offsetof(struct bridge_directive, scheduler_epoch) % 8 == 0,
               "directive u64 alignment drift");
_Static_assert(sizeof(((struct bridge_directive *)0)->action) == 4,
               "action wire width drift");
_Static_assert(ORCHESTRA_ACTION_RUN == 0 && ORCHESTRA_ACTION_SLEEP == 1 &&
               ORCHESTRA_ACTION_MIGRATE == 2 && ORCHESTRA_ACTION_THROTTLE == 3 &&
               ORCHESTRA_ACTION_YIELD == 4,
               "canonical action ABI drift");
#endif

#endif /* ORCHESTRA_BRIDGE_V1_H */
