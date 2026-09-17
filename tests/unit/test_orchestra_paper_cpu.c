#define ORCHESTRA_UNIT_TEST 1
#define main orchestra_demo_main
#include "../../orchestra_paper_cpu_demo/orchestra_paper_cpu.c"
#undef main

#include <float.h>

typedef bool (*test_function_t)(void);

typedef struct {
    const char *name;
    test_function_t function;
} named_test_t;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "    check failed at %s:%d: %s\n",               \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                 \
    do {                                                                        \
        double check_actual_ = (actual);                                        \
        double check_expected_ = (expected);                                    \
        double check_tolerance_ = (tolerance);                                  \
        if (!isfinite(check_actual_) ||                                         \
            fabs(check_actual_ - check_expected_) > check_tolerance_) {         \
            fprintf(stderr,                                                     \
                    "    near check failed at %s:%d: %.17g != %.17g "          \
                    "(tolerance %.3g)\n",                                     \
                    __FILE__, __LINE__, check_actual_, check_expected_,         \
                    check_tolerance_);                                          \
            return false;                                                       \
        }                                                                       \
    } while (0)

static int test_hex_nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool test_bytes_match_hex(const uint8_t *actual, size_t actual_size,
                                 const char *expected_hex) {
    CHECK(strlen(expected_hex) == actual_size * 2u);
    for (size_t index = 0; index < actual_size; ++index) {
        int high = test_hex_nibble(expected_hex[index * 2u]);
        int low = test_hex_nibble(expected_hex[index * 2u + 1u]);
        CHECK(high >= 0 && low >= 0);
        uint8_t expected = (uint8_t)((high << 4) | low);
        CHECK(actual[index] == expected);
    }
    return true;
}

static signal_reader_gates_t test_gates;

static void test_init_bus(signal_bus_t *bus) {
    memset(bus, 0, sizeof(*bus));
    memset(&test_gates, 0, sizeof(test_gates));
    initialize_selected_signal_publication(&bus->publication);
    initialize_signal_reader_gates(&test_gates);
    initialize_signal_publisher_diagnostics(&bus->diagnostics);
    atomic_init(&bus->consensus_lock, 0);
    atomic_init(&bus->stop, 0);
    atomic_init(&bus->kernel_bridge_enabled, 0);
}

static void test_init_worker(worker_state_t *worker) {
    memset(worker, 0, sizeof(*worker));
    atomic_init(&worker->decision_version, UINT64_C(0));
    atomic_init(&worker->action, ACT_SLEEP);
    atomic_init(&worker->previous_action, ACT_SLEEP);
    atomic_init(&worker->has_previous_action, 0);
    atomic_init(&worker->proposed_action, ACT_SLEEP);
    atomic_init(&worker->state_index, 0);
    atomic_init(&worker->next_state_index, 0);
    atomic_init(&worker->cpu_target, 0);
    atomic_init(&worker->exempt_rt, 0);
    atomic_init(&worker->alive, 0);
    atomic_init(&worker->heartbeat, UINT64_C(0));
    atomic_init(&worker->accepted_sequence, UINT64_C(0));
    atomic_init(&worker->rejected_frames, UINT64_C(0));
    atomic_init(&worker->q_update_active, 0);
    atomic_init(&worker->fallback_active, 0);
    atomic_init(&worker->fallback_reason, FALLBACK_REASON_NONE);
    atomic_init(&worker->action_attempted, 0);
    atomic_init(&worker->action_attempt_result, ACTION_ATTEMPT_NOT_ATTEMPTED);
    atomic_init(&worker->action_attempt_success, 0);
    atomic_init(&worker->action_errno, 0);
    atomic_init(&worker->cpu_before_action, -1);
    atomic_init(&worker->requested_cpu, -1);
    atomic_init(&worker->requested_cpu_valid, 0);
    atomic_init(&worker->cpu_after_action, -1);
    atomic_init(&worker->migration_observed, 0);
    atomic_init(&worker->requested_sleep_ns, UINT64_C(0));
    atomic_init(&worker->observed_sleep_ns, UINT64_C(0));
    atomic_init(&worker->yield_attempted, 0);
    atomic_init(&worker->throttle_attempted, 0);
    atomic_init(&worker->effective_action_result, EFFECTIVE_RESULT_NOT_ATTEMPTED);
    atomic_init(&worker->action_sequence, UINT64_C(0));
    atomic_init(&worker->kernel_publish_sequence, UINT64_C(0));
    atomic_init(&worker->kernel_publish_status, EXIT_FAILURE);
    atomic_init(&worker->reward, 0.0);
    atomic_init(&worker->reward_sequence, UINT64_C(0));
    initialize_signal_reader_diagnostics(&worker->publication_diagnostics);
}

static void test_init_workers(worker_block_t *workers, int count) {
    memset(workers, 0, sizeof(*workers));
    workers->worker_count = count;
    for (int index = 0; index < count; ++index) {
        test_init_worker(&workers->worker[index]);
    }
}

static void test_set_worker_snapshot(worker_state_t *worker, action_t action,
                                     action_t previous, uint64_t sequence,
                                     bool alive, bool exempt, bool fallback) {
    atomic_store(&worker->action, action);
    atomic_store(&worker->previous_action, previous);
    atomic_store(&worker->has_previous_action, 1);
    atomic_store(&worker->accepted_sequence, sequence);
    atomic_store(&worker->alive, alive ? 1 : 0);
    atomic_store(&worker->exempt_rt, exempt ? 1 : 0);
    atomic_store(&worker->fallback_active, fallback ? 1 : 0);
}

static void test_set_action_observation(worker_state_t *worker,
                                        const action_observation_t *observation,
                                        uint64_t sequence) {
    atomic_store(&worker->action_attempted, observation->action_attempted ? 1 : 0);
    atomic_store(&worker->action_attempt_result, observation->action_attempt_result);
    atomic_store(&worker->action_attempt_success,
                 observation->action_attempt_result == ACTION_ATTEMPT_SUCCEEDED ? 1 : 0);
    atomic_store(&worker->action_errno, observation->action_errno);
    atomic_store(&worker->cpu_before_action, observation->cpu_before_action);
    atomic_store(&worker->requested_cpu, observation->requested_cpu);
    atomic_store(&worker->requested_cpu_valid, observation->requested_cpu_valid ? 1 : 0);
    atomic_store(&worker->cpu_after_action, observation->cpu_after_action);
    atomic_store(&worker->migration_observed, observation->migration_observed ? 1 : 0);
    atomic_store(&worker->requested_sleep_ns, observation->requested_sleep_ns);
    atomic_store(&worker->observed_sleep_ns, observation->observed_sleep_ns);
    atomic_store(&worker->yield_attempted, observation->yield_attempted ? 1 : 0);
    atomic_store(&worker->throttle_attempted, observation->throttle_attempted ? 1 : 0);
    atomic_store(&worker->fallback_reason, observation->fallback_reason);
    atomic_store(&worker->effective_action_result, observation->effective_action_result);
    atomic_store(&worker->action_sequence, sequence);
}

static action_observation_t test_observation(action_t action) {
    action_observation_t observation = {
        .selected_action = action,
        .action_attempted = true,
        .action_attempt_result = ACTION_ATTEMPT_SUCCEEDED,
        .action_errno = 0,
        .cpu_before_action = 0,
        .requested_cpu = -1,
        .requested_cpu_valid = false,
        .cpu_after_action = 0,
        .migration_observed = false,
        .requested_sleep_ns = 0,
        .observed_sleep_ns = 0,
        .yield_attempted = false,
        .throttle_attempted = false,
        .fallback_reason = FALLBACK_REASON_NONE,
        .effective_action_result = EFFECTIVE_RESULT_SUCCESS
    };
    return observation;
}

static signal_payload_t test_valid_payload(uint64_t sequence) {
    signal_payload_t payload = {
        .magic = SIGNAL_MAGIC,
        .schema_version = SIGNAL_SCHEMA_VERSION,
        .tier = SIGNAL_TIER_CORE_LOCAL,
        .source_id = SIGNAL_SOURCE_LOCAL,
        .sequence = sequence,
        .monotonic_ns = monotonic_ns(),
        .max_age_ns = UINT64_C(1000000000),
        .key_epoch = (uint32_t)(sequence / KEY_EPOCH_TICKS),
        .directive = ACT_RUN,
        .state_schema_version = STATE_SCHEMA_VERSION,
        .prediction_used = 1u,
        .cpu_now = 0.40,
        .cpu_pred = 0.45,
        .decision_cpu = 0.45,
        .memory_pressure = 0.30,
        .thermal_proxy = 0.40,
        .confidence = 0.90,
        .jitter_sigma = 0.02,
        .switch_penalty = 0.05,
        .consensus_blend = 0.05
    };
    return payload;
}

static void test_fill_key(uint8_t key[MASTER_KEY_SIZE]) {
    for (size_t index = 0; index < MASTER_KEY_SIZE; ++index) {
        key[index] = (uint8_t)(index * 7u + 3u);
    }
}

