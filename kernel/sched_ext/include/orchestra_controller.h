/* SPDX-License-Identifier: GPL-2.0 */
/* Native bounded multi-actuator feedback controller. */
#ifndef ORCHESTRA_CONTROLLER_H
#define ORCHESTRA_CONTROLLER_H

#include "orchestra_coord.h"

#ifndef __always_inline
#define __always_inline inline
#endif

static __always_inline void orchestra_controller_set_actuator(
    struct orchestra_actuator_v10 *actuator, uint64_t minimum,
    uint64_t maximum, uint64_t default_value, uint64_t maximum_step)
{
    actuator->minimum = minimum;
    actuator->maximum = maximum;
    actuator->default_value = default_value;
    actuator->current_value = default_value;
    actuator->previous_value = default_value;
    actuator->rollback_value = default_value;
    actuator->maximum_step = maximum_step;
    actuator->generation = 1;
    actuator->last_update_epoch = 0;
    actuator->flags = 0;
    actuator->reserved = 0;
}

static __always_inline void orchestra_controller_reset_bank(
    struct orchestra_controller_bank_v10 *bank)
{
    bank->state = ORCHESTRA_CTRL_NORMAL;
    bank->primary_deficit = ORCH_DEFICIT_NONE;
    bank->secondary_deficit = ORCH_DEFICIT_NONE;
    bank->severity = 0;
    bank->persistence = 0;
    bank->flags = 0;
    bank->generation = 1;
    bank->last_window_generation = 0;
    bank->last_q_permille = 0;
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_JITTER], 0, 200, 20, 10);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_SWITCHING_PENALTY], 0, 500, 50, 25);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_CONSENSUS_BLEND], 0, 1000, 500, 50);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_MIGRATION_THRESHOLD], 0, 1000, 500, 50);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_YIELD_THRESHOLD], 0, 1000, 600, 50);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_THROTTLE_DURATION], UINT64_C(100000),
        UINT64_C(1000000000), UINT64_C(10000000), UINT64_C(1000000));
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_SLEEP_DEFER], UINT64_C(100000),
        UINT64_C(5000000000), UINT64_C(20000000), UINT64_C(2000000));
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_PREDICTION_CONFIDENCE], 0, 1000, 500, 50);
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_PREDICTION_HORIZON], UINT64_C(1000000),
        UINT64_C(5000000000), UINT64_C(100000000), UINT64_C(10000000));
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_SIGNAL_CADENCE], UINT64_C(1000000),
        UINT64_C(60000000000), UINT64_C(10000000), UINT64_C(1000000));
    orchestra_controller_set_actuator(&bank->actuators[
        ORCH_ACTUATOR_COORDINATION_THRESHOLD], 250, 950, 700, 25);
}

static __always_inline void orchestra_controller_copy_bank(
    struct orchestra_controller_bank_v10 *destination,
    const struct orchestra_controller_bank_v10 *source)
{
    destination->state = source->state;
    destination->primary_deficit = source->primary_deficit;
    destination->secondary_deficit = source->secondary_deficit;
    destination->severity = source->severity;
    destination->persistence = source->persistence;
    destination->flags = source->flags;
    destination->generation = source->generation;
    destination->last_window_generation = source->last_window_generation;
    destination->last_q_permille = source->last_q_permille;
#ifdef __BPF__
#pragma unroll
#endif
    for (int index = 0; index < ORCH_ACTUATOR_COUNT; index++)
        destination->actuators[index] = source->actuators[index];
}

static __always_inline int orchestra_controller_next_generation_v10(
    uint64_t current, uint64_t *next)
{
    if (!next || current == UINT64_MAX)
        return 0;
    *next = current + 1u;
    return *next != 0;
}

static __always_inline uint64_t orchestra_controller_deadline_v10(
    uint64_t now, uint64_t duration)
{
    return UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
}

/* Pure actuator transition helpers are shared with deterministic userspace
 * contract tests.  The BPF controller wraps these transitions with map locks
 * and publication logic, but the bound/step semantics are ABI-level behavior. */
static __always_inline int orchestra_controller_step_actuator_v10(
    struct orchestra_actuator_v10 *actuator, int direction, uint64_t epoch)
{
    uint64_t old_value = actuator->current_value;
    uint64_t step = actuator->maximum_step == 0 ? 1 : actuator->maximum_step;
    uint64_t next = old_value;

