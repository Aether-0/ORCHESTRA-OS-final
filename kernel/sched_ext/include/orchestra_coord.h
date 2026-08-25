/* SPDX-License-Identifier: GPL-2.0 */
/* Native bounded coordination-window measurement helpers. */
#ifndef ORCHESTRA_COORD_H
#define ORCHESTRA_COORD_H

#include "orchestra_control_abi.h"

#ifndef __always_inline
#define __always_inline inline
#endif

#ifdef __BPF__
#define ORCHESTRA_COORD_NOINLINE __attribute__((noinline))
#else
#define ORCHESTRA_COORD_NOINLINE
#endif

#ifndef ORCHESTRA_U64_MAX
#define ORCHESTRA_U64_MAX (~(uint64_t)0)
#endif

#ifdef __BPF__
#define ORCHESTRA_COORD_UNROLL _Pragma("unroll")
#else
#define ORCHESTRA_COORD_UNROLL
#endif

struct orchestra_deficit_result_v10 {
    uint32_t class_id;
    uint32_t primary;
    uint32_t secondary;
    uint32_t severity;
    uint32_t deficient_count;
};

static __always_inline uint32_t orchestra_coord_clamp(uint64_t value)
{
    return value > ORCHESTRA_V10_FIXED_POINT_SCALE ?
        ORCHESTRA_V10_FIXED_POINT_SCALE : (uint32_t)value;
}

static __always_inline uint32_t orchestra_coord_ratio(uint64_t numerator,
                                                        uint64_t denominator)
{
#ifdef __BPF__
    if (denominator == 0)
        return 0;
    if (numerator >= denominator)
        return ORCHESTRA_V10_FIXED_POINT_SCALE;
    return (uint32_t)((numerator * ORCHESTRA_V10_FIXED_POINT_SCALE) /
                      denominator);
#else
    uint64_t quotient;
    uint64_t remainder;

    if (denominator == 0)
        return 0;
    if (numerator >= denominator)
        return ORCHESTRA_V10_FIXED_POINT_SCALE;
    quotient = numerator / denominator;
    remainder = numerator % denominator;
    if (quotient > ORCHESTRA_U64_MAX / ORCHESTRA_V10_FIXED_POINT_SCALE)
        return ORCHESTRA_V10_FIXED_POINT_SCALE;
    quotient *= ORCHESTRA_V10_FIXED_POINT_SCALE;
    if (remainder > ORCHESTRA_U64_MAX / ORCHESTRA_V10_FIXED_POINT_SCALE)
        return (uint32_t)quotient;
    quotient += (remainder * ORCHESTRA_V10_FIXED_POINT_SCALE) / denominator;
    return orchestra_coord_clamp(quotient);
#endif
}

/* Integer square root for the largest possible four-factor permille product
 * (1000^4).  Two applications give a verifier-safe fourth root. */
static __always_inline uint64_t orchestra_coord_isqrt(uint64_t value)
{
    uint64_t low = 0;
    uint64_t high = UINT64_C(1000001);

    ORCHESTRA_COORD_UNROLL
    for (int iteration = 0; iteration < 21; iteration++) {
        uint64_t middle = (low + high) / 2;

        if (middle == 0 || middle <= value / middle)
            low = middle + 1;
        else
            high = middle;
    }
    return low == 0 ? 0 : low - 1;
}

static ORCHESTRA_COORD_NOINLINE uint32_t orchestra_coord_geomean4(
    uint32_t s1, uint32_t s2, uint32_t s3, uint32_t s4)
{
#ifdef __BPF__
    uint32_t result = s1 < s2 ? s1 : s2;

    if (s3 < result)
        result = s3;
    if (s4 < result)
        result = s4;
    return result;
#else
    uint64_t product = (uint64_t)s1 * (uint64_t)s2;
    uint64_t estimate;
    uint64_t cube;

    product *= (uint64_t)s3;
    product *= (uint64_t)s4;
    if (product == 0)
        return 0;
    estimate = ((uint64_t)s1 + (uint64_t)s2 + (uint64_t)s3 + (uint64_t)s4) /
        4u;
    if (estimate == 0)
        return 0;
    cube = estimate * estimate * estimate;
    estimate = (3u * estimate + product / cube) / 4u;
    cube = estimate * estimate * estimate;
    estimate = (3u * estimate + product / cube) / 4u;
    cube = estimate * estimate * estimate;
    estimate = (3u * estimate + product / cube) / 4u;
    return orchestra_coord_clamp(estimate);
#endif
}

static __always_inline uint32_t orchestra_coord_min4(uint32_t a, uint32_t b,
                                                      uint32_t c, uint32_t d)
{
    uint32_t result = a < b ? a : b;

    if (c < result)
        result = c;
    if (d < result)
        result = d;
    return result;
}

static __always_inline uint32_t orchestra_coord_deficit_for_score(
    uint32_t score, uint32_t threshold)
{
    return score < threshold ? 1u : 0u;
}

/* The deficit-to-actuator matrix is a shared ABI-level policy.  The kernel
 * controller, telemetry, and userspace diagnostics all use these IDs/masks. */
static __always_inline uint32_t orchestra_deficit_actuator_mask(
    uint32_t deficit)
{
    switch (deficit) {
    case ORCH_DEFICIT_SIGNAL:
        return (1u << ORCH_ACTUATOR_PREDICTION_CONFIDENCE) |
               (1u << ORCH_ACTUATOR_PREDICTION_HORIZON) |
               (1u << ORCH_ACTUATOR_SIGNAL_CADENCE);
    case ORCH_DEFICIT_COMPLIANCE:
        return (1u << ORCH_ACTUATOR_CONSENSUS_BLEND) |
               (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD) |
               (1u << ORCH_ACTUATOR_YIELD_THRESHOLD);
    case ORCH_DEFICIT_COHERENCE:
        return (1u << ORCH_ACTUATOR_CONSENSUS_BLEND) |
               (1u << ORCH_ACTUATOR_SWITCHING_PENALTY) |
               (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD) |
               (1u << ORCH_ACTUATOR_COORDINATION_THRESHOLD);
    case ORCH_DEFICIT_STABILITY:
        return (1u << ORCH_ACTUATOR_JITTER) |
               (1u << ORCH_ACTUATOR_SWITCHING_PENALTY) |
               (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD) |
               (1u << ORCH_ACTUATOR_THROTTLE_DURATION) |
               (1u << ORCH_ACTUATOR_SLEEP_DEFER);
    case ORCH_DEFICIT_MIXED:
        return (1u << ORCH_ACTUATOR_JITTER) |
               (1u << ORCH_ACTUATOR_SWITCHING_PENALTY) |
               (1u << ORCH_ACTUATOR_CONSENSUS_BLEND) |
               (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD) |
               (1u << ORCH_ACTUATOR_YIELD_THRESHOLD) |
               (1u << ORCH_ACTUATOR_PREDICTION_CONFIDENCE) |
               (1u << ORCH_ACTUATOR_PREDICTION_HORIZON) |
               (1u << ORCH_ACTUATOR_SIGNAL_CADENCE) |
               (1u << ORCH_ACTUATOR_COORDINATION_THRESHOLD);
    default:
        return 0;
    }
}

static __always_inline int orchestra_deficit_actuator_direction(
    uint32_t deficit, uint32_t actuator)
{
    switch (deficit) {
    case ORCH_DEFICIT_SIGNAL:
        if (actuator == ORCH_ACTUATOR_PREDICTION_HORIZON ||
            actuator == ORCH_ACTUATOR_SIGNAL_CADENCE)
            return -1;
        return actuator == ORCH_ACTUATOR_PREDICTION_CONFIDENCE ? 1 : 0;
    case ORCH_DEFICIT_COMPLIANCE:
        return (actuator == ORCH_ACTUATOR_CONSENSUS_BLEND ||
                actuator == ORCH_ACTUATOR_MIGRATION_THRESHOLD ||
                actuator == ORCH_ACTUATOR_YIELD_THRESHOLD) ? 1 : 0;
    case ORCH_DEFICIT_COHERENCE:
        if (actuator == ORCH_ACTUATOR_COORDINATION_THRESHOLD)
            return -1;
        return (actuator == ORCH_ACTUATOR_CONSENSUS_BLEND ||
                actuator == ORCH_ACTUATOR_SWITCHING_PENALTY ||
                actuator == ORCH_ACTUATOR_MIGRATION_THRESHOLD) ? 1 : 0;
    case ORCH_DEFICIT_STABILITY:
        return (actuator == ORCH_ACTUATOR_JITTER ||
                actuator == ORCH_ACTUATOR_SWITCHING_PENALTY ||
                actuator == ORCH_ACTUATOR_MIGRATION_THRESHOLD ||
                actuator == ORCH_ACTUATOR_THROTTLE_DURATION ||
                actuator == ORCH_ACTUATOR_SLEEP_DEFER) ? 1 : 0;
    case ORCH_DEFICIT_MIXED:
        return 1;
    default:
        return 0;
    }
}

