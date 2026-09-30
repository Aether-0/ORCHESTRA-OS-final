#include "../../kernel/sched_ext/include/orchestra_bridge_v1.h"
#include "../../kernel/sched_ext/include/orchestra_coord.h"
#include "../../kernel/sched_ext/include/orchestra_controller.h"

#include <assert.h>

static void test_fixed_point_metrics(void)
{
    assert(orchestra_coord_ratio(0, 10) == 0);
    assert(orchestra_coord_ratio(10, 10) == 1000);
    assert(orchestra_coord_ratio(5, 10) == 500);
    assert(orchestra_coord_geomean4(1000, 1000, 1000, 1000) == 1000);
    assert(orchestra_coord_geomean4(500, 500, 500, 500) == 500);
    assert(orchestra_coord_geomean4(1000, 1000, 1000, 0) == 0);
    assert(orchestra_coord_geomean4(900, 800, 700, 600) >= 740);
    assert(orchestra_coord_geomean4(900, 800, 700, 600) <= 742);
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
    return 0;
}
