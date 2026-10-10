/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ORCHESTRA-OS kernel-resident adaptive scheduler, bridge ABI v2 + kernel ABI v8.
 *
 * Target API: Linux 7.0 sched_ext (dsq_insert / dsq_move). Wrappers below were
 * reviewed against v6.13+ kfunc rename: insert replaces dispatch, move
 * replaces dispatch_from_dsq. A compile-only token rename is still not a port.
 *
 * Safety model:
 *   - full switch avoids fair-class-over-ext starvation from partial mode;
 *   - every absent, unstable, stale or invalid directive dispatches as RUN;
 *   - directives are coherent BPF_F_LOCK snapshots keyed by exact task
 *     lifetime identity (TGID, TID, start_boottime);
 *   - SLEEP and exhausted THROTTLE budgets enter a deadline-ordered DSQ which
 *     is drained by a bounded timer callback;
 *   - YIELD enters the shared global queue tail;
 *   - MIGRATE is accepted only for a currently online, affinity-allowed CPU;
 *   - task/outcome records carry identity, generation, action and CPUs.
 */
#include <scx/common.bpf.h>
#include "../include/orchestra_bridge_abi_v2.h"
#include "../include/orchestra_task_accounting.h"

/*
 * tools/sched_ext/include/scx/enums.autogen.bpf.h replaces DSQ/kick/enq
 * constants with zeroed volatile ksyms filled only by SCX_OPS_LOAD(). This
 * loader uses generic libbpf, so restore the running kernel's vmlinux enum
 * values or enqueue hits DSQ 0x0 and the scheduler aborts.
 */
#undef SCX_DSQ_GLOBAL
#undef SCX_DSQ_LOCAL
#undef SCX_DSQ_LOCAL_ON
#undef SCX_DSQ_FLAG_BUILTIN
#undef SCX_KICK_IDLE
#undef SCX_ENQ_HEAD

char _license[] SEC("license") = "GPL";

#if ORCHESTRA_SCX_API_VERSION != 70012u
#error "Review DSQ insertion/struct_ops semantics before changing the sched_ext API target"
#endif

#define ORCHESTRA_U64_MAX (~(uint64_t)0)
#define ORCHESTRA_NOINLINE __attribute__((noinline))
#define orchestra_dsq_insert(p, dsq, slice, flags) \
    scx_bpf_dsq_insert((p), (dsq), (slice), (flags))
#define orchestra_dsq_insert_vtime(p, dsq, slice, vtime, flags) \
    scx_bpf_dsq_insert_vtime((p), (dsq), (slice), (vtime), (flags))
#define orchestra_dsq_move_from_dsq(it, p, dsq, flags) \
    scx_bpf_dsq_move((it), (p), (dsq), (flags))

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct bridge_control);
} orch_control SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct bridge_directive);
} orch_directives SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_pid_key);
    __type(value, struct bridge_identity_record);
} orch_identity SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct bridge_task_state);
} orch_task_state SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct bridge_task_telemetry);
} orch_task_tel SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct bridge_telemetry);
} orch_telemetry SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct bridge_defer_timer);
} orch_defer_tmr SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct bridge_signal_frame);
} orch_signal SEC(".maps");

/* ABI v8 kernel-resident adaptive scheduling maps.  These are additive to
 * the v2 bridge maps above so older bridge clients retain their exact value
 * sizes and map names. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_runtime_state_v8);
} orch_runtime_v8 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_policy_meta_v8);
} orch_meta_v8 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, ORCHESTRA_KERNEL_POLICY_ENTRY_COUNT);
    __type(key, uint32_t);
    __type(value, struct orchestra_policy_entry_v8);
} orch_entry_v8 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct orchestra_task_hot_v8);
} orch_task_v8 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct orchestra_task_diag_v8);
} orch_diag_v8 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_telemetry_v8);
} orch_tel_v8 SEC(".maps");

/* Native v10 coordination/controller maps.  The two-window coordination
 * array is bounded by CPU/NUMA/global domain slots; per-CPU scratch detects
 * transition bursts without scanning the task population. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, ORCHESTRA_COORD_MAP_ENTRY_COUNT);
    __type(key, uint32_t);
    __type(value, struct orchestra_coordination_state_v10);
} orch_coord_v10 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_coord_cpu_v10);
} orch_coord_cpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_controller_state_v10);
} orch_ctrl_v10 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_controller_telemetry_v10);
} orch_ctrl_tel SEC(".maps");

#define orch_ctrl_tel_v10 orch_ctrl_tel

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_runtime_state_v10);
} orch_runtime10 SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, BRIDGE_MAX_TASKS);
    __type(key, struct orchestra_task_identity);
    __type(value, struct orchestra_task_coord_v10);
} orch_task_coord SEC(".maps");

#include "../include/orchestra_coordination.h"
#include "../include/orchestra_controller.h"

struct directive_snapshot {
    uint32_t flags;
    uint64_t scheduler_epoch;
    uint64_t state_generation;
    uint64_t controller_generation;
    uint64_t generation;
    uint64_t slice_ns;
    uint64_t not_before_ns;
    uint64_t throttle_period_ns;
    uint64_t throttle_budget_ns;
    uint64_t expiry_ns;
    uint32_t action;
    uint32_t target_cpu;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint64_t policy_generation;
    uint32_t migration_outcome;
    uint32_t controller_override_action;
    uint32_t policy_selected_action;
    uint32_t controller_adjusted_action;
    uint32_t capability_adjusted_action;
    uint32_t actual_executed_action;
    uint32_t previous_policy_state;
};

/* Timer-only scratch: no scheduler callback shares this value. The timer
 * re-arms after returning, so its snapshot cannot overlap on one CPU. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct directive_snapshot);
} orch_defer_snap SEC(".maps");

struct signal_snapshot {
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
};

struct control_snapshot {
    uint64_t scheduler_epoch;
    uint64_t last_generation;
    uint64_t heartbeat_ns;
    uint64_t lease_ns;
    uint64_t policy_generation;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint32_t capabilities;
    uint32_t publication_status;
    uint32_t valid;
};

struct runtime_snapshot_v8 {
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
    uint64_t controller_generation;
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
    uint32_t policy_mode;
    uint32_t capability_flags;
    uint64_t policy_generation;
    uint64_t published_ns;
    uint64_t expires_ns;
};

struct policy_meta_snapshot_v8 {
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
    uint32_t valid;
};

struct decision_snapshot_v8 {
    struct directive_snapshot directive;
    uint32_t state_index;
    uint32_t capability_flags;
    uint32_t action_capability_mask;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint32_t source;
    uint32_t fallback_reason;
    uint32_t migration_outcome;
    uint32_t policy_selected_action;
    uint32_t controller_adjusted_action;
    uint32_t capability_adjusted_action;
    uint32_t previous_policy_state;
    uint32_t cpu_now_permille;
    uint32_t queue_pressure_permille;
    uint64_t controller_generation;
    uint32_t valid;
};

/* Non-sleepable callbacks run without migration. Each nested stage owns
 * distinct fields; the deferred timer has its own scratch map. */
struct orchestra_decision_work {
    struct decision_snapshot_v8 decision;
    struct directive_snapshot bridge;
    struct runtime_snapshot_v8 policy_state;
    struct policy_meta_snapshot_v8 meta, verify_meta;
    struct control_snapshot policy_control, verify_control;
    struct control_snapshot read_control;
    struct signal_snapshot read_signal;
    struct orchestra_controller_view_v10 controller;
    struct orchestra_coordination_metrics_v10 coordination;
};
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct orchestra_decision_work);
} orch_dec_work SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct runtime_snapshot_v8);
} orch_runtime_scratch SEC(".maps");

#define ORCHESTRA_DECISION_SOURCE_BRIDGE 1u
#define ORCHESTRA_DECISION_SOURCE_POLICY 2u

static __always_inline struct orchestra_telemetry_v8 *global_v8_tel(void)
{
    uint32_t key = 0;

    return bpf_map_lookup_elem(&orch_tel_v8, &key);
}

static __always_inline void tel8_inc(uint64_t *counter)
{
    if (counter)
        __sync_fetch_and_add(counter, 1);
}

static __always_inline void tel8_action_inc(uint64_t counters[ORCHESTRA_ACTION_COUNT],
                                             uint32_t action)
{
    if (action < ORCHESTRA_ACTION_COUNT)
        tel8_inc(&counters[action]);
}

static ORCHESTRA_NOINLINE struct orchestra_task_hot_v8 *get_task_hot_v8(
    const struct orchestra_task_identity *id, int create)
{
    struct orchestra_task_hot_v8 zero = {
        .magic = ORCHESTRA_ABI_MAGIC,
        .abi_version = ORCHESTRA_KERNEL_ABI_VERSION,
        .value_size = sizeof(struct orchestra_task_hot_v8),
        .task_schema_version = ORCHESTRA_KERNEL_TASK_SCHEMA_VERSION,
        .tgid = id->tgid,
        .tid = id->tid,
        .start_boottime_ns = id->start_boottime_ns,
        .target_cpu = ORCHESTRA_CPU_ANY,
        .current_action = ORCHESTRA_ACTION_RUN,
        .previous_action = ORCHESTRA_ACTION_RUN,
        .controller_override_action = ORCHESTRA_ACTION_RUN,
    };
    struct orchestra_task_hot_v8 *hot;

    hot = bpf_map_lookup_elem(&orch_task_v8, id);
    if (!hot && create) {
        if (bpf_map_update_elem(&orch_task_v8, id, &zero, BPF_NOEXIST) != 0)
            return NULL;
        hot = bpf_map_lookup_elem(&orch_task_v8, id);
        if (hot) {
            struct orchestra_telemetry_v8 *tel = global_v8_tel();
            if (tel)
                tel8_inc(&tel->task_state_create_count);
        }
    }
    return hot;
}

static ORCHESTRA_NOINLINE struct orchestra_task_diag_v8 *get_task_diag_v8(
    const struct orchestra_task_identity *id, int create)
{
    struct orchestra_task_diag_v8 zero = {
        .magic = ORCHESTRA_ABI_MAGIC,
        .abi_version = ORCHESTRA_KERNEL_ABI_VERSION,
        .value_size = sizeof(struct orchestra_task_diag_v8),
        .task_schema_version = ORCHESTRA_KERNEL_TASK_SCHEMA_VERSION,
    };
    struct orchestra_task_diag_v8 *diag;

    diag = bpf_map_lookup_elem(&orch_diag_v8, id);
    if (!diag && create) {
        if (bpf_map_update_elem(&orch_diag_v8, id, &zero, BPF_NOEXIST) != 0)
            return NULL;
        diag = bpf_map_lookup_elem(&orch_diag_v8, id);
    }
    return diag;
}

static ORCHESTRA_NOINLINE struct orchestra_task_coord_v10 *
get_task_coord_v10(const struct orchestra_task_identity *id, int create)
{
    struct orchestra_task_coord_v10 zero = {
        .magic = ORCHESTRA_ABI_MAGIC,
        .abi_version = ORCHESTRA_CONTROL_ABI_VERSION,
        .value_size = sizeof(struct orchestra_task_coord_v10),
        .schema_version = ORCHESTRA_COORD_SCHEMA_VERSION,
        .tgid = id->tgid,
        .tid = id->tid,
        .start_boottime_ns = id->start_boottime_ns,
        .policy_selected_action = ORCHESTRA_ACTION_RUN,
        .controller_adjusted_action = ORCHESTRA_ACTION_RUN,
        .capability_adjusted_action = ORCHESTRA_ACTION_RUN,
        .actual_executed_action = ORCHESTRA_ACTION_RUN,
        .previous_action = ORCHESTRA_ACTION_RUN,
        .current_action = ORCHESTRA_ACTION_RUN,
    };
    struct orchestra_task_coord_v10 *state;

    state = bpf_map_lookup_elem(&orch_task_coord, id);
    if (!state && create) {
        if (bpf_map_update_elem(&orch_task_coord, id, &zero,
                                BPF_NOEXIST) != 0)
            return NULL;
        state = bpf_map_lookup_elem(&orch_task_coord, id);
    }
    return state;
}

static __always_inline int cpu_is_online(uint32_t cpu);
static __always_inline int cpu_allowed_online(struct task_struct *p,
                                               uint32_t cpu);
static __always_inline uint64_t clamp_slice(uint64_t requested);

static __always_inline struct bridge_telemetry *global_tel(void)
{
    uint32_t key = 0;

    return bpf_map_lookup_elem(&orch_telemetry, &key);
}

static __always_inline int snapshot_signal(struct signal_snapshot *out)
{
    struct bridge_signal_frame *frame;
    uint32_t key = 0;

    frame = bpf_map_lookup_elem(&orch_signal, &key);
    if (!frame)
        return 0;
    bpf_spin_lock(&frame->lock);
    out->magic = frame->magic;
    out->abi_version = frame->abi_version;
    out->value_size = frame->value_size;
    out->flags = frame->flags;
    out->tier = frame->tier;
    out->source_id = frame->source_id;
    out->scheduler_epoch = frame->scheduler_epoch;
    out->sequence = frame->sequence;
    out->published_ns = frame->published_ns;
    out->expires_ns = frame->expires_ns;
    out->key_epoch = frame->key_epoch;
    out->directive = frame->directive;
    out->state_schema_version = frame->state_schema_version;
    out->prediction_used = frame->prediction_used;
    out->confidence_permille = frame->confidence_permille;
    out->cpu_now_permille = frame->cpu_now_permille;
    out->cpu_pred_permille = frame->cpu_pred_permille;
    out->decision_cpu_permille = frame->decision_cpu_permille;
    out->memory_pressure_permille = frame->memory_pressure_permille;
    out->thermal_permille = frame->thermal_permille;
    out->s1_permille = frame->s1_permille;
    out->s2_permille = frame->s2_permille;
    out->s3_permille = frame->s3_permille;
    out->s4_permille = frame->s4_permille;
    out->q_permille = frame->q_permille;
    out->controller_state = frame->controller_state;
    out->policy_mode = frame->policy_mode;
    out->policy_generation = frame->policy_generation;
    bpf_spin_unlock(&frame->lock);
    return 1;
}