static __always_inline struct orchestra_deficit_result_v10
orchestra_classify_deficit_v10(uint32_t s1, uint32_t s2, uint32_t s3,
                               uint32_t s4, uint32_t q, uint32_t threshold)
{
    struct orchestra_deficit_result_v10 result = { 0 };
    uint32_t scores[4] = { s1, s2, s3, s4 };
    uint32_t classes[4] = {
        ORCH_DEFICIT_SIGNAL,
        ORCH_DEFICIT_COMPLIANCE,
        ORCH_DEFICIT_COHERENCE,
        ORCH_DEFICIT_STABILITY
    };
    uint32_t lowest = ORCHESTRA_V10_FIXED_POINT_SCALE + 1u;
    uint32_t second = ORCHESTRA_V10_FIXED_POINT_SCALE + 1u;
    uint32_t lowest_class = ORCH_DEFICIT_NONE;
    uint32_t second_class = ORCH_DEFICIT_NONE;

    ORCHESTRA_COORD_UNROLL
    for (int index = 0; index < 4; index++) {
        if (scores[index] < threshold) {
            result.deficient_count++;
            if (scores[index] < lowest) {
                second = lowest;
                second_class = lowest_class;
                lowest = scores[index];
                lowest_class = classes[index];
            } else if (scores[index] < second) {
                second = scores[index];
                second_class = classes[index];
            }
        }
    }
    result.class_id = result.deficient_count == 0 ? ORCH_DEFICIT_NONE :
        (result.deficient_count == 1 ? lowest_class : ORCH_DEFICIT_MIXED);
    if (result.deficient_count == 0) {
        result.primary = ORCH_DEFICIT_NONE;
        result.secondary = ORCH_DEFICIT_NONE;
    } else if (result.deficient_count == 1) {
        result.primary = lowest_class;
        result.secondary = ORCH_DEFICIT_NONE;
    } else {
        result.primary = lowest_class;
        result.secondary = second_class;
    }
    result.severity = q >= ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - q;
    return result;
}

#ifdef __BPF__
static __always_inline struct orchestra_deficit_result_v10
orchestra_classify_deficit_bpf(uint32_t s1, uint32_t s2, uint32_t s3,
                               uint32_t s4, uint32_t q, uint32_t threshold)
{
    struct orchestra_deficit_result_v10 result = { 0 };

    if (s1 < threshold) {
        result.deficient_count++;
        result.primary = ORCH_DEFICIT_SIGNAL;
    }
    if (s2 < threshold) {
        if (result.deficient_count == 0)
            result.primary = ORCH_DEFICIT_COMPLIANCE;
        else if (result.deficient_count == 1)
            result.secondary = ORCH_DEFICIT_COMPLIANCE;
        result.deficient_count++;
    }
    if (s3 < threshold) {
        if (result.deficient_count == 0)
            result.primary = ORCH_DEFICIT_COHERENCE;
        else if (result.deficient_count == 1)
            result.secondary = ORCH_DEFICIT_COHERENCE;
        result.deficient_count++;
    }
    if (s4 < threshold) {
        if (result.deficient_count == 0)
            result.primary = ORCH_DEFICIT_STABILITY;
        else if (result.deficient_count == 1)
            result.secondary = ORCH_DEFICIT_STABILITY;
        result.deficient_count++;
    }
    result.class_id = result.deficient_count == 0 ? ORCH_DEFICIT_NONE :
        (result.deficient_count == 1 ? result.primary : ORCH_DEFICIT_MIXED);
    result.severity = q >= ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - q;
    return result;
}
#endif

static __always_inline int orchestra_deficit_pair_direction(
    uint32_t deficit_class, uint32_t primary, uint32_t secondary,
    uint32_t actuator)
{
    if (deficit_class != ORCH_DEFICIT_MIXED)
        return orchestra_deficit_actuator_direction(primary, actuator);
    if (orchestra_deficit_actuator_mask(primary) & (1u << actuator))
        return orchestra_deficit_actuator_direction(primary, actuator);
    return orchestra_deficit_actuator_direction(secondary, actuator);
}

#ifdef __BPF__

/* The controller owns the window duration.  Invalid values fall back to a
 * bounded default before they can influence map indexing. */
static __always_inline uint64_t orchestra_coord_window_duration_ns(void)
{
    struct orchestra_controller_state_v10 *controller;
    uint32_t key = 0;
    uint64_t duration = ORCHESTRA_COORD_DEFAULT_WINDOW_NS;

    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (!controller)
        return duration;
    bpf_spin_lock(&controller->lock);
    if (controller->update_period_ns >= UINT64_C(1000000) &&
        controller->update_period_ns <= UINT64_C(1000000000))
        duration = controller->update_period_ns / 10;
    bpf_spin_unlock(&controller->lock);
    if (duration < UINT64_C(1000000))
        duration = UINT64_C(1000000);
    if (duration > UINT64_C(1000000000))
        duration = UINT64_C(1000000000);
    return duration;
}

static __always_inline uint32_t orchestra_coordination_threshold_v10(void)
{
    struct orchestra_controller_state_v10 *controller;
    uint32_t key = 0;
    uint32_t threshold = 700u;

    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (!controller)
        return threshold;
    bpf_spin_lock(&controller->lock);
    if (controller->active.actuators[ORCH_ACTUATOR_COORDINATION_THRESHOLD].current_value >=
            250u &&
        controller->active.actuators[ORCH_ACTUATOR_COORDINATION_THRESHOLD].current_value <=
            950u)
        threshold = (uint32_t)controller->active.actuators[
            ORCH_ACTUATOR_COORDINATION_THRESHOLD].current_value;
    bpf_spin_unlock(&controller->lock);
    return threshold;
}

static __always_inline uint32_t orchestra_coord_cpu_domain(void)
{
    uint32_t cpu = bpf_get_smp_processor_id();

    return cpu < ORCHESTRA_COORD_MAX_CPU_DOMAINS ? cpu :
        ORCHESTRA_COORD_MAX_CPU_DOMAINS - 1u;
}

static __always_inline uint32_t orchestra_coord_numa_domain(void)
{
    long node = bpf_get_numa_node_id();

    if (node < 0 || (uint64_t)node >= ORCHESTRA_COORD_MAX_NUMA_DOMAINS)
        return 0;
    return (uint32_t)node;
}

static __always_inline uint32_t orchestra_coord_slot(uint32_t scope,
                                                      uint32_t domain)
{
    if (scope == ORCHESTRA_COORD_SCOPE_CPU)
        return domain < ORCHESTRA_COORD_MAX_CPU_DOMAINS ? domain :
            ORCHESTRA_COORD_MAX_CPU_DOMAINS - 1u;
    if (scope == ORCHESTRA_COORD_SCOPE_NUMA)
        return ORCHESTRA_COORD_MAX_CPU_DOMAINS +
            (domain < ORCHESTRA_COORD_MAX_NUMA_DOMAINS ? domain : 0u);
    return ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT;
}

static __always_inline uint32_t orchestra_coord_key(uint32_t slot,
                                                     uint64_t generation)
{
    return slot * ORCHESTRA_COORD_WINDOW_BANK_COUNT +
        (uint32_t)(generation & 1u);
}