static bool test_sha256_hmac_vectors(void) {
    static const uint8_t empty[1] = {0};
    static const uint8_t abc[] = {'a', 'b', 'c'};
    uint8_t digest[HMAC_SIZE];
    sha256_ctx_t context;

    sha256_init(&context);
    sha256_update(&context, empty, 0u);
    sha256_final(&context, digest);
    CHECK(test_bytes_match_hex(
        digest, sizeof(digest),
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855"));

    sha256_init(&context);
    sha256_update(&context, abc, sizeof(abc));
    sha256_final(&context, digest);
    CHECK(test_bytes_match_hex(
        digest, sizeof(digest),
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"));

    uint8_t hmac_key[20];
    memset(hmac_key, 0x0b, sizeof(hmac_key));
    static const uint8_t hmac_data[] = "Hi There";
    hmac_sha256(hmac_key, sizeof(hmac_key), hmac_data,
                sizeof(hmac_data) - 1u, digest);
    CHECK(test_bytes_match_hex(
        digest, sizeof(digest),
        "b0344c61d8db38535ca8afceaf0bf12b"
        "881dc200c9833da726e9376c2e32cff7"));
    CHECK(constant_time_equal(digest, digest, sizeof(digest)));
    uint8_t changed[HMAC_SIZE];
    memcpy(changed, digest, sizeof(changed));
    changed[HMAC_SIZE - 1u] ^= 1u;
    CHECK(!constant_time_equal(digest, changed, sizeof(digest)));
    return true;
}

static bool test_canonical_serialization(void) {
    CHECK(SIGNAL_WIRE_SIZE == 128);
    signal_payload_t payload = {
        .magic = UINT32_C(0x01020304),
        .schema_version = UINT32_C(0x11121314),
        .tier = UINT32_C(0x21222324),
        .source_id = UINT32_C(0x31323334),
        .sequence = UINT64_C(0x0102030405060708),
        .monotonic_ns = UINT64_C(0x1112131415161718),
        .max_age_ns = UINT64_C(0x2122232425262728),
        .key_epoch = UINT32_C(0x41424344),
        .directive = UINT32_C(0x51525354),
        .state_schema_version = UINT32_C(0x61626364),
        .prediction_used = UINT32_C(0x71727374),
        .cpu_now = 1.0,
        .cpu_pred = 2.0,
        .decision_cpu = 4.0,
        .memory_pressure = 8.0,
        .thermal_proxy = 16.0,
        .confidence = 32.0,
        .jitter_sigma = 64.0,
        .switch_penalty = 128.0,
        .consensus_blend = 256.0
    };
    uint8_t wire[SIGNAL_WIRE_SIZE];
    uint8_t second_wire[SIGNAL_WIRE_SIZE];
    serialize_payload(&payload, wire);
    serialize_payload(&payload, second_wire);
    CHECK(memcmp(wire, second_wire, sizeof(wire)) == 0);
    CHECK(test_bytes_match_hex(
        wire, sizeof(wire),
        "01020304111213142122232431323334"
        "01020304050607081112131415161718"
        "21222324252627284142434451525354"
        "61626364717273743ff0000000000000"
        "40000000000000004010000000000000"
        "40200000000000004030000000000000"
        "40400000000000004050000000000000"
        "40600000000000004070000000000000"));
    return true;
}

static bool test_frame_verification_matrix(void) {
    signal_bus_t bus;
    signal_payload_t output;
    uint8_t key[MASTER_KEY_SIZE];
    test_fill_key(key);

    test_init_bus(&bus);
    signal_payload_t valid = test_valid_payload(UINT64_C(201));
    CHECK(publish_frame(&bus, &test_gates, &valid, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(200), &output) == FRAME_VALID);
    CHECK(output.sequence == valid.sequence);
    CHECK(read_verified_frame(&bus, &test_gates, key, valid.sequence, &output) == FRAME_NO_NEW);
    CHECK(read_verified_frame(&bus, &test_gates, key, valid.sequence + 1u, &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t tampered = test_valid_payload(UINT64_C(202));
    CHECK(publish_frame(&bus, &test_gates, &tampered, key, true) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t stale = test_valid_payload(UINT64_C(203));
    stale.max_age_ns = UINT64_C(1000);
    uint64_t now = monotonic_ns();
    CHECK(now > UINT64_C(1000000));
    stale.monotonic_ns = now - UINT64_C(1000000);
    CHECK(publish_frame(&bus, &test_gates, &stale, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t wrong_schema = test_valid_payload(UINT64_C(204));
    wrong_schema.schema_version = SIGNAL_SCHEMA_VERSION + 1u;
    CHECK(publish_frame(&bus, &test_gates, &wrong_schema, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t wrong_identity = test_valid_payload(UINT64_C(205));
    wrong_identity.tier = SIGNAL_TIER_CORE_LOCAL + 1u;
    CHECK(publish_frame(&bus, &test_gates, &wrong_identity, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    wrong_identity = test_valid_payload(UINT64_C(205));
    wrong_identity.source_id = SIGNAL_SOURCE_LOCAL + 1u;
    CHECK(publish_frame(&bus, &test_gates, &wrong_identity, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t future = test_valid_payload(UINT64_C(205));
    future.monotonic_ns = monotonic_ns() + UINT64_C(1000000000);
    CHECK(publish_frame(&bus, &test_gates, &future, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t zero_sequence = test_valid_payload(UINT64_C(0));
    CHECK(publish_frame(&bus, &test_gates, &zero_sequence, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t wrong_epoch = test_valid_payload(UINT64_C(205));
    wrong_epoch.key_epoch += 1u;
    CHECK(publish_frame(&bus, &test_gates, &wrong_epoch, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    signal_payload_t malformed = test_valid_payload(UINT64_C(206));
    malformed.cpu_now = NAN;
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    malformed = test_valid_payload(UINT64_C(207));
    malformed.directive = ACTION_COUNT;
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    malformed = test_valid_payload(UINT64_C(208));
    malformed.jitter_sigma = nextafter(0.20, INFINITY);
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    malformed = test_valid_payload(UINT64_C(209));
    malformed.max_age_ns = MAX_FRAME_AGE_NS + 1u;
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    malformed = test_valid_payload(UINT64_C(210));
    malformed.state_schema_version = STATE_SCHEMA_VERSION + 1u;
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
    malformed = test_valid_payload(UINT64_C(211));
    malformed.prediction_used = 2u;
    CHECK(publish_frame(&bus, &test_gates, &malformed, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output) == FRAME_INVALID);

    test_init_bus(&bus);
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    atomic_store(&bus.publication.publish_version, UINT64_C(1));
#else
    atomic_store(&bus.publication.publication_token.value, UINT64_C(2));
    atomic_store(&bus.publication.slot[0].sequence.value, UINT64_C(3));
    atomic_store(&test_gates.access_state[0].value, SIGNAL_SLOT_WRITER_LOCK);
#endif
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(0), &output)
          == FRAME_UNSTABLE);
    return true;
}

static void test_init_legacy_publication(legacy_signal_publication_t *publication) {
    memset(publication, 0, sizeof(*publication));
    atomic_init(&publication->publish_version, UINT64_C(0));
    for (size_t index = 0; index < SIGNAL_WIRE_SIZE; ++index)
        atomic_init(&publication->wire[index], 0);
    for (size_t index = 0; index < HMAC_SIZE; ++index)
        atomic_init(&publication->hmac[index], 0);
}

static void test_init_generation_publication(generation_signal_publication_t *publication,
                                             signal_reader_gates_t *gates) {
    memset(publication, 0, sizeof(*publication));
    memset(gates, 0, sizeof(*gates));
    atomic_init(&publication->publication_token.value, UINT64_C(0));
    for (size_t slot = 0; slot < SIGNAL_PUBLICATION_SLOT_COUNT; ++slot) {
        atomic_init(&publication->slot[slot].sequence.value, UINT64_C(0));
        atomic_init(&gates->access_state[slot].value, UINT64_C(0));
        for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word)
            atomic_init(&publication->slot[slot].words[word], UINT64_C(0));
    }
}

static bool test_publication_canonical_equivalence(void) {
    legacy_signal_publication_t legacy;
    generation_signal_publication_t optimized;
    signal_reader_gates_t gates;
    signal_reader_diagnostics_t legacy_diagnostics;
    signal_reader_diagnostics_t optimized_diagnostics;
    uint8_t key[MASTER_KEY_SIZE];
    uint8_t expected[SIGNAL_FRAME_SIZE];
    uint8_t legacy_bytes[SIGNAL_FRAME_SIZE];
    uint8_t optimized_bytes[SIGNAL_FRAME_SIZE];
    signal_payload_t legacy_payload;
    signal_payload_t optimized_payload;
    test_fill_key(key);
    test_init_legacy_publication(&legacy);
    test_init_generation_publication(&optimized, &gates);
    initialize_signal_reader_diagnostics(&legacy_diagnostics);
    initialize_signal_reader_diagnostics(&optimized_diagnostics);

    signal_payload_t payload = test_valid_payload(UINT64_C(250));
    /* Both signed zero encodings are legitimate binary64 inputs under the
     * current bounds.  Exact bytes—not numeric equality—are the contract. */
    payload.cpu_now = -0.0;
    payload.cpu_pred = 1.0;
    payload.decision_cpu = -0.0;
    payload.memory_pressure = 1.0;
    payload.thermal_proxy = 0.0;
    payload.confidence = 1.0;
    payload.jitter_sigma = -0.0;
    payload.switch_penalty = 0.30;
    payload.consensus_blend = 0.15;
    prepare_canonical_signal_frame(&payload, key, false, expected);

    CHECK(legacy_publish_canonical_frame(&legacy, expected) == SIGNAL_PUBLISH_OK);
    CHECK(generation_publish_canonical_frame(&optimized, &gates, expected)
          == SIGNAL_PUBLISH_OK);
    CHECK(legacy_copy_canonical_frame(&legacy, legacy_bytes, &legacy_diagnostics)
          == SIGNAL_SNAPSHOT_COPIED);
    CHECK(generation_copy_canonical_frame(&optimized, &gates, optimized_bytes,
                                          &optimized_diagnostics)
          == SIGNAL_SNAPSHOT_COPIED);
    CHECK(memcmp(expected, legacy_bytes, sizeof(expected)) == 0);
    CHECK(memcmp(expected, optimized_bytes, sizeof(expected)) == 0);
    CHECK(memcmp(legacy_bytes, optimized_bytes, sizeof(expected)) == 0);
    CHECK(memcmp(expected + SIGNAL_WIRE_SIZE, legacy_bytes + SIGNAL_WIRE_SIZE,
                 HMAC_SIZE) == 0);
    deserialize_payload(legacy_bytes, &legacy_payload);
    deserialize_payload(optimized_bytes, &optimized_payload);
    CHECK(legacy_payload.sequence == optimized_payload.sequence);
    CHECK(legacy_payload.monotonic_ns == optimized_payload.monotonic_ns);
    CHECK(legacy_payload.directive == optimized_payload.directive);
    CHECK(legacy_payload.state_schema_version == optimized_payload.state_schema_version);
    CHECK(legacy_payload.tier == optimized_payload.tier);
    CHECK(legacy_payload.source_id == optimized_payload.source_id);
    CHECK(legacy_payload.key_epoch == optimized_payload.key_epoch);
    uint64_t legacy_zero_bits = 0;
    uint64_t optimized_zero_bits = 0;
    memcpy(&legacy_zero_bits, &legacy_payload.cpu_now, sizeof(legacy_zero_bits));
    memcpy(&optimized_zero_bits, &optimized_payload.cpu_now, sizeof(optimized_zero_bits));
    CHECK(legacy_zero_bits == UINT64_C(0x8000000000000000));
    CHECK(legacy_zero_bits == optimized_zero_bits);
    CHECK(payload_fields_valid(&legacy_payload));
    CHECK(payload_fields_valid(&optimized_payload));
    return true;
}

static bool test_corrupt_selected_frame_byte(signal_bus_t *bus, size_t index,
                                             uint8_t mask) {
    if (index >= SIGNAL_FRAME_SIZE) return false;
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    if (index < SIGNAL_WIRE_SIZE) {
        (void)atomic_fetch_xor_explicit(&bus->publication.wire[index], mask,
                                        memory_order_relaxed);
    } else {
        (void)atomic_fetch_xor_explicit(&bus->publication.hmac[index - SIGNAL_WIRE_SIZE],
                                        mask, memory_order_relaxed);
    }
#else
    uint64_t token = atomic_load_explicit(&bus->publication.publication_token.value,
                                          memory_order_acquire);
    size_t slot = (size_t)(token & SIGNAL_TOKEN_SLOT_MASK);
    size_t word = index / SIGNAL_FRAME_WORD_SIZE;
    size_t byte = index % SIGNAL_FRAME_WORD_SIZE;
    unsigned int shift = (unsigned int)((SIGNAL_FRAME_WORD_SIZE - 1u - byte) * 8u);
    uint64_t word_mask = (uint64_t)mask << shift;
    (void)atomic_fetch_xor_explicit(&bus->publication.slot[slot].words[word], word_mask,
                                    memory_order_relaxed);
#endif
    return true;
}

#if !ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
typedef enum {
    TEST_HOOK_NONE = 0,
    TEST_HOOK_CHANGE_TOKEN,
    TEST_HOOK_CHANGE_SLOT_SEQUENCE
} test_publication_hook_mode_t;

static signal_bus_t *g_test_hook_bus = NULL;
static test_publication_hook_mode_t g_test_hook_mode = TEST_HOOK_NONE;
static bool g_test_hook_fired = false;

static void test_publication_hook(signal_publication_hook_stage_t stage) {
    if (g_test_hook_bus == NULL || g_test_hook_fired
        || stage != SIGNAL_HOOK_READER_SEQUENCE) {
        return;
    }
    uint64_t token = atomic_load_explicit(
        &g_test_hook_bus->publication.publication_token.value, memory_order_acquire);
    size_t slot = (size_t)(token & SIGNAL_TOKEN_SLOT_MASK);
    if (g_test_hook_mode == TEST_HOOK_CHANGE_TOKEN) {
        atomic_store_explicit(&g_test_hook_bus->publication.publication_token.value,
                              token ^ SIGNAL_TOKEN_SLOT_MASK, memory_order_release);
    } else if (g_test_hook_mode == TEST_HOOK_CHANGE_SLOT_SEQUENCE) {
        uint64_t sequence = atomic_load_explicit(
            &g_test_hook_bus->publication.slot[slot].sequence.value, memory_order_acquire);
        atomic_store_explicit(&g_test_hook_bus->publication.slot[slot].sequence.value,
                              sequence | UINT64_C(1), memory_order_release);
    }
    g_test_hook_fired = true;
}
#endif

static bool test_publication_fault_injection(void) {
    signal_bus_t bus;
    signal_payload_t output = {0};
    uint8_t key[MASTER_KEY_SIZE];
    test_fill_key(key);

    /* All-zero storage is no accepted frame: optimized uses its zero token;
     * the retained byte-wise reference reaches the old invalid-frame path. */
    test_init_bus(&bus);
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_INVALID);
#else
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_NO_NEW);
#endif

    signal_payload_t first = test_valid_payload(UINT64_C(401));
    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_VALID);

    /* A corrupt tag and a corrupt payload word are both rejected by the
     * unchanged HMAC check; neither is ever accepted as a frame. */
    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(test_corrupt_selected_frame_byte(&bus, SIGNAL_WIRE_SIZE + 3u,
                                           UINT8_C(0x80)));
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_INVALID);
    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(test_corrupt_selected_frame_byte(&bus, 7u, UINT8_C(0x01)));
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_INVALID);

    /* Reuse alternates the two slots; the current full frame remains exact. */
    test_init_bus(&bus);
    signal_payload_t second = test_valid_payload(UINT64_C(402));
    signal_payload_t third = test_valid_payload(UINT64_C(403));
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(publish_frame(&bus, &test_gates, &second, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(publish_frame(&bus, &test_gates, &third, key, false) == SIGNAL_PUBLISH_OK);
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(402), &output)
          == FRAME_VALID);
    CHECK(output.sequence == third.sequence);

#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    atomic_store_explicit(&bus.publication.publish_version, UINT64_C(1),
                          memory_order_release);
#else
    uint64_t active = atomic_load_explicit(&bus.publication.publication_token.value,
                                           memory_order_acquire);
    size_t active_slot = (size_t)(active & SIGNAL_TOKEN_SLOT_MASK);
    uint64_t active_generation = active >> SIGNAL_TOKEN_GENERATION_SHIFT;
    atomic_store_explicit(&bus.publication.slot[active_slot].sequence.value,
                          (active_generation << SIGNAL_TOKEN_GENERATION_SHIFT) | UINT64_C(1),
                          memory_order_release);
    atomic_store_explicit(&test_gates.access_state[active_slot].value,
                          SIGNAL_SLOT_WRITER_LOCK, memory_order_release);
#endif
    CHECK(read_verified_frame(&bus, &test_gates, key, UINT64_C(403), &output)
          == FRAME_UNSTABLE);

#if !ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    /* An even slot sequence from a different generation is just as unsafe as
     * an odd writer state.  The reader must reject the token/slot disagreement
     * before it can deserialize or authenticate the copied words. */
    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    active = atomic_load_explicit(&bus.publication.publication_token.value,
                                  memory_order_acquire);
    active_slot = (size_t)(active & SIGNAL_TOKEN_SLOT_MASK);
    active_generation = active >> SIGNAL_TOKEN_GENERATION_SHIFT;
    CHECK(active_generation < SIGNAL_MAX_GENERATION);
    atomic_store_explicit(&bus.publication.slot[active_slot].sequence.value,
                          (active_generation + UINT64_C(1))
                              << SIGNAL_TOKEN_GENERATION_SHIFT,
                          memory_order_release);
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_UNSTABLE);

    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    atomic_store_explicit(&test_gates.access_state[1].value,
                          SIGNAL_SLOT_WRITER_LOCK, memory_order_release);
    CHECK(publish_frame(&bus, &test_gates, &second, key, false)
          == SIGNAL_PUBLISH_CONTENDED);

    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    g_test_hook_bus = &bus;
    g_test_hook_mode = TEST_HOOK_CHANGE_TOKEN;
    g_test_hook_fired = false;
    g_signal_publication_test_hook = test_publication_hook;
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_UNSTABLE);
    CHECK(g_test_hook_fired);
    g_signal_publication_test_hook = NULL;

    test_init_bus(&bus);
    CHECK(publish_frame(&bus, &test_gates, &first, key, false) == SIGNAL_PUBLISH_OK);
    g_test_hook_mode = TEST_HOOK_CHANGE_SLOT_SEQUENCE;
    g_test_hook_fired = false;
    g_signal_publication_test_hook = test_publication_hook;
    CHECK(read_verified_frame(&bus, &test_gates, key, 0, &output) == FRAME_UNSTABLE);
    CHECK(g_test_hook_fired);
    g_signal_publication_test_hook = NULL;
    g_test_hook_bus = NULL;

    test_init_bus(&bus);
    uint8_t bytes[SIGNAL_FRAME_SIZE];
    prepare_canonical_signal_frame(&first, key, false, bytes);
    atomic_store_explicit(&bus.publication.publication_token.value,
                          (SIGNAL_MAX_GENERATION - UINT64_C(1))
                              << SIGNAL_TOKEN_GENERATION_SHIFT,
                          memory_order_release);
    CHECK(selected_publish_canonical_frame(&bus, &test_gates, bytes) == SIGNAL_PUBLISH_OK);
    atomic_store_explicit(&bus.publication.publication_token.value,
                          SIGNAL_MAX_GENERATION << SIGNAL_TOKEN_GENERATION_SHIFT,
                          memory_order_release);
    CHECK(selected_publish_canonical_frame(&bus, &test_gates, bytes)
          == SIGNAL_PUBLISH_GENERATION_EXHAUSTED);
#else
    uint8_t bytes[SIGNAL_FRAME_SIZE];
    prepare_canonical_signal_frame(&first, key, false, bytes);
    atomic_store_explicit(&bus.publication.publish_version, UINT64_MAX - UINT64_C(1),
                          memory_order_release);
    CHECK(selected_publish_canonical_frame(&bus, &test_gates, bytes)
          == SIGNAL_PUBLISH_GENERATION_EXHAUSTED);
#endif
    return true;
}

static bool test_directive_boundaries_and_precedence(void) {
    uint32_t wire = UINT32_MAX;
    CHECK(canonical_action_to_wire(ACT_RUN, &wire) && wire == ORCHESTRA_ACTION_RUN);
    CHECK(canonical_action_to_wire(ACT_SLEEP, &wire) && wire == ORCHESTRA_ACTION_SLEEP);
    CHECK(canonical_action_to_wire(ACT_MIGRATE, &wire) && wire == ORCHESTRA_ACTION_MIGRATE);
    CHECK(canonical_action_to_wire(ACT_THROTTLE, &wire) && wire == ORCHESTRA_ACTION_THROTTLE);
    CHECK(canonical_action_to_wire(ACT_YIELD, &wire) && wire == ORCHESTRA_ACTION_YIELD);
    CHECK(!canonical_action_to_wire((action_t)ACTION_COUNT, &wire));
    CHECK(!canonical_action_to_wire(ACT_RUN, NULL));
    const double low_memory = 0.10;
    const double low_thermal = 0.10;

    CHECK(directive_from_signal(nextafter(0.20, -INFINITY), low_memory,
                                low_thermal) == ACT_SLEEP);
    CHECK(directive_from_signal(0.20, low_memory, low_thermal) == ACT_RUN);
    CHECK(directive_from_signal(nextafter(0.20, INFINITY), low_memory,
                                low_thermal) == ACT_RUN);

    CHECK(directive_from_signal(nextafter(0.68, -INFINITY), low_memory,
                                low_thermal) == ACT_RUN);
    CHECK(directive_from_signal(0.68, low_memory, low_thermal) == ACT_RUN);
    CHECK(directive_from_signal(nextafter(0.68, INFINITY), low_memory,
                                low_thermal) == ACT_YIELD);

    CHECK(directive_from_signal(nextafter(0.82, -INFINITY), low_memory,
                                low_thermal) == ACT_YIELD);
    CHECK(directive_from_signal(0.82, low_memory, low_thermal) == ACT_YIELD);
    CHECK(directive_from_signal(nextafter(0.82, INFINITY), low_memory,
                                low_thermal) == ACT_MIGRATE);

    CHECK(directive_from_signal(nextafter(0.94, -INFINITY), low_memory,
                                low_thermal) == ACT_MIGRATE);
    CHECK(directive_from_signal(0.94, low_memory, low_thermal) == ACT_MIGRATE);
    CHECK(directive_from_signal(nextafter(0.94, INFINITY), low_memory,
                                low_thermal) == ACT_THROTTLE);

    CHECK(directive_from_signal(0.40, nextafter(0.90, -INFINITY),
                                low_thermal) == ACT_RUN);
    CHECK(directive_from_signal(0.40, 0.90, low_thermal) == ACT_RUN);
    CHECK(directive_from_signal(0.40, nextafter(0.90, INFINITY),
                                low_thermal) == ACT_MIGRATE);

    CHECK(directive_from_signal(0.40, low_memory,
                                nextafter(0.90, -INFINITY)) == ACT_RUN);
    CHECK(directive_from_signal(0.40, low_memory, 0.90) == ACT_RUN);
    CHECK(directive_from_signal(0.40, low_memory,
                                nextafter(0.90, INFINITY)) == ACT_THROTTLE);

    CHECK(directive_from_signal(0.10, 0.91, 0.10) == ACT_MIGRATE);
    CHECK(directive_from_signal(0.75, 0.91, 0.10) == ACT_MIGRATE);
    CHECK(directive_from_signal(0.85, 0.10, 0.10) == ACT_MIGRATE);
    CHECK(directive_from_signal(0.95, 0.91, 0.10) == ACT_THROTTLE);
    CHECK(directive_from_signal(0.10, 0.91, 0.91) == ACT_THROTTLE);
    return true;
}

static bool test_state_schema_v2_alignment(void) {
    CHECK(STATE_SCHEMA_VERSION == 2u);
    CHECK(STATE_COUNT == 30);
    const double cpu_samples[] = {
        0.0,
        nextafter(0.20, -INFINITY), 0.20, nextafter(0.20, INFINITY),
        nextafter(0.68, -INFINITY), 0.68, nextafter(0.68, INFINITY),
        nextafter(0.82, -INFINITY), 0.82, nextafter(0.82, INFINITY),
        nextafter(0.94, -INFINITY), 0.94, nextafter(0.94, INFINITY),
        1.0
    };
    const double memory_samples[] = {
        0.0, nextafter(0.90, -INFINITY), 0.90,
        nextafter(0.90, INFINITY), 1.0
    };
    const double thermal_samples[] = {
        0.0,
        nextafter(0.70, -INFINITY), 0.70, nextafter(0.70, INFINITY),
        nextafter(0.90, -INFINITY), 0.90, nextafter(0.90, INFINITY),
        1.0
    };
    bool seen[STATE_COUNT] = {false};
    action_t state_directive[STATE_COUNT] = {ACT_RUN};
    int seen_count = 0;

    for (size_t cpu_index = 0;
         cpu_index < sizeof(cpu_samples) / sizeof(cpu_samples[0]); ++cpu_index) {
        for (size_t memory_index = 0;
             memory_index < sizeof(memory_samples) / sizeof(memory_samples[0]);
             ++memory_index) {
            for (size_t thermal_index = 0;
                 thermal_index < sizeof(thermal_samples) / sizeof(thermal_samples[0]);
                 ++thermal_index) {
                double cpu = cpu_samples[cpu_index];
                double memory = memory_samples[memory_index];
                double thermal = thermal_samples[thermal_index];
                int index = state_index(cpu, memory, thermal);
                action_t directive = directive_from_signal(cpu, memory, thermal);
                CHECK(index >= 0 && index < STATE_COUNT);
                if (!seen[index]) {
                    seen[index] = true;
                    state_directive[index] = directive;
                    seen_count++;
                } else {
                    CHECK(state_directive[index] == directive);
                }
            }
        }
    }
    CHECK(seen_count == STATE_COUNT);
    return true;
}

static bool test_epsilon_schedule(void) {
    CHECK_NEAR(epsilon_for_sequence(UINT64_C(0)), EPSILON_START, 1e-15);
    CHECK_NEAR(epsilon_for_sequence(UINT64_C(1)),
               EPSILON_START * EPSILON_DECAY, 1e-15);
    double previous = epsilon_for_sequence(UINT64_C(0));
    for (uint64_t sequence = UINT64_C(1); sequence <= UINT64_C(5000);
         ++sequence) {
        double epsilon = epsilon_for_sequence(sequence);
        CHECK(epsilon <= previous + DBL_EPSILON);
        CHECK(epsilon >= EPSILON_MIN);
        CHECK(epsilon <= EPSILON_START);
        previous = epsilon;
    }
    CHECK_NEAR(epsilon_for_sequence(UINT64_C(1000000)), EPSILON_MIN, 0.0);
    return true;
}

static bool test_prediction_fallback_decision(void) {
    uint32_t prediction_used = UINT32_MAX;
    CHECK_NEAR(decision_cpu_value(MODE_ORCHESTRA, 0.20, 0.80,
                                  PREDICTION_CONFIDENCE_MIN,
                                  &prediction_used), 0.80, 0.0);
    CHECK(prediction_used == 1u);

    double below_threshold = nextafter(PREDICTION_CONFIDENCE_MIN, -INFINITY);
    CHECK_NEAR(decision_cpu_value(MODE_ORCHESTRA, 0.20, 0.80,
                                  below_threshold, &prediction_used), 0.20, 0.0);
    CHECK(prediction_used == 0u);
    CHECK_NEAR(decision_cpu_value(MODE_ORCHESTRA, 0.20, 0.80, NAN,
                                  &prediction_used), 0.20, 0.0);
    CHECK(prediction_used == 0u);

    CHECK_NEAR(decision_cpu_value(MODE_BASELINE, 0.25, 0.85, 1.0,
                                  &prediction_used), 0.25, 0.0);
    CHECK(prediction_used == 0u);
    CHECK_NEAR(decision_cpu_value(MODE_BASELINE, -1.0, 0.85, 1.0,
                                  &prediction_used), 0.0, 0.0);
    CHECK(prediction_used == 0u);
    return true;
}

static bool test_normalized_geometric_mean_cases(void) {
    const double equal[] = {0.5, 0.5, 0.5, 0.5};
    CHECK_NEAR(normalized_geometric_mean(equal, 4u), 0.5, 1e-15);

    const double factors[] = {0.25, 1.0, 1.0, 1.0};
    CHECK_NEAR(normalized_geometric_mean(factors, 4u), pow(0.25, 0.25),
               1e-15);

    const double clamped[] = {2.0, 1.0, 1.0, 1.0};
    CHECK_NEAR(normalized_geometric_mean(clamped, 4u), 1.0, 0.0);
    const double zero[] = {1.0, 1.0, 0.0, 1.0};
    CHECK(normalized_geometric_mean(zero, 4u) == 0.0);
    const double not_a_number[] = {1.0, 1.0, NAN, 1.0};
    CHECK(normalized_geometric_mean(not_a_number, 4u) == 0.0);
    CHECK(normalized_geometric_mean(NULL, 0u) == 1.0);
    return true;
}

static bool test_metrics_scenarios(void) {
    worker_block_t workers;
    test_init_workers(&workers, 4);
    signal_payload_t frame = test_valid_payload(UINT64_C(50));
    frame.directive = ACT_RUN;
    frame.confidence = 1.0;
    frame.monotonic_ns = monotonic_ns();

    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_RUN, ACT_RUN,
                                 frame.sequence, true, false, false);
    }
    coord_metrics_t stable_correct = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(stable_correct.eligible_workers == 4);
    CHECK(stable_correct.accepted_workers == 4);
    CHECK(stable_correct.s1 == 1.0);
    CHECK(stable_correct.s2 == 1.0);
    CHECK(stable_correct.s3 == 1.0);
    CHECK(stable_correct.s4 == 1.0);
    CHECK(stable_correct.q == 1.0);
    CHECK(coordination_sample_complete(&workers, &stable_correct));

    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_SLEEP, ACT_SLEEP,
                                 frame.sequence, true, false, false);
    }
    coord_metrics_t stable_wrong = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(stable_wrong.s1 == 1.0);
    CHECK(stable_wrong.s2 == 0.0);
    CHECK(stable_wrong.s3 == 1.0);
    CHECK(stable_wrong.s4 == 1.0);
    CHECK(stable_wrong.q == 0.0);

    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_RUN, ACT_SLEEP,
                                 frame.sequence, true, false, false);
    }
    coord_metrics_t mass_switch = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(mass_switch.s1 == 1.0);
    CHECK(mass_switch.s2 == 1.0);
    CHECK(mass_switch.s3 == 1.0);
    CHECK(mass_switch.s4 == 0.0);
    CHECK(mass_switch.q == 0.0);

    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_RUN, ACT_RUN,
                                 frame.sequence - 1u, true, false, false);
    }
    coord_metrics_t unaccepted = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(unaccepted.eligible_workers == 4);
    CHECK(unaccepted.accepted_workers == 0);
    CHECK(unaccepted.s2 == 0.0);
    CHECK(unaccepted.s1 < 1.0);
    CHECK(!coordination_sample_complete(&workers, &unaccepted));

    const coord_metrics_t scenarios[] = {
        stable_correct, stable_wrong, mass_switch, unaccepted
    };
    for (size_t index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]);
         ++index) {
        const double components[] = {
            scenarios[index].s1, scenarios[index].s2, scenarios[index].s3,
            scenarios[index].s4, scenarios[index].q
        };
        for (size_t component = 0;
             component < sizeof(components) / sizeof(components[0]); ++component) {
            CHECK(isfinite(components[component]));
            CHECK(components[component] >= 0.0 && components[component] <= 1.0);
        }
    }
    return true;
}