static __always_inline int signal_is_valid(
    const struct signal_snapshot *signal,
    const struct control_snapshot *ctl, uint64_t now, uint32_t *reason)
{
    if (signal->published_ns == 0 || signal->published_ns > now ||
        signal->expires_ns <= now ||
        now - signal->published_ns > BRIDGE_SIGNAL_MAX_AGE_NS) {
        *reason = BRIDGE_FALLBACK_SIGNAL_STALE;
        return 0;
    }
    if (signal->magic != ORCHESTRA_ABI_MAGIC ||
        signal->abi_version != ORCHESTRA_ABI_VERSION ||
        signal->value_size != sizeof(struct bridge_signal_frame) ||
        (signal->flags & ~(BRIDGE_SIGNAL_F_PREDICTION_VALID |
                           BRIDGE_SIGNAL_F_METRICS_VALID |
                           BRIDGE_SIGNAL_F_CONTROLLER_VALID)) != 0 ||
        (signal->flags & (BRIDGE_SIGNAL_F_METRICS_VALID |
                          BRIDGE_SIGNAL_F_CONTROLLER_VALID)) !=
            (BRIDGE_SIGNAL_F_METRICS_VALID |
             BRIDGE_SIGNAL_F_CONTROLLER_VALID) ||
        (signal->prediction_used &&
         !(signal->flags & BRIDGE_SIGNAL_F_PREDICTION_VALID)) ||
        signal->scheduler_epoch != ctl->scheduler_epoch ||
        signal->sequence == 0 || signal->expires_ns <= signal->published_ns ||
        signal->expires_ns - signal->published_ns > BRIDGE_SIGNAL_MAX_AGE_NS ||
        signal->expires_ns - now > BRIDGE_SIGNAL_MAX_AGE_NS) {
        *reason = BRIDGE_FALLBACK_SIGNAL_INVALID;
        return 0;
    }
    if (signal->confidence_permille > BRIDGE_SIGNAL_SCALE ||
        signal->cpu_now_permille > BRIDGE_SIGNAL_SCALE ||
        signal->cpu_pred_permille > BRIDGE_SIGNAL_SCALE ||
        signal->decision_cpu_permille > BRIDGE_SIGNAL_SCALE ||
        signal->memory_pressure_permille > BRIDGE_SIGNAL_SCALE ||
        signal->thermal_permille > BRIDGE_SIGNAL_SCALE ||
        signal->s1_permille > BRIDGE_SIGNAL_SCALE ||
        signal->s2_permille > BRIDGE_SIGNAL_SCALE ||
        signal->s3_permille > BRIDGE_SIGNAL_SCALE ||
        signal->s4_permille > BRIDGE_SIGNAL_SCALE ||
        signal->q_permille > BRIDGE_SIGNAL_SCALE ||
        signal->directive >= ORCHESTRA_ACTION_COUNT ||
        signal->prediction_used > 1 ||
        signal->state_schema_version == 0 ||
        signal->controller_state >= ORCHESTRA_CTRL_COUNT ||
        signal->policy_mode >= ORCHESTRA_POLICY_COUNT ||
        signal->controller_state != ctl->controller_state ||
        signal->policy_mode != ctl->policy_mode ||
        signal->policy_generation != ctl->policy_generation) {
        *reason = BRIDGE_FALLBACK_SIGNAL_INVALID;
        return 0;
    }
    return 1;
}

static __always_inline uint64_t runtime_signal_generation(uint64_t epoch)
{
    struct orchestra_runtime_state_v8 *runtime;
    uint32_t key = 0;
    uint64_t generation = 0;

    runtime = bpf_map_lookup_elem(&orch_runtime_v8, &key);
    if (!runtime)
        return 0;
    bpf_spin_lock(&runtime->lock);
    if (runtime->magic == ORCHESTRA_ABI_MAGIC &&
        runtime->abi_version == ORCHESTRA_KERNEL_ABI_VERSION &&
        runtime->value_size == sizeof(*runtime) &&
        runtime->scheduler_epoch == epoch)
        generation = runtime->signal_generation;
    bpf_spin_unlock(&runtime->lock);
    return generation;
}

static __always_inline void tel_inc(uint64_t *counter)
{
    if (counter)
        __sync_fetch_and_add(counter, 1);
}

static __always_inline struct orchestra_task_identity task_identity(
    const struct task_struct *p)
{
    struct orchestra_task_identity id = {
        .tgid = p->tgid,
        .tid = p->pid,
        .start_boottime_ns = p->start_boottime,
    };

    return id;
}

static __always_inline int identity_equal(
    const struct orchestra_task_identity *a,
    const struct orchestra_task_identity *b)
{
    return a->tgid == b->tgid && a->tid == b->tid &&
           a->start_boottime_ns == b->start_boottime_ns;
}

static __always_inline int snapshot_control(struct control_snapshot *out)
{
    struct bridge_control *ctl;
    uint32_t key = 0;

    ctl = bpf_map_lookup_elem(&orch_control, &key);
    if (!ctl)
        return 0;

    bpf_spin_lock(&ctl->lock);
    if (ctl->magic == ORCHESTRA_ABI_MAGIC &&
        ctl->abi_version == ORCHESTRA_ABI_VERSION &&
        ctl->value_size == sizeof(*ctl) &&
        ctl->scx_api_version == ORCHESTRA_SCX_API_VERSION &&
        (ctl->capability_flags & BRIDGE_REQUIRED_CAPS) == BRIDGE_REQUIRED_CAPS &&
        ctl->controller_state < ORCHESTRA_CTRL_COUNT &&
        ctl->policy_mode < ORCHESTRA_POLICY_COUNT &&
        ctl->publication_status == BRIDGE_PUB_OK &&
        ctl->scheduler_epoch != 0) {
        out->scheduler_epoch = ctl->scheduler_epoch;
        out->last_generation = ctl->last_generation;
        out->heartbeat_ns = ctl->publisher_heartbeat_ns;
        out->lease_ns = ctl->publisher_lease_ns;
        out->policy_generation = ctl->policy_generation;
        out->controller_state = ctl->controller_state;
        out->policy_mode = ctl->policy_mode;
        out->capabilities = ctl->capability_flags;
        out->publication_status = ctl->publication_status;
        out->valid = 1;
    }
    bpf_spin_unlock(&ctl->lock);
    return out->valid;
}

static __always_inline int control_snapshot_equal(
    const struct control_snapshot *a, const struct control_snapshot *b)
{
    return a->valid && b->valid &&
           a->scheduler_epoch == b->scheduler_epoch &&
           a->last_generation == b->last_generation &&
           a->heartbeat_ns == b->heartbeat_ns &&
           a->lease_ns == b->lease_ns &&
           a->policy_generation == b->policy_generation &&
           a->controller_state == b->controller_state &&
           a->policy_mode == b->policy_mode &&
           a->capabilities == b->capabilities &&
           a->publication_status == b->publication_status;
}

static __always_inline int action_allowed(uint32_t action, uint32_t state)
{
    switch (state) {
    case ORCHESTRA_CTRL_NORMAL:
        return 1;
    case ORCHESTRA_CTRL_DEGRADED:
        return action == ORCHESTRA_ACTION_RUN ||
               action == ORCHESTRA_ACTION_YIELD ||
               action == ORCHESTRA_ACTION_THROTTLE;
    case ORCHESTRA_CTRL_SATURATED:
        return action == ORCHESTRA_ACTION_RUN ||
               action == ORCHESTRA_ACTION_THROTTLE;
    case ORCHESTRA_CTRL_RECOVERY:
        return action == ORCHESTRA_ACTION_RUN ||
               action == ORCHESTRA_ACTION_YIELD;
    case ORCHESTRA_CTRL_ROLLBACK:
    case ORCHESTRA_CTRL_DISABLED:
        return action == ORCHESTRA_ACTION_RUN;
    default:
        return action == ORCHESTRA_ACTION_RUN;
    }
}

static __always_inline void publish_runtime_state_v8(
    const struct runtime_snapshot_v8 *state)
{
    struct orchestra_runtime_state_v8 *runtime;
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint32_t key = 0;
    uint64_t previous_signal_generation;
    uint32_t signal_flags;

    runtime = bpf_map_lookup_elem(&orch_runtime_v8, &key);
    if (!runtime) {
        if (tel)
            tel8_inc(&tel->telemetry_drop_count);
        return;
    }
    bpf_spin_lock(&runtime->lock);
    previous_signal_generation = runtime->signal_generation;
    signal_flags = ORCHESTRA_RUNTIME_V8_F_SIGNAL_VALID |
                   ORCHESTRA_RUNTIME_V8_F_PREDICTION_VALID |
                   ORCHESTRA_RUNTIME_V8_F_PREDICTION_FALLBACK;
    runtime->magic = ORCHESTRA_ABI_MAGIC;
    runtime->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
    runtime->value_size = sizeof(*runtime);
    runtime->state_schema_version = ORCHESTRA_KERNEL_STATE_SCHEMA_VERSION;
    /* A stale reader result must not overwrite a newer accepted signal that
     * another scheduling callback already published.  Equal generations are
     * allowed because the same frame may be consumed by multiple tasks. */
    if (state->signal_generation < previous_signal_generation)
        runtime->flags = (runtime->flags & signal_flags) |
                         (state->flags & ~signal_flags);
    else
        runtime->flags = state->flags;
    if (state->signal_generation >= previous_signal_generation) {
        runtime->cpu_now_permille = state->cpu_now_permille;
        runtime->cpu_pred_permille = state->cpu_pred_permille;
        runtime->queue_pressure_permille = state->queue_pressure_permille;
        runtime->memory_pressure_permille = state->memory_pressure_permille;
        runtime->thermal_permille = state->thermal_permille;
        runtime->prediction_confidence_permille =
            state->prediction_confidence_permille;
        runtime->observed_cpu_permille = state->observed_cpu_permille;
        runtime->predicted_cpu_permille = state->predicted_cpu_permille;
        runtime->prediction_fallback_reason = state->prediction_fallback_reason;
    }
    runtime->scheduler_epoch = state->scheduler_epoch;
    runtime->state_generation = state->state_generation;
    if (state->signal_generation >= previous_signal_generation) {
        runtime->signal_generation = state->signal_generation;
        runtime->prediction_generation = state->prediction_generation;
        runtime->prediction_published_ns = state->prediction_published_ns;
        runtime->prediction_expires_ns = state->prediction_expires_ns;
        runtime->prediction_model_version = state->prediction_model_version;
    }
    runtime->state_index = state->state_index;
    runtime->s1_permille = state->s1_permille;
    runtime->s2_permille = state->s2_permille;
    runtime->s3_permille = state->s3_permille;
    runtime->s4_permille = state->s4_permille;
    runtime->q_permille = state->q_permille;
    runtime->coordination_generation = state->coordination_generation;
    runtime->deficit_class = state->deficit_class;
    runtime->controller_state = state->controller_state;
    runtime->controller_schema_version =
        ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION;
    runtime->controller_generation = state->controller_generation;
    runtime->policy_mode = state->policy_mode;
    runtime->policy_schema_version = ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION;
    runtime->policy_generation = state->policy_generation;
    runtime->published_ns = state->published_ns;
    runtime->expires_ns = state->expires_ns;
    bpf_spin_unlock(&runtime->lock);
}

static ORCHESTRA_NOINLINE void publish_runtime_state_v10(
    const struct runtime_snapshot_v8 *state)
{
    struct orchestra_runtime_state_v10 *runtime;
    struct orchestra_coordination_metrics_v10 metrics = {};
    struct orchestra_controller_view_v10 controller = {};
    uint32_t key = 0;
    uint64_t now = bpf_ktime_get_ns();

    runtime = bpf_map_lookup_elem(&orch_runtime10, &key);
    if (!runtime)
        return;
    (void)orchestra_coord_read_global_summary(now, &metrics);
    (void)orchestra_controller_read_view_v10(&controller);
    bpf_spin_lock(&runtime->lock);
    runtime->magic = ORCHESTRA_ABI_MAGIC;
    runtime->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
    runtime->value_size = sizeof(*runtime);
    runtime->schema_version = ORCHESTRA_RUNTIME_SCHEMA_VERSION;
    runtime->flags = state->flags;
    runtime->scope = metrics.scope;
    runtime->domain_id = metrics.domain_id;
    runtime->scheduler_epoch = state->scheduler_epoch;
    runtime->window_generation = metrics.window_generation;
    runtime->window_start_ns = metrics.window_start_ns;
    runtime->window_end_ns = metrics.window_end_ns;
    runtime->eligible_observations = metrics.eligible_observations;
    runtime->executed_observations = metrics.executed_observations;
    runtime->s1_permille = metrics.s1_permille;
    runtime->s2_permille = metrics.s2_permille;
    runtime->s3_permille = metrics.s3_permille;
    runtime->s4_permille = metrics.s4_permille;
    runtime->q_permille = metrics.q_permille;
    runtime->deficit_class = metrics.deficit_class;
    runtime->primary_deficit = metrics.primary_deficit;
    runtime->secondary_deficit = metrics.secondary_deficit;
    runtime->deficit_severity = metrics.deficit_severity;
    runtime->deficit_persistence = metrics.deficit_persistence;
    runtime->controller_state = controller.state;
    runtime->controller_flags = controller.flags;
    runtime->controller_generation = controller.generation;
    runtime->policy_generation = state->policy_generation;
    runtime->signal_generation = state->signal_generation;
    runtime->prediction_generation = state->prediction_generation;
    runtime->published_ns = now;
    bpf_spin_unlock(&runtime->lock);
}

static ORCHESTRA_NOINLINE int orchestra_read_runtime_state(
    struct task_struct *p, struct runtime_snapshot_v8 *out)
{
    uint32_t scratch_key = 0;
    struct orchestra_decision_work *work =
        bpf_map_lookup_elem(&orch_dec_work, &scratch_key);
    if (!work)
        return 0;
    struct control_snapshot *ctl = &work->read_control;
    struct orchestra_controller_view_v10 *controller = &work->controller;
    struct orchestra_coordination_metrics_v10 *coordination = &work->coordination;
    struct signal_snapshot *signal = &work->read_signal;

    uint32_t signal_reason = BRIDGE_FALLBACK_SIGNAL_STALE;
    uint64_t now = bpf_ktime_get_ns();
    int32_t queued = scx_bpf_dsq_nr_queued(SCX_DSQ_GLOBAL);
    int signal_present;
    int controller_present;
    int native_summary;
    int accepted_signal = 0;

    __builtin_memset(signal, 0, sizeof(*signal));
    __builtin_memset(out, 0, sizeof(*out));
    if (!snapshot_control(ctl))
        return 0;

    out->scheduler_epoch = ctl->scheduler_epoch;
    out->state_generation = ctl->last_generation;
    out->controller_state = ctl->controller_state;
    out->controller_generation = ctl->last_generation;
    out->policy_mode = ctl->policy_mode;
    out->capability_flags = ctl->capabilities;
    out->policy_generation = ctl->policy_generation;
    out->published_ns = now;
    out->expires_ns = 0;

    controller_present = orchestra_controller_read_view_v10(controller);
    if (controller_present) {
        out->controller_state = controller->state;
        out->controller_generation = controller->generation;
    }