    actuator->flags &= ~ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
    if (direction > 0) {
        if (old_value >= actuator->maximum ||
            actuator->maximum - old_value < step) {
            next = actuator->maximum;
            actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
        } else {
            next = old_value + step;
        }
    } else if (direction < 0) {
        if (old_value <= actuator->minimum ||
            old_value - actuator->minimum < step) {
            next = actuator->minimum;
            actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
        } else {
            next = old_value - step;
        }
    }
    if (next == old_value) {
        if (next == actuator->minimum || next == actuator->maximum)
            actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
        return 0;
    }
    if (actuator->generation == UINT64_MAX) {
        actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
        return 0;
    }
    actuator->previous_value = old_value;
    actuator->rollback_value = old_value;
    actuator->current_value = next;
    actuator->generation++;
    actuator->last_update_epoch = epoch;
    actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_UPDATED;
    return 1;
}

static __always_inline int orchestra_controller_restore_actuator_v10(
    struct orchestra_actuator_v10 *actuator, uint64_t epoch)
{
    int direction = actuator->current_value < actuator->default_value ? 1 :
        (actuator->current_value > actuator->default_value ? -1 : 0);

    return orchestra_controller_step_actuator_v10(actuator, direction, epoch);
}

/* Pure state validation is shared by BPF and userspace contract tests. */
static __always_inline int orchestra_controller_state_valid_v10(
    const struct orchestra_controller_state_v10 *controller)
{
    int banks_valid = 1;

    if (!controller)
        return 0;

#ifdef __BPF__
#pragma unroll
#endif
    for (int bank_index = 0; bank_index < 3; bank_index++) {
        const struct orchestra_controller_bank_v10 *bank =
            bank_index == 0 ? &controller->active :
            (bank_index == 1 ? &controller->staging :
                               &controller->previous_good);

        if (bank->state >= ORCHESTRA_CTRL_COUNT ||
            bank->primary_deficit >= ORCH_DEFICIT_COUNT ||
            bank->secondary_deficit >= ORCH_DEFICIT_COUNT ||
            bank->severity > ORCHESTRA_V10_FIXED_POINT_SCALE ||
            bank->persistence > 255u ||
            bank->last_q_permille > ORCHESTRA_V10_FIXED_POINT_SCALE ||
            bank->generation == 0)
            banks_valid = 0;
#ifdef __BPF__
#pragma unroll
#endif
        for (int actuator_index = 0;
             actuator_index < ORCH_ACTUATOR_COUNT; actuator_index++) {
            const struct orchestra_actuator_v10 *actuator =
                &bank->actuators[actuator_index];

            if (actuator->minimum > actuator->maximum ||
                actuator->default_value < actuator->minimum ||
                actuator->default_value > actuator->maximum ||
                actuator->current_value < actuator->minimum ||
                actuator->current_value > actuator->maximum ||
                actuator->previous_value < actuator->minimum ||
                actuator->previous_value > actuator->maximum ||
                actuator->rollback_value < actuator->minimum ||
                actuator->rollback_value > actuator->maximum ||
                actuator->generation == 0 || actuator->maximum_step == 0)
                banks_valid = 0;
        }
    }
    return controller->magic == ORCHESTRA_ABI_MAGIC &&
           controller->abi_version == ORCHESTRA_CONTROL_ABI_VERSION &&
           controller->value_size == sizeof(*controller) &&
           controller->schema_version == ORCHESTRA_CONTROLLER_SCHEMA_VERSION &&
           controller->active_state < ORCHESTRA_CTRL_COUNT &&
           controller->active_primary_deficit < ORCH_DEFICIT_COUNT &&
           controller->active_secondary_deficit < ORCH_DEFICIT_COUNT &&
           controller->active_deficit_class < ORCH_DEFICIT_COUNT &&
           controller->active_severity <= ORCHESTRA_V10_FIXED_POINT_SCALE &&
           controller->active_persistence <= 255u &&
           controller->active_generation != 0 &&
           controller->active_generation == controller->active.generation &&
           controller->staging_generation != 0 &&
           controller->staging_generation == controller->staging.generation &&
           controller->previous_good_generation != 0 &&
           controller->previous_good_generation ==
               controller->previous_good.generation &&
           controller->scheduler_epoch != 0 &&
           controller->controller_epoch != 0 &&
           controller->update_period_ns != 0 &&
           controller->update_period_ns <= UINT64_C(60000000000) &&
           controller->minimum_hold_ns <= UINT64_C(60000000000) &&
           controller->evaluation_baseline_q_permille <=
               ORCHESTRA_V10_FIXED_POINT_SCALE &&
           controller->last_q_permille <= ORCHESTRA_V10_FIXED_POINT_SCALE &&
           controller->best_q_permille <= ORCHESTRA_V10_FIXED_POINT_SCALE &&
           banks_valid;
}