static __always_inline struct orchestra_coordination_state_v10 *
orchestra_coord_lookup(uint32_t scope, uint32_t domain, uint64_t generation)
{
    uint32_t key = orchestra_coord_key(orchestra_coord_slot(scope, domain),
                                       generation);

    return bpf_map_lookup_elem(&orch_coord_v10, &key);
}

static ORCHESTRA_COORD_NOINLINE void orchestra_coord_initialize(
    struct orchestra_coordination_state_v10 *state, uint64_t scope_domain,
    uint64_t generation, uint64_t start_ns, uint64_t duration_ns)
{
    uint32_t scope = (uint32_t)(scope_domain >> 32);
    uint32_t domain = (uint32_t)scope_domain;

#ifndef __BPF__
    __builtin_memset(&state->magic, 0,
                     sizeof(*state) - sizeof(state->lock));
#endif
    state->magic = ORCHESTRA_ABI_MAGIC;
    state->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
    state->value_size = sizeof(*state);
    state->schema_version = ORCHESTRA_COORD_SCHEMA_VERSION;
    state->scope = scope;
    state->domain_id = domain;
    state->flags = ORCHESTRA_COORD_F_VALID;
    state->window_generation = generation;
    state->window_start_ns = start_ns;
    state->window_end_ns = start_ns + duration_ns;
}

static ORCHESTRA_COORD_NOINLINE struct orchestra_coordination_state_v10 *
orchestra_coord_prepare(uint32_t scope, uint32_t domain, uint64_t generation,
                        uint64_t start_ns, uint64_t duration_ns)
{
    struct orchestra_coordination_state_v10 *state =
        orchestra_coord_lookup(scope, domain, generation);

    if (!state)
        return NULL;
    if (state->window_generation != generation ||
        state->schema_version != ORCHESTRA_COORD_SCHEMA_VERSION) {
        bpf_spin_lock(&state->lock);
        if (state->window_generation != generation ||
            state->schema_version != ORCHESTRA_COORD_SCHEMA_VERSION)
            orchestra_coord_initialize(
                state, ((uint64_t)scope << 32) | (uint64_t)domain,
                generation, start_ns, duration_ns);
        bpf_spin_unlock(&state->lock);
    }
    return state;
}

static __always_inline uint32_t orchestra_coord_average_sum(uint64_t sum,
                                                              uint64_t count)
{
    return orchestra_coord_ratio(sum, count);
}

static __always_inline void orchestra_coord_finalize_locked(
    struct orchestra_coordination_state_v10 *state,
    struct orchestra_coordination_metrics_v10 *metrics)
{
#ifdef __BPF__
    uint32_t stability_penalty = orchestra_coord_ratio(
        state->action_transition_count, state->eligible_observations);

    state->s1_permille = orchestra_coord_ratio(
        state->signal_freshness_sum, state->prediction_observations);
    state->s2_permille = orchestra_coord_ratio(
        state->policy_compliance_count, state->executed_observations);
    state->s4_permille = stability_penalty >=
        ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - stability_penalty;
    if (state->synchronized_mass_switch_count != 0)
        state->flags |= ORCHESTRA_COORD_F_MASS_SWITCH;
    if (state->prediction_stale_count != 0 ||
        state->prediction_invalid_count != 0)
        state->flags |= ORCHESTRA_COORD_F_SIGNAL_FALLBACK;
#else
    uint32_t freshness = orchestra_coord_average_sum(
        state->signal_freshness_sum, state->prediction_observations);
    uint32_t confidence = orchestra_coord_average_sum(
        state->prediction_confidence_sum, state->prediction_observations);
    uint32_t prediction_error = orchestra_coord_average_sum(
        state->prediction_error_sum, state->prediction_observations);
    uint32_t continuity = orchestra_coord_average_sum(
        state->generation_continuity_sum, state->prediction_observations);
    uint32_t stability_penalty;

    state->s1_permille = orchestra_coord_clamp(
        (uint64_t)(freshness + confidence + prediction_error + continuity) / 4u);
    state->s2_permille = orchestra_coord_ratio(
        state->policy_compliance_count, state->executed_observations);
    stability_penalty = orchestra_coord_ratio(
        state->action_transition_count, state->eligible_observations);
    if (state->synchronized_mass_switch_count != 0) {
        uint64_t mass_transitions = state->synchronized_mass_switch_count *
            ORCHESTRA_COORD_MASS_SWITCH_THRESHOLD;
        uint32_t mass_penalty = orchestra_coord_ratio(
            mass_transitions, state->eligible_observations);

        if (mass_penalty > stability_penalty)
            stability_penalty = mass_penalty;
    }
    if (state->eligible_observations != 0) {
        uint64_t oscillations = state->repeated_migration_count +
            state->run_yield_oscillation_count +
            state->throttle_cycle_count + state->sleep_cycle_count;
        uint32_t oscillation_penalty = orchestra_coord_ratio(
            oscillations, state->eligible_observations);

        if (oscillation_penalty > stability_penalty)
            stability_penalty = oscillation_penalty;
    }
    state->s4_permille = stability_penalty >=
        ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - stability_penalty;
    if (state->synchronized_mass_switch_count != 0)
        state->flags |= ORCHESTRA_COORD_F_MASS_SWITCH;
    if (state->prediction_stale_count != 0 ||
        state->prediction_invalid_count != 0)
        state->flags |= ORCHESTRA_COORD_F_SIGNAL_FALLBACK;
#endif

    if (!metrics)
        return;
    metrics->scope = state->scope;
    metrics->domain_id = state->domain_id;
    metrics->window_generation = state->window_generation;
    metrics->window_start_ns = state->window_start_ns;
    metrics->window_end_ns = state->window_end_ns;
    metrics->eligible_observations = state->eligible_observations;
    metrics->executed_observations = state->executed_observations;
    metrics->s1_permille = state->s1_permille;
    metrics->s2_permille = state->s2_permille;
    metrics->s4_permille = state->s4_permille;
    metrics->action_transition_count = state->action_transition_count;
    metrics->synchronized_mass_switch_count =
        state->synchronized_mass_switch_count;
    metrics->fallback_count = state->fallback_count;
    metrics->controller_override_count = state->controller_override_count;
    metrics->capability_adjustment_count =
        state->capability_adjustment_count;
}

#ifdef __BPF__
static ORCHESTRA_COORD_NOINLINE uint32_t orchestra_coord_compute_s3(
    const struct orchestra_coordination_state_v10 *state)
{
    uint64_t action0 = state->actual_action_count[0];
    uint64_t action1 = state->actual_action_count[1];
    uint64_t action2 = state->actual_action_count[2];
    uint64_t action3 = state->actual_action_count[3];
    uint64_t action4 = state->actual_action_count[4];
    uint64_t total = action0 + action1 + action2 + action3 + action4;
    uint64_t maximum = action0;

    if (action1 > maximum)
        maximum = action1;
    if (action2 > maximum)
        maximum = action2;
    if (action3 > maximum)
        maximum = action3;
    if (action4 > maximum)
        maximum = action4;
    if (total == 0)
        return 0;
    return orchestra_coord_ratio(maximum, total);
}
#endif

/* Defined by orchestra_controller.h after this header is included. */
static ORCHESTRA_COORD_NOINLINE void orchestra_controller_update_from_coord(
    uint64_t now, const struct orchestra_coordination_metrics_v10 *metrics);