static void test_set_burst_population(worker_block_t *workers,
                                      const action_t previous[],
                                      const action_t current[], int count,
                                      uint64_t sequence, bool accepted,
                                      bool fallback) {
    for (int index = 0; index < count; ++index) {
        test_set_worker_snapshot(&workers->worker[index], current[index],
                                 previous[index],
                                 accepted ? sequence : sequence - UINT64_C(1),
                                 true, false, fallback);
    }
}

static bool test_s4_burst_basic_semantics(void) {
    worker_block_t workers;
    burst_tracker_t tracker = {0};
    const action_t run4[] = {ACT_RUN, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t one_sleep[] = {ACT_SLEEP, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t mixed_minority[] = {ACT_SLEEP, ACT_MIGRATE, ACT_RUN, ACT_RUN};
    const action_t sleep4[] = {ACT_SLEEP, ACT_SLEEP, ACT_SLEEP, ACT_SLEEP};
    const action_t mixed_old[] = {ACT_RUN, ACT_RUN, ACT_SLEEP, ACT_THROTTLE};
    const action_t mixed_new[] = {ACT_SLEEP, ACT_MIGRATE, ACT_RUN, ACT_YIELD};
    test_init_workers(&workers, 4);

    signal_payload_t frame = test_valid_payload(UINT64_C(201));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, run4, 4, frame.sequence, true, false);
    coord_metrics_t no_change = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK_NEAR(no_change.s4, 1.0, 1e-15);
    CHECK_NEAR(no_change.s4_burst, 1.0, 1e-15);
    CHECK(no_change.changed_eligible_workers == 0);
    CHECK(no_change.dominant_old_action == -1);
    CHECK(no_change.dominant_new_action == -1);
    CHECK(no_change.current_directive_valid);
    burst_tracker_record(&tracker, &frame, &no_change);

    frame = test_valid_payload(UINT64_C(202));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, one_sleep, 4, frame.sequence, true, false);
    coord_metrics_t isolated = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK_NEAR(isolated.s4, 0.75, 1e-15);
    CHECK_NEAR(isolated.change_fraction, 0.25, 1e-15);
    CHECK_NEAR(isolated.dominant_transition_fraction, 1.0, 1e-15);
    CHECK_NEAR(isolated.s4_burst, 1.0, 1e-15);
    CHECK(!isolated.large_burst_event);
    burst_tracker_record(&tracker, &frame, &isolated);

    frame = test_valid_payload(UINT64_C(203));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, mixed_minority, 4, frame.sequence,
                              true, false);
    coord_metrics_t staggered = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK_NEAR(staggered.s4, 0.50, 1e-15);
    CHECK(staggered.change_fraction > 0.0 && staggered.change_fraction < 1.0);
    CHECK(staggered.dominant_transition_fraction < 1.0);
    CHECK(staggered.s4_burst > 0.90);
    CHECK(!staggered.large_burst_event);
    burst_tracker_record(&tracker, &frame, &staggered);

    frame = test_valid_payload(UINT64_C(204));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, sleep4, 4, frame.sequence, true, false);
    coord_metrics_t mass = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK_NEAR(mass.s4, 0.0, 1e-15);
    CHECK_NEAR(mass.change_fraction, 1.0, 1e-15);
    CHECK_NEAR(mass.dominant_transition_fraction, 1.0, 1e-15);
    CHECK_NEAR(mass.justified_change_fraction, 0.0, 1e-15);
    CHECK_NEAR(mass.s4_burst, 0.20, 1e-15);
    CHECK(mass.large_burst_event);
    CHECK(mass.rolling_window_burst_count == 1);
    burst_tracker_record(&tracker, &frame, &mass);

    frame = test_valid_payload(UINT64_C(205));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, mixed_old, mixed_new, 4, frame.sequence,
                              true, false);
    coord_metrics_t mixed = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(mixed.changed_eligible_workers == 4);
    CHECK_NEAR(mixed.dominant_transition_fraction, 0.25, 1e-15);
    CHECK(!mixed.large_burst_event);
    CHECK(mixed.rolling_window_burst_count == 0);
    CHECK(mixed.rolling_window_oscillation_count == 0);
    CHECK(mixed.s4_burst > mass.s4_burst);
    return true;
}

