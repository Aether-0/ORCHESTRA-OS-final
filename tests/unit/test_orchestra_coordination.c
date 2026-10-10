#include "../../kernel/sched_ext/include/orchestra_bridge_v1.h"
#include "../../kernel/sched_ext/include/orchestra_coord.h"
#include "../../kernel/sched_ext/include/orchestra_controller.h"
#include "../../kernel/sched_ext/include/orchestra_task_accounting.h"

#include <assert.h>

static void test_fixed_point_metrics(void)
{
    assert(orchestra_coord_ratio(0, 10) == 0);
    assert(orchestra_coord_ratio(10, 10) == 1000);
    assert(orchestra_coord_ratio(5, 10) == 500);
    assert(orchestra_coord_average_sum(10, 1) == 10);
    assert(orchestra_coord_average_sum(1000, 2) == 500);
    assert(orchestra_coord_average_sum(0, 0) == 0);
    assert(orchestra_coord_signal_score(10, 10, 10, 10, 1) == 10);
    assert(orchestra_coord_signal_score(1000, 1000, 1000, 1000, 2) == 500);
    assert(orchestra_coord_geomean4(1000, 1000, 1000, 1000) == 1000);
    assert(orchestra_coord_geomean4(500, 500, 500, 500) == 500);
    assert(orchestra_coord_geomean4(1000, 1000, 1000, 0) == 0);
    assert(orchestra_coord_geomean4(900, 800, 700, 600) >= 740);
    assert(orchestra_coord_geomean4(900, 800, 700, 600) <= 742);
    assert(orchestra_coord_geomean4(1000, 1000, 1000, 1) == 177);
    assert(orchestra_coord_geomean4(1, 1, 1, 1) == 1);
    for (uint32_t value = 0; value <= 1000; value++) {
        uint32_t second = (value * 37u) % 1001u;
        uint32_t third = (value * 173u) % 1001u;
        uint32_t root = orchestra_coord_geomean4(value, second, third, 1000);
        uint64_t product = (uint64_t)value * second * third * 1000u;
        uint64_t square = (uint64_t)root * root;
        uint64_t next_square = (uint64_t)(root + 1u) * (root + 1u);
        assert(square * square <= product);
        assert(next_square * next_square > product);
        assert(orchestra_coord_geomean4(value, value, value, value) == value);
    }
}

static void test_deficit_classification(void)
{
    struct orchestra_deficit_result_v10 result;

    result = orchestra_classify_deficit_v10(900, 900, 900, 900, 900, 700);
    assert(result.class_id == ORCH_DEFICIT_NONE);
    assert(result.primary == ORCH_DEFICIT_NONE);
    assert(result.deficient_count == 0);

    result = orchestra_classify_deficit_v10(400, 900, 900, 900, 700, 700);
    assert(result.class_id == ORCH_DEFICIT_SIGNAL);
    assert(result.primary == ORCH_DEFICIT_SIGNAL);
    assert(result.secondary == ORCH_DEFICIT_NONE);

    result = orchestra_classify_deficit_v10(400, 500, 900, 600, 500, 700);
    assert(result.class_id == ORCH_DEFICIT_MIXED);
    assert(result.primary == ORCH_DEFICIT_SIGNAL);
    assert(result.secondary == ORCH_DEFICIT_COMPLIANCE);
    assert(result.deficient_count == 3);
}

static void test_causal_actuator_matrix(void)
{
    uint32_t signal = orchestra_deficit_actuator_mask(ORCH_DEFICIT_SIGNAL);
    uint32_t stability = orchestra_deficit_actuator_mask(
        ORCH_DEFICIT_STABILITY);

    assert(signal & (1u << ORCH_ACTUATOR_PREDICTION_CONFIDENCE));
    assert(signal & (1u << ORCH_ACTUATOR_PREDICTION_HORIZON));
    assert(!(signal & (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD)));
    assert(stability & (1u << ORCH_ACTUATOR_SWITCHING_PENALTY));
    assert(orchestra_deficit_actuator_mask(ORCH_DEFICIT_COHERENCE) &
           (1u << ORCH_ACTUATOR_COORDINATION_THRESHOLD));
    assert(orchestra_deficit_actuator_direction(
               ORCH_DEFICIT_SIGNAL, ORCH_ACTUATOR_PREDICTION_HORIZON) < 0);
    assert(orchestra_deficit_actuator_direction(
               ORCH_DEFICIT_COHERENCE, ORCH_ACTUATOR_CONSENSUS_BLEND) > 0);
    assert(orchestra_deficit_actuator_direction(
               ORCH_DEFICIT_COHERENCE,
               ORCH_ACTUATOR_COORDINATION_THRESHOLD) < 0);
    assert(orchestra_deficit_pair_direction(
               ORCH_DEFICIT_MIXED, ORCH_DEFICIT_SIGNAL,
               ORCH_DEFICIT_STABILITY, ORCH_ACTUATOR_PREDICTION_HORIZON) < 0);
}