static ORCHESTRA_COORD_NOINLINE void orchestra_coord_begin_window(
    uint64_t now, uint64_t *generation, uint64_t *start_ns,
    uint64_t *duration_ns)
{
#ifdef __BPF__
    uint64_t duration = ORCHESTRA_COORD_DEFAULT_WINDOW_NS;
    uint64_t current_generation = now / duration;
    uint64_t current_start = current_generation * duration;

    if (generation)
        *generation = current_generation;
    if (start_ns)
        *start_ns = current_start;
    if (duration_ns)
        *duration_ns = duration;
#else
    struct orchestra_coordination_state_v10 *current;
    struct orchestra_coordination_state_v10 *previous = NULL;
    struct orchestra_coordination_metrics_v10 metrics = {};
    uint64_t duration = orchestra_coord_window_duration_ns();
    uint64_t current_generation = now / duration;
    uint64_t current_start = current_generation * duration;
    uint32_t current_key = orchestra_coord_key(
        ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT, current_generation);
    uint32_t previous_key = 0;
    uint32_t deficit_threshold = orchestra_coordination_threshold_v10();
    int current_needs_window = 0;

    current = bpf_map_lookup_elem(&orch_coord_v10, &current_key);
    if (current_generation != 0) {
        previous_key = orchestra_coord_key(
            ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT, current_generation - 1u);
        previous = bpf_map_lookup_elem(&orch_coord_v10, &previous_key);
    }
    if (current) {
        bpf_spin_lock(&current->lock);
        current_needs_window =
            current->window_generation != current_generation ||
            current->schema_version != ORCHESTRA_COORD_SCHEMA_VERSION;
        bpf_spin_unlock(&current->lock);
        if (current_needs_window) {
            if (previous && previous->window_generation ==
                    current_generation - 1u) {
                bpf_spin_lock(&previous->lock);
                orchestra_coord_finalize_locked(previous, &metrics);
                bpf_spin_unlock(&previous->lock);
                metrics.s3_permille = orchestra_coord_compute_s3(previous);
                metrics.q_permille = orchestra_coord_geomean4(
                    metrics.s1_permille, metrics.s2_permille,
                    metrics.s3_permille, metrics.s4_permille);
                {
                    struct orchestra_deficit_result_v10 deficit =
                        orchestra_classify_deficit_bpf(
                            metrics.s1_permille, metrics.s2_permille,
                            metrics.s3_permille, metrics.s4_permille,
                            metrics.q_permille, deficit_threshold);

                    metrics.deficit_class = deficit.class_id;
                    metrics.primary_deficit = deficit.primary;
                    metrics.secondary_deficit = deficit.secondary;
                    metrics.deficit_severity = deficit.severity;
                    metrics.deficit_persistence =
                        deficit.deficient_count == 0 ? 0u : 1u;
                }
                bpf_spin_lock(&previous->lock);
                if (!(previous->flags & ORCHESTRA_COORD_F_FINALIZED)) {
                    previous->s3_permille = metrics.s3_permille;
                    previous->q_permille = metrics.q_permille;
                    previous->deficit_class = metrics.deficit_class;
                    previous->primary_deficit = metrics.primary_deficit;
                    previous->secondary_deficit = metrics.secondary_deficit;
                    previous->deficit_severity = metrics.deficit_severity;
                    previous->deficit_persistence =
                        metrics.deficit_persistence;
                    previous->flags |= ORCHESTRA_COORD_F_FINALIZED;
                    previous->finalized_ns = now;
                }
                bpf_spin_unlock(&previous->lock);
            }
            (void)orchestra_coord_prepare(
                ORCHESTRA_COORD_SCOPE_GLOBAL,
                ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT, current_generation,
                current_start, duration);
        }
    }
    if (generation)
        *generation = current_generation;
    if (start_ns)
        *start_ns = current_start;
    if (duration_ns)
        *duration_ns = duration;
#endif
}

static __always_inline void orchestra_coord_record_signal_one(
    struct orchestra_coordination_state_v10 *state, int valid, int stale,
    uint32_t confidence, uint32_t observed, uint32_t predicted,
    uint64_t published_ns, uint64_t expires_ns, uint64_t sequence)
{
    uint64_t previous_sequence;
    uint32_t freshness = 0;
    uint32_t error = 0;
    uint32_t continuity = ORCHESTRA_V10_FIXED_POINT_SCALE;

    if (!state)
        return;
    __sync_fetch_and_add(&state->prediction_observations, 1);
    if (!valid) {
        if (stale)
            __sync_fetch_and_add(&state->prediction_stale_count, 1);
        else
            __sync_fetch_and_add(&state->prediction_invalid_count, 1);
        return;
    }
    if (expires_ns > published_ns) {
        uint64_t duration = expires_ns - published_ns;
        uint64_t remaining = expires_ns > bpf_ktime_get_ns() ?
            expires_ns - bpf_ktime_get_ns() : 0;

        freshness = orchestra_coord_ratio(remaining, duration);
    }
    error = observed > predicted ? observed - predicted : predicted - observed;
    if (error >= ORCHESTRA_V10_FIXED_POINT_SCALE)
        error = ORCHESTRA_V10_FIXED_POINT_SCALE;
    error = ORCHESTRA_V10_FIXED_POINT_SCALE - error;
    previous_sequence = __sync_lock_test_and_set(
        &state->last_signal_generation, sequence);
    if (previous_sequence != 0 &&
        (sequence == 0 || sequence != previous_sequence + 1u)) {
        continuity = 0;
        __sync_fetch_and_add(&state->signal_generation_break_count, 1);
    }
    __sync_fetch_and_add(&state->prediction_valid_count, 1);
    __sync_fetch_and_add(&state->signal_freshness_sum, freshness);
    __sync_fetch_and_add(&state->prediction_confidence_sum,
                         confidence > ORCHESTRA_V10_FIXED_POINT_SCALE ?
                             ORCHESTRA_V10_FIXED_POINT_SCALE : confidence);
    __sync_fetch_and_add(&state->prediction_error_sum, error);
    __sync_fetch_and_add(&state->generation_continuity_sum, continuity);
}

static __always_inline void orchestra_coord_record_signal(
    int valid, int stale, uint32_t confidence, uint32_t observed,
    uint32_t predicted, int prediction_used, uint64_t published_ns,
    uint64_t expires_ns, uint64_t sequence)
{
    uint64_t generation;
    uint64_t start_ns;
    uint64_t duration_ns;
    struct orchestra_coordination_state_v10 *cpu_state;
    struct orchestra_coordination_state_v10 *numa_state;
    struct orchestra_coordination_state_v10 *global_state;

    orchestra_coord_begin_window(bpf_ktime_get_ns(), &generation, &start_ns,
                                  &duration_ns);
    cpu_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_CPU,
                                        orchestra_coord_cpu_domain(),
                                        generation, start_ns, duration_ns);
    numa_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_NUMA,
                                         orchestra_coord_numa_domain(),
                                         generation, start_ns, duration_ns);
    global_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_GLOBAL,
                                           ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                                           generation, start_ns, duration_ns);
    orchestra_coord_record_signal_one(cpu_state, valid, stale, confidence,
                                      observed, predicted, published_ns,
                                      expires_ns, sequence);
    orchestra_coord_record_signal_one(numa_state, valid, stale, confidence,
                                      observed, predicted, published_ns,
                                      expires_ns, sequence);
    orchestra_coord_record_signal_one(global_state, valid, stale, confidence,
                                      observed, predicted, published_ns,
                                      expires_ns, sequence);
    (void)prediction_used;
}

static __always_inline void orchestra_coord_mark_mass_switch(
    struct orchestra_coordination_state_v10 *cpu_state,
    struct orchestra_coordination_state_v10 *numa_state,
    struct orchestra_coordination_state_v10 *global_state)
{
    if (cpu_state)
        __sync_fetch_and_add(&cpu_state->synchronized_mass_switch_count, 1);
    if (numa_state)
        __sync_fetch_and_add(&numa_state->synchronized_mass_switch_count, 1);
    if (global_state)
        __sync_fetch_and_add(&global_state->synchronized_mass_switch_count, 1);
}

struct orchestra_coord_action_context {
    struct orchestra_coordination_state_v10 *state;
    uint32_t policy_action;
    uint32_t controller_action;
    uint32_t capability_action;
    uint32_t actual_action;
    uint32_t state_index;
    uint32_t previous_action;
    uint32_t previous_policy_state;
    uint32_t fallback_reason;
};