static bool test_s4_burst_directive_and_oscillation(void) {
    worker_block_t workers;
    burst_tracker_t tracker = {0};
    const action_t run4[] = {ACT_RUN, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t sleep4[] = {ACT_SLEEP, ACT_SLEEP, ACT_SLEEP, ACT_SLEEP};
    const action_t gradual[] = {ACT_SLEEP, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t gradual_next[] = {ACT_SLEEP, ACT_SLEEP, ACT_RUN, ACT_RUN};
    test_init_workers(&workers, 4);

    signal_payload_t frame = test_valid_payload(UINT64_C(301));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, run4, 4, frame.sequence, true, false);
    coord_metrics_t stable = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(stable.current_directive_valid);
    burst_tracker_record(&tracker, &frame, &stable);

    frame = test_valid_payload(UINT64_C(302));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, run4, sleep4, 4, frame.sequence, true, false);
    coord_metrics_t justified = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(justified.directive_transition_valid);
    CHECK(justified.justified_changed_workers == 4);
    CHECK_NEAR(justified.justified_change_fraction, 1.0, 1e-15);
    CHECK_NEAR(justified.s4_burst, 0.84, 1e-15);
    CHECK(justified.s4_burst > 0.20 && justified.s4_burst < 1.0);
    burst_tracker_record(&tracker, &frame, &justified);

    frame = test_valid_payload(UINT64_C(303));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, sleep4, run4, 4, frame.sequence, true, false);
    coord_metrics_t reverse = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(!reverse.directive_transition_valid);
    CHECK(reverse.large_burst_event);
    CHECK(reverse.repeated_oscillation_event);
    CHECK(reverse.rolling_window_burst_count == 2);
    CHECK(reverse.rolling_window_oscillation_count == 1);
    CHECK_NEAR(reverse.oscillation_penalty, 0.15, 1e-15);
    CHECK_NEAR(reverse.s4_burst, 0.05, 1e-15);
    burst_tracker_record(&tracker, &frame, &reverse);

    frame = test_valid_payload(UINT64_C(304));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, run4, sleep4, 4, frame.sequence, true, false);
    coord_metrics_t repeated = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(repeated.repeated_oscillation_event);
    CHECK(repeated.rolling_window_burst_count == 3);
    CHECK(repeated.s4_burst < 0.20);

    tracker = (burst_tracker_t){0};
    frame = test_valid_payload(UINT64_C(310));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, run4, 4, frame.sequence, true, false);
    stable = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    burst_tracker_record(&tracker, &frame, &stable);
    frame = test_valid_payload(UINT64_C(311));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, run4, gradual, 4, frame.sequence, true, false);
    coord_metrics_t gradual_first = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(gradual_first.directive_transition_valid);
    CHECK(gradual_first.justified_changed_workers == 1);
    CHECK_NEAR(gradual_first.s4_burst, 1.0, 1e-15);
    burst_tracker_record(&tracker, &frame, &gradual_first);
    frame = test_valid_payload(UINT64_C(312));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, gradual, gradual_next, 4, frame.sequence,
                              true, false);
    coord_metrics_t gradual_second = compute_metrics(&workers, &frame, 0.0, 1000,
                                                     &tracker);
    CHECK(!gradual_second.directive_transition_valid);
    CHECK(gradual_second.changed_eligible_workers == 1);
    CHECK_NEAR(gradual_second.s4_burst, 1.0, 1e-15);
    return true;
}