    signal_present = snapshot_signal(signal);
    if (signal_present && signal_is_valid(signal, ctl, now,
                                          &signal_reason)) {
        if (signal->sequence < runtime_signal_generation(ctl->scheduler_epoch)) {
            signal_reason = BRIDGE_FALLBACK_SIGNAL_REPLAY;
        } else if (controller_present && signal->prediction_used &&
            signal->confidence_permille <
                controller->prediction_confidence_threshold) {
            signal_reason = BRIDGE_FALLBACK_SIGNAL_INVALID;
        } else if (controller_present && signal->prediction_used &&
                   signal->expires_ns - signal->published_ns >
                       controller->prediction_horizon_ns) {
            signal_reason = BRIDGE_FALLBACK_SIGNAL_INVALID;
        } else {
            accepted_signal = 1;
        }
    }
    orchestra_coord_record_signal(
        accepted_signal, signal_present && !accepted_signal &&
            signal_reason == BRIDGE_FALLBACK_SIGNAL_STALE,
        signal->confidence_permille, signal->cpu_now_permille,
        signal->cpu_pred_permille, signal->prediction_used,
        signal->published_ns, signal->expires_ns, signal->sequence);
    if (accepted_signal) {
        out->flags |= ORCHESTRA_RUNTIME_V8_F_SIGNAL_VALID;
        out->cpu_now_permille = signal->cpu_now_permille;
        out->cpu_pred_permille = signal->cpu_pred_permille;
        out->queue_pressure_permille = signal->decision_cpu_permille;
        out->memory_pressure_permille = signal->memory_pressure_permille;
        out->thermal_permille = signal->thermal_permille;
        out->prediction_confidence_permille = signal->confidence_permille;
        out->observed_cpu_permille = signal->cpu_now_permille;
        out->predicted_cpu_permille = signal->cpu_pred_permille;
        out->signal_generation = signal->sequence;
        out->prediction_generation = signal->prediction_used ?
            signal->sequence : 0;
        out->prediction_published_ns = signal->published_ns;
        out->prediction_expires_ns = signal->expires_ns;
        out->prediction_model_version = signal->source_id;
        out->flags |= ORCHESTRA_RUNTIME_V8_F_CONTROLLER_VALID;
        if (signal->prediction_used) {
            out->flags |= ORCHESTRA_RUNTIME_V8_F_PREDICTION_VALID;
        } else {
            out->flags |= ORCHESTRA_RUNTIME_V8_F_PREDICTION_FALLBACK;
            out->prediction_fallback_reason = BRIDGE_FALLBACK_SIGNAL_INVALID;
            if (global_v8_tel())
                tel8_inc(&global_v8_tel()->prediction_fallback_count);
        }
        out->published_ns = signal->published_ns;
        out->expires_ns = signal->expires_ns;
    } else {
        /* Observed values are the fail-safe input when prediction/signal data
         * is absent or stale; the policy engine never waits for bridge input. */
        out->flags |= ORCHESTRA_RUNTIME_V8_F_PREDICTION_FALLBACK;
        out->prediction_fallback_reason = signal_reason;
        out->prediction_confidence_permille = 0;
        if (global_v8_tel()) {
            tel8_inc(&global_v8_tel()->prediction_fallback_count);
            tel8_inc(&global_v8_tel()->signal_fallback_count);
        }
    }
    native_summary = orchestra_coord_read_global_summary(now, coordination);
    if (native_summary) {
        out->s1_permille = coordination->s1_permille;
        out->s2_permille = coordination->s2_permille;
        out->s3_permille = coordination->s3_permille;
        out->s4_permille = coordination->s4_permille;
        out->q_permille = coordination->q_permille;
        out->deficit_class = coordination->deficit_class;
        out->coordination_generation = coordination->window_generation;
        out->flags |= ORCHESTRA_RUNTIME_V8_F_COORDINATION_VALID;
    } else if (accepted_signal) {
        /* Before the first completed native window, retain the signal as a
         * diagnostic fallback but do not present it as a computed Q. */
        out->coordination_generation = 0;
    }
    if (queued > 0)
        out->queue_pressure_permille = queued >= 1000 ? 1000u :
            (uint32_t)queued;
    if (p) {
        /* A task-local scheduler sample remains available even without a
         * userspace signal->  The kernel API exposes CPU identity reliably;
         * queue pressure remains the bounded signal-derived value above. */
        (void)p;
    }
    return 1;
}

static __always_inline void orchestra_build_state(
    struct runtime_snapshot_v8 *out)
{
    uint32_t cpu_bucket = out->cpu_now_permille / 250u;
    uint32_t memory_bucket = out->memory_pressure_permille / 250u;
    uint32_t thermal_bucket = out->thermal_permille / 250u;
    uint32_t confidence_bit = out->prediction_confidence_permille >= 500u;
    /* Seven bits encode CPU/memory/thermal/confidence; one bit preserves the
     * normal-vs-protected controller partition.  Exact controller semantics
     * remain in the gate, while the lookup index is <=255. */
    uint32_t controller_bucket = out->controller_state == ORCHESTRA_CTRL_NORMAL ?
        0u : 1u;

    if (cpu_bucket > 3u)
        cpu_bucket = 3u;
    if (memory_bucket > 3u)
        memory_bucket = 3u;
    if (thermal_bucket > 3u)
        thermal_bucket = 3u;
    out->state_index = cpu_bucket | (memory_bucket << 2) |
        (thermal_bucket << 4) | (confidence_bit << 6) |
        (controller_bucket << 7);
    /* v10 publishes the diagnosed class from the finalized coordination
     * window.  A zero value before the first completed window means that no
     * deficit has yet been diagnosed; it is not a Q bucket. */
    out->flags |= ORCHESTRA_RUNTIME_V8_F_CONTROLLER_VALID;
}

static ORCHESTRA_NOINLINE void orchestra_publish_observation(
    struct task_struct *p)
{
    struct orchestra_task_identity id = task_identity(p);
    struct runtime_snapshot_v8 *state;
    uint32_t key = 0;

    if (!bpf_map_lookup_elem(&orch_directives, &id))
        return;
    state = bpf_map_lookup_elem(&orch_runtime_scratch, &key);
    if (!state || !orchestra_read_runtime_state(p, state))
        return;
    orchestra_build_state(state);
    publish_runtime_state_v8(state);
    publish_runtime_state_v10(state);
}

static ORCHESTRA_NOINLINE void orchestra_flush_observation(void)
{
    struct runtime_snapshot_v8 *state;
    uint32_t key = 0;

    state = bpf_map_lookup_elem(&orch_runtime_scratch, &key);
    if (!state)
        return;
    orchestra_build_state(state);
    publish_runtime_state_v8(state);
    publish_runtime_state_v10(state);
}

static __always_inline int snapshot_policy_meta_v8(
    struct policy_meta_snapshot_v8 *out)
{
    struct orchestra_policy_meta_v8 *meta;
    uint32_t key = 0;

    __builtin_memset(out, 0, sizeof(*out));
    meta = bpf_map_lookup_elem(&orch_meta_v8, &key);
    if (!meta)
        return 0;
    bpf_spin_lock(&meta->lock);
    if (meta->magic == ORCHESTRA_ABI_MAGIC &&
        meta->abi_version == ORCHESTRA_KERNEL_ABI_VERSION &&
        meta->value_size == sizeof(*meta) &&
        meta->policy_schema_version == ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION &&
        meta->active_bank < ORCHESTRA_KERNEL_POLICY_BANK_COUNT &&
        meta->entry_count <= ORCHESTRA_KERNEL_MAX_POLICY_STATES &&
        meta->policy_mode < ORCHESTRA_POLICY_COUNT &&
        meta->controller_state < ORCHESTRA_CTRL_COUNT &&
        meta->controller_schema_version ==
            ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION) {
        out->active_bank = meta->active_bank;
        out->entry_count = meta->entry_count;
        out->policy_mode = meta->policy_mode;
        out->controller_state = meta->controller_state;
        out->controller_schema_version = meta->controller_schema_version;
        out->flags = meta->flags;
        out->scheduler_epoch = meta->scheduler_epoch;
        out->policy_generation = meta->policy_generation;
        out->previous_generation = meta->previous_generation;
        out->published_ns = meta->published_ns;
        out->capability_flags = meta->capability_flags;
        out->valid = 1;
    }
    bpf_spin_unlock(&meta->lock);
    return out->valid;
}

static __always_inline int policy_meta_equal_v8(
    const struct policy_meta_snapshot_v8 *a,
    const struct policy_meta_snapshot_v8 *b)
{
    return a->valid && b->valid && a->active_bank == b->active_bank &&
           a->entry_count == b->entry_count && a->flags == b->flags &&
           a->capability_flags == b->capability_flags &&
           a->published_ns == b->published_ns &&
           a->previous_generation == b->previous_generation &&
           a->policy_generation == b->policy_generation &&
           a->scheduler_epoch == b->scheduler_epoch &&
           a->policy_mode == b->policy_mode &&
           a->controller_state == b->controller_state;
}

static __always_inline void policy_default_v8(
    const struct runtime_snapshot_v8 *state,
    struct directive_snapshot *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    out->scheduler_epoch = state->scheduler_epoch;
    out->state_generation = state->state_generation;
    out->controller_generation = state->controller_generation;
    out->generation = state->state_generation ? state->state_generation : 1;
    out->slice_ns = ORCHESTRA_V8_DEFAULT_SLICE_NS;
    out->action = ORCHESTRA_ACTION_RUN;
    out->target_cpu = ORCHESTRA_CPU_ANY;
    out->controller_state = state->controller_state;
    out->policy_mode = state->policy_mode;
    out->policy_generation = state->policy_generation;
}

static ORCHESTRA_NOINLINE int orchestra_policy_lookup(
    struct task_struct *p, struct decision_snapshot_v8 *decision)
{
    uint32_t scratch_key = 0;
    struct orchestra_decision_work *work =
        bpf_map_lookup_elem(&orch_dec_work, &scratch_key);
    if (!work)
        return 0;
    struct runtime_snapshot_v8 *state = &work->policy_state;
    struct policy_meta_snapshot_v8 *meta = &work->meta;
    struct policy_meta_snapshot_v8 *verify_meta = &work->verify_meta;
    struct control_snapshot *policy_control = &work->policy_control;
    struct control_snapshot *verify_control = &work->verify_control;

    struct orchestra_policy_entry_v8 *entry;
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint32_t key;
    int coherent = 0;
    int have_entry = 0;
    int invalid_entry = 0;

    __builtin_memset(decision, 0, sizeof(*decision));
    if (!orchestra_read_runtime_state(p, state))
        return 0;
    orchestra_build_state(state);
    publish_runtime_state_v8(state);
    publish_runtime_state_v10(state);
    decision->state_index = state->state_index;
    decision->capability_flags = state->capability_flags;
    decision->action_capability_mask = state->capability_flags;
    decision->controller_state = state->controller_state;
    decision->policy_mode = state->policy_mode;
    decision->cpu_now_permille = state->cpu_now_permille;
    decision->queue_pressure_permille = state->queue_pressure_permille;
    decision->controller_generation = state->controller_generation;
    if (tel)
        tel8_inc(&tel->policy_lookup_count);

    if (!snapshot_policy_meta_v8(meta) ||
        meta->scheduler_epoch != state->scheduler_epoch ||
        !(meta->flags & ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID) ||
        (meta->capability_flags & ORCHESTRA_KERNEL_REQUIRED_CAPS) !=
            ORCHESTRA_KERNEL_REQUIRED_CAPS) {
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = BRIDGE_FALLBACK_POLICY_MISSING;
        decision->source = ORCHESTRA_DECISION_SOURCE_POLICY;
        decision->valid = 1;
        return 1;
    }

    /* The policy metadata and the bridge control record are a cross-map
     * publication.  If the second half of a commit failed, never execute the
     * newly selected bank under the old control generation/state-> */
    if (meta->policy_generation != state->policy_generation ||
        meta->policy_mode != state->policy_mode ||
        (!snapshot_control(policy_control) ||
         meta->controller_state != policy_control->controller_state)) {
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = BRIDGE_FALLBACK_POLICY_GENERATION;
        decision->source = ORCHESTRA_DECISION_SOURCE_POLICY;
        decision->valid = 1;
        return 1;
    }

    if (meta->entry_count != 0 && state->state_index >= meta->entry_count) {
        if (tel)
            tel8_inc(&tel->invalid_policy_index_count);
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = BRIDGE_FALLBACK_POLICY_MISSING;
        decision->source = ORCHESTRA_DECISION_SOURCE_POLICY;
        decision->valid = 1;
        return 1;
    }

#pragma unroll
    for (int attempt = 0; attempt < 2; attempt++) {
        __builtin_memset(verify_meta, 0, sizeof(*verify_meta));
        coherent = 0;
        have_entry = 0;
        invalid_entry = 0;
        if (!snapshot_control(policy_control) ||
            !snapshot_policy_meta_v8(meta) ||
            policy_control->scheduler_epoch != state->scheduler_epoch ||
            policy_control->policy_generation != state->policy_generation ||
            policy_control->policy_mode != state->policy_mode ||
            meta->scheduler_epoch != policy_control->scheduler_epoch ||
            meta->policy_generation != policy_control->policy_generation ||
            meta->policy_mode != policy_control->policy_mode ||
            meta->controller_state != policy_control->controller_state ||
            !(meta->flags & ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID))
            continue;
        key = meta->active_bank * ORCHESTRA_KERNEL_MAX_POLICY_STATES +
            state->state_index;
        entry = bpf_map_lookup_elem(&orch_entry_v8, &key);
        if (entry) {
            bpf_spin_lock(&entry->lock);
            decision->directive.flags = entry->flags;
            decision->directive.scheduler_epoch = state->scheduler_epoch;
            decision->directive.state_generation = state->state_generation;
            decision->directive.controller_generation =
                state->controller_generation;
            decision->directive.generation = entry->policy_generation ?
                entry->policy_generation : state->state_generation;
            decision->directive.action = entry->action;
            decision->directive.target_cpu = entry->target_cpu;
            decision->directive.slice_ns = entry->slice_ns;
            decision->directive.not_before_ns = entry->not_before_ns;
            decision->directive.throttle_period_ns = entry->throttle_period_ns;
            decision->directive.throttle_budget_ns = entry->throttle_budget_ns;
            decision->directive.controller_state = state->controller_state;
            decision->directive.policy_mode = state->policy_mode;
            decision->directive.policy_generation = meta->policy_generation;
            decision->action_capability_mask = entry->capability_mask ?
                entry->capability_mask : state->capability_flags;
            have_entry = entry->value_size != 0;
            if (entry->value_size != 0 &&
                (entry->magic != ORCHESTRA_ABI_MAGIC ||
                 entry->abi_version != ORCHESTRA_KERNEL_ABI_VERSION ||
                 entry->value_size != sizeof(*entry) ||
                 entry->policy_schema_version !=
                     ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION ||
                 !(entry->flags & ORCHESTRA_POLICY_V8_F_VALID) ||
                 (entry->flags & ~(ORCHESTRA_POLICY_V8_F_VALID |
                                   ORCHESTRA_POLICY_V8_F_TARGET_CPU |
                                   ORCHESTRA_POLICY_V8_F_DEFERRED |
                                   ORCHESTRA_POLICY_V8_F_THROTTLE_BUDGET)) != 0))
                invalid_entry = 1;
            if (entry->value_size != 0 && !invalid_entry &&
                entry->policy_generation == meta->policy_generation &&
                entry->state_index == state->state_index)
                coherent = 1;
            bpf_spin_unlock(&entry->lock);
        }
        if (snapshot_policy_meta_v8(verify_meta) &&
            snapshot_control(verify_control) &&
            policy_meta_equal_v8(meta, verify_meta) &&
            control_snapshot_equal(policy_control, verify_control))
            break;
        coherent = 0;
    }

    if (invalid_entry) {
        if (tel)
            tel8_inc(&tel->invalid_policy_index_count);
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = BRIDGE_FALLBACK_BAD_ABI;
    } else if (!coherent || !have_entry) {
        if (tel) {
            if (!coherent)
                tel8_inc(&tel->policy_generation_mismatch_count);
        }
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = coherent ? BRIDGE_FALLBACK_POLICY_MISSING :
            BRIDGE_FALLBACK_POLICY_GENERATION;
    } else if (decision->directive.action >= ORCHESTRA_ACTION_COUNT) {
        if (tel) {
            tel8_inc(&tel->invalid_policy_action_count);
            tel8_inc(&tel->policy_fallback_count);
        }
        policy_default_v8(state, &decision->directive);
        decision->fallback_reason = BRIDGE_FALLBACK_BAD_ACTION;
    } else {
        decision->fallback_reason = BRIDGE_FALLBACK_NONE;
        if (decision->directive.action == ORCHESTRA_ACTION_THROTTLE &&
            (decision->directive.throttle_period_ns == 0 ||
             decision->directive.throttle_budget_ns == 0)) {
            decision->directive.throttle_period_ns =
                ORCHESTRA_V8_DEFAULT_THROTTLE_PERIOD_NS;
            decision->directive.throttle_budget_ns =
                ORCHESTRA_V8_DEFAULT_THROTTLE_BUDGET_NS;
        }
    }
    decision->source = ORCHESTRA_DECISION_SOURCE_POLICY;
    decision->valid = 1;
    return 1;
}