static ORCHESTRA_COORD_NOINLINE void orchestra_coord_record_action_state(
    const struct orchestra_coord_action_context *context)
{
#ifdef __BPF__
    struct orchestra_coordination_state_v10 *state = context->state;

    if (!state)
        return;
    __sync_fetch_and_add(&state->eligible_observations, 1);
    if (context->policy_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->policy_action_count[
            context->policy_action], 1);
    if (context->controller_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->controller_action_count[
            context->controller_action], 1);
    if (context->capability_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->capability_action_count[
            context->capability_action], 1);
    if (context->actual_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->coherence_hist[context->state_index &
            (ORCHESTRA_COORD_STATE_CLASS_COUNT - 1u)][context->actual_action], 1);
    if (context->actual_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->actual_action_count[
            context->actual_action], 1);
    if (context->fallback_reason != BRIDGE_FALLBACK_NONE)
        __sync_fetch_and_add(&state->fallback_count, 1);
    if (context->previous_action != context->actual_action)
        __sync_fetch_and_add(&state->action_transition_count, 1);
    if (context->previous_policy_state != context->state_index)
        __sync_fetch_and_add(&state->policy_state_switch_count, 1);
#else
    struct orchestra_coordination_state_v10 *state = context->state;
    uint32_t policy_action = context->policy_action;
    uint32_t controller_action = context->controller_action;
    uint32_t capability_action = context->capability_action;
    uint32_t actual_action = context->actual_action;
    uint32_t state_index = context->state_index;
    uint32_t previous_action = context->previous_action;
    uint32_t previous_policy_state = context->previous_policy_state;
    uint32_t fallback_reason = context->fallback_reason;
    int transitioned = previous_action != actual_action;

    if (!state)
        return;
    __sync_fetch_and_add(&state->eligible_observations, 1);
    if (policy_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->policy_action_count[policy_action], 1);
    if (controller_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->controller_action_count[controller_action], 1);
    if (capability_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->capability_action_count[capability_action], 1);
    if (actual_action < ORCHESTRA_ACTION_COUNT)
        __sync_fetch_and_add(&state->coherence_hist[state_index &
            (ORCHESTRA_COORD_STATE_CLASS_COUNT - 1u)][actual_action], 1);
    if (fallback_reason != BRIDGE_FALLBACK_NONE)
        __sync_fetch_and_add(&state->fallback_count, 1);
    if (controller_action != policy_action)
        __sync_fetch_and_add(&state->controller_override_count, 1);
    if (capability_action != controller_action)
        __sync_fetch_and_add(&state->capability_adjustment_count, 1);
    if (transitioned)
        __sync_fetch_and_add(&state->action_transition_count, 1);
    if (previous_action == ORCHESTRA_ACTION_MIGRATE &&
        actual_action == ORCHESTRA_ACTION_MIGRATE)
        __sync_fetch_and_add(&state->repeated_migration_count, 1);
    if ((previous_action == ORCHESTRA_ACTION_RUN &&
         actual_action == ORCHESTRA_ACTION_YIELD) ||
        (previous_action == ORCHESTRA_ACTION_YIELD &&
         actual_action == ORCHESTRA_ACTION_RUN))
        __sync_fetch_and_add(&state->run_yield_oscillation_count, 1);
    if (previous_action != actual_action &&
        (previous_action == ORCHESTRA_ACTION_THROTTLE ||
         actual_action == ORCHESTRA_ACTION_THROTTLE))
        __sync_fetch_and_add(&state->throttle_cycle_count, 1);
    if (previous_action != actual_action &&
        (previous_action == ORCHESTRA_ACTION_SLEEP ||
         actual_action == ORCHESTRA_ACTION_SLEEP))
        __sync_fetch_and_add(&state->sleep_cycle_count, 1);
    if (previous_policy_state != state_index)
        __sync_fetch_and_add(&state->policy_state_switch_count, 1);
#endif
}