static bool test_s4_burst_invalid_and_zero_semantics(void) {
    worker_block_t workers;
    burst_tracker_t tracker = {0};
    const action_t run4[] = {ACT_RUN, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t sleep4[] = {ACT_SLEEP, ACT_SLEEP, ACT_SLEEP, ACT_SLEEP};
    test_init_workers(&workers, 4);

    signal_payload_t frame = test_valid_payload(UINT64_C(401));
    frame.directive = ACT_RUN;
    test_set_burst_population(&workers, run4, run4, 4, frame.sequence, true, false);
    coord_metrics_t valid = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    burst_tracker_record(&tracker, &frame, &valid);
    size_t remembered_index = tracker.next_index;

    frame = test_valid_payload(UINT64_C(402));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, run4, sleep4, 4, frame.sequence, false, false);
    coord_metrics_t stale = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(!stale.current_directive_valid);
    CHECK(stale.previous_directive_valid);
    CHECK(!stale.directive_transition_valid);
    CHECK(stale.justified_changed_workers == 0);
    CHECK_NEAR(stale.s4_burst, 0.20, 1e-15);
    burst_tracker_record(&tracker, &frame, &stale);
    CHECK(tracker.next_index == remembered_index);

    frame = test_valid_payload(UINT64_C(403));
    frame.directive = ACT_SLEEP;
    test_set_burst_population(&workers, run4, sleep4, 4, frame.sequence, true, true);
    coord_metrics_t rejected = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(!rejected.current_directive_valid);
    CHECK(!rejected.directive_transition_valid);
    CHECK(rejected.justified_changed_workers == 0);
    burst_tracker_record(&tracker, &frame, &rejected);
    CHECK(tracker.next_index == remembered_index);

    test_init_workers(&workers, 1);
    workers.rt_exempt_count = 1;
    test_set_worker_snapshot(&workers.worker[0], ACT_RUN, ACT_SLEEP,
                             UINT64_C(404), true, true, false);
    frame = test_valid_payload(UINT64_C(404));
    coord_metrics_t zero = compute_metrics(&workers, &frame, 0.0, 1000, &tracker);
    CHECK(zero.eligible_workers == 0);
    CHECK_NEAR(zero.s4, 1.0, 1e-15);
    CHECK_NEAR(zero.s4_burst, 1.0, 1e-15);
    CHECK_NEAR(zero.change_fraction, 0.0, 1e-15);
    CHECK(zero.dominant_old_action == -1 && zero.dominant_new_action == -1);
    CHECK(!zero.current_directive_valid);

    test_init_workers(&workers, 1);
    frame = test_valid_payload(UINT64_C(405));
    test_set_worker_snapshot(&workers.worker[0], ACT_SLEEP, ACT_RUN,
                             frame.sequence, true, false, false);
    coord_metrics_t one = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(one.eligible_workers == 1);
    CHECK_NEAR(one.s4, 0.0, 1e-15);
    CHECK_NEAR(one.s4_burst, 1.0, 1e-15);
    CHECK(!one.large_burst_event);

    for (int action = ACT_RUN; action < ACTION_COUNT; ++action) {
        test_set_worker_snapshot(&workers.worker[0], (action_t)action,
                                 (action_t)action, frame.sequence,
                                 true, false, false);
        coord_metrics_t boundary = compute_metrics(&workers, &frame, 0.0, 1000,
                                                   NULL);
        CHECK(boundary.changed_eligible_workers == 0);
        CHECK_NEAR(boundary.s4_burst, 1.0, 1e-15);
    }
    return true;
}

static bool test_s4_burst_history_and_random_invariants(void) {
    worker_block_t workers;
    burst_tracker_t tracker = {0};
    const action_t run4[] = {ACT_RUN, ACT_RUN, ACT_RUN, ACT_RUN};
    const action_t sleep4[] = {ACT_SLEEP, ACT_SLEEP, ACT_SLEEP, ACT_SLEEP};
    test_init_workers(&workers, 4);

    for (uint64_t sequence = UINT64_C(500); sequence < UINT64_C(520); ++sequence) {
        bool to_sleep = (sequence & UINT64_C(1)) == 0;
        signal_payload_t frame = test_valid_payload(sequence);
        frame.directive = ACT_RUN;
        test_set_burst_population(&workers, to_sleep ? run4 : sleep4,
                                  to_sleep ? sleep4 : run4,
                                  4, frame.sequence, true, false);
        coord_metrics_t metrics = compute_metrics(&workers, &frame, 0.0, 1000,
                                                  &tracker);
        CHECK(isfinite(metrics.s4_burst));
        CHECK(metrics.s4_burst >= 0.0 && metrics.s4_burst <= 1.0);
        burst_tracker_record(&tracker, &frame, &metrics);
    }
    CHECK(tracker.next_index == 4u);

    test_init_workers(&workers, 8);
    uint64_t rng = UINT64_C(0x3ebf670f50f21167);
    for (int trial = 0; trial < 500; ++trial) {
        signal_payload_t frame = test_valid_payload(UINT64_C(600) + (uint64_t)trial);
        frame.directive = (uint32_t)(rng_next(&rng) % ACTION_COUNT);
        bool accepted = (rng_next(&rng) & UINT64_C(3)) != UINT64_C(0);
        for (int worker = 0; worker < workers.worker_count; ++worker) {
            action_t previous = (action_t)(rng_next(&rng) % ACTION_COUNT);
            action_t action = (action_t)(rng_next(&rng) % ACTION_COUNT);
            test_set_worker_snapshot(&workers.worker[worker], action, previous,
                                     accepted ? frame.sequence : frame.sequence - 1u,
                                     true, false, false);
        }
        coord_metrics_t metrics = compute_metrics(&workers, &frame, 0.0, 1000,
                                                  &tracker);
        const double bounded[] = {
            metrics.s4, metrics.s4_burst, metrics.change_fraction,
            metrics.dominant_transition_fraction, metrics.justified_change_fraction,
            metrics.oscillation_penalty
        };
        for (size_t index = 0; index < sizeof(bounded) / sizeof(bounded[0]); ++index) {
            CHECK(isfinite(bounded[index]));
            CHECK(bounded[index] >= 0.0 && bounded[index] <= 1.0);
        }
        CHECK(metrics.changed_eligible_workers >= 0);
        CHECK(metrics.changed_eligible_workers <= metrics.eligible_workers);
        CHECK(metrics.justified_changed_workers >= 0);
        CHECK(metrics.justified_changed_workers <= metrics.changed_eligible_workers);
        CHECK(metrics.dominant_transition_count >= 0);
        CHECK(metrics.dominant_transition_count <= metrics.changed_eligible_workers);
        if (metrics.changed_eligible_workers == 0) {
            CHECK(metrics.dominant_old_action == -1);
            CHECK(metrics.dominant_new_action == -1);
            CHECK_NEAR(metrics.s4_burst, 1.0, 1e-15);
        } else {
            CHECK(metrics.dominant_old_action >= ACT_RUN
                  && metrics.dominant_old_action < ACTION_COUNT);
            CHECK(metrics.dominant_new_action >= ACT_RUN
                  && metrics.dominant_new_action < ACTION_COUNT);
        }
        if (!metrics.current_directive_valid) {
            CHECK(!metrics.directive_transition_valid);
            CHECK(metrics.justified_changed_workers == 0);
        }
        burst_tracker_record(&tracker, &frame, &metrics);
    }
    return true;
}