static __always_inline void orchestra_controller_gate(
    struct decision_snapshot_v8 *decision)
{
    uint32_t scratch_key = 0;
    struct orchestra_decision_work *work =
        bpf_map_lookup_elem(&orch_dec_work, &scratch_key);
    if (!work) {
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_MAP_ERROR;
        return;
    }
    struct orchestra_controller_view_v10 *view = &work->controller;
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint32_t original = decision->directive.action;
    int publisher_allows = action_allowed(original,
                                          decision->directive.controller_state);
    uint64_t now = bpf_ktime_get_ns();
    int changed = 0;

    decision->policy_selected_action = original;
    decision->directive.policy_selected_action = original;
    decision->directive.controller_generation = decision->controller_generation;
    if (orchestra_controller_read_view_v10(view)) {
        decision->controller_state = view->state;
        decision->controller_generation = view->generation;
        decision->directive.controller_generation = view->generation;
        decision->directive.controller_state = view->state;
        if (!publisher_allows || orchestra_controller_should_fallback_v10(
                view, original, decision->cpu_now_permille,
                decision->queue_pressure_permille)) {
            decision->directive.action = ORCHESTRA_ACTION_RUN;
            decision->directive.controller_override_action = original;
            decision->directive.slice_ns = ORCHESTRA_V8_DEFAULT_SLICE_NS;
            decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
            decision->directive.not_before_ns = 0;
            decision->directive.throttle_period_ns = 0;
            decision->directive.throttle_budget_ns = 0;
            decision->fallback_reason = BRIDGE_FALLBACK_CONTROLLER_OVERRIDE;
            changed = 1;
        } else if (original == ORCHESTRA_ACTION_THROTTLE) {
            decision->directive.throttle_period_ns =
                orchestra_controller_clamp_duration(
                    view->throttle_duration_ns, BRIDGE_SLICE_MIN_NS,
                    BRIDGE_THROTTLE_MAX_NS);
            decision->directive.throttle_budget_ns =
                decision->directive.throttle_period_ns / 5u;
            if (decision->directive.throttle_budget_ns < BRIDGE_SLICE_MIN_NS)
                decision->directive.throttle_budget_ns = BRIDGE_SLICE_MIN_NS;
        } else if (original == ORCHESTRA_ACTION_SLEEP) {
            decision->directive.not_before_ns = orchestra_controller_deadline_v10(now,
                orchestra_controller_clamp_duration(
                    view->sleep_defer_ns, BRIDGE_SLICE_MIN_NS,
                    BRIDGE_SLEEP_MAX_NS));
        }
    } else if (original != ORCHESTRA_ACTION_RUN) {
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->directive.controller_override_action = original;
        decision->directive.slice_ns = ORCHESTRA_V8_DEFAULT_SLICE_NS;
        decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
        decision->directive.not_before_ns = 0;
        decision->directive.throttle_period_ns = 0;
        decision->directive.throttle_budget_ns = 0;
        decision->fallback_reason = BRIDGE_FALLBACK_CONTROLLER_OVERRIDE;
        changed = 1;
    }
    decision->controller_adjusted_action = decision->directive.action;
    decision->directive.controller_adjusted_action = decision->directive.action;
    if (!changed && decision->directive.controller_override_action != 0)
        changed = 1;
    if (changed)
        orchestra_controller_note_override_v10();
    if (tel) {
        if (changed)
            tel8_inc(&tel->controller_override_count);
        if (decision->controller_state == ORCHESTRA_CTRL_ROLLBACK && changed)
            tel8_inc(&tel->controller_rollback_count);
        if (decision->controller_state == ORCHESTRA_CTRL_RECOVERY && changed)
            tel8_inc(&tel->controller_recovery_count);
    }
}

static __always_inline void orchestra_validate_action(
    struct task_struct *p, struct decision_snapshot_v8 *decision)
{
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint64_t now = bpf_ktime_get_ns();
    uint32_t current_cpu = bpf_get_smp_processor_id();
    int cpu;