static void test_controller_actuator_bounds(void)
{
    struct orchestra_controller_bank_v10 bank = { 0 };
    struct orchestra_controller_bank_v10 copy = { 0 };
    struct orchestra_actuator_v10 *actuator;
    uint64_t initial;

    orchestra_controller_reset_bank(&bank);
    for (int index = 0; index < ORCH_ACTUATOR_COUNT; index++) {
        actuator = &bank.actuators[index];
        assert(actuator->minimum <= actuator->default_value);
        assert(actuator->default_value <= actuator->maximum);
        assert(actuator->current_value == actuator->default_value);
        assert(actuator->generation == 1);
    }

    actuator = &bank.actuators[ORCH_ACTUATOR_CONSENSUS_BLEND];
    initial = actuator->current_value;
    assert(orchestra_controller_step_actuator_v10(actuator, 1, 10) == 1);
    assert(actuator->current_value == initial + actuator->maximum_step);
    assert(actuator->current_value <= actuator->maximum);
    for (int step = 0; step < 10000; step++)
        (void)orchestra_controller_step_actuator_v10(
            actuator, 1, (uint64_t)(20 + step));
    assert(actuator->current_value == actuator->maximum);
    assert(actuator->flags & ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED);
    assert(orchestra_controller_step_actuator_v10(actuator, 1, 20000) == 0);
    assert(orchestra_controller_restore_actuator_v10(actuator, 30000) == 1);
    assert(actuator->current_value < actuator->maximum);
    assert(actuator->current_value >= actuator->minimum);

    orchestra_controller_copy_bank(&copy, &bank);
    assert(copy.generation == bank.generation);
    assert(copy.actuators[ORCH_ACTUATOR_CONSENSUS_BLEND].current_value ==
           bank.actuators[ORCH_ACTUATOR_CONSENSUS_BLEND].current_value);

    actuator->current_value = actuator->minimum;
    actuator->generation = UINT64_MAX;
    initial = actuator->current_value;
    assert(orchestra_controller_step_actuator_v10(actuator, 1, 40000) == 0);
    assert(actuator->current_value == initial);
    assert(actuator->flags & ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED);
    assert(orchestra_controller_next_generation_v10(17, &bank.generation));
    assert(bank.generation == 18);
    assert(!orchestra_controller_next_generation_v10(UINT64_MAX,
                                                     &bank.generation));
    assert(orchestra_controller_deadline_v10(UINT64_MAX - 1, 5) == UINT64_MAX);
}

static void test_controller_state_integrity(void)
{
    struct orchestra_controller_state_v10 controller = { 0 };

    controller.magic = ORCHESTRA_ABI_MAGIC;
    controller.abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
    controller.value_size = sizeof(controller);
    controller.schema_version = ORCHESTRA_CONTROLLER_SCHEMA_VERSION;
    controller.active_state = ORCHESTRA_CTRL_NORMAL;
    controller.scheduler_epoch = 1;
    controller.active_generation = 1;
    controller.staging_generation = 1;
    controller.previous_good_generation = 1;
    controller.controller_epoch = 1;
    controller.update_period_ns = 1;
    controller.minimum_hold_ns = 1;
    orchestra_controller_reset_bank(&controller.active);
    orchestra_controller_reset_bank(&controller.staging);
    orchestra_controller_reset_bank(&controller.previous_good);
    assert(orchestra_controller_state_valid_v10(&controller));

    controller.active.actuators[0].previous_value =
        controller.active.actuators[0].maximum + 1;
    assert(!orchestra_controller_state_valid_v10(&controller));
    controller.active.actuators[0].previous_value =
        controller.active.actuators[0].default_value;
    controller.active_deficit_class = ORCH_DEFICIT_COUNT;
    assert(!orchestra_controller_state_valid_v10(&controller));
    controller.active_deficit_class = ORCH_DEFICIT_NONE;
    controller.active_persistence = 256;
    assert(!orchestra_controller_state_valid_v10(&controller));
    controller.active_persistence = 0;
    assert(!orchestra_controller_state_valid_v10(NULL));
    controller.active_generation = 2;
    assert(!orchestra_controller_state_valid_v10(&controller));
}