static bool test_state_conditioned_coherence(void) {
    worker_block_t workers;
    test_init_workers(&workers, 4);
    signal_payload_t frame = test_valid_payload(UINT64_C(77));
    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_RUN, ACT_RUN,
                                 frame.sequence, true, false, false);
        atomic_store(&workers.worker[index].state_index, 3);
    }
    coord_metrics_t homogeneous = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK_NEAR(homogeneous.s3_global, 1.0, 1e-15);
    CHECK_NEAR(homogeneous.s3_conditioned, 1.0, 1e-15);
    CHECK_NEAR(homogeneous.s3, homogeneous.s3_conditioned, 1e-15);

    /* Different states legitimately choose different actions: global diversity
     * is retained, while each state-local cohort is coherent. */
    for (int index = 0; index < workers.worker_count; ++index) {
        action_t action = index < 2 ? ACT_RUN : ACT_SLEEP;
        test_set_worker_snapshot(&workers.worker[index], action, action,
                                 frame.sequence, true, false, false);
        atomic_store(&workers.worker[index].state_index, index < 2 ? 1 : 2);
    }
    coord_metrics_t heterogeneous = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(heterogeneous.s3_global < 1.0);
    CHECK_NEAR(heterogeneous.s3_conditioned, 1.0, 1e-15);
    CHECK_NEAR(heterogeneous.s3, 1.0, 1e-15);

    for (int index = 0; index < workers.worker_count; ++index) {
        action_t action = (action_t)index;
        test_set_worker_snapshot(&workers.worker[index], action, action,
                                 frame.sequence, true, false, false);
        atomic_store(&workers.worker[index].state_index, 4);
    }
    coord_metrics_t divergent = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(divergent.s3_global < 1.0);
    CHECK(divergent.s3_conditioned < 1.0);
    CHECK_NEAR(divergent.s3, divergent.s3_conditioned, 1e-15);
    return true;
}

static bool test_first_decision_has_no_s4_penalty(void) {
    worker_block_t workers;
    signal_payload_t frame = test_valid_payload(UINT64_C(78));

    test_init_workers(&workers, 1);
    record_worker_decision(&workers.worker[0], ACT_RUN, 1, false,
                           FALLBACK_REASON_NONE, frame.sequence);
    CHECK(atomic_load(&workers.worker[0].has_previous_action) == 1);
    CHECK(atomic_load(&workers.worker[0].previous_action) == ACT_RUN);
    atomic_store(&workers.worker[0].alive, 1);
    coord_metrics_t metrics = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK_NEAR(metrics.s4, 1.0, 1e-15);
    return true;
}

static bool test_effective_action_rules(void) {
    action_observation_t observation = test_observation(ACT_RUN);
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.fallback_reason = FALLBACK_REASON_NO_VALID_FRAME;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_FALLBACK);
    observation = test_observation((action_t)ACTION_COUNT);
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_INVALID_ACTION);

    observation = test_observation(ACT_SLEEP);
    observation.requested_sleep_ns = 0;
    observation.observed_sleep_ns = 0;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.requested_sleep_ns = MAX_ACTION_SLEEP_NS;
    observation.observed_sleep_ns = MAX_ACTION_SLEEP_NS + SLEEP_OVERSLEEP_MAX_NS;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.observed_sleep_ns = observation.requested_sleep_ns
                                  - SLEEP_UNDERSLEEP_TOLERANCE_NS - 1u;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_UNDERSLEEP);
    observation.observed_sleep_ns = observation.requested_sleep_ns
                                  + SLEEP_OVERSLEEP_MAX_NS + 1u;
    CHECK(classify_effective_action(&observation)
          == EFFECTIVE_RESULT_EXCESSIVE_OVERSLEEP);
    observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
    observation.action_errno = EINTR;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_ACTION_ERROR);

    observation = test_observation(ACT_MIGRATE);
    observation.requested_cpu = -1;
    observation.requested_cpu_valid = false;
    observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
    observation.action_errno = EINVAL;
    CHECK(classify_effective_action(&observation)
          == EFFECTIVE_RESULT_INVALID_REQUESTED_CPU);
    observation = test_observation(ACT_MIGRATE);
    observation.requested_cpu = 2;
    observation.requested_cpu_valid = true;
    observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
    observation.action_errno = EPERM;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_ACTION_ERROR);
    observation = test_observation(ACT_MIGRATE);
    observation.requested_cpu = 2;
    observation.requested_cpu_valid = true;
    observation.cpu_after_action = 2;
    observation.migration_observed = true;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.migration_observed = false;
    CHECK(classify_effective_action(&observation)
          == EFFECTIVE_RESULT_MIGRATION_NOT_OBSERVED);
    observation.requested_cpu = observation.cpu_before_action;
    observation.cpu_after_action = observation.cpu_before_action;
    observation.migration_observed = true;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);

    observation = test_observation(ACT_THROTTLE);
    observation.throttle_attempted = true;
    observation.requested_sleep_ns = UINT64_C(25000000);
    observation.observed_sleep_ns = observation.requested_sleep_ns;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.fallback_reason = FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_FALLBACK);

    observation = test_observation(ACT_YIELD);
    observation.yield_attempted = true;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_SUCCESS);
    observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
    observation.action_errno = EAGAIN;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_ACTION_ERROR);
    observation.fallback_reason = FALLBACK_REASON_NO_VALID_FRAME;
    CHECK(classify_effective_action(&observation) == EFFECTIVE_RESULT_FALLBACK);
    return true;
}

static bool test_migrate_restores_affinity(void) {
    cpu_set_t before, after;
    uint64_t rng = UINT64_C(0x41f23ac782b9d501);

    CHECK(sched_getaffinity(0, sizeof(before), &before) == 0);
    action_observation_t observation = perform_action(
        ACT_MIGRATE, 0, &rng, FALLBACK_REASON_NONE);
    CHECK(sched_getaffinity(0, sizeof(after), &after) == 0);
    CHECK(CPU_EQUAL(&before, &after));
    CHECK(observation.requested_cpu_valid);
    return true;
}

static bool test_bounded_run_action_execution(void) {
    uint64_t rng = UINT64_C(0x4bc3a81d52e7906f);
    uint64_t start = monotonic_ns();
    action_observation_t observation = perform_action(ACT_RUN, 0, &rng,
                                                      FALLBACK_REASON_NONE);
    uint64_t elapsed = monotonic_ns() - start;
    CHECK(observation.action_attempted);
    CHECK(observation.action_attempt_result == ACTION_ATTEMPT_SUCCEEDED);
    CHECK(observation.action_errno == 0);
    CHECK(observation.effective_action_result == EFFECTIVE_RESULT_SUCCESS);
    /* This is a bounded work-path test, not an assertion about dispatch time. */
    CHECK(elapsed >= UINT64_C(1000000));
    CHECK(elapsed <= UINT64_C(1000000000));
    return true;
}

static bool test_bounded_yield_action_execution(void) {
    uint64_t rng = UINT64_C(0x015c4ab8e2d97f63);
    action_observation_t observation = perform_action(ACT_YIELD, 0, &rng,
                                                      FALLBACK_REASON_NONE);
    CHECK(observation.action_attempted);
    CHECK(observation.yield_attempted);
    CHECK(observation.action_attempt_result == ACTION_ATTEMPT_SUCCEEDED);
    CHECK(observation.action_errno == 0);
    CHECK(observation.effective_action_result == EFFECTIVE_RESULT_SUCCESS);
    return true;
}

static bool test_effective_metric_aggregation(void) {
    worker_block_t workers;
    test_init_workers(&workers, ACTION_COUNT);
    signal_payload_t frame = test_valid_payload(UINT64_C(88));
    frame.directive = ACT_RUN;
    frame.confidence = 1.0;
    frame.monotonic_ns = monotonic_ns();
    for (int index = 0; index < workers.worker_count; ++index) {
        action_t action = (action_t)index;
        action_observation_t observation = test_observation(action);
        if (action == ACT_SLEEP) {
            observation.requested_sleep_ns = UINT64_C(35000000);
            observation.observed_sleep_ns = observation.requested_sleep_ns;
        } else if (action == ACT_MIGRATE) {
            observation.requested_cpu = 1;
            observation.requested_cpu_valid = true;
            observation.cpu_after_action = 1;
            observation.migration_observed = true;
        } else if (action == ACT_THROTTLE) {
            observation.throttle_attempted = true;
            observation.requested_sleep_ns = UINT64_C(25000000);
            observation.observed_sleep_ns = observation.requested_sleep_ns;
        } else if (action == ACT_YIELD) {
            observation.yield_attempted = true;
        }
        test_set_worker_snapshot(&workers.worker[index], action, action,
                                 frame.sequence, true, false, false);
        test_set_action_observation(&workers.worker[index], &observation,
                                    frame.sequence);
    }
    coord_metrics_t all_effective = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(all_effective.action_attempt_count == ACTION_COUNT);
    CHECK(all_effective.effective_action_success_count == ACTION_COUNT);
    CHECK(all_effective.action_error_count == 0);
    CHECK_NEAR(all_effective.s2_selected, 1.0 / ACTION_COUNT, 1e-15);
    CHECK_NEAR(all_effective.s2_effective, 1.0, 1e-15);
    CHECK_NEAR(all_effective.migration_observed_success_fraction, 1.0, 1e-15);
    CHECK_NEAR(all_effective.sleep_effectiveness_fraction, 1.0, 1e-15);
    CHECK_NEAR(all_effective.yield_call_success_fraction, 1.0, 1e-15);
    CHECK_NEAR(all_effective.throttle_operation_success_fraction, 1.0, 1e-15);

    for (int index = 0; index < workers.worker_count; ++index) {
        action_observation_t observation = test_observation((action_t)index);
        observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
        observation.action_errno = EIO;
        observation.effective_action_result = classify_effective_action(&observation);
        test_set_action_observation(&workers.worker[index], &observation,
                                    frame.sequence);
    }
    coord_metrics_t none_effective = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(none_effective.effective_action_success_count == 0);
    CHECK(none_effective.action_error_count == ACTION_COUNT);
    CHECK_NEAR(none_effective.s2_effective, 0.0, 1e-15);

    action_observation_t stale = test_observation(ACT_RUN);
    test_set_action_observation(&workers.worker[0], &stale, frame.sequence - 1u);
    coord_metrics_t stale_frame = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(stale_frame.action_attempt_count == ACTION_COUNT - 1);

    for (int index = 0; index < workers.worker_count; ++index) {
        action_observation_t observation = test_observation((action_t)index);
        test_set_action_observation(&workers.worker[index], &observation,
                                    frame.sequence - 1u);
    }
    coord_metrics_t rejected_or_stale = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(rejected_or_stale.action_attempt_count == 0);
    CHECK(rejected_or_stale.effective_action_success_count == 0);
    CHECK_NEAR(rejected_or_stale.s2_effective, 0.0, 1e-15);

    test_set_worker_snapshot(&workers.worker[0], ACT_THROTTLE, ACT_THROTTLE,
                             frame.sequence, true, false, true);
    atomic_store(&workers.worker[0].fallback_reason,
                 FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED);
    coord_metrics_t fallback = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(fallback.fallback_workers == 1);
    CHECK(fallback.fallback_reason == FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED);
    CHECK_NEAR(fallback.fallback_fraction, 1.0 / ACTION_COUNT, 1e-15);

    test_init_workers(&workers, 1);
    test_set_worker_snapshot(&workers.worker[0], ACT_RUN, ACT_RUN,
                             frame.sequence, true, true, false);
    coord_metrics_t exempt = compute_metrics(&workers, &frame, 0.0, 1000, NULL);
    CHECK(exempt.eligible_workers == 0);
    CHECK_NEAR(exempt.s2_effective, 1.0, 1e-15);
    CHECK_NEAR(exempt.fallback_fraction, 0.0, 1e-15);
    return true;
}