    if (decision->directive.action >= ORCHESTRA_ACTION_COUNT) {
        if (tel)
            tel8_inc(&tel->invalid_policy_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_BAD_ACTION;
        return;
    }
    if ((decision->capability_flags & (1u << decision->directive.action)) == 0) {
        if (tel)
            tel8_inc(&tel->unsupported_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
        return;
    }
    if ((decision->action_capability_mask &
         (1u << decision->directive.action)) == 0) {
        if (tel)
            tel8_inc(&tel->unsupported_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
        return;
    }
    if (decision->directive.slice_ns != 0 &&
        !(decision->capability_flags & ORCHESTRA_KERNEL_CAP_ADAPTIVE_SLICE)) {
        if (tel)
            tel8_inc(&tel->unsupported_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->directive.slice_ns = ORCHESTRA_V8_DEFAULT_SLICE_NS;
        decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
        return;
    }
    if (decision->directive.action == ORCHESTRA_ACTION_SLEEP &&
        !(decision->capability_flags & ORCHESTRA_KERNEL_CAP_SLEEP_DEFER_COMPAT)) {
        if (tel)
            tel8_inc(&tel->unsupported_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
        return;
    }
    if (decision->directive.action == ORCHESTRA_ACTION_THROTTLE &&
        !(decision->capability_flags &
          ORCHESTRA_KERNEL_CAP_THROTTLE_DEFER_COMPAT)) {
        if (tel)
            tel8_inc(&tel->unsupported_action_count);
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
        return;
    }
    if (decision->directive.action == ORCHESTRA_ACTION_YIELD) {
        struct orchestra_task_identity id = task_identity(p);
        struct orchestra_task_hot_v8 *hot = get_task_hot_v8(&id, 0);
        int repeated = 0;

        if (hot) {
            bpf_spin_lock(&hot->lock);
            repeated = hot->current_action == ORCHESTRA_ACTION_YIELD &&
                (hot->flags & ORCHESTRA_TASK_V8_F_ENQUEUED);
            if (repeated)
                hot->flags |= ORCHESTRA_TASK_V8_F_PROGRESS_GUARD;
            bpf_spin_unlock(&hot->lock);
        }
        if (repeated) {
            decision->directive.action = ORCHESTRA_ACTION_RUN;
            decision->directive.slice_ns = ORCHESTRA_V8_DEFAULT_SLICE_NS;
            decision->fallback_reason = BRIDGE_FALLBACK_PROGRESS_GUARD;
            if (tel)
                tel8_inc(&tel->policy_fallback_count);
            return;
        }
    }
    if (decision->directive.action == ORCHESTRA_ACTION_MIGRATE) {
        if (decision->directive.target_cpu == ORCHESTRA_CPU_ANY &&
            !(decision->capability_flags &
              ORCHESTRA_KERNEL_CAP_STATE_CPU_SELECTION)) {
            if (tel)
                tel8_inc(&tel->unsupported_action_count);
            decision->directive.action = ORCHESTRA_ACTION_RUN;
            decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
            decision->fallback_reason = BRIDGE_FALLBACK_UNSUPPORTED_ACTION;
            return;
        }
        if (decision->directive.target_cpu == ORCHESTRA_CPU_ANY) {
            cpu = scx_bpf_pick_any_cpu(p->cpus_ptr, 0);
            if (cpu < 0) {
                decision->migration_outcome = ORCHESTRA_MIGRATE_V8_NO_TARGET;
                if (tel)
                    tel8_inc(&tel->policy_fallback_count);
                decision->directive.action = ORCHESTRA_ACTION_RUN;
                decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
                decision->fallback_reason = BRIDGE_FALLBACK_BAD_CPU;
                return;
            }
            decision->directive.target_cpu = (uint32_t)cpu;
        }
        if (decision->directive.target_cpu == current_cpu) {
            decision->migration_outcome = ORCHESTRA_MIGRATE_V8_ALREADY_LOCAL;
            decision->directive.action = ORCHESTRA_ACTION_RUN;
            decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
            return;
        }
        if (!cpu_allowed_online(p, decision->directive.target_cpu)) {
            decision->migration_outcome = cpu_is_online(
                decision->directive.target_cpu) ?
                ORCHESTRA_MIGRATE_V8_AFFINITY_REJECT :
                ORCHESTRA_MIGRATE_V8_CPU_OFFLINE;
            cpu = scx_bpf_pick_any_cpu(p->cpus_ptr, 0);
            if (cpu < 0 || !cpu_allowed_online(p, (uint32_t)cpu)) {
                decision->migration_outcome = ORCHESTRA_MIGRATE_V8_NO_TARGET;
                if (tel)
                    tel8_inc(&tel->policy_fallback_count);
                decision->directive.action = ORCHESTRA_ACTION_RUN;
                decision->directive.target_cpu = ORCHESTRA_CPU_ANY;
                decision->fallback_reason = BRIDGE_FALLBACK_BAD_CPU;
            } else {
                decision->directive.target_cpu = (uint32_t)cpu;
                decision->migration_outcome = ORCHESTRA_MIGRATE_V8_FALLBACK;
            }
        } else {
            decision->migration_outcome = ORCHESTRA_MIGRATE_V8_SELECTED;
        }
    }
    if (decision->directive.action == ORCHESTRA_ACTION_SLEEP &&
        (decision->directive.not_before_ns <= now ||
         decision->directive.not_before_ns - now > BRIDGE_SLEEP_MAX_NS)) {
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->directive.not_before_ns = 0;
        decision->fallback_reason = BRIDGE_FALLBACK_BAD_PARAMETERS;
    }
    if (decision->directive.action == ORCHESTRA_ACTION_THROTTLE &&
        (decision->directive.throttle_period_ns < BRIDGE_SLICE_MIN_NS ||
         decision->directive.throttle_budget_ns < BRIDGE_SLICE_MIN_NS ||
         decision->directive.throttle_budget_ns >=
             decision->directive.throttle_period_ns)) {
        decision->directive.action = ORCHESTRA_ACTION_RUN;
        decision->fallback_reason = BRIDGE_FALLBACK_BAD_PARAMETERS;
    }
}

static ORCHESTRA_NOINLINE int load_directive(
    struct task_struct *p, struct directive_snapshot *out,
    uint32_t *fallback_reason);

/* Internal task-state provenance; does not change any map layout. */
#define ORCHESTRA_TASK_STATE_F_POLICY (1U << 31)

static ORCHESTRA_NOINLINE int orchestra_decide(
    struct task_struct *p, struct decision_snapshot_v8 *decision)
{
    uint32_t scratch_key = 0;
    struct orchestra_decision_work *work =
        bpf_map_lookup_elem(&orch_dec_work, &scratch_key);
    if (!work)
        return 0;
    struct directive_snapshot *bridge = &work->bridge;

    uint32_t reason = BRIDGE_FALLBACK_NO_DIRECTIVE;
    int have_policy;

    __builtin_memset(decision, 0, sizeof(*decision));
    struct orchestra_task_identity admission = task_identity(p);
    if (!bpf_map_lookup_elem(&orch_directives, &admission)) {
        decision->fallback_reason = BRIDGE_FALLBACK_NO_DIRECTIVE;
        return 0;
    }
    have_policy = orchestra_policy_lookup(p, decision);
    /* Validate the admission after the policy snapshot. Matching epoch and
     * policy generation below prevent mixing publications across commits. */
    if (!load_directive(p, bridge, &reason)) {
        decision->fallback_reason = reason;
        return 0;
    }
    if (have_policy && bridge->policy_generation != 0) {
        if (decision->directive.scheduler_epoch != bridge->scheduler_epoch ||
            decision->directive.policy_generation != bridge->policy_generation ||
            decision->directive.policy_mode != bridge->policy_mode) {
            decision->fallback_reason = BRIDGE_FALLBACK_POLICY_GENERATION;
            return 0;
        }
        /* A published but invalid/missing entry fails closed to the RUN
         * selected by policy_lookup, rather than bypassing the policy bank. */
        decision->directive.generation = bridge->generation;
        decision->directive.expiry_ns = bridge->expiry_ns;
        decision->directive.flags = bridge->flags;
        decision->directive.controller_state = bridge->controller_state;
    } else if (bridge->policy_generation != 0) {
        decision->fallback_reason = BRIDGE_FALLBACK_POLICY_MISSING;
        return 0;
    } else {
        decision->directive = *bridge;
        decision->source = ORCHESTRA_DECISION_SOURCE_BRIDGE;
        decision->fallback_reason = BRIDGE_FALLBACK_NONE;
        decision->valid = 1;
        if (!have_policy)
            decision->capability_flags = BRIDGE_REQUIRED_CAPS;
        decision->action_capability_mask = decision->capability_flags;
        decision->controller_state = bridge->controller_state;
        decision->policy_mode = bridge->policy_mode;
    }
    decision->policy_selected_action = decision->directive.action;
    decision->directive.policy_selected_action = decision->policy_selected_action;
    decision->directive.controller_generation = decision->controller_generation;
    orchestra_controller_gate(decision);
    orchestra_validate_action(p, decision);
    decision->capability_adjusted_action = decision->directive.action;
    decision->directive.capability_adjusted_action = decision->capability_adjusted_action;
    return 1;
}

static ORCHESTRA_NOINLINE int load_directive(
    struct task_struct *p, struct directive_snapshot *out,
    uint32_t *fallback_reason)
{
    struct bridge_telemetry *tel = global_tel();
    struct orchestra_task_identity id = task_identity(p);
    struct orchestra_task_identity value_id = {};
    struct control_snapshot ctl = {};
    struct control_snapshot verify_ctl = {};
    struct bridge_directive *dir;
    uint64_t now = bpf_ktime_get_ns();
    uint32_t abi_version = 0, value_size = 0;
    int coherent = 0;

    dir = bpf_map_lookup_elem(&orch_directives, &id);
    if (!dir) {
        *fallback_reason = BRIDGE_FALLBACK_NO_DIRECTIVE;
        return 0;
    }

    /* The control record and per-task value form one logical snapshot.  A
     * writer may update them between locks, so validate a control/directive/
     * control sequence and retry once.  Unstable publication safely falls
     * back to RUN; no shared cross-CPU cache is mutated. */
#pragma unroll
    for (int attempt = 0; attempt < 2; attempt++) {
        __builtin_memset(&ctl, 0, sizeof(ctl));
        __builtin_memset(&verify_ctl, 0, sizeof(verify_ctl));
        if (!snapshot_control(&ctl))
            continue;

        bpf_spin_lock(&dir->lock);
        abi_version = dir->abi_version;
        value_size = dir->value_size;
        out->flags = dir->flags;
        out->scheduler_epoch = dir->scheduler_epoch;
        out->state_generation = ctl.last_generation;
        out->generation = dir->generation;
        value_id = dir->identity;
        out->action = dir->action;
        out->target_cpu = dir->target_cpu;
        out->slice_ns = dir->slice_ns;
        out->not_before_ns = dir->not_before_ns;
        out->throttle_period_ns = dir->throttle_period_ns;
        out->throttle_budget_ns = dir->throttle_budget_ns;
        out->expiry_ns = dir->expiry_ns;
        out->controller_state = dir->controller_state;
        out->policy_mode = dir->policy_mode;
        out->policy_generation = dir->policy_generation;
        bpf_spin_unlock(&dir->lock);

        if (snapshot_control(&verify_ctl) &&
            control_snapshot_equal(&ctl, &verify_ctl) &&
            out->generation != 0 &&
            out->generation <= ctl.last_generation) {
            coherent = 1;
            break;
        }
    }
    if (!coherent) {
        *fallback_reason = BRIDGE_FALLBACK_UNSTABLE_PUBLICATION;
        return 0;
    }

    if (ctl.lease_ns == 0 || ctl.lease_ns > BRIDGE_LEASE_MAX_NS ||
        ctl.heartbeat_ns == 0 || ctl.heartbeat_ns > now ||
        now - ctl.heartbeat_ns > ctl.lease_ns) {
        *fallback_reason = BRIDGE_FALLBACK_STALE_LEASE;
        if (tel)
            tel_inc(&tel->stale_lease_count);
        return 0;
    }

    if (abi_version != ORCHESTRA_ABI_VERSION || value_size != sizeof(*dir) ||
        out->scheduler_epoch != ctl.scheduler_epoch) {
        *fallback_reason = BRIDGE_FALLBACK_BAD_ABI;
        return 0;
    }
    if (!identity_equal(&id, &value_id) || id.start_boottime_ns == 0) {
        *fallback_reason = BRIDGE_FALLBACK_BAD_IDENTITY;
        if (tel)
            tel_inc(&tel->invalid_identity_count);
        return 0;
    }
    if (out->expiry_ns == 0 || out->expiry_ns < now ||
        out->expiry_ns - now > BRIDGE_EXPIRY_MAX_NS) {
        *fallback_reason = BRIDGE_FALLBACK_EXPIRED;
        if (tel)
            tel_inc(&tel->expired_directive_count);
        return 0;
    }
    if (out->action >= ORCHESTRA_ACTION_COUNT) {
        *fallback_reason = BRIDGE_FALLBACK_BAD_ACTION;
        if (tel)
            tel_inc(&tel->invalid_action_count);
        return 0;
    }
    if (!action_allowed(out->action, ctl.controller_state) ||
        out->controller_state != ctl.controller_state ||
        out->policy_mode != ctl.policy_mode ||
        out->policy_generation != ctl.policy_generation) {
        *fallback_reason = BRIDGE_FALLBACK_CONTROLLER;
        return 0;
    }

    if (out->flags & BRIDGE_DIRECTIVE_F_REQUIRE_SIGNAL) {
        struct signal_snapshot signal = {};
        uint32_t signal_reason = BRIDGE_FALLBACK_SIGNAL_INVALID;

        if (!snapshot_signal(&signal) ||
            !signal_is_valid(&signal, &ctl, now, &signal_reason) ||
            signal.sequence < runtime_signal_generation(ctl.scheduler_epoch)) {
            if (signal.sequence < runtime_signal_generation(ctl.scheduler_epoch))
                signal_reason = BRIDGE_FALLBACK_SIGNAL_REPLAY;
            *fallback_reason = signal_reason;
            if (tel) {
                if (signal_reason == BRIDGE_FALLBACK_SIGNAL_STALE)
                    tel_inc(&tel->signal_stale_count);
                else
                    tel_inc(&tel->signal_invalid_count);
            }
            return 0;
        }
        if (tel)
            tel_inc(&tel->signal_accepted_count);
    }

    if (tel)
        tel_inc(&tel->accepted_directive_count);
    return 1;
}

static __always_inline uint64_t clamp_slice(uint64_t requested)
{
    if (requested == 0)
        return BRIDGE_SLICE_RUN_NS;
    if (requested < BRIDGE_SLICE_MIN_NS)
        return BRIDGE_SLICE_MIN_NS;
    if (requested > BRIDGE_SLICE_MAX_NS)
        return BRIDGE_SLICE_MAX_NS;
    return requested;
}

static __always_inline int cpu_is_online(uint32_t cpu)
{
    const struct cpumask *online;
    int valid = 0;

    if (cpu == ORCHESTRA_CPU_ANY || cpu >= (uint32_t)scx_bpf_nr_cpu_ids())
        return 0;
    online = scx_bpf_get_online_cpumask();
    if (online) {
        valid = bpf_cpumask_test_cpu(cpu, online);
        scx_bpf_put_cpumask(online);
    }
    return valid;
}

static __always_inline int cpu_allowed_online(struct task_struct *p,
                                               uint32_t cpu)
{
    return cpu_is_online(cpu) && bpf_cpumask_test_cpu(cpu, p->cpus_ptr);
}

static ORCHESTRA_NOINLINE struct bridge_task_state *get_task_state(
    const struct orchestra_task_identity *id, int create)
{
    struct bridge_task_state zero = {};
    struct bridge_task_state *state;

    state = bpf_map_lookup_elem(&orch_task_state, id);
    if (!state && create) {
        if (bpf_map_update_elem(&orch_task_state, id, &zero,
                                BPF_NOEXIST) != 0) {
            struct bridge_telemetry *tel = global_tel();
            if (tel)
                tel_inc(&tel->map_error_count);
            return NULL;
        }
        state = bpf_map_lookup_elem(&orch_task_state, id);
    }
    return state;
}

/* A new directive generation (or a changed action) starts a fresh accounting
 * period for a changed action. THROTTLE renewals retain runtime_used_ns so
 * a publisher cannot refund consumed service by advancing generation. Keep
 * this transition before dispatch/defer to correlate state and telemetry. */
static __always_inline void sync_task_state_for_action(
    struct bridge_task_state *state, uint64_t generation, uint32_t action,
    uint32_t requested_cpu, uint64_t now)
{
    if (!state)
        return;
    bpf_spin_lock(&state->lock);
    orchestra_sync_task_accounting(state, generation, action, requested_cpu, now);
    bpf_spin_unlock(&state->lock);
}

static ORCHESTRA_NOINLINE struct bridge_task_telemetry *get_task_tel(
    const struct orchestra_task_identity *id, int create)
{
    struct bridge_task_telemetry zero = {
        .dispatched_cpu = -1,
        .actual_cpu = -1,
    };
    struct bridge_task_telemetry *task_tel;

    task_tel = bpf_map_lookup_elem(&orch_task_tel, id);
    if (!task_tel && create) {
        if (bpf_map_update_elem(&orch_task_tel, id, &zero,
                                BPF_NOEXIST) != 0)
            return NULL;
        task_tel = bpf_map_lookup_elem(&orch_task_tel, id);
    }
    return task_tel;
}

static ORCHESTRA_NOINLINE void orchestra_record_result(
    const struct orchestra_task_identity *id,
    const struct directive_snapshot *dir, uint32_t state_index,
    uint32_t source, uint32_t fallback_reason)
{
    struct orchestra_task_hot_v8 *hot = get_task_hot_v8(id, 1);
    struct orchestra_task_diag_v8 *diag = get_task_diag_v8(id, 1);
    struct orchestra_task_coord_v10 *coord_task = get_task_coord_v10(id, 1);
    struct orchestra_runtime_state_v8 *runtime;
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint32_t zero = 0;
    uint64_t signal_generation = 0;
    uint64_t now = bpf_ktime_get_ns();
    uint32_t previous_action = ORCHESTRA_ACTION_RUN;
    uint32_t coordination_previous_action = ORCHESTRA_ACTION_RUN;
    uint32_t coordination_previous_policy_state = 0;
    uint64_t previous_policy_generation = 0;
    uint64_t previous_signal_generation = 0;
    uint64_t previous_controller_generation = 0;
    int action_changed = 0;

    runtime = bpf_map_lookup_elem(&orch_runtime_v8, &zero);
    if (runtime) {
        bpf_spin_lock(&runtime->lock);
        signal_generation = runtime->signal_generation;
        bpf_spin_unlock(&runtime->lock);
    }
    if (hot) {
        bpf_spin_lock(&hot->lock);
        hot->magic = ORCHESTRA_ABI_MAGIC;
        hot->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
        hot->value_size = sizeof(*hot);
        hot->task_schema_version = ORCHESTRA_KERNEL_TASK_SCHEMA_VERSION;
        hot->tgid = id->tgid;
        hot->tid = id->tid;
        hot->start_boottime_ns = id->start_boottime_ns;
        hot->scheduler_epoch = dir->scheduler_epoch;
        previous_policy_generation = hot->policy_generation;
        previous_signal_generation = hot->signal_generation;
        previous_controller_generation = hot->controller_generation;
        hot->signal_generation = signal_generation;
        hot->policy_generation = dir->policy_generation;
        hot->controller_generation = dir->controller_generation;
        hot->policy_index = state_index;
        previous_action = hot->current_action;
        action_changed = previous_action != dir->action;
        hot->previous_action = previous_action;
        hot->current_action = dir->action;
        hot->controller_override_action = dir->controller_override_action;
        hot->target_cpu = dir->target_cpu;
        hot->slice_ns = clamp_slice(dir->slice_ns);
        hot->last_enqueue_ns = now;
        hot->state_transition_ns = now;
        hot->flags |= ORCHESTRA_TASK_V8_F_ENQUEUED;
        hot->flags &= ~(ORCHESTRA_TASK_V8_F_FALLBACK |
                        ORCHESTRA_TASK_V8_F_CONTROLLER_OVERRIDE |
                        ORCHESTRA_TASK_V8_F_POLICY_CACHE_VALID);
        if (fallback_reason != BRIDGE_FALLBACK_NONE)
            hot->flags |= ORCHESTRA_TASK_V8_F_FALLBACK;
        if (fallback_reason == BRIDGE_FALLBACK_CONTROLLER_OVERRIDE)
            hot->flags |= ORCHESTRA_TASK_V8_F_CONTROLLER_OVERRIDE;
        if (dir->action == ORCHESTRA_ACTION_SLEEP)
            hot->sleep_deadline_ns = dir->not_before_ns;
        else
            hot->sleep_deadline_ns = 0;
        if (dir->action == ORCHESTRA_ACTION_THROTTLE)
            hot->throttle_deadline_ns = orchestra_controller_deadline_v10(
                now, dir->throttle_period_ns);
        else
            hot->throttle_deadline_ns = 0;
        if (source == ORCHESTRA_DECISION_SOURCE_POLICY &&
            fallback_reason == BRIDGE_FALLBACK_NONE &&
            dir->action != ORCHESTRA_ACTION_SLEEP &&
            dir->action != ORCHESTRA_ACTION_THROTTLE)
            hot->flags |= ORCHESTRA_TASK_V8_F_POLICY_CACHE_VALID;
        bpf_spin_unlock(&hot->lock);
        if (tel)
            tel8_inc(&tel->task_state_update_count);
    }
    if (diag) {
        bpf_spin_lock(&diag->lock);
        diag->magic = ORCHESTRA_ABI_MAGIC;
        diag->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
        diag->value_size = sizeof(*diag);
        diag->task_schema_version = ORCHESTRA_KERNEL_TASK_SCHEMA_VERSION;
        diag->last_migration_outcome = dir->migration_outcome;
        diag->action_transition_count += (uint64_t)action_changed;
        diag->enqueue_count++;
        if (dir->action == ORCHESTRA_ACTION_MIGRATE)
            diag->migration_request_count++;
        if (dir->action == ORCHESTRA_ACTION_YIELD)
            diag->yield_count++;
        if (dir->action == ORCHESTRA_ACTION_THROTTLE)
            diag->throttle_request_count++;
        if (dir->action == ORCHESTRA_ACTION_SLEEP)
            diag->sleep_request_count++;
        if (fallback_reason == BRIDGE_FALLBACK_BAD_CPU ||
            (dir->action == ORCHESTRA_ACTION_MIGRATE &&
             dir->migration_outcome != ORCHESTRA_MIGRATE_V8_NONE &&
             dir->migration_outcome != ORCHESTRA_MIGRATE_V8_SELECTED &&
             dir->migration_outcome != ORCHESTRA_MIGRATE_V8_ALREADY_LOCAL))
            diag->migration_fallback_count++;
        if (fallback_reason != BRIDGE_FALLBACK_NONE) {
            diag->fallback_count++;
            diag->last_fallback_reason = fallback_reason;
        }
        if (fallback_reason == BRIDGE_FALLBACK_UNSUPPORTED_ACTION)
            diag->unsupported_action_count++;
        if (fallback_reason == BRIDGE_FALLBACK_BAD_ACTION ||
            fallback_reason == BRIDGE_FALLBACK_POLICY_GENERATION)
            diag->invalid_policy_count++;
        if (fallback_reason == BRIDGE_FALLBACK_CONTROLLER_OVERRIDE)
            diag->controller_override_count++;
        if (fallback_reason == BRIDGE_FALLBACK_PROGRESS_GUARD)
            diag->repeated_yield_guard_count++;
        bpf_spin_unlock(&diag->lock);
    }
    if (coord_task) {
        bpf_spin_lock(&coord_task->lock);
        coordination_previous_action = coord_task->actual_executed_action <
            ORCHESTRA_ACTION_COUNT ? coord_task->actual_executed_action :
            coord_task->previous_action;
        coordination_previous_policy_state = coord_task->policy_state;
        coord_task->magic = ORCHESTRA_ABI_MAGIC;
        coord_task->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
        coord_task->value_size = sizeof(*coord_task);
        coord_task->schema_version = ORCHESTRA_COORD_SCHEMA_VERSION;
        coord_task->tgid = id->tgid;
        coord_task->tid = id->tid;
        coord_task->start_boottime_ns = id->start_boottime_ns;
        coord_task->policy_state = state_index;
        coord_task->previous_policy_state = coordination_previous_policy_state;
        coord_task->policy_selected_action =
            dir->policy_selected_action < ORCHESTRA_ACTION_COUNT ?
                dir->policy_selected_action : dir->action;
        coord_task->controller_adjusted_action =
            dir->controller_adjusted_action < ORCHESTRA_ACTION_COUNT ?
                dir->controller_adjusted_action : dir->action;
        coord_task->capability_adjusted_action =
            dir->capability_adjusted_action < ORCHESTRA_ACTION_COUNT ?
                dir->capability_adjusted_action : dir->action;
        /* Execution is recorded after dispatch.  Until then this field is an
         * explicit sentinel and cannot be mistaken for an effective action. */
        coord_task->actual_executed_action = ORCHESTRA_ACTION_COUNT;
        coord_task->previous_action = coordination_previous_action;
        coord_task->current_action = dir->action;
        coord_task->last_fallback_reason = fallback_reason;
        coord_task->last_decision_ns = now;
        bpf_spin_unlock(&coord_task->lock);
    }
    if (tel) {
        if (previous_controller_generation != 0 &&
            previous_controller_generation != dir->controller_generation)
            tel8_inc(&tel->state_generation_change_count);
        if (previous_policy_generation != 0 &&
            previous_policy_generation != dir->policy_generation)
            tel8_inc(&tel->policy_generation_change_count);
        if (previous_signal_generation != 0 &&
            previous_signal_generation != signal_generation)
            tel8_inc(&tel->signal_generation_change_count);
        tel8_action_inc(tel->action_requested_count, dir->action);
        if (fallback_reason != BRIDGE_FALLBACK_NONE)
            tel8_action_inc(tel->action_fallback_count, dir->action);
        if (dir->policy_mode == ORCHESTRA_POLICY_TRAIN)
            tel8_inc(&tel->lifecycle_train_count);
        else if (dir->policy_mode == ORCHESTRA_POLICY_ADAPT)
            tel8_inc(&tel->lifecycle_adapt_count);
        else
            tel8_inc(&tel->lifecycle_evaluate_count);
        if (source == ORCHESTRA_DECISION_SOURCE_POLICY &&
            fallback_reason != BRIDGE_FALLBACK_NONE)
            tel8_inc(&tel->policy_fallback_count);
    }
}

static ORCHESTRA_NOINLINE void orchestra_record_execution_v8(
    const struct orchestra_task_identity *id, uint32_t action, int32_t cpu,
    uint32_t migration_outcome)
{
    struct orchestra_task_hot_v8 *hot = get_task_hot_v8(id, 0);
    struct orchestra_task_diag_v8 *diag = get_task_diag_v8(id, 0);
    struct orchestra_task_coord_v10 *coord_task = get_task_coord_v10(id, 0);
    struct orchestra_telemetry_v8 *tel = global_v8_tel();
    uint64_t now = bpf_ktime_get_ns();
    uint32_t policy_action = action;
    uint32_t capability_action = action;
    uint32_t controller_action = action;
    uint32_t state_index = 0;
    uint32_t previous_policy_state = 0;
    uint32_t previous_action = ORCHESTRA_ACTION_RUN;
    uint32_t fallback_reason = BRIDGE_FALLBACK_NONE;

    if (hot) {
        bpf_spin_lock(&hot->lock);
        hot->target_cpu = cpu < 0 ? ORCHESTRA_CPU_ANY : (uint32_t)cpu;
        hot->last_running_ns = now;
        hot->flags &= ~(ORCHESTRA_TASK_V8_F_DEFERRED |
                        ORCHESTRA_TASK_V8_F_THROTTLED);
        bpf_spin_unlock(&hot->lock);
    }
    if (diag) {
        bpf_spin_lock(&diag->lock);
        if (migration_outcome != ORCHESTRA_MIGRATE_V8_NONE)
            diag->last_migration_outcome = migration_outcome;
        if (action == ORCHESTRA_ACTION_MIGRATE &&
            migration_outcome == ORCHESTRA_MIGRATE_V8_SELECTED)
            diag->migration_success_count++;
        bpf_spin_unlock(&diag->lock);
    }
    if (coord_task) {
        bpf_spin_lock(&coord_task->lock);
        policy_action = coord_task->policy_selected_action;
        controller_action = coord_task->controller_adjusted_action;
        capability_action = coord_task->capability_adjusted_action;
        state_index = coord_task->policy_state;
        previous_policy_state = coord_task->previous_policy_state;
        previous_action = coord_task->previous_action;
        if (coord_task->last_execution_ns == 0) {
            previous_action = action;
            previous_policy_state = state_index;
        }
        fallback_reason = coord_task->last_fallback_reason;
        coord_task->actual_executed_action = action;
        coord_task->previous_action = action;
        coord_task->previous_policy_state = state_index;
        coord_task->last_execution_ns = now;
        if (action == ORCHESTRA_ACTION_MIGRATE) {
            coord_task->last_migration_ns = now;
            coord_task->last_migration_target = cpu < 0 ?
                ORCHESTRA_CPU_ANY : (uint32_t)cpu;
        }
        bpf_spin_unlock(&coord_task->lock);
    }
#ifdef __BPF__
    {
        struct orchestra_coord_action_context action_context = {
            .state = NULL,
            .policy_action = policy_action,
            .controller_action = controller_action,
            .capability_action = capability_action,
            .actual_action = action,
            .state_index = state_index,
            .previous_action = previous_action,
            .previous_policy_state = previous_policy_state,
            .fallback_reason = fallback_reason,
        };

        orchestra_coord_record_action(&action_context, now);
    }
#else
    orchestra_coord_record_action(policy_action, controller_action,
                                  capability_action, action, state_index,
                                  previous_action, previous_policy_state,
                                  fallback_reason, now);
#endif
    orchestra_coord_record_execution(policy_action, capability_action, action,
                                     now);
    if (tel)
        tel8_action_inc(tel->action_executed_count, action);
}

static ORCHESTRA_NOINLINE void orchestra_record_defer_v8(
    const struct orchestra_task_identity *id, uint32_t action,
    uint64_t deadline_ns)
{
    struct orchestra_task_hot_v8 *hot = get_task_hot_v8(id, 0);
    struct orchestra_task_diag_v8 *diag = get_task_diag_v8(id, 0);

    if (hot) {
        bpf_spin_lock(&hot->lock);
        hot->flags |= ORCHESTRA_TASK_V8_F_DEFERRED;
        if (action == ORCHESTRA_ACTION_THROTTLE) {
            hot->flags |= ORCHESTRA_TASK_V8_F_THROTTLED;
            hot->throttle_deadline_ns = deadline_ns;
        } else if (action == ORCHESTRA_ACTION_SLEEP) {
            hot->sleep_deadline_ns = deadline_ns;
        }
        bpf_spin_unlock(&hot->lock);
    }
    if (diag) {
        bpf_spin_lock(&diag->lock);
        if (action == ORCHESTRA_ACTION_THROTTLE)
            diag->throttle_defer_count++;
        else if (action == ORCHESTRA_ACTION_SLEEP)
            diag->sleep_defer_count++;
        bpf_spin_unlock(&diag->lock);
    }
}

static __always_inline void record_fallback(
    const struct orchestra_task_identity *id, uint32_t reason,
    int directive_was_present)
{
    struct bridge_telemetry *tel = global_tel();
    struct orchestra_task_coord_v10 *coord = get_task_coord_v10(id, 0);
    struct bridge_task_telemetry *task_tel;

    if (tel)
        tel_inc(&tel->fallback_count);
    if (coord) {
        bpf_spin_lock(&coord->lock);
        coord->last_fallback_reason = reason;
        bpf_spin_unlock(&coord->lock);
    }
    /* Avoid filling telemetry with every untargeted full-switch task. */
    if (!directive_was_present)
        return;
    task_tel = get_task_tel(id, 1);
    if (task_tel) {
        bpf_spin_lock(&task_tel->lock);
        task_tel->fallback_count++;
        task_tel->fallback_reason = reason;
        bpf_spin_unlock(&task_tel->lock);
    }
}

static __always_inline void record_accepted(
    const struct orchestra_task_identity *id,
    const struct directive_snapshot *dir)
{
    struct bridge_task_telemetry *task_tel = get_task_tel(id, 1);
    uint64_t now = bpf_ktime_get_ns();

    if (!task_tel)
        return;
    bpf_spin_lock(&task_tel->lock);
    task_tel->generation = dir->generation;
    task_tel->action = dir->action;
    task_tel->requested_cpu = dir->target_cpu;
    task_tel->accepted_ns = now;
    task_tel->accepted_count++;
    task_tel->fallback_reason = BRIDGE_FALLBACK_NONE;
    bpf_spin_unlock(&task_tel->lock);
}

static __always_inline void record_dispatched(
    const struct orchestra_task_identity *id, uint64_t generation,
    uint32_t action, int32_t cpu, uint32_t migration_outcome)
{
    struct bridge_telemetry *tel = global_tel();
    struct bridge_task_telemetry *task_tel = get_task_tel(id, 1);
    uint64_t now = bpf_ktime_get_ns();

    if (tel)
        tel_inc(&tel->dispatched_action_count);
    if (task_tel) {
        bpf_spin_lock(&task_tel->lock);
        task_tel->generation = generation;
        task_tel->action = action;
        task_tel->dispatched_cpu = cpu;
        task_tel->dispatched_ns = now;
        task_tel->dispatched_count++;
        bpf_spin_unlock(&task_tel->lock);
    }
    (void)migration_outcome;
}

static __always_inline void retire_released_sleep_directive(
    const struct orchestra_task_identity *id, uint64_t generation)
{
    struct bridge_directive *dir = bpf_map_lookup_elem(&orch_directives, id);

    if (!dir)
        return;

    /* SLEEP is a one-shot eligibility transition.  Retire only the exact
     * generation that was released; a newer userspace directive must remain
     * authoritative.  Converting it to RUN keeps the task work-conserving
     * after release and prevents every later enqueue from reporting the
     * already-expired not_before value as a parameter failure. */
    bpf_spin_lock(&dir->lock);
    if (dir->action == ORCHESTRA_ACTION_SLEEP &&
        dir->generation == generation && identity_equal(&dir->identity, id)) {
        dir->action = ORCHESTRA_ACTION_RUN;
        dir->slice_ns = BRIDGE_SLICE_RUN_NS;
        dir->not_before_ns = 0;
        dir->throttle_period_ns = 0;
        dir->throttle_budget_ns = 0;
    }
    bpf_spin_unlock(&dir->lock);
}

/* A deferred entry outlives the enqueue callback that accepted it.  Re-read
 * the complete directive at release time so a clear, replacement, expiry, or
 * controller transition cannot cause the timer to execute an old action.  A
 * false result is deliberately handled by the caller as a work-conserving
 * RUN fallback. */
static __always_inline int deferred_directive_is_current(
    struct task_struct *p, uint64_t generation, uint32_t action,
    uint64_t eligible_ns, int policy_source, uint32_t *reason)
{
    uint32_t zero = 0;
    struct directive_snapshot *current =
        bpf_map_lookup_elem(&orch_defer_snap, &zero);
    uint32_t current_reason = BRIDGE_FALLBACK_UNSTABLE_PUBLICATION;

    if (!reason || !current)
        return 0;
    *reason = current_reason;
    if (!load_directive(p, current, &current_reason)) {
        *reason = current_reason;
        return 0;
    }
    if (current->generation != generation ||
        (!policy_source && current->action != action)) {
        *reason = BRIDGE_FALLBACK_UNSTABLE_PUBLICATION;
        return 0;
    }
    if (policy_source) {
        struct orchestra_policy_meta_v8 *meta =
            bpf_map_lookup_elem(&orch_meta_v8, &zero);
        int coherent = 0;

        if (meta) {
            bpf_spin_lock(&meta->lock);
            coherent = meta->magic == ORCHESTRA_ABI_MAGIC &&
                meta->abi_version == ORCHESTRA_KERNEL_ABI_VERSION &&
                meta->value_size == sizeof(*meta) &&
                (meta->flags & ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID) &&
                meta->scheduler_epoch == current->scheduler_epoch &&
                meta->policy_generation == current->policy_generation &&
                meta->policy_mode == current->policy_mode &&
                meta->controller_state == current->controller_state;
            bpf_spin_unlock(&meta->lock);
        }
        if (!coherent) {
            *reason = BRIDGE_FALLBACK_POLICY_GENERATION;
            return 0;
        }
    }
    /* The native controller can shorten/replace the effective defer time.
     * The immutable source generation identifies the original directive;
     * comparing its original deadline to the adjusted deadline is invalid. */
    if (action == ORCHESTRA_ACTION_SLEEP && eligible_ns == 0) {
        *reason = BRIDGE_FALLBACK_UNSTABLE_PUBLICATION;
        return 0;
    }
    *reason = BRIDGE_FALLBACK_NONE;
    return 1;
}

static __always_inline void dispatch_run(struct task_struct *p,
                                         uint64_t enq_flags,
                                         int direct_local, int32_t cpu)
{
    struct bridge_telemetry *tel = global_tel();

    orchestra_dsq_insert(p, direct_local ? SCX_DSQ_LOCAL : SCX_DSQ_GLOBAL,
                         BRIDGE_SLICE_RUN_NS, enq_flags);
    if (tel)
        tel_inc(&tel->run_dispatched_count);
    (void)cpu;
}

static __always_inline int defer_task(
    struct task_struct *p, const struct orchestra_task_identity *id,
    struct bridge_task_state *state, uint64_t eligible_ns,
    uint64_t generation, uint32_t action, uint64_t enq_flags)
{
    struct bridge_telemetry *tel = global_tel();

    if (!state || eligible_ns <= bpf_ktime_get_ns())
        return 0;
    bpf_spin_lock(&state->lock);
    state->generation = generation;
    state->action = action;
    state->eligible_ns = eligible_ns;
    state->flags |= BRIDGE_TASK_F_DEFERRED;
    bpf_spin_unlock(&state->lock);
    orchestra_dsq_insert_vtime(p, BRIDGE_DEFERRED_DSQ,
                               BRIDGE_SLICE_RUN_NS, eligible_ns, enq_flags);
    if (tel)
        tel_inc(&tel->deferred_count);
    /* Insertion into the deferred DSQ is queued/deferred, not CPU dispatch. */
    return 1;
}

static __always_inline void orchestra_execute_action(
    struct task_struct *p, uint64_t enq_flags,
    const struct directive_snapshot *dir, int have_directive,
    uint32_t fallback_reason, int direct_insert, int direct_local,
    int32_t selected_cpu, uint32_t state_index, uint32_t source)
{
    struct orchestra_task_identity id = task_identity(p);
    struct bridge_telemetry *tel = global_tel();
    struct bridge_task_state *state;
    uint64_t now = bpf_ktime_get_ns();
    uint64_t slice;

    if (tel)
        tel_inc(&tel->placement_count);
    if (tel && direct_insert)
        tel_inc(&tel->select_cpu_direct_insert_count);
    if (have_directive)
        orchestra_record_result(&id, dir, state_index, source, fallback_reason);
    if (!have_directive) {
        /* A cleared/expired admission must also clear the previous effective
         * action before RUN is visible on another CPU. Do not allocate
         * adaptive state for ordinary, unadmitted host tasks. */
        state = get_task_state(&id, 0);
        if (state) {
            bpf_spin_lock(&state->lock);
            orchestra_clear_task_action(state);
            bpf_spin_unlock(&state->lock);
        }
        record_fallback(&id, fallback_reason,
                        fallback_reason != BRIDGE_FALLBACK_NO_DIRECTIVE);
        dispatch_run(p, enq_flags, direct_local, selected_cpu);
        return;
    }

    record_accepted(&id, dir);
    if (fallback_reason != BRIDGE_FALLBACK_NONE)
        record_fallback(&id, fallback_reason, 1);
    state = get_task_state(&id, 1);
    if (!state) {
        record_fallback(&id, BRIDGE_FALLBACK_MAP_ERROR, 1);
        dispatch_run(p, enq_flags, direct_local, selected_cpu);
        record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
        return;
    }
    sync_task_state_for_action(state, dir->generation, dir->action,
                               dir->target_cpu, now);
    bpf_spin_lock(&state->lock);
    state->flags &= ~ORCHESTRA_TASK_STATE_F_POLICY;
    if (source == ORCHESTRA_DECISION_SOURCE_POLICY &&
        fallback_reason == BRIDGE_FALLBACK_NONE)
        state->flags |= ORCHESTRA_TASK_STATE_F_POLICY;
    bpf_spin_unlock(&state->lock);
    slice = clamp_slice(dir->slice_ns);

    switch (dir->action) {
    case ORCHESTRA_ACTION_RUN:
        orchestra_dsq_insert(p, direct_local ? SCX_DSQ_LOCAL : SCX_DSQ_GLOBAL,
                             slice, enq_flags);
        if (tel)
            tel_inc(&tel->run_dispatched_count);
        bpf_spin_lock(&state->lock);
        state->dispatched_cpu = direct_local ? (uint32_t)selected_cpu : ORCHESTRA_CPU_ANY;
        bpf_spin_unlock(&state->lock);
        record_dispatched(&id, dir->generation, dir->action,
                          direct_local ? selected_cpu : -1,
                          dir->migration_outcome);
        return;

    case ORCHESTRA_ACTION_YIELD:
        /* Queue-tail relinquish with the minimum legal opportunity.  A lone
         * task remains work-conserving, while peers get priority over it. */
        orchestra_dsq_insert(p, SCX_DSQ_GLOBAL, BRIDGE_SLICE_MIN_NS,
                             enq_flags & ~SCX_ENQ_HEAD);
        if (tel)
            tel_inc(&tel->yield_dispatched_count);
        bpf_spin_lock(&state->lock);
        state->dispatched_cpu = ORCHESTRA_CPU_ANY;
        bpf_spin_unlock(&state->lock);
        record_dispatched(&id, dir->generation, dir->action, -1,
                          dir->migration_outcome);
        return;

    case ORCHESTRA_ACTION_SLEEP:
        if (dir->not_before_ns <= now ||
            dir->not_before_ns - now > BRIDGE_SLEEP_MAX_NS) {
            sync_task_state_for_action(state, dir->generation,
                                       ORCHESTRA_ACTION_RUN, ORCHESTRA_CPU_ANY, now);
            record_fallback(&id, BRIDGE_FALLBACK_BAD_PARAMETERS, 1);
            dispatch_run(p, enq_flags, direct_local, selected_cpu);
            record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
            return;
        }
        if (tel) {
            tel_inc(&tel->sleep_accepted_count);
            tel_inc(&tel->sleep_deferred_count);
        }
        if (!defer_task(p, &id, state, dir->not_before_ns,
                        dir->generation, dir->action, enq_flags)) {
            sync_task_state_for_action(state, dir->generation,
                                       ORCHESTRA_ACTION_RUN, ORCHESTRA_CPU_ANY, now);
            record_fallback(&id, BRIDGE_FALLBACK_MAP_ERROR, 1);
            dispatch_run(p, enq_flags, direct_local, selected_cpu);
            record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
        } else {
            orchestra_record_defer_v8(&id, dir->action, dir->not_before_ns);
        }
        return;

    case ORCHESTRA_ACTION_THROTTLE: {
        uint64_t period = dir->throttle_period_ns;
        uint64_t budget = dir->throttle_budget_ns;
        uint64_t elapsed;
        uint64_t remaining;

        if (period < BRIDGE_SLICE_MIN_NS || period > BRIDGE_THROTTLE_MAX_NS ||
            budget < BRIDGE_SLICE_MIN_NS || budget >= period) {
            sync_task_state_for_action(state, dir->generation,
                                       ORCHESTRA_ACTION_RUN, ORCHESTRA_CPU_ANY, now);
            record_fallback(&id, BRIDGE_FALLBACK_BAD_PARAMETERS, 1);
            dispatch_run(p, enq_flags, direct_local, selected_cpu);
            record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
            return;
        }
        if (tel)
            tel_inc(&tel->throttle_accepted_count);
        bpf_spin_lock(&state->lock);
        elapsed = now - state->period_start_ns;
        if (elapsed >= period) {
            state->period_start_ns = now;
            state->runtime_used_ns = 0;
        }
        remaining = state->runtime_used_ns;
        bpf_spin_unlock(&state->lock);
        if (remaining >= budget) {
            bpf_spin_lock(&state->lock);
            state->flags |= BRIDGE_TASK_F_THROTTLED;
            bpf_spin_unlock(&state->lock);
            if (tel)
                tel_inc(&tel->throttle_deferred_count);
            bpf_spin_lock(&state->lock);
            elapsed = state->period_start_ns + period;
            bpf_spin_unlock(&state->lock);
            if (!defer_task(p, &id, state,
                            elapsed,
                            dir->generation, dir->action, enq_flags)) {
                sync_task_state_for_action(state, dir->generation,
                                       ORCHESTRA_ACTION_RUN, ORCHESTRA_CPU_ANY, now);
            record_fallback(&id, BRIDGE_FALLBACK_MAP_ERROR, 1);
                dispatch_run(p, enq_flags, direct_local, selected_cpu);
                record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
            } else {
                orchestra_record_defer_v8(&id, dir->action, elapsed);
            }
            return;
        }
        remaining = budget - remaining;
        if (slice > remaining)
            slice = remaining;
        orchestra_dsq_insert(p, direct_local ? SCX_DSQ_LOCAL : SCX_DSQ_GLOBAL,
                             slice, enq_flags);
        bpf_spin_lock(&state->lock);
        state->dispatched_cpu = direct_local ? (uint32_t)selected_cpu : ORCHESTRA_CPU_ANY;
        bpf_spin_unlock(&state->lock);
        record_dispatched(&id, dir->generation, dir->action,
                          direct_local ? selected_cpu : -1,
                          dir->migration_outcome);
        return;
    }

    case ORCHESTRA_ACTION_MIGRATE:
        if (!cpu_allowed_online(p, dir->target_cpu)) {
            if (tel)
                tel_inc(&tel->invalid_cpu_count);
            sync_task_state_for_action(state, dir->generation,
                                       ORCHESTRA_ACTION_RUN, ORCHESTRA_CPU_ANY, now);
            record_fallback(&id, BRIDGE_FALLBACK_BAD_CPU, 1);
            /* A target which became illegal between selection and insertion
             * must not receive a LOCAL direct dispatch. */
            dispatch_run(p, enq_flags, 0, -1);
            record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
            return;
        }
        if (tel)
            tel_inc(&tel->migrate_accepted_count);
        /* select_cpu pairs SCX_DSQ_LOCAL with its returned target.  Normal
         * re-enqueues have no returned CPU, so use the explicit LOCAL_ON DSQ
         * after repeating the online/affinity check above. */
        if (!direct_local || selected_cpu != (int32_t)dir->target_cpu) {
            orchestra_dsq_insert(p, SCX_DSQ_LOCAL_ON | dir->target_cpu,
                                 slice, enq_flags);
            bpf_spin_lock(&state->lock);
            state->dispatched_cpu = dir->target_cpu;
            bpf_spin_unlock(&state->lock);
            if (tel)
                tel_inc(&tel->migrate_dispatched_count);
            record_dispatched(&id, dir->generation, dir->action,
                              (int32_t)dir->target_cpu,
                              dir->migration_outcome);
            return;
        }
        orchestra_dsq_insert(p, SCX_DSQ_LOCAL, slice, enq_flags);
        bpf_spin_lock(&state->lock);
        state->dispatched_cpu = dir->target_cpu;
        bpf_spin_unlock(&state->lock);
        if (tel)
            tel_inc(&tel->migrate_dispatched_count);
        record_dispatched(&id, dir->generation, dir->action, selected_cpu,
                          dir->migration_outcome);
        return;

    default:
        if (tel)
            tel_inc(&tel->invalid_action_count);
        record_fallback(&id, BRIDGE_FALLBACK_BAD_ACTION, 1);
        dispatch_run(p, enq_flags, direct_local, selected_cpu);
        record_dispatched(&id, dir->generation, ORCHESTRA_ACTION_RUN,
                          -1, dir->migration_outcome);
        return;
    }
}

static ORCHESTRA_NOINLINE void orchestra_drain_deferred(uint64_t now)
{
    struct task_struct *p;
    struct bridge_telemetry *global = global_tel();
    uint32_t scanned = 0;

    if (global)
        tel_inc(&global->deferred_timer_tick_count);
    bpf_rcu_read_lock();
    bpf_for_each(scx_dsq, p, BRIDGE_DEFERRED_DSQ, 0) {
        struct orchestra_task_identity id = task_identity(p);
        struct bridge_task_state *state = get_task_state(&id, 0);
        uint64_t generation = 0;
        uint64_t eligible = 0;
        uint64_t destination = SCX_DSQ_GLOBAL;
        uint32_t action = ORCHESTRA_ACTION_RUN;
        uint32_t old_flags = 0;
        uint32_t old_cpu = ORCHESTRA_CPU_ANY;
        uint32_t reason = BRIDGE_FALLBACK_MAP_ERROR;
        int32_t cpu = -1;
        int current = 0;

        if (scanned++ >= BRIDGE_DEFER_SCAN_MAX)
            break;
        if (global)
            tel_inc(&global->deferred_timer_scanned_count);
        if (state) {
            bpf_spin_lock(&state->lock);
            generation = state->generation;
            eligible = state->eligible_ns;
            action = state->action;
            old_flags = state->flags;
            old_cpu = state->dispatched_cpu;
            bpf_spin_unlock(&state->lock);
            current = deferred_directive_is_current(
                p, generation, action, eligible,
                (old_flags & ORCHESTRA_TASK_STATE_F_POLICY) != 0, &reason);
            if (current && !orchestra_controller_action_allowed_v10(
                    orchestra_controller_read_state_v10(), action)) {
                current = 0;
                reason = BRIDGE_FALLBACK_CONTROLLER_OVERRIDE;
            }
        }
        /* Revoke stale work promptly, including entries whose original
         * eligibility deadline is still in the future. */
        if (current && eligible > now) {
            if (global)
                tel_inc(&global->deferred_timer_future_count);
            continue;
        }
        if (current) {
            cpu = scx_bpf_pick_any_cpu(p->cpus_ptr, 0);
            if (cpu >= 0 && cpu_allowed_online(p, (uint32_t)cpu))
                destination = SCX_DSQ_LOCAL_ON | (uint32_t)cpu;
            else {
                current = 0;
                reason = BRIDGE_FALLBACK_BAD_CPU;
                if (global)
                    tel_inc(&global->deferred_cpu_failure_count);
            }
        }
        /* Publish the action before the move: another CPU can run the task
         * immediately. Queue insertion is not execution evidence. */
        if (!current)
            record_fallback(&id, reason, 1);
        if (state) {
            bpf_spin_lock(&state->lock);
            state->flags &= ~(BRIDGE_TASK_F_DEFERRED |
                              BRIDGE_TASK_F_THROTTLED);
            state->action = current ? action : ORCHESTRA_ACTION_RUN;
            state->dispatched_cpu = current ? (uint32_t)cpu : ORCHESTRA_CPU_ANY;
            bpf_spin_unlock(&state->lock);
        }
        if (!orchestra_dsq_move_from_dsq(BPF_FOR_EACH_ITER, p,
                                         destination, 0)) {
            /* A local destination can disappear during hotplug. */
            current = 0;
            reason = BRIDGE_FALLBACK_BAD_CPU;
            record_fallback(&id, reason, 1);
            if (state) {
                bpf_spin_lock(&state->lock);
                state->action = ORCHESTRA_ACTION_RUN;
                state->dispatched_cpu = ORCHESTRA_CPU_ANY;
                bpf_spin_unlock(&state->lock);
            }
            if (destination == SCX_DSQ_GLOBAL ||
                !orchestra_dsq_move_from_dsq(BPF_FOR_EACH_ITER, p,
                                             SCX_DSQ_GLOBAL, 0)) {
                if (state) {
                    bpf_spin_lock(&state->lock);
                    state->flags = old_flags;
                    state->action = action;
                    state->dispatched_cpu = old_cpu;
                    bpf_spin_unlock(&state->lock);
                }
                if (global)
                    tel_inc(&global->deferred_release_failure_count);
                continue;
            }
        }
        if (action == ORCHESTRA_ACTION_SLEEP)
            retire_released_sleep_directive(&id, generation);
        record_dispatched(&id, generation,
                          current ? action : ORCHESTRA_ACTION_RUN,
                          current ? cpu : -1, ORCHESTRA_MIGRATE_V8_NONE);
        if (global) {
            tel_inc(&global->deferred_release_count);
            if (!current)
                tel_inc(&global->run_dispatched_count);
        }
        if (current)
            scx_bpf_kick_cpu(cpu, SCX_KICK_IDLE);
    }
    bpf_rcu_read_unlock();
}

static int deferred_timerfn(void *map, int *key, struct bpf_timer *timer)
{
    uint64_t now = bpf_ktime_get_ns();
    (void)map;
    (void)key;
    /* The outer control loop is periodic even if global RUN stays busy.
     * Separate calls bound stack depth without bypassing feedback gates. */
    orchestra_coord_finalize_global_live(now);
    orchestra_drain_deferred(now);
    if (bpf_timer_start(timer, BRIDGE_DEFER_TICK_NS, 0) != 0)
        scx_bpf_error("ORCHESTRA deferred timer re-arm failed");
    return 0;
}

static ORCHESTRA_NOINLINE int orchestra_start_deferred_timer(uint32_t key)
{
    struct bridge_defer_timer *defer;
    int ret;

    defer = bpf_map_lookup_elem(&orch_defer_tmr, &key);
    if (!defer)
        return -ESRCH;
    ret = bpf_timer_init(&defer->timer, &orch_defer_tmr, CLOCK_MONOTONIC);
    if (ret)
        return ret;
    ret = bpf_timer_set_callback(&defer->timer, deferred_timerfn);
    if (ret)
        return ret;
    return bpf_timer_start(&defer->timer, BRIDGE_DEFER_TICK_NS, 0);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(orchestra_sched_init)
{
    struct bridge_control *ctl;
    struct bridge_signal_frame *signal;
    struct orchestra_runtime_state_v8 *runtime;
    struct orchestra_runtime_state_v10 *runtime10;
    struct orchestra_policy_meta_v8 *meta;
    struct orchestra_telemetry_v8 *kernel_tel;
    struct bridge_telemetry *tel = global_tel();
    uint32_t key = 0;
    uint64_t epoch = bpf_ktime_get_ns();
    int ret;

    if (tel)
        tel_inc(&tel->load_count);
    if (!epoch)
        epoch = 1;

    ctl = bpf_map_lookup_elem(&orch_control, &key);
    if (!ctl)
        return -ESRCH;
    bpf_spin_lock(&ctl->lock);
    ctl->magic = ORCHESTRA_ABI_MAGIC;
    ctl->abi_version = ORCHESTRA_ABI_VERSION;
    ctl->value_size = sizeof(*ctl);
    ctl->capability_flags = ORCHESTRA_KERNEL_V10_REQUIRED_CAPS |
                            BRIDGE_LEGACY_REQUIRED_CAPS;
    ctl->scheduler_epoch = epoch;
    ctl->last_generation = 0;
    ctl->publisher_heartbeat_ns = 0;
    ctl->publisher_lease_ns = 0;
    ctl->policy_generation = 0;
    ctl->controller_state = ORCHESTRA_CTRL_NORMAL;
    ctl->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    ctl->publication_status = BRIDGE_PUB_OK;
    ctl->scx_api_version = ORCHESTRA_SCX_API_VERSION;
    bpf_spin_unlock(&ctl->lock);

    signal = bpf_map_lookup_elem(&orch_signal, &key);
    if (!signal)
        return -ESRCH;
    bpf_spin_lock(&signal->lock);
    signal->magic = ORCHESTRA_ABI_MAGIC;
    signal->abi_version = ORCHESTRA_ABI_VERSION;
    signal->value_size = sizeof(*signal);
    signal->flags = 0;
    signal->tier = 0;
    signal->source_id = 0;
    signal->scheduler_epoch = epoch;
    signal->sequence = 0;
    signal->published_ns = 0;
    signal->expires_ns = 0;
    signal->key_epoch = 0;
    signal->directive = ORCHESTRA_ACTION_RUN;
    signal->state_schema_version = 0;
    signal->prediction_used = 0;
    signal->confidence_permille = 0;
    signal->cpu_now_permille = 0;
    signal->cpu_pred_permille = 0;
    signal->decision_cpu_permille = 0;
    signal->memory_pressure_permille = 0;
    signal->thermal_permille = 0;
    signal->s1_permille = 0;
    signal->s2_permille = 0;
    signal->s3_permille = 0;
    signal->s4_permille = 0;
    signal->q_permille = 0;
    signal->controller_state = ORCHESTRA_CTRL_NORMAL;
    signal->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    signal->policy_generation = 0;
    signal->reserved = 0;
    bpf_spin_unlock(&signal->lock);

    runtime = bpf_map_lookup_elem(&orch_runtime_v8, &key);
    if (!runtime)
        return -ESRCH;
    bpf_spin_lock(&runtime->lock);
    runtime->magic = ORCHESTRA_ABI_MAGIC;
    runtime->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
    runtime->value_size = sizeof(*runtime);
    runtime->state_schema_version = ORCHESTRA_KERNEL_STATE_SCHEMA_VERSION;
    runtime->flags = ORCHESTRA_RUNTIME_V8_F_CONTROLLER_VALID |
                     ORCHESTRA_RUNTIME_V8_F_POLICY_VALID;
    runtime->scheduler_epoch = epoch;
    runtime->state_generation = 0;
    runtime->signal_generation = 0;
    runtime->prediction_generation = 0;
    runtime->prediction_fallback_reason = BRIDGE_FALLBACK_SIGNAL_STALE;
    runtime->controller_state = ORCHESTRA_CTRL_NORMAL;
    runtime->controller_schema_version =
        ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION;
    runtime->controller_generation = 0;
    runtime->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    runtime->policy_schema_version = ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION;
    runtime->policy_generation = 0;
    runtime->published_ns = epoch;
    runtime->expires_ns = 0;
    bpf_spin_unlock(&runtime->lock);

    meta = bpf_map_lookup_elem(&orch_meta_v8, &key);
    if (!meta)
        return -ESRCH;
    bpf_spin_lock(&meta->lock);
    meta->magic = ORCHESTRA_ABI_MAGIC;
    meta->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
    meta->value_size = sizeof(*meta);
    meta->policy_schema_version = ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION;
    meta->active_bank = 0;
    meta->entry_count = ORCHESTRA_KERNEL_MAX_POLICY_STATES;
    meta->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    meta->controller_state = ORCHESTRA_CTRL_NORMAL;
    meta->controller_schema_version =
        ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION;
    meta->flags = ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID |
                  ORCHESTRA_POLICY_META_V8_F_EVALUATE;
    meta->scheduler_epoch = epoch;
    meta->policy_generation = 0;
    meta->previous_generation = 0;
    meta->published_ns = epoch;
    meta->capability_flags = ORCHESTRA_KERNEL_V10_REQUIRED_CAPS;
    bpf_spin_unlock(&meta->lock);

    kernel_tel = global_v8_tel();
    if (!kernel_tel)
        return -ESRCH;
    bpf_spin_lock(&kernel_tel->lock);
    kernel_tel->magic = ORCHESTRA_ABI_MAGIC;
    kernel_tel->abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
    kernel_tel->value_size = sizeof(*kernel_tel);
    kernel_tel->telemetry_schema_version =
        ORCHESTRA_KERNEL_TELEMETRY_SCHEMA_VERSION;
    bpf_spin_unlock(&kernel_tel->lock);

    runtime10 = bpf_map_lookup_elem(&orch_runtime10, &key);
    if (!runtime10)
        return -ESRCH;
    bpf_spin_lock(&runtime10->lock);
    runtime10->magic = ORCHESTRA_ABI_MAGIC;
    runtime10->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
    runtime10->value_size = sizeof(*runtime10);
    runtime10->schema_version = ORCHESTRA_RUNTIME_SCHEMA_VERSION;
    runtime10->flags = ORCHESTRA_CONTROLLER_F_VALID;
    runtime10->scope = ORCHESTRA_COORD_SCOPE_GLOBAL;
    runtime10->domain_id = ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT;
    runtime10->scheduler_epoch = epoch;
    runtime10->controller_state = ORCHESTRA_CTRL_NORMAL;
    runtime10->controller_flags = ORCHESTRA_CONTROLLER_F_VALID;
    runtime10->controller_generation = 1;
    runtime10->published_ns = epoch;
    bpf_spin_unlock(&runtime10->lock);

    orchestra_controller_init_v10(epoch);

    ret = scx_bpf_create_dsq(BRIDGE_DEFERRED_DSQ, -1);
    if (ret)
        return ret;
    return orchestra_start_deferred_timer(key);
}

void BPF_STRUCT_OPS(orchestra_sched_exit, struct scx_exit_info *ei)
{
    struct bridge_telemetry *tel = global_tel();

    if (!tel)
        return;
    tel_inc(&tel->unload_count);
    if (ei && (ei->kind == SCX_EXIT_ERROR || ei->kind == SCX_EXIT_UNREG))
        tel_inc(&tel->scheduler_error_count);
}

s32 BPF_STRUCT_OPS(orchestra_sched_enable, struct task_struct *p)
{
    struct orchestra_pid_key key = { .tgid = p->tgid, .tid = p->pid };
    struct bridge_identity_record identity = {
        .start_boottime_ns = p->start_boottime,
    };
    struct bridge_control *ctl;
    struct bridge_telemetry *tel = global_tel();
    uint32_t zero = 0;

    ctl = bpf_map_lookup_elem(&orch_control, &zero);
    if (ctl)
        identity.scheduler_epoch = ctl->scheduler_epoch;
    if (bpf_map_update_elem(&orch_identity, &key, &identity, BPF_ANY) != 0 && tel)
        tel_inc(&tel->map_error_count);
    if (tel)
        tel_inc(&tel->task_enable_count);
    return 0;
}

void BPF_STRUCT_OPS(orchestra_sched_disable, struct task_struct *p)
{
    struct orchestra_task_identity id = task_identity(p);
    struct orchestra_pid_key key = { .tgid = p->tgid, .tid = p->pid };
    struct bridge_identity_record *record;
    struct bridge_telemetry *tel = global_tel();

    record = bpf_map_lookup_elem(&orch_identity, &key);
    if (record && record->start_boottime_ns == id.start_boottime_ns)
        bpf_map_delete_elem(&orch_identity, &key);
    bpf_map_delete_elem(&orch_directives, &id);
    bpf_map_delete_elem(&orch_task_state, &id);
    bpf_map_delete_elem(&orch_task_tel, &id);
    bpf_map_delete_elem(&orch_task_v8, &id);
    bpf_map_delete_elem(&orch_diag_v8, &id);
    bpf_map_delete_elem(&orch_task_coord, &id);
    if (tel)
        tel_inc(&tel->task_disable_count);
}

s32 BPF_STRUCT_OPS(orchestra_sched_select_cpu, struct task_struct *p,
                   s32 prev_cpu, u64 wake_flags)
{
    struct bridge_telemetry *tel = global_tel();
    bool is_idle = false;
    s32 cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);

    if (tel)
        tel_inc(&tel->select_cpu_count);
    (void)is_idle;
    return cpu;
}

void BPF_STRUCT_OPS(orchestra_sched_enqueue, struct task_struct *p,
                    u64 enq_flags)
{
    struct bridge_telemetry *tel = global_tel();

    if (tel)
        tel_inc(&tel->enqueue_callback_count);
    uint32_t scratch_key = 0;
    struct orchestra_decision_work *work =
        bpf_map_lookup_elem(&orch_dec_work, &scratch_key);
    if (!work) {
        dispatch_run(p, enq_flags, 0, -1);
        return;
    }
    struct decision_snapshot_v8 *decision = &work->decision;
    int valid = orchestra_decide(p, decision);
    orchestra_execute_action(p, enq_flags, &decision->directive, valid,
                             decision->fallback_reason, 0, 0, -1,
                             decision->state_index, decision->source);
}

void BPF_STRUCT_OPS(orchestra_sched_dispatch, s32 cpu,
                    struct task_struct *prev)
{
    struct orchestra_task_identity id;
    struct runtime_snapshot_v8 *state;
    struct bridge_telemetry *tel = global_tel();
    uint32_t key = 0;

    if (tel)
        tel_inc(&tel->dispatch_callback_count);
    if (prev) {
        id = task_identity(prev);
        if (bpf_map_lookup_elem(&orch_directives, &id)) {
            state = bpf_map_lookup_elem(&orch_runtime_scratch, &key);
            if (state && orchestra_read_runtime_state(prev, state))
                orchestra_flush_observation();
        }
    }
    /*
     * The core consumes SCX_DSQ_GLOBAL before invoking dispatch().
     * scx_bpf_consume() accepts only user-created non-local DSQs; passing
     * the reserved global ID is a runtime sched_ext error which detaches
     * the scheduler.  Deferred work is promoted by deferred_timerfn(), so
     * there is no custom DSQ for this callback to consume.
     */
    (void)cpu;
    (void)prev;
}

void BPF_STRUCT_OPS(orchestra_sched_running, struct task_struct *p)
{
    struct orchestra_task_identity id = task_identity(p);
    struct bridge_task_state *state = get_task_state(&id, 0);
    struct bridge_task_telemetry *task_tel = get_task_tel(&id, 0);
    struct orchestra_task_hot_v8 *hot = get_task_hot_v8(&id, 0);
    struct orchestra_task_diag_v8 *diag = get_task_diag_v8(&id, 0);
    struct bridge_telemetry *tel = global_tel();
    uint64_t now = bpf_ktime_get_ns();
    uint64_t telemetry_generation = 0;
    uint64_t state_generation = 0;
    uint64_t eligible_ns = 0;
    uint32_t action = ORCHESTRA_ACTION_RUN;
    uint32_t requested_cpu = ORCHESTRA_CPU_ANY;
    int32_t cpu = bpf_get_smp_processor_id();
    int migrate_target = 0;
    int migrate_other = 0;

    if (tel)
        tel_inc(&tel->running_count);
    if (!state || !task_tel)
        return;
    bpf_spin_lock(&state->lock);
    state_generation = state->generation;
    action = state->action;
    requested_cpu = state->requested_cpu;
    eligible_ns = state->eligible_ns;
    bpf_spin_unlock(&state->lock);
    bpf_spin_lock(&task_tel->lock);
    telemetry_generation = task_tel->generation;
    bpf_spin_unlock(&task_tel->lock);
    if (state_generation != telemetry_generation)
        return;
    if (hot) {
        bpf_spin_lock(&hot->lock);
        hot->flags |= ORCHESTRA_TASK_V8_F_RUNNING;
        hot->flags &= ~(ORCHESTRA_TASK_V8_F_DEFERRED |
                        ORCHESTRA_TASK_V8_F_THROTTLED |
                        ORCHESTRA_TASK_V8_F_ENQUEUED);
        hot->target_cpu = (uint32_t)cpu;
        hot->last_running_ns = now;
        hot->running_since_ns = now;
        bpf_spin_unlock(&hot->lock);
    }
    if (diag) {
        bpf_spin_lock(&diag->lock);
        diag->running_count++;
        bpf_spin_unlock(&diag->lock);
    }
    bpf_spin_lock(&state->lock);
    state->running_since_ns = now;
    state->flags |= BRIDGE_TASK_F_RUNNING;
    bpf_spin_unlock(&state->lock);
    bpf_spin_lock(&task_tel->lock);
    task_tel->actual_cpu = cpu;
    task_tel->running_ns = now;
    task_tel->running_count++;

    switch (action) {
    case ORCHESTRA_ACTION_MIGRATE:
        if ((uint32_t)cpu == requested_cpu) {
            task_tel->effective_count++;
            migrate_target = 1;
        } else {
            task_tel->error_count++;
            migrate_other = 1;
        }
        break;
    case ORCHESTRA_ACTION_SLEEP:
    case ORCHESTRA_ACTION_THROTTLE:
        if (now < eligible_ns)
            task_tel->error_count++;
        else
            task_tel->effective_count++;
        break;
    case ORCHESTRA_ACTION_RUN:
    case ORCHESTRA_ACTION_YIELD:
        task_tel->effective_count++;
        break;
    default:
        task_tel->error_count++;
        break;
    }
    bpf_spin_unlock(&task_tel->lock);
    if (tel && migrate_target)
        tel_inc(&tel->migrate_running_target_count);
    if (tel && migrate_other)
        tel_inc(&tel->migrate_running_other_count);
    orchestra_record_execution_v8(&id, action, cpu,
        migrate_target ? ORCHESTRA_MIGRATE_V8_SELECTED :
        (migrate_other ? ORCHESTRA_MIGRATE_V8_FALLBACK :
                         ORCHESTRA_MIGRATE_V8_NONE));
}

void BPF_STRUCT_OPS(orchestra_sched_stopping, struct task_struct *p,
                    bool runnable)
{
    struct orchestra_task_identity id = task_identity(p);
    struct bridge_task_state *state = get_task_state(&id, 0);
    struct bridge_task_telemetry *task_tel = get_task_tel(&id, 0);
    struct orchestra_task_hot_v8 *hot = get_task_hot_v8(&id, 0);
    struct bridge_telemetry *tel = global_tel();
    uint64_t now = bpf_ktime_get_ns();
    uint64_t delta = 0;
    uint64_t running_since = 0;

    if (tel)
        tel_inc(&tel->stopping_count);
    if (!state)
        return;
    bpf_spin_lock(&state->lock);
    if (!(state->flags & BRIDGE_TASK_F_RUNNING)) {
        bpf_spin_unlock(&state->lock);
        return;
    }
    running_since = state->running_since_ns;
    if (now >= running_since)
        delta = now - running_since;
    if (ORCHESTRA_U64_MAX - state->runtime_used_ns < delta)
        state->runtime_used_ns = ORCHESTRA_U64_MAX;
    else
        state->runtime_used_ns += delta;
    state->running_since_ns = 0;
    state->flags &= ~BRIDGE_TASK_F_RUNNING;
    bpf_spin_unlock(&state->lock);
    if (hot) {
        bpf_spin_lock(&hot->lock);
        hot->flags &= ~ORCHESTRA_TASK_V8_F_RUNNING;
        hot->running_since_ns = 0;
        bpf_spin_unlock(&hot->lock);
    }
    if (task_tel) {
        bpf_spin_lock(&task_tel->lock);
        task_tel->stopped_ns = now;
        if (ORCHESTRA_U64_MAX - task_tel->runtime_ns < delta)
            task_tel->runtime_ns = ORCHESTRA_U64_MAX;
        else
            task_tel->runtime_ns += delta;
        bpf_spin_unlock(&task_tel->lock);
    }
    (void)runnable;
}

SCX_OPS_DEFINE(orchestra_sched_ops,
    .select_cpu = (void *)orchestra_sched_select_cpu,
    .enqueue = (void *)orchestra_sched_enqueue,
    .dispatch = (void *)orchestra_sched_dispatch,
    .running = (void *)orchestra_sched_running,
    .stopping = (void *)orchestra_sched_stopping,
    .enable = (void *)orchestra_sched_enable,
    .disable = (void *)orchestra_sched_disable,
    .init = (void *)orchestra_sched_init,
    .exit = (void *)orchestra_sched_exit,
    .flags = SCX_OPS_KEEP_BUILTIN_IDLE | SCX_OPS_ENQ_LAST,
    .name = "orchestra_scx_v8",
    .timeout_ms = 30000U);