#ifdef __BPF__
static ORCHESTRA_COORD_NOINLINE void orchestra_coord_record_action(
    const struct orchestra_coord_action_context *input, uint64_t now)
{
    struct orchestra_coordination_state_v10 *cpu_state;
    struct orchestra_coordination_state_v10 *numa_state;
    struct orchestra_coordination_state_v10 *global_state;
    struct orchestra_coord_action_context context = {
        .state = NULL,
        .policy_action = input->policy_action,
        .controller_action = input->controller_action,
        .capability_action = input->capability_action,
        .actual_action = input->actual_action,
        .state_index = input->state_index,
        .previous_action = input->previous_action,
        .previous_policy_state = input->previous_policy_state,
        .fallback_reason = input->fallback_reason,
    };
    uint64_t generation;
    uint64_t start_ns;
    uint64_t duration_ns;

    orchestra_coord_begin_window(now, &generation, &start_ns, &duration_ns);
    cpu_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_CPU,
                                        orchestra_coord_cpu_domain(),
                                        generation, start_ns, duration_ns);
    numa_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_NUMA,
                                         orchestra_coord_numa_domain(),
                                         generation, start_ns, duration_ns);
    global_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_GLOBAL,
                                           ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                                           generation, start_ns, duration_ns);
    context.state = cpu_state;
    orchestra_coord_record_action_state(&context);
    context.state = numa_state;
    orchestra_coord_record_action_state(&context);
    context.state = global_state;
    orchestra_coord_record_action_state(&context);
}
#else
static __always_inline void orchestra_coord_record_action(
    uint32_t policy_action, uint32_t controller_action,
    uint32_t capability_action, uint32_t actual_action, uint32_t state_index,
    uint32_t previous_action, uint32_t previous_policy_state,
    uint32_t fallback_reason, uint64_t now)
{
    struct orchestra_coord_cpu_v10 *local;
    struct orchestra_coordination_state_v10 *cpu_state;
    struct orchestra_coordination_state_v10 *numa_state;
    struct orchestra_coordination_state_v10 *global_state;
    uint32_t local_key = 0;
    uint64_t generation;
    uint64_t start_ns;
    uint64_t duration_ns;
    int transitioned = previous_action != actual_action;
    int mass_switch = 0;
    struct orchestra_coord_action_context action_context = {
        .state = NULL,
        .policy_action = policy_action,
        .controller_action = controller_action,
        .capability_action = capability_action,
        .actual_action = actual_action,
        .state_index = state_index,
        .previous_action = previous_action,
        .previous_policy_state = previous_policy_state,
        .fallback_reason = fallback_reason,
    };

    orchestra_coord_begin_window(now, &generation, &start_ns, &duration_ns);
    cpu_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_CPU,
                                        orchestra_coord_cpu_domain(),
                                        generation, start_ns, duration_ns);
    numa_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_NUMA,
                                         orchestra_coord_numa_domain(),
                                         generation, start_ns, duration_ns);
    global_state = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_GLOBAL,
                                           ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                                           generation, start_ns, duration_ns);
    local = bpf_map_lookup_elem(&orch_coord_cpu, &local_key);
    if (local) {
        if (local->window_generation != generation) {
            local->window_generation = generation;
            local->burst_start_ns = now;
            local->burst_transition_count = 0;
            local->last_action = ORCHESTRA_ACTION_RUN;
            local->last_policy_state = state_index;
        }
        if (transitioned) {
            if (local->burst_start_ns == 0 ||
                now - local->burst_start_ns >
                    ORCHESTRA_COORD_MASS_SWITCH_WINDOW_NS) {
                local->burst_start_ns = now;
                local->burst_transition_count = 0;
            }
            local->burst_transition_count++;
            local->last_transition_ns = now;
            if (local->burst_transition_count >=
                    ORCHESTRA_COORD_MASS_SWITCH_THRESHOLD) {
                mass_switch = 1;
                local->burst_transition_count =
                    ORCHESTRA_COORD_MASS_SWITCH_THRESHOLD / 2u;
            }
        }
        local->last_action = actual_action;
        local->last_policy_state = state_index;
    }
    action_context.state = cpu_state;
    orchestra_coord_record_action_state(&action_context);
    action_context.state = numa_state;
    orchestra_coord_record_action_state(&action_context);
    action_context.state = global_state;
    orchestra_coord_record_action_state(&action_context);
    if (mass_switch)
        orchestra_coord_mark_mass_switch(cpu_state, numa_state, global_state);
    return;
    if (cpu_state) {
        __sync_fetch_and_add(&cpu_state->eligible_observations, 1);
        if (policy_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&cpu_state->policy_action_count[policy_action], 1);
        if (controller_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&cpu_state->controller_action_count[controller_action], 1);
        if (capability_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&cpu_state->capability_action_count[capability_action], 1);
        if (actual_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&cpu_state->coherence_hist[state_index &
                (ORCHESTRA_COORD_STATE_CLASS_COUNT - 1u)][actual_action], 1);
        if (fallback_reason != BRIDGE_FALLBACK_NONE)
            __sync_fetch_and_add(&cpu_state->fallback_count, 1);
        if (controller_action != policy_action)
            __sync_fetch_and_add(&cpu_state->controller_override_count, 1);
        if (capability_action != controller_action)
            __sync_fetch_and_add(&cpu_state->capability_adjustment_count, 1);
        if (transitioned)
            __sync_fetch_and_add(&cpu_state->action_transition_count, 1);
        if (previous_action == ORCHESTRA_ACTION_MIGRATE &&
            actual_action == ORCHESTRA_ACTION_MIGRATE)
            __sync_fetch_and_add(&cpu_state->repeated_migration_count, 1);
        if ((previous_action == ORCHESTRA_ACTION_RUN &&
             actual_action == ORCHESTRA_ACTION_YIELD) ||
            (previous_action == ORCHESTRA_ACTION_YIELD &&
             actual_action == ORCHESTRA_ACTION_RUN))
            __sync_fetch_and_add(&cpu_state->run_yield_oscillation_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_THROTTLE ||
             actual_action == ORCHESTRA_ACTION_THROTTLE))
            __sync_fetch_and_add(&cpu_state->throttle_cycle_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_SLEEP ||
             actual_action == ORCHESTRA_ACTION_SLEEP))
            __sync_fetch_and_add(&cpu_state->sleep_cycle_count, 1);
        if (previous_policy_state != state_index)
            __sync_fetch_and_add(&cpu_state->policy_state_switch_count, 1);
    }
    if (numa_state) {
        __sync_fetch_and_add(&numa_state->eligible_observations, 1);
        if (policy_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&numa_state->policy_action_count[policy_action], 1);
        if (controller_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&numa_state->controller_action_count[controller_action], 1);
        if (capability_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&numa_state->capability_action_count[capability_action], 1);
        if (actual_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&numa_state->coherence_hist[state_index &
                (ORCHESTRA_COORD_STATE_CLASS_COUNT - 1u)][actual_action], 1);
        if (fallback_reason != BRIDGE_FALLBACK_NONE)
            __sync_fetch_and_add(&numa_state->fallback_count, 1);
        if (controller_action != policy_action)
            __sync_fetch_and_add(&numa_state->controller_override_count, 1);
        if (capability_action != controller_action)
            __sync_fetch_and_add(&numa_state->capability_adjustment_count, 1);
        if (transitioned)
            __sync_fetch_and_add(&numa_state->action_transition_count, 1);
        if (previous_action == ORCHESTRA_ACTION_MIGRATE &&
            actual_action == ORCHESTRA_ACTION_MIGRATE)
            __sync_fetch_and_add(&numa_state->repeated_migration_count, 1);
        if ((previous_action == ORCHESTRA_ACTION_RUN &&
             actual_action == ORCHESTRA_ACTION_YIELD) ||
            (previous_action == ORCHESTRA_ACTION_YIELD &&
             actual_action == ORCHESTRA_ACTION_RUN))
            __sync_fetch_and_add(&numa_state->run_yield_oscillation_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_THROTTLE ||
             actual_action == ORCHESTRA_ACTION_THROTTLE))
            __sync_fetch_and_add(&numa_state->throttle_cycle_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_SLEEP ||
             actual_action == ORCHESTRA_ACTION_SLEEP))
            __sync_fetch_and_add(&numa_state->sleep_cycle_count, 1);
        if (previous_policy_state != state_index)
            __sync_fetch_and_add(&numa_state->policy_state_switch_count, 1);
    }
    if (global_state) {
        __sync_fetch_and_add(&global_state->eligible_observations, 1);
        if (policy_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&global_state->policy_action_count[policy_action], 1);
        if (controller_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&global_state->controller_action_count[controller_action], 1);
        if (capability_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&global_state->capability_action_count[capability_action], 1);
        if (actual_action < ORCHESTRA_ACTION_COUNT)
            __sync_fetch_and_add(&global_state->coherence_hist[state_index &
                (ORCHESTRA_COORD_STATE_CLASS_COUNT - 1u)][actual_action], 1);
        if (fallback_reason != BRIDGE_FALLBACK_NONE)
            __sync_fetch_and_add(&global_state->fallback_count, 1);
        if (controller_action != policy_action)
            __sync_fetch_and_add(&global_state->controller_override_count, 1);
        if (capability_action != controller_action)
            __sync_fetch_and_add(&global_state->capability_adjustment_count, 1);
        if (transitioned)
            __sync_fetch_and_add(&global_state->action_transition_count, 1);
        if (previous_action == ORCHESTRA_ACTION_MIGRATE &&
            actual_action == ORCHESTRA_ACTION_MIGRATE)
            __sync_fetch_and_add(&global_state->repeated_migration_count, 1);
        if ((previous_action == ORCHESTRA_ACTION_RUN &&
             actual_action == ORCHESTRA_ACTION_YIELD) ||
            (previous_action == ORCHESTRA_ACTION_YIELD &&
             actual_action == ORCHESTRA_ACTION_RUN))
            __sync_fetch_and_add(&global_state->run_yield_oscillation_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_THROTTLE ||
             actual_action == ORCHESTRA_ACTION_THROTTLE))
            __sync_fetch_and_add(&global_state->throttle_cycle_count, 1);
        if (previous_action != actual_action &&
            (previous_action == ORCHESTRA_ACTION_SLEEP ||
             actual_action == ORCHESTRA_ACTION_SLEEP))
            __sync_fetch_and_add(&global_state->sleep_cycle_count, 1);
        if (previous_policy_state != state_index)
            __sync_fetch_and_add(&global_state->policy_state_switch_count, 1);
    }
    if (mass_switch) {
        orchestra_coord_mark_mass_switch(cpu_state, numa_state, global_state);
    }
}
#endif

static __always_inline void orchestra_coord_record_execution(
    uint32_t policy_action, uint32_t capability_action, uint32_t actual_action,
    uint64_t now)
{
    uint64_t generation;
    uint64_t start_ns;
    uint64_t duration_ns;
    struct orchestra_coordination_state_v10 *states[3];

    orchestra_coord_begin_window(now, &generation, &start_ns, &duration_ns);
    states[0] = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_CPU,
                                        orchestra_coord_cpu_domain(),
                                        generation, start_ns, duration_ns);
    states[1] = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_NUMA,
                                        orchestra_coord_numa_domain(),
                                        generation, start_ns, duration_ns);
    states[2] = orchestra_coord_prepare(ORCHESTRA_COORD_SCOPE_GLOBAL,
                                        ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                                        generation, start_ns, duration_ns);
    ORCHESTRA_COORD_UNROLL
    for (int index = 0; index < 3; index++) {
        if (states[index]) {
            __sync_fetch_and_add(&states[index]->executed_observations, 1);
            if (actual_action < ORCHESTRA_ACTION_COUNT)
                __sync_fetch_and_add(&states[index]->actual_action_count[
                    actual_action], 1);
            if (actual_action == policy_action)
                __sync_fetch_and_add(&states[index]->policy_compliance_count, 1);
            if (actual_action == capability_action)
                __sync_fetch_and_add(&states[index]->directive_compliance_count, 1);
        }
    }
}

#ifdef __BPF__
static ORCHESTRA_COORD_NOINLINE uint32_t orchestra_coord_geomean4_runtime(
    uint32_t s1, uint32_t s2, uint32_t s3, uint32_t s4)
{
    uint64_t product = (uint64_t)s1 * (uint64_t)s2;
    uint64_t estimate;

    product *= (uint64_t)s3;
    product *= (uint64_t)s4;
    if (product == 0)
        return 0;
    estimate = ((uint64_t)s1 + (uint64_t)s2 + (uint64_t)s3 +
                (uint64_t)s4) / 4u;
    if (estimate == 0)
        return 0;
#pragma unroll
    for (int iteration = 0; iteration < 4; iteration++) {
        uint64_t cube = estimate * estimate * estimate;

        if (cube == 0)
            return 0;
        estimate = (3u * estimate + product / cube) / 4u;
    }
    return orchestra_coord_clamp(estimate);
}