#ifdef __BPF__

static __always_inline struct orchestra_controller_telemetry_v10 *
orchestra_controller_telemetry(void)
{
    uint32_t key = 0;

    return bpf_map_lookup_elem(&orch_ctrl_tel_v10, &key);
}

static __always_inline void orchestra_controller_note_state(
    uint32_t state, uint32_t primary, uint32_t secondary,
    uint32_t deficit_class, uint64_t generation, uint64_t window_generation,
    uint32_t q,
    uint32_t actuator, int direction, int changed, int saturated,
    int rollback, int recovery, int no_op)
{
    struct orchestra_controller_telemetry_v10 *tel =
        orchestra_controller_telemetry();

    if (!tel)
        return;
    bpf_spin_lock(&tel->lock);
    tel->update_count += changed != 0;
    tel->no_op_count += no_op != 0;
    tel->saturation_count += saturated != 0;
    tel->rollback_count += rollback != 0;
    tel->recovery_count += recovery != 0;
    tel->state_transition_count += tel->last_state != state;
    tel->last_update_epoch = generation;
    tel->last_window_generation = window_generation;
    tel->last_q_permille = q;
    tel->last_state = state;
    tel->last_primary_deficit = primary;
    tel->last_secondary_deficit = secondary;
    tel->last_actuator = actuator;
    tel->last_update_direction = direction > 0 ? 1u :
        (direction < 0 ? 2u : 0u);
    if (changed && actuator < ORCH_ACTUATOR_COUNT)
        tel->actuator_update_count[actuator]++;
    if (deficit_class < ORCH_DEFICIT_COUNT)
        tel->deficit_count[deficit_class]++;
    if (state == ORCHESTRA_CTRL_DISABLED)
        tel->disabled_count++;
    bpf_spin_unlock(&tel->lock);
}

static __always_inline void orchestra_controller_note_actuator_changes(
    const struct orchestra_controller_state_v10 *controller)
{
    struct orchestra_controller_telemetry_v10 *tel =
        orchestra_controller_telemetry();

    if (!tel)
        return;
    bpf_spin_lock(&tel->lock);
#pragma unroll
    for (int index = 0; index < ORCH_ACTUATOR_COUNT; index++) {
        const struct orchestra_actuator_v10 *current =
            &controller->active.actuators[index];
        const struct orchestra_actuator_v10 *previous =
            &controller->previous_good.actuators[index];

        if (current->current_value != previous->current_value) {
            tel->actuator_update_count[index]++;
            tel->last_actuator = (uint32_t)index;
            tel->last_update_direction = current->current_value >=
                previous->current_value ? 1u : 2u;
        }
    }
    bpf_spin_unlock(&tel->lock);
}

static __always_inline int orchestra_controller_valid(
    const struct orchestra_controller_state_v10 *controller)
{
    return orchestra_controller_state_valid_v10(controller);
}