static void test_throttle_renewal_retains_service(void)
{
    struct bridge_task_state state = { 0 };
    orchestra_sync_task_accounting(&state, 1, ORCHESTRA_ACTION_THROTTLE,
                                   ORCHESTRA_CPU_ANY, 100);
    state.runtime_used_ns = 10000;
    state.flags = BRIDGE_TASK_F_THROTTLED;
    /* Hundreds of refreshed generations cannot replenish the allowance. */
    for (uint64_t generation = 2; generation <= 100; generation++) {
        orchestra_sync_task_accounting(&state, generation,
            ORCHESTRA_ACTION_THROTTLE, ORCHESTRA_CPU_ANY, 100 + generation);
        assert(state.period_start_ns == 100);
        assert(state.runtime_used_ns == 10000);
        assert(state.generation == generation);
    }
    orchestra_sync_task_accounting(&state, 101, ORCHESTRA_ACTION_RUN, 2, 1000);
    assert(state.runtime_used_ns == 0);
    assert(state.period_start_ns == 1000);
    assert(state.requested_cpu == 2);
    orchestra_sync_task_accounting(&state, 101, ORCHESTRA_ACTION_RUN, 3, 1100);
    assert(state.requested_cpu == 3);
    assert(state.period_start_ns == 1000);
}

static void test_coherence_is_conditioned_on_state(void)
{
    struct orchestra_coordination_state_v10 state = { 0 };
    /* Different states legitimately choose different actions. Global action
     * dominance is 50%, but each state is perfectly coherent. */
    state.coherence_hist[0][ORCHESTRA_ACTION_RUN] = 10;
    state.coherence_hist[1][ORCHESTRA_ACTION_SLEEP] = 10;
    assert(orchestra_coord_compute_s3(&state) == 1000);
    state.coherence_hist[0][ORCHESTRA_ACTION_YIELD] = 10;
    assert(orchestra_coord_compute_s3(&state) == 750);
    state.coherence_hist[0][ORCHESTRA_ACTION_RUN] = 0;
    state.coherence_hist[0][ORCHESTRA_ACTION_YIELD] = 0;
    state.coherence_hist[1][ORCHESTRA_ACTION_SLEEP] = 0;
    assert(orchestra_coord_compute_s3(&state) == 0);
}

static void test_revoked_directive_clears_effective_action(void)
{
    const uint32_t actions[] = { ORCHESTRA_ACTION_SLEEP,
        ORCHESTRA_ACTION_THROTTLE, ORCHESTRA_ACTION_MIGRATE };
    for (unsigned int i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        struct bridge_task_state state = { 0 };
        state.action = actions[i];
        state.generation = 42;
        state.eligible_ns = 10000;
        state.requested_cpu = 3;
        state.dispatched_cpu = 3;
        state.flags = BRIDGE_TASK_F_DEFERRED | BRIDGE_TASK_F_THROTTLED;
        orchestra_clear_task_action(&state);
        assert(state.action == ORCHESTRA_ACTION_RUN);
        assert(state.generation == 42);
        assert(state.eligible_ns == 0);
        assert(state.requested_cpu == ORCHESTRA_CPU_ANY);
        assert(state.dispatched_cpu == ORCHESTRA_CPU_ANY);
        assert(state.flags == 0);
    }
}

int main(void)
{
    assert(orchestra_controller_deadline_v10(100, 20) == 120);
    assert(orchestra_controller_deadline_v10(ORCHESTRA_U64_MAX - 2, 3) ==
           ORCHESTRA_U64_MAX);
    assert(orchestra_controller_deadline_v10(ORCHESTRA_U64_MAX, 0) ==
           ORCHESTRA_U64_MAX);
    test_fixed_point_metrics();
    test_deficit_classification();
    test_causal_actuator_matrix();
    test_controller_actuator_bounds();
    test_controller_state_integrity();
    test_throttle_renewal_retains_service();
    test_coherence_is_conditioned_on_state();
    test_revoked_directive_clears_effective_action();
    return 0;
}