static bool test_worker_sequence_completion_gate(void) {
    worker_block_t workers;
    action_observation_t observation = test_observation(ACT_RUN);
    const uint64_t sequence = UINT64_C(73);

    test_init_workers(&workers, 2);
    CHECK(!workers_completed_sequence(&workers, sequence));

    for (int index = 0; index < workers.worker_count; ++index) {
        test_set_worker_snapshot(&workers.worker[index], ACT_RUN, ACT_RUN,
                                 sequence, true, false, false);
    }
    CHECK(!workers_completed_sequence(&workers, sequence));

    test_set_action_observation(&workers.worker[0], &observation, sequence);
    CHECK(!workers_completed_sequence(&workers, sequence));
    test_set_action_observation(&workers.worker[1], &observation, sequence);
    CHECK(workers_completed_sequence(&workers, sequence));

    atomic_store(&workers.worker[1].action_sequence, sequence - 1u);
    CHECK(!workers_completed_sequence(&workers, sequence));
    atomic_store(&workers.worker[1].exempt_rt, 1);
    CHECK(workers_completed_sequence(&workers, sequence));
    return true;
}

static bool test_difference_reward_local_dominance(void) {
    worker_block_t workers;
    test_init_workers(&workers, 3);
    signal_payload_t frame = test_valid_payload(UINT64_C(60));
    frame.thermal_proxy = 0.20;
    frame.switch_penalty = 0.0;

    int combinations = ACTION_COUNT * ACTION_COUNT * ACTION_COUNT;
    for (int directive_value = 0; directive_value < ACTION_COUNT;
         ++directive_value) {
        frame.directive = (uint32_t)directive_value;
        for (int code = 0; code < combinations; ++code) {
            int remaining = code;
            action_t selected[3];
            for (int index = 0; index < workers.worker_count; ++index) {
                selected[index] = (action_t)(remaining % ACTION_COUNT);
                remaining /= ACTION_COUNT;
                test_set_worker_snapshot(&workers.worker[index], selected[index],
                                         selected[index], frame.sequence, true,
                                         false, false);
            }
            assign_difference_rewards(&workers, &frame);
            for (int index = 0; index < workers.worker_count; ++index) {
                double reward = atomic_load(&workers.worker[index].reward);
                CHECK(atomic_load(&workers.worker[index].reward_sequence)
                      == frame.sequence);
                CHECK(isfinite(reward));
                CHECK(reward >= LOCAL_REWARD_MISMATCH - 1e-12);
                CHECK(reward <= LOCAL_REWARD_MATCH + 1e-12);
                if (selected[index] == (action_t)directive_value) {
                    CHECK(reward >= 0.24 - 1e-12);
                } else {
                    CHECK(reward <= -0.24 + 1e-12);
                }
            }
        }
    }
    return true;
}

static bool test_difference_reward_aggregate_equivalence(void) {
    uint64_t rng = UINT64_C(0x8d12f4a6bc791e03);
    const int sizes[] = {0, 1, 2, 5, MAX_WORKERS};
    for (size_t size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]);
         ++size_index) {
        for (int trial = 0; trial < 200; ++trial) {
            action_t actions[MAX_WORKERS] = {0};
            action_t previous[MAX_WORKERS] = {0};
            int states[MAX_WORKERS] = {0};
            population_counters_t counters = {.eligible = sizes[size_index]};
            action_t directive = (action_t)(rng_next(&rng) % ACTION_COUNT);
            for (int i = 0; i < counters.eligible; ++i) {
                actions[i] = (action_t)(rng_next(&rng) % ACTION_COUNT);
                previous[i] = (action_t)(rng_next(&rng) % ACTION_COUNT);
                states[i] = (int)(rng_next(&rng) % STATE_COUNT);
                counters.state_totals[states[i]]++;
                counters.state_actions[states[i]][actions[i]]++;
                if (actions[i] == directive) counters.compliant++;
                if (actions[i] != previous[i]) counters.changed++;
            }
            CHECK_NEAR(population_utility_from_counters(&counters),
                       population_utility_reference(actions, previous, states,
                                                    counters.eligible, directive),
                       1e-15);
            for (int i = 0; i < counters.eligible; ++i) {
                population_counters_t changed = counters;
                changed.state_actions[states[i]][actions[i]]--;
                changed.state_actions[states[i]][ACT_YIELD]++;
                if (actions[i] == directive) changed.compliant--;
                if (ACT_YIELD == directive) changed.compliant++;
                if (actions[i] != previous[i]) changed.changed--;
                if (ACT_YIELD != previous[i]) changed.changed++;
                action_t reference_actions[MAX_WORKERS];
                memcpy(reference_actions, actions, sizeof(reference_actions));
                reference_actions[i] = ACT_YIELD;
                CHECK_NEAR(population_utility_from_counters(&changed),
                           population_utility_reference(reference_actions, previous,
                                                        states,
                                                        counters.eligible, directive),
                           1e-15);
            }
        }
    }
    return true;
}

static signal_payload_t test_controller_payload(double jitter, double switching,
                                                double consensus) {
    signal_payload_t payload = {0};
    payload.jitter_sigma = jitter;
    payload.switch_penalty = switching;
    payload.consensus_blend = consensus;
    return payload;
}

