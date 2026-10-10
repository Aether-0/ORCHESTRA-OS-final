/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ORCHESTRA_TASK_ACCOUNTING_H
#define ORCHESTRA_TASK_ACCOUNTING_H

#include "orchestra_bridge_v1.h"

#ifndef __always_inline
#define __always_inline inline
#endif

/* Caller owns the lock. Preserve publication identity so the running
 * callback can attribute the fallback to the decision it superseded. */
static __always_inline void orchestra_clear_task_action(
    struct bridge_task_state *state)
{
    state->action = ORCHESTRA_ACTION_RUN;
    state->requested_cpu = ORCHESTRA_CPU_ANY;
    state->dispatched_cpu = ORCHESTRA_CPU_ANY;
    state->eligible_ns = 0;
    state->flags &= BRIDGE_TASK_F_RUNNING;
}

/* Caller owns the task-state lock. Renewal updates publication identity,
 * never refunds CPU service while THROTTLE remains the active action. */
static __always_inline void orchestra_sync_task_accounting(
    struct bridge_task_state *state, uint64_t generation, uint32_t action,
    uint32_t requested_cpu, uint64_t now)
{
    if (state->generation != generation || state->action != action) {
        state->generation = generation;
        if (state->action != ORCHESTRA_ACTION_THROTTLE ||
            action != ORCHESTRA_ACTION_THROTTLE) {
            state->period_start_ns = now;
            state->runtime_used_ns = 0;
        }
        state->running_since_ns = 0;
        state->eligible_ns = 0;
        state->action = action;
        state->dispatched_cpu = ORCHESTRA_CPU_ANY;
        state->flags = 0;
    }
    state->requested_cpu = requested_cpu;
    state->last_enqueue_ns = now;
}

#endif
