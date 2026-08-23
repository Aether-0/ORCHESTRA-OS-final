/*
 * Deterministic, dependency-free mutation target for bridge ABI validators.
 *
 * This is deliberately a bounded property test rather than a claim of
 * exhaustive fuzzing.  It feeds arbitrary fixed-width mutations through the
 * same parser/schema/controller helpers used by the bridge unit tests and is
 * run under ASan/UBSan by tests/security/run.sh.
 */
#define ORCHESTRA_BRIDGE_UNIT_TEST 1
#include "../../kernel/sched_ext/bridge/orchestra_bridge.c"
#include "../../kernel/sched_ext/include/orchestra_controller.h"

#include <stdio.h>
#include <stdlib.h>

static uint64_t fuzz_state = UINT64_C(0x6a09e667f3bcc909);

static uint64_t fuzz_next(void)
{
    fuzz_state ^= fuzz_state << 7;
    fuzz_state ^= fuzz_state >> 9;
    fuzz_state ^= fuzz_state << 8;
    return fuzz_state;
}

static void fuzz_mutate(void *memory, size_t size)
{
    unsigned char *bytes = memory;

    for (size_t count = 0; count < 32 && count < size; count++) {
        size_t index = (size_t)(fuzz_next() % size);

        bytes[index] ^= (unsigned char)(fuzz_next() & UINT64_C(0xff));
    }
}

static void fuzz_valid_control(struct bridge_control *control)
{
    memset(control, 0, sizeof(*control));
    control->magic = ORCHESTRA_ABI_MAGIC;
    control->abi_version = ORCHESTRA_ABI_VERSION;
    control->value_size = sizeof(*control);
    control->capability_flags = BRIDGE_REQUIRED_CAPS;
    control->scheduler_epoch = 1;
    control->publication_status = BRIDGE_PUB_OK;
    control->scx_api_version = ORCHESTRA_SCX_API_VERSION;
}

static void fuzz_valid_signal(struct bridge_signal_frame *signal)
{
    memset(signal, 0, sizeof(*signal));
    signal->magic = ORCHESTRA_ABI_MAGIC;
    signal->abi_version = ORCHESTRA_ABI_VERSION;
    signal->value_size = sizeof(*signal);
    signal->flags = BRIDGE_SIGNAL_F_METRICS_VALID |
        BRIDGE_SIGNAL_F_CONTROLLER_VALID;
    signal->scheduler_epoch = 1;
    signal->sequence = 1;
    signal->published_ns = 100;
    signal->expires_ns = 200;
    signal->state_schema_version = 1;
    signal->controller_state = ORCHESTRA_CTRL_NORMAL;
    signal->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    signal->confidence_permille = BRIDGE_SIGNAL_SCALE;
    signal->cpu_now_permille = BRIDGE_SIGNAL_SCALE;
    signal->cpu_pred_permille = BRIDGE_SIGNAL_SCALE;
    signal->decision_cpu_permille = BRIDGE_SIGNAL_SCALE;
    signal->memory_pressure_permille = BRIDGE_SIGNAL_SCALE;
    signal->thermal_permille = BRIDGE_SIGNAL_SCALE;
    signal->s1_permille = BRIDGE_SIGNAL_SCALE;
    signal->s2_permille = BRIDGE_SIGNAL_SCALE;
    signal->s3_permille = BRIDGE_SIGNAL_SCALE;
    signal->s4_permille = BRIDGE_SIGNAL_SCALE;
    signal->q_permille = BRIDGE_SIGNAL_SCALE;
}

static void fuzz_valid_controller(struct orchestra_controller_state_v10 *controller)
{
    memset(controller, 0, sizeof(*controller));
    controller->magic = ORCHESTRA_ABI_MAGIC;
    controller->abi_version = ORCHESTRA_CONTROL_ABI_VERSION;
    controller->value_size = sizeof(*controller);
    controller->schema_version = ORCHESTRA_CONTROLLER_SCHEMA_VERSION;
    controller->active_state = ORCHESTRA_CTRL_NORMAL;
    controller->scheduler_epoch = 1;
    controller->active_generation = 1;
    controller->staging_generation = 1;
    controller->previous_good_generation = 1;
    controller->controller_epoch = 1;
    controller->update_period_ns = 1;
    controller->minimum_hold_ns = 1;
    controller->best_q_permille = ORCHESTRA_V10_FIXED_POINT_SCALE;
    orchestra_controller_reset_bank(&controller->active);
    orchestra_controller_reset_bank(&controller->staging);
    orchestra_controller_reset_bank(&controller->previous_good);
}