static bool test_controller_mapping_rate_and_bounds(void) {
    coord_metrics_t metrics = {.s2 = 1.0, .s3 = 1.0, .s3_global = 1.0,
                               .s4 = 1.0, .s4_burst = 1.0};
    controller_machine_t machine = {0};
    controller_window_t window = {0};
    oscillation_window_t osc_window = {0};
    machine.state = CONTROL_STATE_NORMAL;
    machine.applied.jitter_sigma = 0.10;
    machine.applied.switch_penalty = 0.10;
    machine.applied.consensus_blend = 0.10;
    
    signal_payload_t next = test_controller_payload(0.10, 0.10, 0.10);
    controller_event_t none = controller_machine_update(&next, &metrics, 0.01,
                                                UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(none.reason == CONTROL_REASON_NONE);
    CHECK(strcmp(controller_reason_name(none.reason), "NONE") == 0);
    CHECK_NEAR(next.jitter_sigma, 0.10 - none.beta * 0.05, 1e-15);
    CHECK_NEAR(next.switch_penalty, 0.10 - none.beta * 0.04, 1e-15);
    CHECK_NEAR(next.consensus_blend, 0.10 - none.beta * 0.03, 1e-15);

    metrics.s3 = metrics.s3_global = 0.0;
    metrics.s4 = 1.0;
    machine.applied.jitter_sigma = 0.10;
    machine.applied.switch_penalty = 0.10;
    machine.applied.consensus_blend = 0.10;
    next = test_controller_payload(0.10, 0.10, 0.10);
    controller_event_t s3 = controller_machine_update(&next, &metrics, 0.01,
                                              UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(s3.reason == CONTROL_REASON_S3);
    CHECK(strcmp(controller_reason_name(s3.reason), "S3") == 0);
    CHECK_NEAR(next.jitter_sigma, 0.10 - s3.beta * 0.05, 1e-15);
    CHECK_NEAR(next.switch_penalty, 0.10 + s3.beta * 0.15, 1e-15);
    CHECK_NEAR(next.consensus_blend, 0.10 + s3.beta * 0.10, 1e-15);

    metrics.s3 = metrics.s3_global = 1.0;
    metrics.s4 = 0.0;
    machine.applied.jitter_sigma = 0.10;
    machine.applied.switch_penalty = 0.10;
    machine.applied.consensus_blend = 0.10;
    next = test_controller_payload(0.10, 0.10, 0.10);
    controller_event_t s4 = controller_machine_update(&next, &metrics, 0.01,
                                              UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(s4.reason == CONTROL_REASON_S4);
    CHECK(strcmp(controller_reason_name(s4.reason), "S4") == 0);
    CHECK_NEAR(next.jitter_sigma, 0.10 + s4.beta * 0.20, 1e-15);
    CHECK_NEAR(next.switch_penalty, 0.10 + s4.beta * 0.15, 1e-15);
    CHECK_NEAR(next.consensus_blend, 0.10 - s4.beta * 0.03, 1e-15);

    metrics.s3 = metrics.s3_global = 0.0;
    metrics.s4 = 0.0;
    machine.applied.jitter_sigma = 0.10;
    machine.applied.switch_penalty = 0.10;
    machine.applied.consensus_blend = 0.10;
    next = test_controller_payload(0.10, 0.10, 0.10);
    controller_event_t both = controller_machine_update(&next, &metrics, 0.01,
                                                UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(both.reason == (CONTROL_REASON_S3 | CONTROL_REASON_S4));
    CHECK(strcmp(controller_reason_name(both.reason), "S3+S4") == 0);
    CHECK(strcmp(controller_reason_name(UINT32_C(0x80000000)), "INVALID") == 0);

    machine.applied.jitter_sigma = 0.10;
    machine.applied.switch_penalty = 0.10;
    machine.applied.consensus_blend = 0.10;
    signal_payload_t later_payload = test_controller_payload(0.10, 0.10, 0.10);
    controller_event_t later = controller_machine_update(&later_payload, &metrics, 0.01,
                                                 UINT64_C(100), &machine, &window, &osc_window, true);
    CHECK_NEAR(both.beta, CONTROLLER_BETA0 / sqrt(2.0), 1e-15);
    CHECK_NEAR(later.beta, CONTROLLER_BETA0 / sqrt(101.0), 1e-15);
    CHECK(later.beta < both.beta);

    machine.applied.jitter_sigma = 0.199;
    machine.applied.switch_penalty = 0.299;
    machine.applied.consensus_blend = 0.149;
    next = test_controller_payload(0.199, 0.299, 0.149);
    controller_event_t upper = controller_machine_update(&next, &metrics, 0.0,
                                                 UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(upper.jitter_saturated);
    CHECK(upper.switch_saturated);
    CHECK(upper.consensus_saturated);
    CHECK(next.jitter_sigma == 0.20);
    CHECK(next.switch_penalty == 0.30);
    CHECK(next.consensus_blend == 0.15);

    metrics.s3 = metrics.s3_global = 1.0;
    metrics.s4 = 1.0;
    machine.applied.jitter_sigma = 0.0;
    machine.applied.switch_penalty = 0.0;
    machine.applied.consensus_blend = 0.0;
    next = test_controller_payload(0.0, 0.0, 0.0);
    controller_event_t lower = controller_machine_update(&next, &metrics, 0.01,
                                                 UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(lower.jitter_saturated);
    CHECK(lower.switch_saturated);
    CHECK(lower.consensus_saturated);
    CHECK(next.jitter_sigma == 0.01);
    CHECK(next.switch_penalty == 0.0);
    CHECK(next.consensus_blend == 0.0);

    machine.applied.jitter_sigma = 0.0;
    machine.applied.switch_penalty = 0.0;
    machine.applied.consensus_blend = 0.0;
    next = test_controller_payload(0.0, 0.0, 0.0);
    controller_event_t high_floor = controller_machine_update(&next, &metrics, 0.80,
                                                      UINT64_C(1), &machine, &window, &osc_window, true);
    CHECK(high_floor.jitter_saturated);
    CHECK(next.jitter_sigma == 0.20);
    CHECK(next.jitter_sigma >= 0.0 && next.jitter_sigma <= 0.20);
    CHECK(next.switch_penalty >= 0.0 && next.switch_penalty <= 0.30);
    CHECK(next.consensus_blend >= 0.0 && next.consensus_blend <= 0.15);
    return true;
}

static double test_gain_mse(const double *trace, int count, double gain) {
    double estimate = trace[0];
    double sum = 0.0;
    for (int index = 1; index < count; ++index) {
        double error = trace[index] - estimate;
        sum += error * error;
        estimate += gain * error;
    }
    return sum / (double)(count - 1);
}

static bool test_predictor_calibration_and_noise(void) {
    const double short_trace[] = {0.1, 0.2, 0.3};
    CHECK_NEAR(calibrate_fixed_gain(short_trace, 3), 0.90, 0.0);
    CHECK_NEAR(robust_noise_sigma(short_trace, 3), 0.02, 0.0);
    CHECK(calibration_sample_target(0) == 0);
    CHECK(calibration_sample_target(1) == 20);
    CHECK(calibration_sample_target(INT_MAX) == MAX_CAL_SAMPLES);

    const double constant_trace[] = {0.4, 0.4, 0.4, 0.4, 0.4};
    CHECK_NEAR(calibrate_fixed_gain(constant_trace, 5), 0.01, 0.0);
    CHECK_NEAR(robust_noise_sigma(constant_trace, 5), 0.001, 0.0);

    const double varied_trace[] = {0.10, 0.40, 0.20, 0.80, 0.30,
                                   0.90, 0.20, 0.10};
    int varied_count = (int)(sizeof(varied_trace) / sizeof(varied_trace[0]));
    double selected_gain = calibrate_fixed_gain(varied_trace, varied_count);
    CHECK(selected_gain >= 0.01 && selected_gain <= 0.99);
    double selected_mse = test_gain_mse(varied_trace, varied_count, selected_gain);
    for (int gain_index = 1; gain_index <= 99; ++gain_index) {
        double candidate_gain = (double)gain_index / 100.0;
        CHECK(selected_mse <= test_gain_mse(varied_trace, varied_count,
                                            candidate_gain) + 1e-15);
    }

    const double alternating[] = {0.0, 1.0, 0.0, 1.0, 0.0};
    CHECK_NEAR(robust_noise_sigma(alternating, 5),
               1.4826 / sqrt(2.0), 1e-15);
    const double held_out[] = {0.2, 0.25, 0.30};
    double held_out_mse = evaluate_fixed_gain_held_out(
        varied_trace, varied_count, held_out, 3, selected_gain);
    CHECK(isfinite(held_out_mse));
    CHECK(held_out_mse >= 0.0);
    CHECK(evaluate_fixed_gain_held_out(varied_trace, 0, held_out, 3,
                                       selected_gain) == HUGE_VAL);
    CHECK_NEAR(calibrated_prediction_confidence(0.0, 0.01, 0.02), 1.0, 0.0);
    double moderate = calibrated_prediction_confidence(0.1, 0.01, 0.02);
    double large = calibrated_prediction_confidence(0.4, 0.01, 0.02);
    CHECK(moderate > large);
    CHECK(moderate >= 0.0 && moderate <= 1.0);
    CHECK(large >= 0.0 && large <= 1.0);
    CHECK(calibrated_prediction_confidence(NAN, 0.01, 0.02) == 0.0);
    CHECK(calibrated_prediction_confidence(0.1, HUGE_VAL, 0.02) == 0.0);
    CHECK(calibrated_prediction_confidence(0.1, 0.01, 0.0) == 0.0);
    return true;
}

static bool test_strict_cli_numeric_parsing(void) {
    int signed_value = 0;
    uint64_t unsigned_value = 0;

    CHECK(parse_int_arg("-12", &signed_value) && signed_value == -12);
    CHECK(!parse_int_arg(" 12", &signed_value));
    CHECK(!parse_int_arg("12x", &signed_value));
    CHECK(!parse_int_arg("", &signed_value));
    CHECK(parse_u64_arg("18446744073709551615", &unsigned_value));
    CHECK(unsigned_value == UINT64_MAX);
    CHECK(!parse_u64_arg("-1", &unsigned_value));
    CHECK(!parse_u64_arg(" 1", &unsigned_value));
    CHECK(!parse_u64_arg("18446744073709551616", &unsigned_value));
    return true;
}

static bool test_policy_aggregation_excludes_rt(void) {
    worker_block_t workers;
    double aggregate[QTABLE_SIZE];

    test_init_workers(&workers, 3);
    for (int q = 0; q < QTABLE_SIZE; q++) {
        workers.worker[0].qtable[q] = 1000.0;
        workers.worker[1].qtable[q] = 2.0;
        workers.worker[2].qtable[q] = 4.0;
    }
    atomic_store(&workers.worker[0].exempt_rt, 1);
    CHECK(aggregate_adaptive_policy(&workers, aggregate) == 2);
    for (int q = 0; q < QTABLE_SIZE; q++)
        CHECK_NEAR(aggregate[q], 3.0, 0.0);
    return true;
}

static bool test_policy_atomic_save_uses_private_temp(void) {
    char directory[] = "/tmp/orchestra-policy-unit.XXXXXX";
    char path[PATH_MAX];
    double qtable[QTABLE_SIZE];
    struct stat state;
    uint8_t serialized[POLICY_MAX_FILE_SIZE];

    CHECK(mkdtemp(directory) != NULL);
    int written = snprintf(path, sizeof(path), "%s/policy.bin", directory);
    CHECK(written > 0 && (size_t)written < sizeof(path));
    for (int index = 0; index < QTABLE_SIZE; ++index)
        qtable[index] = (double)index / 100.0;
    CHECK(policy_save_atomic(qtable, path, 7, 8, 9) == POLICY_SAVE_OK);
    CHECK(stat(path, &state) == 0);
    CHECK(S_ISREG(state.st_mode));
    CHECK((state.st_mode & 0777u) == 0600u);

    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    size_t bytes = fread(serialized, 1, sizeof(serialized), file);
    CHECK(ferror(file) == 0);
    CHECK(fclose(file) == 0);
    double loaded[QTABLE_SIZE];
    uint64_t generation = 0, train = 0, adapt = 0;
    CHECK(policy_deserialize(serialized, bytes, loaded, &generation,
                             &train, &adapt) == POLICY_LOAD_OK);
    CHECK(generation == 7 && train == 8 && adapt == 9);
    CHECK(memcmp(qtable, loaded, sizeof(qtable)) == 0);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    return true;
}

static bool test_consensus_blending(void) {
    signal_bus_t bus;
    worker_block_t workers;
    test_init_bus(&bus);
    test_init_workers(&workers, 3);
    g_stop = 0;

    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        for (int q_index = 0; q_index < QTABLE_SIZE; ++q_index) {
            workers.worker[worker_index].qtable[q_index] =
                (double)q_index * 0.125;
        }
    }
    double original[3][QTABLE_SIZE];
    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        memcpy(original[worker_index], workers.worker[worker_index].qtable,
               sizeof(original[worker_index]));
    }
    CHECK(!consensus_blend_qtables(&bus, &workers, 0.0));
    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        CHECK(memcmp(original[worker_index], workers.worker[worker_index].qtable,
                     sizeof(original[worker_index])) == 0);
    }

    CHECK(consensus_blend_qtables(&bus, &workers, 0.15));
    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        for (int q_index = 0; q_index < QTABLE_SIZE; ++q_index) {
            CHECK_NEAR(workers.worker[worker_index].qtable[q_index],
                       original[worker_index][q_index], 1e-12);
        }
    }

    for (int q_index = 0; q_index < QTABLE_SIZE; ++q_index) {
        workers.worker[0].qtable[q_index] = (double)q_index;
        workers.worker[1].qtable[q_index] = (double)q_index + 10.0;
        workers.worker[2].qtable[q_index] = (double)q_index + 20.0;
    }
    CHECK(consensus_blend_qtables(&bus, &workers, 0.25));
    for (int q_index = 0; q_index < QTABLE_SIZE; ++q_index) {
        double original_mean = (double)q_index + 10.0;
        double blended_mean =
            (workers.worker[0].qtable[q_index]
             + workers.worker[1].qtable[q_index]
             + workers.worker[2].qtable[q_index]) / 3.0;
        CHECK_NEAR(blended_mean, original_mean, 1e-12);
        CHECK_NEAR(workers.worker[0].qtable[q_index],
                   (double)q_index + 2.5, 1e-12);
        CHECK_NEAR(workers.worker[1].qtable[q_index],
                   (double)q_index + 10.0, 1e-12);
        CHECK_NEAR(workers.worker[2].qtable[q_index],
                   (double)q_index + 17.5, 1e-12);
    }

    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        memcpy(original[worker_index], workers.worker[worker_index].qtable,
               sizeof(original[worker_index]));
    }
    atomic_store(&workers.worker[1].q_update_active, 1);
    CHECK(!consensus_blend_qtables(&bus, &workers, 0.10));
    CHECK(atomic_load(&bus.consensus_lock) == 0);
    for (int worker_index = 0; worker_index < workers.worker_count;
         ++worker_index) {
        CHECK(memcmp(original[worker_index], workers.worker[worker_index].qtable,
                     sizeof(original[worker_index])) == 0);
    }
    atomic_store(&workers.worker[1].q_update_active, 0);
    return true;
}

int main(void) {
    const named_test_t tests[] = {
        {"sha256_hmac_known_vectors", test_sha256_hmac_vectors},
        {"canonical_128_byte_serialization", test_canonical_serialization},
        {"frame_verification_matrix", test_frame_verification_matrix},
        {"publication_canonical_equivalence", test_publication_canonical_equivalence},
        {"publication_fault_injection", test_publication_fault_injection},
        {"directive_boundaries_and_precedence", test_directive_boundaries_and_precedence},
        {"state_schema_v2_alignment", test_state_schema_v2_alignment},
        {"epsilon_schedule", test_epsilon_schedule},
        {"prediction_and_baseline_fallback", test_prediction_fallback_decision},
        {"normalized_geometric_mean", test_normalized_geometric_mean_cases},
        {"coordination_metric_scenarios", test_metrics_scenarios},
        {"s4_burst_basic_semantics", test_s4_burst_basic_semantics},
        {"s4_burst_directive_and_oscillation", test_s4_burst_directive_and_oscillation},
        {"s4_burst_invalid_and_zero_semantics", test_s4_burst_invalid_and_zero_semantics},
        {"s4_burst_history_and_random_invariants", test_s4_burst_history_and_random_invariants},
        {"state_conditioned_coherence", test_state_conditioned_coherence},
        {"first_decision_has_no_s4_penalty", test_first_decision_has_no_s4_penalty},
        {"effective_action_rules", test_effective_action_rules},
        {"migrate_restores_affinity", test_migrate_restores_affinity},
        {"bounded_run_action_execution", test_bounded_run_action_execution},
        {"bounded_yield_action_execution", test_bounded_yield_action_execution},
        {"effective_metric_aggregation", test_effective_metric_aggregation},
        {"worker_sequence_completion_gate", test_worker_sequence_completion_gate},
        {"difference_reward_local_dominance", test_difference_reward_local_dominance},
        {"difference_reward_aggregate_equivalence", test_difference_reward_aggregate_equivalence},
        {"controller_mapping_rate_and_bounds", test_controller_mapping_rate_and_bounds},
        {"predictor_calibration_and_noise", test_predictor_calibration_and_noise},
        {"strict_cli_numeric_parsing", test_strict_cli_numeric_parsing},
        {"policy_aggregation_excludes_rt", test_policy_aggregation_excludes_rt},
        {"policy_atomic_save_uses_private_temp", test_policy_atomic_save_uses_private_temp},
        {"consensus_blending_and_timeout", test_consensus_blending}
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0; index < test_count; ++index) {
        if (!tests[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return EXIT_FAILURE;
        }
        printf("[PASS] %s\n", tests[index].name);
    }
    printf("PASS summary: %zu/%zu named unit tests passed\n",
           test_count, test_count);
    return EXIT_SUCCESS;
}
