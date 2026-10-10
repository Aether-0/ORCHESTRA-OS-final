/* Exercise the actual BPF header branches with bounded map/clock mocks.
 * This is logic coverage, not verifier or hardware evidence. */
#include "../../kernel/sched_ext/include/orchestra_bridge_v1.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ORCHESTRA_KERNEL_LOGIC_TEST 1
#define __BPF__ 1

static int orch_coord_v10, orch_coord_cpu, orch_ctrl_v10, orch_ctrl_tel;
static unsigned int held_locks;
static uint64_t clock_ns = UINT64_C(1000000000);
static struct orchestra_coordination_state_v10 windows[ORCHESTRA_COORD_MAP_ENTRY_COUNT];
static struct orchestra_coord_cpu_v10 mock_cpu_state;
static struct orchestra_controller_state_v10 mock_controller;
static struct orchestra_controller_telemetry_v10 mock_telemetry;

static void *bpf_map_lookup_elem(const void *map, const uint32_t *key)
{
    assert(held_locks == 0);
    if (map == &orch_coord_v10)
        return *key < ORCHESTRA_COORD_MAP_ENTRY_COUNT ? &windows[*key] : NULL;
    assert(*key == 0);
    if (map == &orch_coord_cpu) return &mock_cpu_state;
    if (map == &orch_ctrl_v10) return &mock_controller;
    if (map == &orch_ctrl_tel) return &mock_telemetry;
    return NULL;
}

static uint64_t bpf_ktime_get_ns(void) { assert(held_locks == 0); return clock_ns; }
static uint32_t bpf_get_smp_processor_id(void) { assert(held_locks == 0); return 0; }
static long bpf_get_numa_node_id(void) { assert(held_locks == 0); return 0; }
static void bpf_spin_lock(struct orchestra_userspace_map_lock *lock)
{
    assert(held_locks == 0 && lock->opaque == 0);
    held_locks++;
    lock->opaque = 1;
}
static void bpf_spin_unlock(struct orchestra_userspace_map_lock *lock)
{
    assert(held_locks == 1 && lock->opaque == 1);
    held_locks--;
    lock->opaque = 0;
}

#include "../../kernel/sched_ext/include/orchestra_coord.h"
#include "../../kernel/sched_ext/include/orchestra_controller.h"

static void reset(void)
{
    memset(windows, 0, sizeof(windows));
    memset(&mock_cpu_state, 0, sizeof(mock_cpu_state));
    memset(&mock_controller, 0, sizeof(mock_controller));
    memset(&mock_telemetry, 0, sizeof(mock_telemetry));
    held_locks = 0;
    clock_ns = UINT64_C(1000000000);
    orchestra_controller_init_v10(123);
    assert(orchestra_controller_valid(&mock_controller));
}

static void test_window_scoring_and_reuse(void)
{
    reset();
    uint64_t generation = clock_ns / ORCHESTRA_COORD_DEFAULT_WINDOW_NS;
    uint32_t key = orchestra_coord_key(ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                                       generation);
    struct orchestra_coord_action_context action = {
        .policy_action = ORCHESTRA_ACTION_RUN,
        .controller_action = ORCHESTRA_ACTION_RUN,
        .capability_action = ORCHESTRA_ACTION_RUN,
        .actual_action = ORCHESTRA_ACTION_RUN,
        .previous_action = ORCHESTRA_ACTION_RUN,
        .fallback_reason = BRIDGE_FALLBACK_NONE
    };
    for (int sample = 0; sample < 2; sample++) {
        orchestra_coord_record_signal(1, 0, 10, 500, 500, 1,
            clock_ns - UINT64_C(99000000), clock_ns + UINT64_C(1000000), 1);
        orchestra_coord_record_action(&action, clock_ns);
        orchestra_coord_record_execution(ORCHESTRA_ACTION_RUN,
            ORCHESTRA_ACTION_RUN, ORCHESTRA_ACTION_RUN, clock_ns);
    }
    assert(windows[key].actual_action_count[ORCHESTRA_ACTION_RUN] == 2);
    assert(windows[key].signal_generation_break_count == 0);
    clock_ns += ORCHESTRA_COORD_DEFAULT_WINDOW_NS;
    orchestra_coord_finalize_global_live(clock_ns);
    assert(windows[key].flags & ORCHESTRA_COORD_F_FINALIZED);
    assert(windows[key].s1_permille == 505);
    assert(windows[key].s2_permille == 1000);
    assert(windows[key].s3_permille == 1000);
    assert(windows[key].s4_permille == 1000);
    assert(windows[key].q_permille > 840 && windows[key].q_permille < 850);
    clock_ns += ORCHESTRA_COORD_DEFAULT_WINDOW_NS;
    orchestra_coord_record_signal(0, 1, 0, 0, 0, 0, 0, 0, 0);
    assert(windows[key].window_generation == generation + 2);
    assert(windows[key].executed_observations == 0);
    assert(windows[key].signal_freshness_sum == 0);
    assert(windows[key].prediction_observations == 1);
}

static void test_controller_actuation_and_rollback(void)
{
    reset();
    struct orchestra_coordination_metrics_v10 metrics = {
        .scope = ORCHESTRA_COORD_SCOPE_GLOBAL,
        .window_generation = 1, .s1_permille = 400,
        .s2_permille = 1000, .s3_permille = 1000, .s4_permille = 1000,
        .q_permille = 790, .deficit_class = ORCH_DEFICIT_SIGNAL,
        .primary_deficit = ORCH_DEFICIT_SIGNAL, .deficit_severity = 210
    };
    uint64_t old_confidence = mock_controller.active.actuators[
        ORCH_ACTUATOR_PREDICTION_CONFIDENCE].current_value;
    for (uint64_t window = 1; window <= 3; window++) {
        metrics.window_generation = window;
        clock_ns += ORCHESTRA_CONTROLLER_DEFAULT_PERIOD_NS;
        orchestra_controller_update_from_coord(clock_ns, &metrics);
    }
    assert(mock_controller.active.actuators[
        ORCH_ACTUATOR_PREDICTION_CONFIDENCE].current_value > old_confidence);
    assert(mock_controller.flags & ORCHESTRA_CONTROLLER_F_EVALUATING);
    metrics.window_generation++;
    metrics.q_permille = 100;
    clock_ns = mock_controller.evaluation_until_ns;
    orchestra_controller_update_from_coord(clock_ns, &metrics);
    assert(mock_controller.active_state == ORCHESTRA_CTRL_ROLLBACK);
    assert(mock_controller.rollback_count == 1);
    assert(mock_telemetry.rollback_count == 1);
    uint64_t generation = mock_controller.active_generation;
    metrics.window_generation++;
    clock_ns++;
    orchestra_controller_update_from_coord(clock_ns, &metrics);
    assert(mock_controller.active_generation == generation);
}

int main(void)
{
    test_window_scoring_and_reuse();
    test_controller_actuation_and_rollback();
    puts("PASS BPF-branch window reuse, arithmetic, actuation and rollback");
    return 0;
}