int main(int argc, char **argv)
{
    unsigned long iterations = 50000;

    if (argc > 2) {
        fputs("usage: fuzz_abi_state [iterations]\n", stderr);
        return 2;
    }
    if (argc == 2) {
        char *end = NULL;

        errno = 0;
        iterations = strtoul(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            iterations == 0 || iterations > 200000)
            return 2;
    }

    (void)valid_control(NULL);
    (void)valid_policy_meta_v8(NULL, 1);
    (void)directive_equal(NULL, NULL);
    (void)signal_equal(NULL, NULL);
    (void)stream_signal_fields_valid(NULL);
    (void)parse_proc_stat_start(NULL, NULL);
    (void)parse_controller(NULL, NULL);
    (void)parse_options(0, NULL, NULL);

    for (unsigned long iteration = 0; iteration < iterations; iteration++) {
        struct bridge_control control;
        struct bridge_signal_frame signal;
        struct bridge_signal_frame signal_copy;
        struct bridge_stream_request request = {
            .stream_flags = BRIDGE_STREAM_F_PUBLISH_SIGNAL |
                BRIDGE_STREAM_F_REQUIRE_SIGNAL,
            .signal_sequence = 1,
            .signal_max_age_ns = BRIDGE_SIGNAL_MAX_AGE_NS,
            .signal_directive = ORCHESTRA_ACTION_RUN,
            .signal_state_schema_version = 1,
            .signal_prediction_used = 0
        };
        struct bridge_directive directive = {
            .abi_version = ORCHESTRA_ABI_VERSION,
            .value_size = sizeof(struct bridge_directive),
            .scheduler_epoch = 1,
            .generation = 1,
            .identity = { .tgid = 1, .tid = 1, .start_boottime_ns = 1 },
            .action = ORCHESTRA_ACTION_RUN,
            .target_cpu = ORCHESTRA_CPU_ANY,
            .expiry_ns = UINT64_C(1000000)
        };
        struct bridge_directive directive_copy;
        struct orchestra_policy_meta_v8 meta = {
            .magic = ORCHESTRA_ABI_MAGIC,
            .abi_version = ORCHESTRA_KERNEL_ABI_VERSION,
            .value_size = sizeof(struct orchestra_policy_meta_v8),
            .policy_schema_version = ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION,
            .active_bank = 0,
            .entry_count = ORCHESTRA_KERNEL_MAX_POLICY_STATES,
            .scheduler_epoch = 1,
            .policy_generation = 1,
            .policy_mode = ORCHESTRA_POLICY_EVALUATE,
            .controller_state = ORCHESTRA_CTRL_NORMAL,
            .capability_flags = ORCHESTRA_KERNEL_REQUIRED_CAPS
        };
        struct orchestra_controller_state_v10 controller;
        char action_text[16] = "RUN";
        char target_text[32] = "1";
        char *options_argv[] = {
            (char *)"bridge", (char *)"--publish", (char *)"--action",
            action_text, (char *)"--target-pid", target_text
        };
        struct options options;
        uint32_t wire = 0;

        fuzz_valid_control(&control);
        fuzz_valid_signal(&signal);
        signal_copy = signal;
        fuzz_valid_controller(&controller);
        fuzz_mutate(&control, sizeof(control));
        fuzz_mutate(&signal, sizeof(signal));
        fuzz_mutate(&signal_copy, sizeof(signal_copy));
        fuzz_mutate(&request, sizeof(request));
        fuzz_mutate(&directive, sizeof(directive));
        directive_copy = directive;
        fuzz_mutate(&directive_copy, sizeof(directive_copy));
        fuzz_mutate(&meta, sizeof(meta));
        fuzz_mutate(&controller, sizeof(controller));
        fuzz_mutate(action_text, sizeof(action_text) - 1);
        fuzz_mutate(target_text, sizeof(target_text) - 1);
        action_text[sizeof(action_text) - 1] = '\0';
        target_text[sizeof(target_text) - 1] = '\0';

        (void)valid_control(&control);
        (void)valid_policy_meta_v8(&meta, 1);
        (void)signal_equal(&signal, &signal_copy);
        (void)stream_signal_fields_valid(&request);
        (void)directive_equal(&directive, &directive_copy);
        (void)orchestra_controller_state_valid_v10(&controller);
        (void)canonical_to_wire((enum orchestra_action_id)
                                (fuzz_next() % (ORCHESTRA_ACTION_COUNT + 2u)),
                                &wire);
        (void)parse_options((int)(sizeof(options_argv) /
                                  sizeof(options_argv[0])), options_argv,
                            &options);
    }
    printf("PASS ABI/state mutation target iterations=%lu\n", iterations);
    return 0;
}