static __always_inline void orchestra_controller_init_v10(uint64_t epoch)
{
    struct orchestra_controller_state_v10 *controller;
    struct orchestra_controller_telemetry_v10 *telemetry;
    uint32_t key = 0;

    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (controller) {
        bpf_spin_lock(&controller->lock);
        controller->magic = ORCHESTRA_ABI_MAGIC;
        controller->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
        controller->value_size = sizeof(*controller);
        controller->schema_version = ORCHESTRA_CONTROLLER_SCHEMA_VERSION;
        controller->flags = ORCHESTRA_CONTROLLER_F_VALID;
        controller->active_state = ORCHESTRA_CTRL_NORMAL;
        controller->scheduler_epoch = epoch;
        controller->active_generation = 1;
        controller->staging_generation = 1;
        controller->previous_good_generation = 1;
        controller->controller_epoch = 1;
        controller->update_period_ns = ORCHESTRA_CONTROLLER_DEFAULT_PERIOD_NS;
        controller->minimum_hold_ns = ORCHESTRA_CONTROLLER_MIN_HOLD_NS;
        controller->cooldown_until_ns = 0;
        controller->evaluation_until_ns = 0;
        controller->best_q_permille = ORCHESTRA_V10_FIXED_POINT_SCALE;
        orchestra_controller_reset_bank(&controller->active);
        orchestra_controller_reset_bank(&controller->staging);
        orchestra_controller_reset_bank(&controller->previous_good);
        bpf_spin_unlock(&controller->lock);
    }
    telemetry = orchestra_controller_telemetry();
    if (telemetry) {
        bpf_spin_lock(&telemetry->lock);
        telemetry->magic = ORCHESTRA_ABI_MAGIC;
        telemetry->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
        telemetry->value_size = sizeof(*telemetry);
        telemetry->schema_version =
            ORCHESTRA_CONTROLLER_TELEMETRY_VERSION;
        telemetry->last_state = ORCHESTRA_CTRL_NORMAL;
        bpf_spin_unlock(&telemetry->lock);
    }
}

static __always_inline int orchestra_controller_read_view_v10(
    struct orchestra_controller_view_v10 *view)
{
    struct orchestra_controller_state_v10 *controller;
    uint32_t key = 0;

    __builtin_memset(view, 0, sizeof(*view));
    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (!controller)
        return 0;
    bpf_spin_lock(&controller->lock);
    if (orchestra_controller_valid(controller)) {
        const struct orchestra_actuator_v10 *a = controller->active.actuators;

        view->state = controller->active_state;
        view->primary_deficit = controller->active_primary_deficit;
        view->secondary_deficit = controller->active_secondary_deficit;
        view->severity = controller->active_severity;
        view->persistence = controller->active_persistence;
        view->flags = controller->flags;
        view->generation = controller->active_generation;
        view->window_generation = controller->active.last_window_generation;
        view->jitter = a[ORCH_ACTUATOR_JITTER].current_value;
        view->switching_penalty =
            a[ORCH_ACTUATOR_SWITCHING_PENALTY].current_value;
        view->consensus_blend =
            a[ORCH_ACTUATOR_CONSENSUS_BLEND].current_value;
        view->migration_threshold =
            a[ORCH_ACTUATOR_MIGRATION_THRESHOLD].current_value;
        view->yield_threshold =
            a[ORCH_ACTUATOR_YIELD_THRESHOLD].current_value;
        view->throttle_duration_ns =
            a[ORCH_ACTUATOR_THROTTLE_DURATION].current_value;
        view->sleep_defer_ns = a[ORCH_ACTUATOR_SLEEP_DEFER].current_value;
        view->prediction_confidence_threshold =
            a[ORCH_ACTUATOR_PREDICTION_CONFIDENCE].current_value;
        view->prediction_horizon_ns =
            a[ORCH_ACTUATOR_PREDICTION_HORIZON].current_value;
        view->signal_cadence_ns =
            a[ORCH_ACTUATOR_SIGNAL_CADENCE].current_value;
        view->coordination_threshold =
            a[ORCH_ACTUATOR_COORDINATION_THRESHOLD].current_value;
        bpf_spin_unlock(&controller->lock);
        return 1;
    }
    bpf_spin_unlock(&controller->lock);
    return 0;
}