static ORCHESTRA_COORD_NOINLINE void orchestra_controller_record_live_v10(
    const struct orchestra_coordination_state_v10 *coord, uint64_t now)
{
    struct orchestra_controller_state_v10 *controller;
    struct orchestra_controller_telemetry_v10 *telemetry;
    uint64_t next_generation;
    uint32_t key = 0;
    uint32_t state;
    uint32_t previous_state;
    uint32_t actuator_index = ORCH_ACTUATOR_COUNT;
    uint32_t actuator_mask = 0;
    uint32_t actuator_direction = 0;
    uint32_t actuator_saturated = 0;
    int actuator_changed = 0;

    if (!coord)
        return;
    controller = bpf_map_lookup_elem(&orch_ctrl_v10, &key);
    if (!controller)
        return;
    bpf_spin_lock(&controller->lock);
    if (coord->window_generation <= controller->active.last_window_generation) {
        controller->no_op_count++;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (controller->next_update_ns != 0 && now < controller->next_update_ns) {
        controller->no_op_count++;
        controller->active.last_window_generation = coord->window_generation;
        controller->active.last_q_permille = coord->q_permille;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    if (controller->active_generation == ORCHESTRA_U64_MAX) {
        controller->active_state = ORCHESTRA_CTRL_DISABLED;
        controller->flags = ORCHESTRA_CONTROLLER_F_VALID;
        controller->no_op_count++;
        bpf_spin_unlock(&controller->lock);
        return;
    }
    next_generation = controller->active_generation + 1u;
    previous_state = controller->active_state;
    state = coord->deficit_class == ORCH_DEFICIT_NONE ?
        ORCHESTRA_CTRL_NORMAL : ORCHESTRA_CTRL_DEGRADED;
    if (coord->deficit_class != ORCH_DEFICIT_NONE) {
        actuator_mask = orchestra_deficit_actuator_mask(
            coord->deficit_class);
        if (actuator_mask & (1u << ORCH_ACTUATOR_JITTER))
            actuator_index = ORCH_ACTUATOR_JITTER;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_SWITCHING_PENALTY))
            actuator_index = ORCH_ACTUATOR_SWITCHING_PENALTY;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_CONSENSUS_BLEND))
            actuator_index = ORCH_ACTUATOR_CONSENSUS_BLEND;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_MIGRATION_THRESHOLD))
            actuator_index = ORCH_ACTUATOR_MIGRATION_THRESHOLD;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_YIELD_THRESHOLD))
            actuator_index = ORCH_ACTUATOR_YIELD_THRESHOLD;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_THROTTLE_DURATION))
            actuator_index = ORCH_ACTUATOR_THROTTLE_DURATION;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_SLEEP_DEFER))
            actuator_index = ORCH_ACTUATOR_SLEEP_DEFER;
        else if (actuator_mask &
                 (1u << ORCH_ACTUATOR_PREDICTION_CONFIDENCE))
            actuator_index = ORCH_ACTUATOR_PREDICTION_CONFIDENCE;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_PREDICTION_HORIZON))
            actuator_index = ORCH_ACTUATOR_PREDICTION_HORIZON;
        else if (actuator_mask & (1u << ORCH_ACTUATOR_SIGNAL_CADENCE))
            actuator_index = ORCH_ACTUATOR_SIGNAL_CADENCE;
        else if (actuator_mask &
                 (1u << ORCH_ACTUATOR_COORDINATION_THRESHOLD))
            actuator_index = ORCH_ACTUATOR_COORDINATION_THRESHOLD;
        if (actuator_index < ORCH_ACTUATOR_COUNT)
            actuator_direction = orchestra_deficit_pair_direction(
                coord->deficit_class, coord->primary_deficit,
                coord->secondary_deficit, actuator_index) > 0 ? 1u : 2u;
    } else {
        if (controller->active.actuators[ORCH_ACTUATOR_JITTER].current_value !=
                controller->active.actuators[ORCH_ACTUATOR_JITTER].default_value)
            actuator_index = ORCH_ACTUATOR_JITTER;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_SWITCHING_PENALTY].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_SWITCHING_PENALTY].default_value)
            actuator_index = ORCH_ACTUATOR_SWITCHING_PENALTY;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_CONSENSUS_BLEND].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_CONSENSUS_BLEND].default_value)
            actuator_index = ORCH_ACTUATOR_CONSENSUS_BLEND;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_MIGRATION_THRESHOLD].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_MIGRATION_THRESHOLD].default_value)
            actuator_index = ORCH_ACTUATOR_MIGRATION_THRESHOLD;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_YIELD_THRESHOLD].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_YIELD_THRESHOLD].default_value)
            actuator_index = ORCH_ACTUATOR_YIELD_THRESHOLD;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_THROTTLE_DURATION].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_THROTTLE_DURATION].default_value)
            actuator_index = ORCH_ACTUATOR_THROTTLE_DURATION;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_SLEEP_DEFER].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_SLEEP_DEFER].default_value)
            actuator_index = ORCH_ACTUATOR_SLEEP_DEFER;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_PREDICTION_CONFIDENCE].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_PREDICTION_CONFIDENCE].default_value)
            actuator_index = ORCH_ACTUATOR_PREDICTION_CONFIDENCE;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_PREDICTION_HORIZON].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_PREDICTION_HORIZON].default_value)
            actuator_index = ORCH_ACTUATOR_PREDICTION_HORIZON;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_SIGNAL_CADENCE].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_SIGNAL_CADENCE].default_value)
            actuator_index = ORCH_ACTUATOR_SIGNAL_CADENCE;
        else if (controller->active.actuators[
                     ORCH_ACTUATOR_COORDINATION_THRESHOLD].current_value !=
                 controller->active.actuators[
                     ORCH_ACTUATOR_COORDINATION_THRESHOLD].default_value)
            actuator_index = ORCH_ACTUATOR_COORDINATION_THRESHOLD;
        if (actuator_index < ORCH_ACTUATOR_COUNT)
            actuator_direction = controller->active.actuators[
                actuator_index].current_value < controller->active.actuators[
                actuator_index].default_value ? 1u : 2u;
    }
    if (actuator_index < ORCH_ACTUATOR_COUNT && actuator_direction != 0) {
        struct orchestra_actuator_v10 *actuator =
            &controller->active.actuators[actuator_index];
        uint64_t old_value = actuator->current_value;
        uint64_t step = actuator->maximum_step == 0 ? 1 :
            actuator->maximum_step;
        uint64_t next_value = old_value;

        actuator->flags &= ~ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
        if (actuator_direction == 1) {
            if (old_value >= actuator->maximum ||
                actuator->maximum - old_value < step) {
                next_value = actuator->maximum;
                actuator_saturated = 1;
            } else {
                next_value = old_value + step;
            }
        } else if (old_value <= actuator->minimum ||
                   old_value - actuator->minimum < step) {
            next_value = actuator->minimum;
            actuator_saturated = 1;
        } else {
            next_value = old_value - step;
        }
        if (next_value != old_value &&
            actuator->generation != ORCHESTRA_U64_MAX) {
            actuator->previous_value = old_value;
            actuator->rollback_value = old_value;
            actuator->current_value = next_value;
            actuator->generation++;
            actuator->last_update_epoch = next_generation;
            actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_UPDATED;
            actuator_changed = 1;
        } else if (next_value == old_value ||
                   actuator->generation == ORCHESTRA_U64_MAX) {
            actuator_saturated = 1;
        }
        if (actuator_saturated)
            actuator->flags |= ORCHESTRA_CONTROLLER_ACTUATOR_F_SATURATED;
    }
    controller->active_state = state;
    controller->active_primary_deficit = coord->primary_deficit;
    controller->active_secondary_deficit = coord->secondary_deficit;
    controller->active_deficit_class = coord->deficit_class;
    controller->active_severity = coord->deficit_severity;
    controller->active_persistence = coord->deficit_persistence;
    controller->active_generation = next_generation;
    controller->active.generation = next_generation;
    controller->active.state = state;
    controller->active.primary_deficit = coord->primary_deficit;
    controller->active.secondary_deficit = coord->secondary_deficit;
    controller->active.severity = coord->deficit_severity;
    controller->active.persistence = coord->deficit_persistence;
    controller->active.last_window_generation = coord->window_generation;
    controller->active.last_q_permille = coord->q_permille;
    controller->controller_epoch++;
    if (controller->controller_epoch == 0)
        controller->controller_epoch = 1;
    controller->last_update_ns = now;
    controller->last_q_permille = coord->q_permille;
    controller->next_update_ns = ORCHESTRA_U64_MAX - now <
        controller->update_period_ns ? ORCHESTRA_U64_MAX :
        now + controller->update_period_ns;
    controller->flags = ORCHESTRA_CONTROLLER_F_VALID;
    bpf_spin_unlock(&controller->lock);
    telemetry = bpf_map_lookup_elem(&orch_ctrl_tel, &key);
    if (!telemetry)
        return;
    bpf_spin_lock(&telemetry->lock);
    telemetry->update_count++;
    telemetry->state_transition_count += previous_state != state;
    telemetry->last_update_epoch = next_generation;
    telemetry->last_window_generation = coord->window_generation;
    telemetry->last_q_permille = coord->q_permille;
    telemetry->last_state = state;
    telemetry->last_primary_deficit = coord->primary_deficit;
    telemetry->last_secondary_deficit = coord->secondary_deficit;
    if (actuator_changed) {
        telemetry->actuator_update_count[actuator_index]++;
        telemetry->last_actuator = actuator_index;
        telemetry->last_update_direction = actuator_direction;
    }
    if (actuator_saturated)
        telemetry->saturation_count++;
    if (coord->deficit_class < ORCH_DEFICIT_COUNT)
        telemetry->deficit_count[coord->deficit_class]++;
    bpf_spin_unlock(&telemetry->lock);
}