static __always_inline int orchestra_controller_action_allowed_v10(
    uint32_t state, uint32_t action)
{
    switch (state) {
    case ORCHESTRA_CTRL_NORMAL:
        return action < ORCHESTRA_ACTION_COUNT;
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

static __always_inline int orchestra_controller_should_fallback_v10(
    const struct orchestra_controller_view_v10 *view, uint32_t action,
    uint32_t cpu_now_permille, uint32_t queue_pressure_permille)
{
    if (!orchestra_controller_action_allowed_v10(view->state, action))
        return 1;
    if (action == ORCHESTRA_ACTION_YIELD &&
        cpu_now_permille < view->yield_threshold)
        return 1;
    if (action == ORCHESTRA_ACTION_MIGRATE &&
        queue_pressure_permille < view->migration_threshold)
        return 1;
    return 0;
}

static __always_inline uint64_t orchestra_controller_clamp_duration(
    uint64_t value, uint64_t minimum, uint64_t maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static __always_inline int orchestra_controller_publish_staging(
    struct orchestra_controller_state_v10 *controller,
    uint32_t state, uint32_t primary, uint32_t secondary, uint32_t severity,
    uint32_t persistence, uint64_t window_generation, uint32_t q,
    uint64_t epoch, uint32_t deficit_class, int restore)
{
    uint32_t mask = orchestra_deficit_actuator_mask(deficit_class);
    uint32_t changed = 0;
    uint32_t first_actuator = ORCH_ACTUATOR_COUNT;
    uint32_t first_direction = 0;
    uint64_t previous_generation = controller->active_generation;
    uint64_t next_generation;

    if (!orchestra_controller_next_generation_v10(previous_generation,
                                                   &next_generation)) {
        /* Generation exhaustion is an explicit fail-safe transition.  Do not
         * publish generation zero, which could make an old controller bank
         * appear current after wraparound. */
        controller->active_state = ORCHESTRA_CTRL_DISABLED;
        controller->flags |= ORCHESTRA_CONTROLLER_F_VALID;
        controller->flags &= ~(ORCHESTRA_CONTROLLER_F_STAGING |
                               ORCHESTRA_CONTROLLER_F_EVALUATING);
        controller->no_op_count++;
        return 0;
    }

    orchestra_controller_copy_bank(&controller->previous_good,
                                   &controller->active);
    orchestra_controller_copy_bank(&controller->staging,
                                   &controller->active);
    controller->staging.state = state;
    controller->staging.primary_deficit = primary;
    controller->staging.secondary_deficit = secondary;
    controller->staging.severity = severity;
    controller->staging.persistence = persistence;
    controller->staging.last_window_generation = window_generation;
    controller->staging.last_q_permille = q;
    controller->staging.generation = next_generation;

#pragma unroll
    for (int index = 0; index < ORCH_ACTUATOR_COUNT; index++) {
        int direction = 0;

        if (restore) {
            int restored = orchestra_controller_restore_actuator_v10(
                &controller->staging.actuators[index], epoch);

            if (restored) {
                direction = controller->staging.actuators[index].current_value >=
                    controller->staging.actuators[index].previous_value ? 1 : -1;
                if (first_actuator == ORCH_ACTUATOR_COUNT) {
                    first_actuator = (uint32_t)index;
                    first_direction = direction > 0 ? 1u : 2u;
                }
                changed++;
                if (changed >= 2u)
                    break;
            }
        } else if ((mask & (1u << index)) != 0) {
            direction = orchestra_deficit_pair_direction(
                deficit_class, primary, secondary, (uint32_t)index);
            if (direction != 0 && orchestra_controller_step_actuator_v10(
                    &controller->staging.actuators[index], direction, epoch)) {
                if (first_actuator == ORCH_ACTUATOR_COUNT) {
                    first_actuator = (uint32_t)index;
                    first_direction = direction > 0 ? 1u : 2u;
                }
                changed++;
                if (changed >= 2u)
                    break;
            }
        }
    }
    if (changed == 0 && controller->active_state == state &&
        controller->active_primary_deficit == primary &&
        controller->active_secondary_deficit == secondary &&
        controller->active_deficit_class == deficit_class &&
        controller->active_severity == severity &&
        controller->active_persistence == persistence) {
        controller->no_op_count++;
        return 0;
    }
    orchestra_controller_copy_bank(&controller->active,
                                   &controller->staging);
    controller->active_state = state;
    controller->active_primary_deficit = primary;
    controller->active_secondary_deficit = secondary;
    controller->active_deficit_class = deficit_class;
    controller->active_severity = severity;
    controller->active_persistence = persistence;
    controller->active_generation = controller->staging.generation;
    controller->staging_generation = controller->staging.generation;
    controller->previous_good_generation = previous_generation;
    controller->controller_epoch = epoch;
    controller->flags &= ~(ORCHESTRA_CONTROLLER_F_STAGING |
                           ORCHESTRA_CONTROLLER_F_RECOVERING);
    if (state == ORCHESTRA_CTRL_RECOVERY)
        controller->flags |= ORCHESTRA_CONTROLLER_F_RECOVERING;
    if (state == ORCHESTRA_CTRL_SATURATED)
        controller->flags |= ORCHESTRA_CONTROLLER_F_SATURATED;
    if (state == ORCHESTRA_CTRL_ROLLBACK)
        controller->flags |= ORCHESTRA_CONTROLLER_F_ROLLBACK;
    (void)first_actuator;
    (void)first_direction;
    return 1;
}

static __always_inline void orchestra_controller_rollback_locked(
    struct orchestra_controller_state_v10 *controller, uint64_t now,
    const struct orchestra_coordination_metrics_v10 *metrics)
{
    uint64_t next_generation;

    if (!orchestra_controller_next_generation_v10(
            controller->active_generation, &next_generation)) {
        controller->active_state = ORCHESTRA_CTRL_DISABLED;
        controller->flags |= ORCHESTRA_CONTROLLER_F_VALID;
        controller->flags &= ~(ORCHESTRA_CONTROLLER_F_ROLLBACK |
                               ORCHESTRA_CONTROLLER_F_EVALUATING |
                               ORCHESTRA_CONTROLLER_F_STAGING);
        controller->rollback_count++;
        return;
    }
    orchestra_controller_copy_bank(&controller->staging,
                                   &controller->previous_good);
    controller->staging.state = ORCHESTRA_CTRL_ROLLBACK;
    controller->staging.generation = next_generation;
    orchestra_controller_copy_bank(&controller->active,
                                   &controller->staging);
    controller->active_generation = controller->staging.generation;
    controller->staging_generation = controller->staging.generation;
    controller->active_state = ORCHESTRA_CTRL_ROLLBACK;
    controller->active_primary_deficit = metrics->primary_deficit;
    controller->active_secondary_deficit = metrics->secondary_deficit;
    controller->active_deficit_class = metrics->deficit_class;
    controller->active_severity = metrics->deficit_severity;
    controller->active_persistence = metrics->deficit_persistence;
    controller->staging.last_window_generation = metrics->window_generation;
    controller->staging.last_q_permille = metrics->q_permille;
    controller->flags |= ORCHESTRA_CONTROLLER_F_ROLLBACK;
    controller->flags &= ~(ORCHESTRA_CONTROLLER_F_EVALUATING |
                           ORCHESTRA_CONTROLLER_F_STAGING);
    controller->rollback_count++;
    controller->cooldown_until_ns = orchestra_controller_deadline_v10(
        now, ORCHESTRA_CONTROLLER_COOLDOWN_NS);
    controller->next_update_ns = controller->cooldown_until_ns;
    controller->evaluation_until_ns = 0;
}

static __always_inline void orchestra_controller_update_from_coord(
    uint64_t now, const struct orchestra_coordination_metrics_v10 *metrics)
{
    struct orchestra_controller_state_v10 *controller;
    struct orchestra_controller_telemetry_v10 *telemetry;
    uint32_t key = 0;
    uint32_t state;
    uint32_t persistence;
    int changed = 0;
    int saturated = 0;
    int recovery = 0;
    int no_op = 0;

    if (!metrics || metrics->scope != ORCHESTRA_COORD_SCOPE_GLOBAL)
        return;
    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (!controller)
        return;
    telemetry = orchestra_controller_telemetry();
    bpf_spin_lock(&controller->lock);
    if (!orchestra_controller_valid(controller)) {
        controller->flags = ORCHESTRA_CONTROLLER_F_VALID;
        controller->active_state = ORCHESTRA_CTRL_DISABLED;
        if (telemetry)
            orchestra_controller_note_state(ORCHESTRA_CTRL_DISABLED,
                                             ORCH_DEFICIT_NONE,
                                             ORCH_DEFICIT_NONE,
                                             ORCH_DEFICIT_NONE,
                                             controller->active_generation,
                                             metrics->window_generation,
                                             metrics->q_permille,
                                             ORCH_ACTUATOR_COUNT, 0, 0, 0, 0,
                                             0, 1);
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (metrics->q_permille > ORCHESTRA_V10_FIXED_POINT_SCALE ||
        metrics->s1_permille > ORCHESTRA_V10_FIXED_POINT_SCALE ||
        metrics->s2_permille > ORCHESTRA_V10_FIXED_POINT_SCALE ||
        metrics->s3_permille > ORCHESTRA_V10_FIXED_POINT_SCALE ||
        metrics->s4_permille > ORCHESTRA_V10_FIXED_POINT_SCALE) {
        controller->active_state = ORCHESTRA_CTRL_DISABLED;
        controller->flags |= ORCHESTRA_CONTROLLER_F_VALID;
        controller->flags &= ~ORCHESTRA_CONTROLLER_F_EVALUATING;
        controller->no_op_count++;
        if (telemetry)
            orchestra_controller_note_state(ORCHESTRA_CTRL_DISABLED,
                                             ORCH_DEFICIT_NONE,
                                             ORCH_DEFICIT_NONE,
                                             ORCH_DEFICIT_NONE,
                                             controller->active_generation,
                                             metrics->window_generation,
                                             metrics->q_permille,
                                             ORCH_ACTUATOR_COUNT, 0, 0, 0, 0,
                                             0, 1);
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (metrics->window_generation == controller->active.last_window_generation) {
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (controller->cooldown_until_ns != 0 &&
        now < controller->cooldown_until_ns) {
        controller->flags |= ORCHESTRA_CONTROLLER_F_COOLDOWN;
        controller->no_op_count++;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    controller->flags &= ~ORCHESTRA_CONTROLLER_F_COOLDOWN;
    if (controller->last_update_ns != 0 &&
        (now < controller->last_update_ns ||
         now - controller->last_update_ns < controller->minimum_hold_ns)) {
        controller->no_op_count++;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (controller->next_update_ns != 0 && now < controller->next_update_ns) {
        controller->last_q_permille = metrics->q_permille;
        controller->active.last_window_generation = metrics->window_generation;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (controller->evaluation_until_ns != 0 &&
        now >= controller->evaluation_until_ns) {
        uint64_t baseline = controller->evaluation_baseline_q_permille;

        if (baseline > metrics->q_permille + 50u) {
            orchestra_controller_rollback_locked(controller, now, metrics);
            if (telemetry)
                orchestra_controller_note_state(ORCHESTRA_CTRL_ROLLBACK,
                                                 metrics->primary_deficit,
                                                 metrics->secondary_deficit,
                                                 metrics->deficit_class,
                                                 controller->active_generation,
                                                 metrics->window_generation,
                                                 metrics->q_permille,
                                                 ORCH_ACTUATOR_COUNT, 0, 0, 1,
                                                 0, 0, 0);
            bpf_spin_unlock(&controller->lock);
            return;
        }
        controller->evaluation_until_ns = 0;
        controller->flags &= ~ORCHESTRA_CONTROLLER_F_EVALUATING;
    }
    controller->last_q_permille = metrics->q_permille;
    if (metrics->q_permille > controller->best_q_permille)
        controller->best_q_permille = metrics->q_permille;
    if (metrics->deficit_class == ORCH_DEFICIT_NONE) {
        controller->active_persistence =
            controller->active_state == ORCHESTRA_CTRL_RECOVERY ?
                controller->active_persistence + 1u : 1u;
        if (controller->active_persistence > 3u)
            controller->active_persistence = 3u;
        if (controller->active_state == ORCHESTRA_CTRL_ROLLBACK ||
            controller->active_state == ORCHESTRA_CTRL_SATURATED ||
            controller->active_state == ORCHESTRA_CTRL_DEGRADED) {
            state = ORCHESTRA_CTRL_RECOVERY;
            recovery = 1;
        } else if (controller->active_state == ORCHESTRA_CTRL_RECOVERY &&
                   controller->active_persistence >= 3u) {
            state = ORCHESTRA_CTRL_NORMAL;
        } else {
            state = controller->active_state;
        }
        controller->active_primary_deficit = ORCH_DEFICIT_NONE;
        controller->active_secondary_deficit = ORCH_DEFICIT_NONE;
        controller->active_deficit_class = ORCH_DEFICIT_NONE;
        controller->active_severity = 0;
        changed = orchestra_controller_publish_staging(
            controller, state, ORCH_DEFICIT_NONE, ORCH_DEFICIT_NONE, 0,
            controller->active_persistence, metrics->window_generation,
            metrics->q_permille, now,
            ORCH_DEFICIT_NONE, state == ORCHESTRA_CTRL_RECOVERY);
        if (!changed && state == controller->active_state)
            no_op = 1;
    } else {
        persistence = controller->active_deficit_class == metrics->deficit_class &&
            controller->active_primary_deficit == metrics->primary_deficit ?
                controller->active_persistence + 1u : 1u;
        if (persistence > 255u)
            persistence = 255u;
        state = controller->active_state;
        if (state == ORCHESTRA_CTRL_NORMAL && persistence >= 2u)
            state = ORCHESTRA_CTRL_DEGRADED;
        if (state == ORCHESTRA_CTRL_ROLLBACK)
            no_op = 1;
        else {
            changed = orchestra_controller_publish_staging(
                controller, state, metrics->primary_deficit,
                metrics->secondary_deficit, metrics->deficit_severity,
                persistence, metrics->window_generation, metrics->q_permille,
                now,
                metrics->deficit_class, 0);
            if (!changed)
                no_op = 1;
        }
        controller->active_persistence = persistence;
        controller->active_primary_deficit = metrics->primary_deficit;
        controller->active_secondary_deficit = metrics->secondary_deficit;
        controller->active_deficit_class = metrics->deficit_class;
        controller->active_severity = metrics->deficit_severity;
        if (!changed && persistence >= 3u) {
            saturated = 1;
            if (orchestra_controller_publish_staging(
                    controller, ORCHESTRA_CTRL_SATURATED,
                    metrics->primary_deficit, metrics->secondary_deficit,
                    metrics->deficit_severity, persistence,
                    metrics->window_generation, metrics->q_permille,
                    now,
                    metrics->deficit_class, 0))
                changed = 1;
        }
        if (saturated) {
            controller->active_state = ORCHESTRA_CTRL_SATURATED;
            controller->flags |= ORCHESTRA_CONTROLLER_F_SATURATED;
            controller->saturation_count++;
        }
    }
    controller->last_update_ns = now;
    controller->next_update_ns = orchestra_controller_deadline_v10(
        now, controller->update_period_ns);
    controller->active.last_window_generation = metrics->window_generation;
    controller->active.last_q_permille = metrics->q_permille;
    if (changed) {
        controller->evaluation_baseline_q_permille = metrics->q_permille;
        controller->evaluation_until_ns = orchestra_controller_deadline_v10(
            now, ORCHESTRA_CONTROLLER_EVALUATION_NS);
        controller->flags |= ORCHESTRA_CONTROLLER_F_EVALUATING;
    }
    if (telemetry)
        orchestra_controller_note_state(controller->active_state,
                                         metrics->primary_deficit,
                                         metrics->secondary_deficit,
                                         metrics->deficit_class,
                                         controller->active_generation,
                                         metrics->window_generation,
                                         metrics->q_permille,
                                         ORCH_ACTUATOR_COUNT, 0, changed,
                                         saturated, 0, recovery, no_op);
    if (telemetry && changed)
        orchestra_controller_note_actuator_changes(controller);
    bpf_spin_unlock(&controller->lock);
}

static __always_inline void orchestra_controller_note_override_v10(void)
{
    struct orchestra_controller_state_v10 *controller;
    struct orchestra_controller_telemetry_v10 *telemetry;
    uint32_t key = 0;

    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    telemetry = orchestra_controller_telemetry();
    if (controller) {
        bpf_spin_lock(&controller->lock);
        controller->override_count++;
        bpf_spin_unlock(&controller->lock);
    }
    if (telemetry) {
        bpf_spin_lock(&telemetry->lock);
        telemetry->override_count++;
        bpf_spin_unlock(&telemetry->lock);
    }
}

#endif /* __BPF__ */

#endif /* ORCHESTRA_CONTROLLER_H */