static ORCHESTRA_COORD_NOINLINE void orchestra_coord_finalize_global_live(
    uint64_t now)
{
    struct orchestra_coordination_state_v10 *state;
    uint64_t generation = now / ORCHESTRA_COORD_DEFAULT_WINDOW_NS;
    uint64_t previous_generation;
    uint64_t total;
    uint64_t maximum;
    uint32_t key;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t q;
    uint32_t primary = ORCH_DEFICIT_NONE;
    uint32_t secondary = ORCH_DEFICIT_NONE;
    uint32_t deficit_count = 0;

    if (generation == 0)
        return;
    previous_generation = generation - 1u;
    key = orchestra_coord_key(ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT,
                              previous_generation);
    state = bpf_map_lookup_elem(&orch_coord_v10, &key);
    if (!state || state->window_generation != previous_generation)
        return;
    bpf_spin_lock(&state->lock);
    if (state->flags & ORCHESTRA_COORD_F_FINALIZED) {
        bpf_spin_unlock(&state->lock);
        return;
    }
    s1 = orchestra_coord_ratio(state->signal_freshness_sum,
                               state->prediction_observations);
    s2 = orchestra_coord_ratio(state->policy_compliance_count,
                               state->executed_observations);
    total = state->actual_action_count[0] + state->actual_action_count[1] +
        state->actual_action_count[2] + state->actual_action_count[3] +
        state->actual_action_count[4];
    maximum = state->actual_action_count[0];
    if (state->actual_action_count[1] > maximum)
        maximum = state->actual_action_count[1];
    if (state->actual_action_count[2] > maximum)
        maximum = state->actual_action_count[2];
    if (state->actual_action_count[3] > maximum)
        maximum = state->actual_action_count[3];
    if (state->actual_action_count[4] > maximum)
        maximum = state->actual_action_count[4];
    s3 = orchestra_coord_ratio(maximum, total);
    s4 = orchestra_coord_ratio(state->action_transition_count,
                               state->eligible_observations);
    s4 = s4 >= ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - s4;
    q = orchestra_coord_geomean4_runtime(s1, s2, s3, s4);
    if (s1 < 700u) {
        primary = ORCH_DEFICIT_SIGNAL;
        deficit_count++;
    }
    if (s2 < 700u) {
        if (deficit_count == 0)
            primary = ORCH_DEFICIT_COMPLIANCE;
        else if (deficit_count == 1)
            secondary = ORCH_DEFICIT_COMPLIANCE;
        deficit_count++;
    }
    if (s3 < 700u) {
        if (deficit_count == 0)
            primary = ORCH_DEFICIT_COHERENCE;
        else if (deficit_count == 1)
            secondary = ORCH_DEFICIT_COHERENCE;
        deficit_count++;
    }
    if (s4 < 700u) {
        if (deficit_count == 0)
            primary = ORCH_DEFICIT_STABILITY;
        else if (deficit_count == 1)
            secondary = ORCH_DEFICIT_STABILITY;
        deficit_count++;
    }
    state->s1_permille = s1;
    state->s2_permille = s2;
    state->s3_permille = s3;
    state->s4_permille = s4;
    state->q_permille = q;
    state->primary_deficit = primary;
    state->secondary_deficit = secondary;
    state->deficit_class = deficit_count == 0 ? ORCH_DEFICIT_NONE :
        (deficit_count == 1 ? primary : ORCH_DEFICIT_MIXED);
    state->deficit_severity = q >= ORCHESTRA_V10_FIXED_POINT_SCALE ? 0u :
        ORCHESTRA_V10_FIXED_POINT_SCALE - q;
    state->deficit_persistence = deficit_count == 0 ? 0u : 1u;
    state->flags |= ORCHESTRA_COORD_F_FINALIZED;
    state->finalized_ns = now;
    bpf_spin_unlock(&state->lock);
    orchestra_controller_record_live_v10(state, now);
}
#endif

static __always_inline int orchestra_coord_read_global_summary(
    uint64_t now, struct orchestra_coordination_metrics_v10 *metrics)
{
    uint64_t duration = orchestra_coord_window_duration_ns();
    uint64_t generation = now / duration;
    uint32_t current_key = orchestra_coord_key(
        ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT, generation);
    uint32_t previous_key = 0;
    struct orchestra_coordination_state_v10 *state;

    __builtin_memset(metrics, 0, sizeof(*metrics));
    state = bpf_map_lookup_elem(&orch_coord_v10, &current_key);
    if (!state || !(state->flags & ORCHESTRA_COORD_F_FINALIZED)) {
        if (generation == 0)
            return 0;
        previous_key = orchestra_coord_key(
            ORCHESTRA_COORD_GLOBAL_DOMAIN_SLOT, generation - 1u);
        state = bpf_map_lookup_elem(&orch_coord_v10, &previous_key);
        if (!state || !(state->flags & ORCHESTRA_COORD_F_FINALIZED))
            return 0;
    }
    bpf_spin_lock(&state->lock);
    if (!(state->flags & ORCHESTRA_COORD_F_FINALIZED)) {
        bpf_spin_unlock(&state->lock);
        return 0;
    }
    metrics->scope = state->scope;
    metrics->domain_id = state->domain_id;
    metrics->window_generation = state->window_generation;
    metrics->window_start_ns = state->window_start_ns;
    metrics->window_end_ns = state->window_end_ns;
    metrics->eligible_observations = state->eligible_observations;
    metrics->executed_observations = state->executed_observations;
    metrics->s1_permille = state->s1_permille;
    metrics->s2_permille = state->s2_permille;
    metrics->s3_permille = state->s3_permille;
    metrics->s4_permille = state->s4_permille;
    metrics->q_permille = state->q_permille;
    metrics->deficit_class = state->deficit_class;
    metrics->primary_deficit = state->primary_deficit;
    metrics->secondary_deficit = state->secondary_deficit;
    metrics->deficit_severity = state->deficit_severity;
    metrics->deficit_persistence = state->deficit_persistence;
    metrics->action_transition_count = state->action_transition_count;
    metrics->synchronized_mass_switch_count =
        state->synchronized_mass_switch_count;
    metrics->fallback_count = state->fallback_count;
    metrics->controller_override_count = state->controller_override_count;
    metrics->capability_adjustment_count =
        state->capability_adjustment_count;
    bpf_spin_unlock(&state->lock);
    return 1;
}

#endif /* __BPF__ */

#endif /* ORCHESTRA_COORD_H */
