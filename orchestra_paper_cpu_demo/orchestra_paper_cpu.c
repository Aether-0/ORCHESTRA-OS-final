#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../kernel/sched_ext/include/orchestra_bridge_abi_v2.h"

/*
 * ORCHESTRA-OS paper-aligned real-CPU userspace research prototype.
 *
 * Implemented paper mechanisms:
 *   - predictive shared signal bus backed by read-only mmap in workers
 *   - HMAC-SHA256 integrity, monotonic sequence validation, key epochs
 *   - per-process Adaptive Response Function with tabular Q-learning
 *   - RUN/SLEEP/MIGRATE/THROTTLE/YIELD actions on real Linux processes
 *   - hard-real-time bypass path (attempts SCHED_FIFO; requires privilege)
 *   - corrected S1/S2/conditioned-S3/burst-sensitive-S4 coordination index
 *     with geometric-mean aggregation
 *   - reward/directive consistency, aligned state buckets, epsilon annealing
 *   - anti-synchronization perceptual jitter sized from calibration noise
 *   - difference-reward credit assignment
 *   - multi-actuator, slower-timescale controller and Q-table consensus
 *   - instrumented observed-state reactive reference mode
 *
 * This is not a kernel scheduling class. Linux CFS/EEVDF still performs final
 * dispatch. The program runs real processes and actions on real CPU cores.
 */

#define MAX_WORKERS 64
#define MAX_CAL_SAMPLES 1024
#define ACTION_COUNT ((int)ORCHESTRA_ACTION_COUNT)
#define CPU_BUCKETS 5
#define MEMORY_BUCKETS 2
#define THERMAL_BUCKETS 3
#define STATE_COUNT (CPU_BUCKETS * MEMORY_BUCKETS * THERMAL_BUCKETS)
#define QTABLE_SIZE (STATE_COUNT * ACTION_COUNT)
#define HMAC_SIZE 32
#define MASTER_KEY_SIZE 32
#define SIGNAL_WIRE_SIZE 128
#define SIGNAL_FRAME_SIZE (SIGNAL_WIRE_SIZE + HMAC_SIZE)
#define SIGNAL_FRAME_WORD_SIZE 8u
#define SIGNAL_FRAME_WORD_COUNT (SIGNAL_FRAME_SIZE / SIGNAL_FRAME_WORD_SIZE)
#define SIGNAL_PUBLICATION_SLOT_COUNT 2u
#define SIGNAL_PUBLICATION_RETRY_LIMIT 10u
#define SIGNAL_CACHE_ALIGNMENT 64u

/* The selected implementation defaults to the generation-stamped transport.
 * The legacy byte-wise path is retained only as a reference/equivalence build
 * (`-DORCHESTRA_SIGNAL_PUBLICATION_LEGACY=1`), never as a runtime mode. */
#ifndef ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
#define ORCHESTRA_SIGNAL_PUBLICATION_LEGACY 0
#endif

#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY != 0 && ORCHESTRA_SIGNAL_PUBLICATION_LEGACY != 1
#error "ORCHESTRA_SIGNAL_PUBLICATION_LEGACY must be 0 or 1"
#endif

#define SIGNAL_TOKEN_SLOT_MASK UINT64_C(1)
#define SIGNAL_TOKEN_GENERATION_SHIFT 1u
#define SIGNAL_MAX_GENERATION (UINT64_MAX >> SIGNAL_TOKEN_GENERATION_SHIFT)
/* Gate bit 63 is a writer-exclusive lease; lower bits count bounded reader
 * pins.  The gate is separate from the read-only frame mapping. */
#define SIGNAL_SLOT_WRITER_LOCK UINT64_C(0x8000000000000000)
#define SIGNAL_SLOT_READER_MAX (SIGNAL_SLOT_WRITER_LOCK - UINT64_C(1))

#define SIGNAL_MAGIC 0x4f524348u /* "ORCH" */
#define SIGNAL_SCHEMA_VERSION 1u
#define STATE_SCHEMA_VERSION 2u
#define SIGNAL_TIER_CORE_LOCAL 1u
#define SIGNAL_SOURCE_LOCAL 0u

#define DEFAULT_WORKERS 12
#define DEFAULT_RT_EXEMPT 2
#define DEFAULT_DURATION_SEC 30
#define DEFAULT_INTERVAL_MS 100
#define DEFAULT_CALIBRATION_SEC 3
#define PREDICTOR_MODEL_VERSION 1u
#define CONTROLLER_PERIOD_TICKS 20
#define KEY_EPOCH_TICKS 100

#define RL_ALPHA 0.20
#define RL_GAMMA 0.90
#define EPSILON_START 0.30
#define EPSILON_MIN 0.02
#define EPSILON_DECAY 0.998
#define LOCAL_REWARD_MATCH 0.60
#define LOCAL_REWARD_MISMATCH -0.60
#define DIFFERENCE_WEIGHT 0.30
#define JITTER_MULTIPLIER_DEFAULT 1.50
#define CONTROLLER_BETA0 0.02
#define PREDICTION_CONFIDENCE_MIN 0.50
#define FRAME_MAX_AGE_MULTIPLIER 3u
#define MAX_FRAME_AGE_NS 10000000000ull
#define CONTROLLER_THRESHOLD 0.82
#define SLEEP_UNDERSLEEP_TOLERANCE_NS UINT64_C(1000000)
#define SLEEP_OVERSLEEP_MAX_NS UINT64_C(250000000)
#define MAX_ACTION_SLEEP_NS UINT64_C(500000000)

_Static_assert(SIGNAL_FRAME_SIZE == 160u,
               "schema-1 payload plus HMAC must remain 160 bytes");
_Static_assert(SIGNAL_FRAME_SIZE % SIGNAL_FRAME_WORD_SIZE == 0u,
               "the signal frame must split into exact machine words");
_Static_assert(SIGNAL_FRAME_WORD_COUNT == 20u,
               "the optimized transport uses exactly twenty 64-bit words");
_Static_assert(sizeof(uint64_t) == SIGNAL_FRAME_WORD_SIZE,
               "the signal transport requires 64-bit uint64_t");

/* Experimental S4_burst parameters.  They are deliberately independent of
 * the historical S4 and of Q.  The bounded history is owned by the parent
 * process, which is the only metrics writer. */
#define BURST_HISTORY_SIZE 8u
#define BURST_WINDOW_SEQUENCE_DISTANCE 4u
#define LARGE_BURST_CHANGE_FRACTION 0.50
#define LARGE_BURST_DOMINANT_FRACTION 0.75
#define BURST_PENALTY_WEIGHT 0.80
#define JUSTIFIED_BURST_FACTOR 0.20
#define OSCILLATION_PENALTY_WEIGHT 0.15
#define MAX_OSCILLATION_PENALTY 0.50

/* ------------------------------- SHA-256 ------------------------------- */

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx_t;

static const uint32_t sha256_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) |
               ((uint32_t)data[j+2] << 8) | (uint32_t)data[j+3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i-15], 7) ^ rotr32(m[i-15], 18) ^ (m[i-15] >> 3);
        uint32_t s1 = rotr32(m[i-2], 17) ^ rotr32(m[i-2], 19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }

    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2], d=ctx->state[3];
    uint32_t e=ctx->state[4], f=ctx->state[5], g=ctx->state[6], h=ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + m[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }

    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0]=0x6a09e667u; ctx->state[1]=0xbb67ae85u;
    ctx->state[2]=0x3c6ef372u; ctx->state[3]=0xa54ff53au;
    ctx->state[4]=0x510e527fu; ctx->state[5]=0x9b05688cu;
    ctx->state[6]=0x1f83d9abu; ctx->state[7]=0x5be0cd19u;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;
    ctx->data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    for (int j = 7; j >= 0; --j) ctx->data[56 + (7-j)] = (uint8_t)(ctx->bitlen >> (j*8));
    sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        hash[i]      = (uint8_t)(ctx->state[0] >> (24 - i*8));
        hash[i+4]    = (uint8_t)(ctx->state[1] >> (24 - i*8));
        hash[i+8]    = (uint8_t)(ctx->state[2] >> (24 - i*8));
        hash[i+12]   = (uint8_t)(ctx->state[3] >> (24 - i*8));
        hash[i+16]   = (uint8_t)(ctx->state[4] >> (24 - i*8));
        hash[i+20]   = (uint8_t)(ctx->state[5] >> (24 - i*8));
        hash[i+24]   = (uint8_t)(ctx->state[6] >> (24 - i*8));
        hash[i+28]   = (uint8_t)(ctx->state[7] >> (24 - i*8));
    }
}

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    uint8_t key_block[64] = {0};
    if (key_len > 64) {
        sha256_ctx_t h;
        sha256_init(&h); sha256_update(&h, key, key_len); sha256_final(&h, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }
    uint8_t ipad[64], opad[64], inner[32];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = key_block[i] ^ 0x36u;
        opad[i] = key_block[i] ^ 0x5cu;
    }
    sha256_ctx_t h;
    sha256_init(&h); sha256_update(&h, ipad, 64); sha256_update(&h, data, data_len); sha256_final(&h, inner);
    sha256_init(&h); sha256_update(&h, opad, 64); sha256_update(&h, inner, 32); sha256_final(&h, out);
}

static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

/* ------------------------------- Model -------------------------------- */

typedef enum orchestra_action_id action_t;
#define ACT_RUN ORCHESTRA_ACTION_RUN
#define ACT_SLEEP ORCHESTRA_ACTION_SLEEP
#define ACT_MIGRATE ORCHESTRA_ACTION_MIGRATE
#define ACT_THROTTLE ORCHESTRA_ACTION_THROTTLE
#define ACT_YIELD ORCHESTRA_ACTION_YIELD

_Static_assert(ACT_RUN == 0 && ACT_SLEEP == 1 && ACT_MIGRATE == 2 &&
               ACT_THROTTLE == 3 && ACT_YIELD == 4,
               "userspace action ABI must equal the C/BPF wire ABI");

static bool canonical_action_to_wire(action_t action, uint32_t *wire_action) {
    if (wire_action == NULL) return false;
    switch (action) {
        case ACT_RUN: *wire_action = ORCHESTRA_ACTION_RUN; return true;
        case ACT_SLEEP: *wire_action = ORCHESTRA_ACTION_SLEEP; return true;
        case ACT_MIGRATE: *wire_action = ORCHESTRA_ACTION_MIGRATE; return true;
        case ACT_THROTTLE: *wire_action = ORCHESTRA_ACTION_THROTTLE; return true;
        case ACT_YIELD: *wire_action = ORCHESTRA_ACTION_YIELD; return true;
        default: return false;
    }
}

typedef enum {
    ACTION_ATTEMPT_NOT_ATTEMPTED = 0,
    ACTION_ATTEMPT_SUCCEEDED = 1,
    ACTION_ATTEMPT_FAILED = 2
} action_attempt_result_t;

typedef enum {
    FALLBACK_REASON_NONE = 0,
    FALLBACK_REASON_NO_VALID_FRAME = 1,
    FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED = 2,
    /* Aggregate-only value; a worker never publishes this reason. */
    FALLBACK_REASON_MULTIPLE = 3
} fallback_reason_t;

typedef enum {
    EFFECTIVE_RESULT_NOT_ATTEMPTED = 0,
    EFFECTIVE_RESULT_SUCCESS = 1,
    EFFECTIVE_RESULT_FALLBACK = 2,
    EFFECTIVE_RESULT_INVALID_ACTION = 3,
    EFFECTIVE_RESULT_ACTION_ERROR = 4,
    EFFECTIVE_RESULT_UNDERSLEEP = 5,
    EFFECTIVE_RESULT_EXCESSIVE_OVERSLEEP = 6,
    EFFECTIVE_RESULT_MIGRATION_NOT_OBSERVED = 7,
    EFFECTIVE_RESULT_INVALID_REQUESTED_CPU = 8
} effective_action_result_t;

typedef enum {
    MODE_ORCHESTRA = 0,
    MODE_BASELINE = 1
} run_mode_t;

typedef enum {
    POLICY_MODE_TRAIN = 0,
    POLICY_MODE_ADAPT = 1,
    POLICY_MODE_EVALUATE = 2
} policy_mode_t;

_Static_assert((int)POLICY_MODE_TRAIN == (int)ORCHESTRA_POLICY_TRAIN &&
               (int)POLICY_MODE_ADAPT == (int)ORCHESTRA_POLICY_ADAPT &&
               (int)POLICY_MODE_EVALUATE == (int)ORCHESTRA_POLICY_EVALUATE,
               "policy mode ABI drift");

typedef enum {
    POLICY_LOAD_OK = 0,
    POLICY_LOAD_SKIP = 1,
    POLICY_LOAD_MAGIC = 2,
    POLICY_LOAD_VERSION = 3,
    POLICY_LOAD_SCHEMA = 4,
    POLICY_LOAD_TRUNCATED = 5,
    POLICY_LOAD_OVERSIZED = 6,
    POLICY_LOAD_OVERFLOW = 7,
    POLICY_LOAD_STATE_COUNT = 8,
    POLICY_LOAD_ACTION_COUNT = 9,
    POLICY_LOAD_DIMENSIONS = 10,
    POLICY_LOAD_NON_FINITE = 11,
    POLICY_LOAD_DIGEST = 12,
    POLICY_LOAD_TRAILING = 13,
    POLICY_LOAD_MISSING = 14
} policy_load_status_t;

typedef enum {
    POLICY_SAVE_OK = 0,
    POLICY_SAVE_WRITE_ERROR = 1,
    POLICY_SAVE_RENAME_ERROR = 2,
    POLICY_SAVE_TEMP_FAILED = 3,
    POLICY_SAVE_VALIDATION_FAILED = 4
} policy_save_status_t;

typedef enum {
    POLICY_SUPPRESS_NONE = 0,
    POLICY_SUPPRESS_MODE = 1,
    POLICY_SUPPRESS_STATE = 2,
    POLICY_SUPPRESS_FRAME_INVALID = 3,
    POLICY_SUPPRESS_CADENCE = 4,
    POLICY_SUPPRESS_DELTA = 5,
    POLICY_SUPPRESS_EXPLORATION_DISABLED = 6
} policy_suppress_reason_t;

#define POLICY_MAGIC 0x504f4c59u /* "POLY" */
#define POLICY_FORMAT_VERSION 1u
#define POLICY_SCHEMA_VERSION 1u
#define POLICY_HEADER_SIZE 64u
#define POLICY_MAX_FILE_SIZE (POLICY_HEADER_SIZE + (size_t)QTABLE_SIZE * 8u + 32u)
#define POLICY_ADAPT_CADENCE 5u
#define POLICY_ADAPT_MAX_DELTA 0.10

static const char *policy_mode_name(policy_mode_t m) {
    switch (m) {
        case POLICY_MODE_TRAIN: return "TRAIN";
        case POLICY_MODE_ADAPT: return "ADAPT";
        case POLICY_MODE_EVALUATE: return "EVALUATE";
        default: return "UNKNOWN";
    }
}

static const char *policy_load_status_name(policy_load_status_t s) {
    switch (s) {
        case POLICY_LOAD_OK: return "OK";
        case POLICY_LOAD_SKIP: return "SKIP";
        case POLICY_LOAD_MAGIC: return "MAGIC";
        case POLICY_LOAD_VERSION: return "VERSION";
        case POLICY_LOAD_SCHEMA: return "SCHEMA";
        case POLICY_LOAD_TRUNCATED: return "TRUNCATED";
        case POLICY_LOAD_OVERSIZED: return "OVERSIZED";
        case POLICY_LOAD_OVERFLOW: return "OVERFLOW";
        case POLICY_LOAD_STATE_COUNT: return "STATE_COUNT";
        case POLICY_LOAD_ACTION_COUNT: return "ACTION_COUNT";
        case POLICY_LOAD_DIMENSIONS: return "DIMENSIONS";
        case POLICY_LOAD_NON_FINITE: return "NON_FINITE";
        case POLICY_LOAD_DIGEST: return "DIGEST";
        case POLICY_LOAD_TRAILING: return "TRAILING";
        case POLICY_LOAD_MISSING: return "MISSING";
        default: return "UNKNOWN";
    }
}

static const char *policy_save_status_name(policy_save_status_t s) {
    switch (s) {
        case POLICY_SAVE_OK: return "OK";
        case POLICY_SAVE_WRITE_ERROR: return "WRITE_ERROR";
        case POLICY_SAVE_RENAME_ERROR: return "RENAME_ERROR";
        case POLICY_SAVE_TEMP_FAILED: return "TEMP_FAILED";
        case POLICY_SAVE_VALIDATION_FAILED: return "VALIDATION_FAILED";
        default: return "UNKNOWN";
    }
}

static const char *policy_suppress_reason_name(policy_suppress_reason_t r) {
    switch (r) {
        case POLICY_SUPPRESS_NONE: return "NONE";
        case POLICY_SUPPRESS_MODE: return "MODE";
        case POLICY_SUPPRESS_STATE: return "STATE";
        case POLICY_SUPPRESS_FRAME_INVALID: return "FRAME_INVALID";
        case POLICY_SUPPRESS_CADENCE: return "CADENCE";
        case POLICY_SUPPRESS_DELTA: return "DELTA";
        case POLICY_SUPPRESS_EXPLORATION_DISABLED: return "EXPLORATION_DISABLED";
        default: return "UNKNOWN";
    }
}

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t tier;
    uint32_t source_id;
    uint64_t sequence;
    uint64_t monotonic_ns;
    uint64_t max_age_ns;
    uint32_t key_epoch;
    uint32_t directive;
    uint32_t state_schema_version;
    uint32_t prediction_used;
    double cpu_now;
    double cpu_pred;
    double decision_cpu;
    double memory_pressure;
    double thermal_proxy;
    double confidence;
    double jitter_sigma;
    double switch_penalty;
    double consensus_blend;
} signal_payload_t;

/* Alignment is a performance separation only: correctness comes from the
 * acquire/release protocol and atomic words, not from a cache-line-size
 * assumption. */
typedef struct {
    _Alignas(SIGNAL_CACHE_ALIGNMENT) _Atomic uint64_t value;
} signal_cacheline_atomic_u64_t;

typedef struct {
    _Atomic uint64_t publish_version; /* odd while byte-wise writer updates */
    _Atomic uint8_t wire[SIGNAL_WIRE_SIZE];
    _Atomic uint8_t hmac[HMAC_SIZE];
} legacy_signal_publication_t;

typedef struct {
    signal_cacheline_atomic_u64_t sequence;
    _Alignas(SIGNAL_CACHE_ALIGNMENT)
        _Atomic uint64_t words[SIGNAL_FRAME_WORD_COUNT];
} generation_signal_slot_t;

typedef struct {
    signal_cacheline_atomic_u64_t publication_token;
    generation_signal_slot_t slot[SIGNAL_PUBLICATION_SLOT_COUNT];
} generation_signal_publication_t;

/* This companion mapping remains writable to readers after they make the
 * canonical signal-frame mapping read-only.  Bit 63 is a writer lease; the
 * remaining bits are reader pins.  The lease prevents slot reuse while a
 * reader copies atomic words, avoiding a C11 seqlock-style torn snapshot. */
typedef struct {
    signal_cacheline_atomic_u64_t access_state[SIGNAL_PUBLICATION_SLOT_COUNT];
} signal_reader_gates_t;

typedef struct {
    _Atomic uint64_t read_attempts;
    _Atomic uint64_t verified_reads;
    _Atomic uint64_t retries;
    _Atomic uint64_t retry_exhaustions;
    _Atomic uint64_t unstable_slot_observations;
    _Atomic uint64_t contention_safe_fallbacks;
    _Atomic uint64_t invalid_fields;
    _Atomic uint64_t invalid_schema;
    _Atomic uint64_t invalid_tier;
    _Atomic uint64_t invalid_source;
    _Atomic uint64_t invalid_directive;
    _Atomic uint64_t invalid_sequence;
    _Atomic uint64_t stale_frames;
    _Atomic uint64_t invalid_key_epochs;
    _Atomic uint64_t hmac_failures;
} signal_reader_diagnostics_t;

typedef struct {
    _Atomic uint64_t publication_attempts;
    _Atomic uint64_t publications;
    _Atomic uint64_t publication_contention;
    _Atomic uint64_t generation_exhaustions;
} signal_publisher_diagnostics_t;

#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
typedef legacy_signal_publication_t selected_signal_publication_t;
#else
typedef generation_signal_publication_t selected_signal_publication_t;
#endif

typedef struct {
    selected_signal_publication_t publication;
    signal_publisher_diagnostics_t diagnostics;
    _Atomic int consensus_lock;
    _Atomic int stop;
    _Atomic int policy_mode;
    _Atomic int controller_state;
    _Atomic int policy_update_allowed;
    _Atomic int policy_exploration_enabled;
    _Atomic int policy_consensus_enabled;
    _Atomic int kernel_bridge_enabled;
} signal_bus_t;

typedef struct {
    _Atomic uint64_t decision_version; /* per-worker seqlock: odd=update */
    _Atomic int action;
    _Atomic int previous_action;
    _Atomic int has_previous_action;
    _Atomic int proposed_action;
    _Atomic int state_index;
    _Atomic int next_state_index;
    _Atomic int cpu_target;
    _Atomic int exempt_rt;
    _Atomic int alive;
    _Atomic uint64_t heartbeat;
    _Atomic uint64_t accepted_sequence;
    _Atomic uint64_t rejected_frames;
    _Atomic int q_update_active;
    _Atomic int fallback_active;
    _Atomic int fallback_reason;
    _Atomic int action_attempted;
    _Atomic int action_attempt_result;
    _Atomic int action_attempt_success;
    _Atomic int action_errno;
    _Atomic int cpu_before_action;
    _Atomic int requested_cpu;
    _Atomic int requested_cpu_valid;
    _Atomic int cpu_after_action;
    _Atomic int migration_observed;
    _Atomic uint64_t requested_sleep_ns;
    _Atomic uint64_t observed_sleep_ns;
    _Atomic int yield_attempted;
    _Atomic int throttle_attempted;
    _Atomic int effective_action_result;
    _Atomic uint64_t action_sequence;
    _Atomic uint64_t kernel_publish_sequence;
    _Atomic int kernel_publish_status;
    _Atomic double reward;
    _Atomic uint64_t reward_sequence;
    signal_reader_diagnostics_t publication_diagnostics;
    double qtable[QTABLE_SIZE];
} worker_state_t;

_Static_assert(_Alignof(signal_cacheline_atomic_u64_t) >= SIGNAL_CACHE_ALIGNMENT,
               "publication metadata requires the requested optimization alignment");
_Static_assert(_Alignof(generation_signal_publication_t)
                   >= SIGNAL_CACHE_ALIGNMENT,
               "publication token must retain its declared alignment");

typedef struct {
    int worker_count;
    int rt_exempt_count;
    worker_state_t worker[MAX_WORKERS];
} worker_block_t;

typedef struct {
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_sample_t;

typedef struct {
    double s1, s2, s2_selected, s2_effective, s3, s3_global, s3_conditioned, s4, q;
    int counts[ACTION_COUNT];
    int eligible_workers;
    int accepted_workers;
    int fallback_workers;
    int action_attempt_count;
    int effective_action_success_count;
    int action_error_count;
    int migration_attempt_count;
    int migration_valid_requested_cpu_count;
    int migration_affinity_success_count;
    int migration_observed_success_count;
    int sleep_attempt_count;
    int sleep_effective_success_count;
    int yield_attempt_count;
    int yield_call_success_count;
    int throttle_attempt_count;
    int throttle_operation_success_count;
    uint64_t requested_sleep_ns_total;
    uint64_t observed_sleep_ns_total;
    double migration_observed_success_fraction;
    double sleep_effectiveness_fraction;
    double yield_call_success_fraction;
    double throttle_operation_success_fraction;
    double fallback_fraction;
    fallback_reason_t fallback_reason;
    /* v4 experimental, userspace-observable burst diagnostics. */
    double s4_burst;
    double change_fraction;
    double dominant_transition_fraction;
    double justified_change_fraction;
    double oscillation_penalty;
    int dominant_old_action;
    int dominant_new_action;
    int changed_eligible_workers;
    int justified_changed_workers;
    int dominant_transition_count;
    int rolling_window_burst_count;
    int rolling_window_oscillation_count;
    bool current_directive_valid;
    bool previous_directive_valid;
    bool directive_transition_valid;
    bool large_burst_event;
    bool repeated_oscillation_event;
} coord_metrics_t;

typedef struct {
    bool valid;
    bool large_burst;
    int old_action;
    int new_action;
    uint64_t sequence;
} burst_history_entry_t;

/* One parent writer, no shared-memory publication: this has no worker data
 * race and uses fixed storage independent of the worker population. */
typedef struct {
    bool previous_directive_valid;
    action_t previous_directive;
    uint64_t previous_sequence;
    burst_history_entry_t history[BURST_HISTORY_SIZE];
    size_t next_index;
} burst_tracker_t;

enum {
    CONTROL_REASON_NONE = 0u,
    CONTROL_REASON_S3 = 1u << 0,
    CONTROL_REASON_S4 = 1u << 1
};

typedef enum {
    CONTROL_STATE_NORMAL = 0,
    CONTROL_STATE_DEGRADED = 1,
    CONTROL_STATE_SATURATED = 2,
    CONTROL_STATE_DISABLED = 3,
    CONTROL_STATE_ROLLBACK = 4,
    CONTROL_STATE_RECOVERY = 5
} controller_state_t;

_Static_assert((int)CONTROL_STATE_NORMAL == (int)ORCHESTRA_CTRL_NORMAL &&
               (int)CONTROL_STATE_DEGRADED == (int)ORCHESTRA_CTRL_DEGRADED &&
               (int)CONTROL_STATE_SATURATED == (int)ORCHESTRA_CTRL_SATURATED &&
               (int)CONTROL_STATE_DISABLED == (int)ORCHESTRA_CTRL_DISABLED &&
               (int)CONTROL_STATE_ROLLBACK == (int)ORCHESTRA_CTRL_ROLLBACK &&
               (int)CONTROL_STATE_RECOVERY == (int)ORCHESTRA_CTRL_RECOVERY,
               "controller state ABI drift");

typedef enum {
    TRANSITION_REASON_NONE = 0,
    TRANSITION_REASON_DEGRADED_COORDINATION = 1,
    TRANSITION_REASON_RECOVERED_COORDINATION = 2,
    TRANSITION_REASON_SATURATED_ACTUATOR = 3,
    TRANSITION_REASON_OSCILLATION = 4,
    TRANSITION_REASON_CRITICAL_FAULT = 5,
    TRANSITION_REASON_ROLLBACK = 6,
    TRANSITION_REASON_RECOVERY_COMPLETE = 7,
    TRANSITION_REASON_RECOVERY_FAILED = 8,
    TRANSITION_REASON_DEFAULT_RECOVERY = 9,
    TRANSITION_REASON_ROLLBACK_COMPLETE = 10
} transition_reason_t;

typedef struct {
    double jitter_sigma;
    double switch_penalty;
    double consensus_blend;
} actuator_vector_t;

#define CONTROLLER_WINDOW_SIZE 10
#define CONTROLLER_OSCILLATION_WINDOW 8

typedef struct {
    double s3[CONTROLLER_WINDOW_SIZE];
    double s4[CONTROLLER_WINDOW_SIZE];
    double s4_burst[CONTROLLER_WINDOW_SIZE];
    size_t index;
    size_t count;
} controller_window_t;

typedef struct {
    actuator_vector_t history[CONTROLLER_OSCILLATION_WINDOW];
    size_t index;
    size_t count;
} oscillation_window_t;

typedef struct {
    controller_state_t state;
    controller_state_t previous_state;
    transition_reason_t transition_reason;
    uint64_t state_residence_time;
    uint64_t valid_control_history_count;
    uint64_t invalid_frame_fault_count;
    uint32_t saturation_bitmask;
    int saturation_direction;
    uint64_t saturation_persistence;
    double oscillation_score;
    int oscillation_event;
    int rollback_event;
    transition_reason_t rollback_reason;
    double recovery_progress;
    int last_known_good_available;
    actuator_vector_t requested;
    actuator_vector_t applied;
    actuator_vector_t last_known_good;
    int update_accepted;
    int update_suppressed;
    int suppression_reason;
} controller_machine_t;

typedef struct {
    bool updated;
    uint64_t step;
    uint32_t reason;
    double beta;
    bool jitter_saturated;
    bool switch_saturated;
    bool consensus_saturated;
    bool consensus_applied;
} controller_event_t;

typedef struct {
    action_t action;
    action_t previous_action;
    int state_index;
    bool alive;
    bool exempt_rt;
    bool fallback_active;
    bool has_previous_action;
    uint64_t accepted_sequence;
    bool action_attempted;
    action_attempt_result_t action_attempt_result;
    bool action_attempt_success;
    int action_errno;
    int cpu_before_action;
    int requested_cpu;
    bool requested_cpu_valid;
    int cpu_after_action;
    bool migration_observed;
    uint64_t requested_sleep_ns;
    uint64_t observed_sleep_ns;
    bool yield_attempted;
    bool throttle_attempted;
    fallback_reason_t fallback_reason;
    effective_action_result_t effective_action_result;
    uint64_t action_sequence;
} worker_snapshot_t;

typedef struct {
    int request_fd;
    int response_fd;
    pid_t pid;
    uint64_t next_sequence;
    bool active;
} kernel_bridge_session_t;

typedef struct {
    action_t selected_action;
    bool action_attempted;
    action_attempt_result_t action_attempt_result;
    int action_errno;
    int cpu_before_action;
    int requested_cpu;
    bool requested_cpu_valid;
    int cpu_after_action;
    bool migration_observed;
    uint64_t requested_sleep_ns;
    uint64_t observed_sleep_ns;
    bool yield_attempted;
    bool throttle_attempted;
    fallback_reason_t fallback_reason;
    effective_action_result_t effective_action_result;
    bool fatal_process_state;
} action_observation_t;

static volatile sig_atomic_t g_stop = 0;
static _Atomic uint64_t g_policy_generation = 0;
static _Atomic uint64_t g_policy_train_count = 0;
static _Atomic uint64_t g_policy_adapt_count = 0;
static _Atomic int g_policy_update_applied = 0;
static _Atomic int g_policy_suppression_reason = POLICY_SUPPRESS_NONE;
static _Atomic int g_policy_load_status = POLICY_LOAD_SKIP;
static _Atomic int g_policy_save_status = POLICY_SAVE_OK;
static _Atomic uint64_t g_policy_digest_prefix = 0;

static void initialize_signal_reader_diagnostics(signal_reader_diagnostics_t *diagnostics) {
    atomic_init(&diagnostics->read_attempts, 0);
    atomic_init(&diagnostics->verified_reads, 0);
    atomic_init(&diagnostics->retries, 0);
    atomic_init(&diagnostics->retry_exhaustions, 0);
    atomic_init(&diagnostics->unstable_slot_observations, 0);
    atomic_init(&diagnostics->contention_safe_fallbacks, 0);
    atomic_init(&diagnostics->invalid_fields, 0);
    atomic_init(&diagnostics->invalid_schema, 0);
    atomic_init(&diagnostics->invalid_tier, 0);
    atomic_init(&diagnostics->invalid_source, 0);
    atomic_init(&diagnostics->invalid_directive, 0);
    atomic_init(&diagnostics->invalid_sequence, 0);
    atomic_init(&diagnostics->stale_frames, 0);
    atomic_init(&diagnostics->invalid_key_epochs, 0);
    atomic_init(&diagnostics->hmac_failures, 0);
}

static void initialize_signal_publisher_diagnostics(signal_publisher_diagnostics_t *diagnostics) {
    atomic_init(&diagnostics->publication_attempts, 0);
    atomic_init(&diagnostics->publications, 0);
    atomic_init(&diagnostics->publication_contention, 0);
    atomic_init(&diagnostics->generation_exhaustions, 0);
}

static void initialize_selected_signal_publication(selected_signal_publication_t *publication) {
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    atomic_init(&publication->publish_version, 0);
    for (size_t i = 0; i < SIGNAL_WIRE_SIZE; ++i)
        atomic_init(&publication->wire[i], 0);
    for (size_t i = 0; i < HMAC_SIZE; ++i)
        atomic_init(&publication->hmac[i], 0);
#else
    atomic_init(&publication->publication_token.value, 0);
    for (size_t slot = 0; slot < SIGNAL_PUBLICATION_SLOT_COUNT; ++slot) {
        atomic_init(&publication->slot[slot].sequence.value, 0);
        for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word)
            atomic_init(&publication->slot[slot].words[word], 0);
    }
#endif
}

static void initialize_signal_reader_gates(signal_reader_gates_t *gates) {
    for (size_t slot = 0; slot < SIGNAL_PUBLICATION_SLOT_COUNT; ++slot)
        atomic_init(&gates->access_state[slot].value, 0);
}

static void initialize_shared_state(signal_bus_t *bus, signal_reader_gates_t *gates,
                                    worker_block_t *workers, int worker_count,
                                    int rt_exempt_count) {
    memset(bus, 0, sizeof(*bus));
    memset(gates, 0, sizeof(*gates));
    memset(workers, 0, sizeof(*workers));
    initialize_selected_signal_publication(&bus->publication);
    initialize_signal_reader_gates(gates);
    initialize_signal_publisher_diagnostics(&bus->diagnostics);
    atomic_init(&bus->consensus_lock, 0);
    atomic_init(&bus->stop, 0);
    atomic_init(&bus->policy_mode, POLICY_MODE_TRAIN);
    atomic_init(&bus->controller_state, CONTROL_STATE_NORMAL);
    atomic_init(&bus->policy_update_allowed, 1);
    atomic_init(&bus->policy_exploration_enabled, 1);
    atomic_init(&bus->policy_consensus_enabled, 1);
    atomic_init(&bus->kernel_bridge_enabled, 0);
    workers->worker_count = worker_count;
    workers->rt_exempt_count = rt_exempt_count;
    for (int i = 0; i < worker_count; ++i) {
        worker_state_t *worker = &workers->worker[i];
        atomic_init(&worker->decision_version, 0);
        atomic_init(&worker->action, ACT_SLEEP);
        atomic_init(&worker->previous_action, ACT_SLEEP);
        atomic_init(&worker->has_previous_action, 0);
        atomic_init(&worker->proposed_action, ACT_SLEEP);
        atomic_init(&worker->state_index, 0);
        atomic_init(&worker->next_state_index, 0);
        atomic_init(&worker->cpu_target, 0);
        atomic_init(&worker->exempt_rt, 0);
        atomic_init(&worker->alive, 0);
        atomic_init(&worker->heartbeat, 0);
        atomic_init(&worker->accepted_sequence, 0);
        atomic_init(&worker->rejected_frames, 0);
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
        atomic_init(&worker->requested_sleep_ns, 0);
        atomic_init(&worker->observed_sleep_ns, 0);
        atomic_init(&worker->yield_attempted, 0);
        atomic_init(&worker->throttle_attempted, 0);
        atomic_init(&worker->effective_action_result, EFFECTIVE_RESULT_NOT_ATTEMPTED);
        atomic_init(&worker->action_sequence, 0);
        atomic_init(&worker->kernel_publish_sequence, 0);
        atomic_init(&worker->kernel_publish_status, EXIT_FAILURE);
        atomic_init(&worker->reward, 0.0);
        atomic_init(&worker->reward_sequence, 0);
        initialize_signal_reader_diagnostics(&worker->publication_diagnostics);
    }
}

static bool selected_signal_publication_atomics_are_lock_free(
    selected_signal_publication_t *publication) {
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    if (!atomic_is_lock_free(&publication->publish_version)) return false;
    for (size_t i = 0; i < SIGNAL_WIRE_SIZE; ++i)
        if (!atomic_is_lock_free(&publication->wire[i])) return false;
    for (size_t i = 0; i < HMAC_SIZE; ++i)
        if (!atomic_is_lock_free(&publication->hmac[i])) return false;
#else
    if (!atomic_is_lock_free(&publication->publication_token.value)) return false;
    for (size_t slot = 0; slot < SIGNAL_PUBLICATION_SLOT_COUNT; ++slot) {
        if (!atomic_is_lock_free(&publication->slot[slot].sequence.value)) return false;
        for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word)
            if (!atomic_is_lock_free(&publication->slot[slot].words[word])) return false;
    }
#endif
    return true;
}

static bool shared_atomics_are_lock_free(signal_bus_t *bus,
                                         signal_reader_gates_t *gates,
                                         worker_block_t *workers) {
    bool gates_lock_free = true;
    for (size_t slot = 0; slot < SIGNAL_PUBLICATION_SLOT_COUNT; ++slot)
        gates_lock_free = gates_lock_free
                       && atomic_is_lock_free(&gates->access_state[slot].value);
    return selected_signal_publication_atomics_are_lock_free(&bus->publication)
        && gates_lock_free
        && atomic_is_lock_free(&bus->consensus_lock)
        && atomic_is_lock_free(&workers->worker[0].decision_version)
        && atomic_is_lock_free(&workers->worker[0].action)
        && atomic_is_lock_free(&workers->worker[0].reward)
        && atomic_is_lock_free(&workers->worker[0].requested_sleep_ns)
        && atomic_is_lock_free(&workers->worker[0].action_sequence)
        && atomic_is_lock_free(&bus->policy_mode)
        && atomic_is_lock_free(&bus->controller_state);
}

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void sleep_ms(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR && !g_stop) {}
}

static void sleep_until_ns(uint64_t deadline_ns) {
    struct timespec deadline = {
        .tv_sec = (time_t)(deadline_ns / 1000000000ull),
        .tv_nsec = (long)(deadline_ns % 1000000000ull)
    };
    int rc;
    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    } while (rc == EINTR && !g_stop);
}

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool read_worker_snapshot(const worker_state_t *ws,
                                 worker_snapshot_t *out);

#define BRIDGE_IO_TIMEOUT_NS UINT64_C(1000000000)

static bool bridge_wait_fd(int fd, short events, uint64_t deadline) {
    while (!g_stop) {
        uint64_t now = monotonic_ns();
        if (now >= deadline) { errno = ETIMEDOUT; return false; }
        uint64_t remaining_ms = (deadline - now + UINT64_C(999999))
                              / UINT64_C(1000000);
        int wait_ms = remaining_ms > 50u ? 50 : (int)remaining_ms;
        struct pollfd ready = { .fd = fd, .events = events };
        int result = poll(&ready, 1, wait_ms);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return false;
        if (result > 0 && (ready.revents & (events | POLLHUP | POLLERR)))
            return true;
        if (result > 0 && (ready.revents & POLLNVAL)) {
            errno = EBADF;
            return false;
        }
    }
    errno = EINTR;
    return false;
}

static bool fd_write_full(int fd, const void *buffer, size_t size) {
    const uint8_t *cursor = buffer;
    size_t offset = 0;
    uint64_t deadline = monotonic_ns() + BRIDGE_IO_TIMEOUT_NS;
    while (offset < size) {
        if (!bridge_wait_fd(fd, POLLOUT, deadline)) return false;
        ssize_t count = write(fd, cursor + offset, size - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return false;
        offset += (size_t)count;
    }
    return true;
}

static bool fd_read_full(int fd, void *buffer, size_t size) {
    uint8_t *cursor = buffer;
    size_t offset = 0;
    uint64_t deadline = monotonic_ns() + BRIDGE_IO_TIMEOUT_NS;
    while (offset < size) {
        if (!bridge_wait_fd(fd, POLLIN, deadline)) return false;
        ssize_t count = read(fd, cursor + offset, size - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return false;
        offset += (size_t)count;
    }
    return true;
}

static void kernel_bridge_close_fds(kernel_bridge_session_t *session) {
    if (session->request_fd >= 0) close(session->request_fd);
    if (session->response_fd >= 0) close(session->response_fd);
    session->request_fd = -1;
    session->response_fd = -1;
}

static void kernel_bridge_stop(kernel_bridge_session_t *session);

static bool kernel_bridge_start(kernel_bridge_session_t *session,
                                const char *bridge_path) {
    int requests[2] = {-1, -1};
    int responses[2] = {-1, -1};
    pid_t pid;

    memset(session, 0, sizeof(*session));
    session->request_fd = -1;
    session->response_fd = -1;
    if (pipe2(requests, O_CLOEXEC) != 0 || pipe2(responses, O_CLOEXEC) != 0) {
        if (requests[0] >= 0) close(requests[0]);
        if (requests[1] >= 0) close(requests[1]);
        if (responses[0] >= 0) close(responses[0]);
        if (responses[1] >= 0) close(responses[1]);
        return false;
    }
    pid = fork();
    if (pid < 0) {
        close(requests[0]); close(requests[1]);
        close(responses[0]); close(responses[1]);
        return false;
    }
    if (pid == 0) {
        if (dup2(requests[0], STDIN_FILENO) < 0
            || dup2(responses[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(requests[0]); close(requests[1]);
        close(responses[0]); close(responses[1]);
        execl(bridge_path, bridge_path, "--stream", (char *)NULL);
        _exit(127);
    }
    close(requests[0]);
    close(responses[1]);
    session->request_fd = requests[1];
    session->response_fd = responses[0];
    session->pid = pid;
    session->next_sequence = 1;
    session->active = true;
    if (fcntl(requests[1], F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(responses[0], F_SETFL, O_NONBLOCK) < 0) {
        int saved_errno = errno;
        kernel_bridge_stop(session);
        errno = saved_errno;
        return false;
    }
    return true;
}

static void kernel_bridge_stop(kernel_bridge_session_t *session) {
    int status;

    if (!session->active) return;
    kernel_bridge_close_fds(session);
    /* Closing stdin normally terminates the stream. A stopped or hung child
     * gets a bounded grace period followed by termination and kill. */
    for (unsigned int phase = 0; phase < 3u; phase++) {
        uint64_t deadline = monotonic_ns() + UINT64_C(200000000);
        if (phase == 1u) (void)kill(session->pid, SIGTERM);
        if (phase == 2u) (void)kill(session->pid, SIGKILL);
        do {
            pid_t result = waitpid(session->pid, &status, WNOHANG);
            if (result == session->pid || (result < 0 && errno == ECHILD)) {
                session->active = false;
                return;
            }
            struct timespec pause = { .tv_sec = 0, .tv_nsec = 1000000 };
            (void)nanosleep(&pause, NULL);
        } while (monotonic_ns() < deadline);
    }
    fprintf(stderr, "Bridge child did not reap within the shutdown deadline\n");
    session->active = false;
}

static int choose_migration_cpu_for_pid(pid_t pid, int worker_index) {
    cpu_set_t allowed;
    int first = -1;

    CPU_ZERO(&allowed);
    if (sched_getaffinity(pid, sizeof(allowed), &allowed) != 0) return -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET((size_t)(unsigned int)cpu, &allowed)) continue;
        if (first < 0) first = cpu;
        if ((cpu + worker_index) % 2 == 0) return cpu;
    }
    return first;
}

static uint32_t signal_permille(double value) {
    if (!isfinite(value) || value <= 0.0) return 0;
    if (value >= 1.0) return BRIDGE_SIGNAL_SCALE;
    return (uint32_t)llround(value * (double)BRIDGE_SIGNAL_SCALE);
}

static bool kernel_bridge_publish(kernel_bridge_session_t *session,
                                  worker_state_t *worker, pid_t tid,
                                  int worker_index,
                                  const worker_snapshot_t *snapshot,
                                  const signal_payload_t *signal,
                                  const coord_metrics_t *metrics,
                                  bool publish_signal,
                                  bool require_signal,
                                  uint64_t frame_max_age_ns,
                                  uint32_t controller_state,
                                  uint32_t policy_mode,
                                  uint64_t policy_generation) {
    if (session->next_sequence == 0 || session->next_sequence == UINT64_MAX)
        return false;
    struct bridge_stream_request request = {
        .magic = BRIDGE_STREAM_MAGIC,
        .abi_version = ORCHESTRA_ABI_VERSION,
        .value_size = sizeof(request),
        .sequence = session->next_sequence++,
        .target_tid = (uint32_t)tid,
        .target_cpu = ORCHESTRA_CPU_ANY,
        .controller_state = controller_state,
        .policy_mode = policy_mode,
        .policy_generation = policy_generation,
        .expiry_duration_ns = frame_max_age_ns,
        .signal_sequence = publish_signal && signal != NULL ? signal->sequence : 0,
        .signal_max_age_ns = publish_signal && signal != NULL
            ? (signal->max_age_ns > BRIDGE_SIGNAL_MAX_AGE_NS
                ? BRIDGE_SIGNAL_MAX_AGE_NS : signal->max_age_ns) : 0,
        .stream_flags = (publish_signal ? BRIDGE_STREAM_F_PUBLISH_SIGNAL : 0u)
            | (require_signal ? BRIDGE_STREAM_F_REQUIRE_SIGNAL : 0u)
    };
    struct bridge_stream_response response;
    uint32_t wire_action;
    uint64_t now = monotonic_ns();

    if (publish_signal && signal != NULL && metrics != NULL) {
        request.signal_tier = signal->tier;
        request.signal_source_id = signal->source_id;
        request.signal_key_epoch = signal->key_epoch;
        request.signal_directive = signal->directive;
        request.signal_state_schema_version = signal->state_schema_version;
        request.signal_prediction_used = signal->prediction_used;
        request.signal_confidence_permille = signal_permille(signal->confidence);
        request.signal_cpu_now_permille = signal_permille(signal->cpu_now);
        request.signal_cpu_pred_permille = signal_permille(signal->cpu_pred);
        request.signal_decision_cpu_permille = signal_permille(signal->decision_cpu);
        request.signal_memory_pressure_permille =
            signal_permille(signal->memory_pressure);
        request.signal_thermal_permille = signal_permille(signal->thermal_proxy);
        request.signal_s1_permille = signal_permille(metrics->s1);
        request.signal_s2_permille = signal_permille(metrics->s2);
        request.signal_s3_permille = signal_permille(metrics->s3);
        request.signal_s4_permille = signal_permille(metrics->s4);
        request.signal_q_permille = signal_permille(metrics->q);
    }

    if (!canonical_action_to_wire(snapshot->action, &wire_action))
        wire_action = ORCHESTRA_ACTION_RUN;
    request.action = wire_action;
    if (snapshot->action == ACT_SLEEP) {
        request.not_before_ns = now + UINT64_C(35000000);
    } else if (snapshot->action == ACT_THROTTLE) {
        request.throttle_period_ns = UINT64_C(27000000);
        request.throttle_budget_ns = UINT64_C(2000000);
    } else if (snapshot->action == ACT_MIGRATE) {
        int target = choose_migration_cpu_for_pid(tid, worker_index);
        if (target < 0) return false;
        request.target_cpu = (uint32_t)target;
    }
    if (!fd_write_full(session->request_fd, &request, sizeof(request))
        || !fd_read_full(session->response_fd, &response, sizeof(response))
        || response.magic != BRIDGE_STREAM_MAGIC
        || response.abi_version != ORCHESTRA_ABI_VERSION
        || response.value_size != sizeof(response)
        || response.sequence != request.sequence) {
        return false;
    }
    atomic_store_explicit(&worker->kernel_publish_status, response.status,
                          memory_order_relaxed);
    atomic_store_explicit(&worker->kernel_publish_sequence,
                          snapshot->accepted_sequence, memory_order_release);
    return response.status == 0;
}

static bool publish_kernel_decisions(kernel_bridge_session_t *session,
                                     worker_block_t *workers,
                                     const pid_t pids[MAX_WORKERS],
                                     uint64_t frame_sequence,
                                     uint64_t frame_max_age_ns,
                                     const signal_bus_t *bus,
                                     const signal_payload_t *signal,
                                     const coord_metrics_t *metrics) {
    bool all_ok = true;
    bool signal_published = false;

    for (int i = 0; i < workers->worker_count; ++i) {
        worker_snapshot_t snapshot;
        if (!read_worker_snapshot(&workers->worker[i], &snapshot)
            || !snapshot.alive || snapshot.exempt_rt
            || snapshot.accepted_sequence != frame_sequence)
            continue;
        if (!kernel_bridge_publish(session, &workers->worker[i], pids[i], i,
                                   &snapshot, signal, metrics,
                                   !signal_published, true,
                                   frame_max_age_ns,
                                   (uint32_t)atomic_load_explicit(
                                       &bus->controller_state,
                                       memory_order_relaxed),
                                   (uint32_t)atomic_load_explicit(
                                       &bus->policy_mode,
                                       memory_order_relaxed),
                                   atomic_load_explicit(&g_policy_generation,
                                                        memory_order_relaxed))) {
            all_ok = false;
            break;
        }
        signal_published = true;
    }
    return all_ok;
}

static double clamp01(double x) {
    if (!isfinite(x)) return 0.0;
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static const char *action_name(action_t a) {
    switch (a) {
        case ACT_RUN: return "RUN";
        case ACT_SLEEP: return "SLEEP";
        case ACT_MIGRATE: return "MIGRATE";
        case ACT_THROTTLE: return "THROTTLE";
        case ACT_YIELD: return "YIELD";
        default: return "UNKNOWN";
    }
}

static const char *fallback_reason_name(fallback_reason_t reason) {
    switch (reason) {
        case FALLBACK_REASON_NONE: return "NONE";
        case FALLBACK_REASON_NO_VALID_FRAME: return "NO_VALID_FRAME";
        case FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED: return "LAST_KNOWN_GOOD_EXPIRED";
        case FALLBACK_REASON_MULTIPLE: return "MULTIPLE";
        default: return "INVALID";
    }
}

static void record_worker_decision(worker_state_t *ws, action_t action, int state,
                                   bool fallback_active,
                                   fallback_reason_t fallback_reason,
                                   uint64_t accepted_sequence) {
    int had_previous = atomic_load_explicit(&ws->has_previous_action,
                                             memory_order_relaxed);
    action_t previous = had_previous
        ? (action_t)atomic_load_explicit(&ws->action, memory_order_relaxed)
        : action;
    atomic_fetch_add_explicit(&ws->decision_version, 1u, memory_order_acq_rel);
    atomic_store_explicit(&ws->previous_action, previous, memory_order_relaxed);
    atomic_store_explicit(&ws->has_previous_action, 1, memory_order_relaxed);
    atomic_store_explicit(&ws->proposed_action, action, memory_order_relaxed);
    atomic_store_explicit(&ws->state_index, state, memory_order_relaxed);
    atomic_store_explicit(&ws->action, action, memory_order_relaxed);
    atomic_store_explicit(&ws->fallback_active, fallback_active ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->fallback_reason, fallback_reason, memory_order_relaxed);
    atomic_store_explicit(&ws->accepted_sequence, accepted_sequence,
                          memory_order_relaxed);
    /* An outcome is attributable only after this decision's action completes. */
    atomic_store_explicit(&ws->action_attempted, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->action_attempt_result, ACTION_ATTEMPT_NOT_ATTEMPTED,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->action_attempt_success, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->action_errno, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->cpu_before_action, -1, memory_order_relaxed);
    atomic_store_explicit(&ws->requested_cpu, -1, memory_order_relaxed);
    atomic_store_explicit(&ws->requested_cpu_valid, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->cpu_after_action, -1, memory_order_relaxed);
    atomic_store_explicit(&ws->migration_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->requested_sleep_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->observed_sleep_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->yield_attempted, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->throttle_attempted, 0, memory_order_relaxed);
    atomic_store_explicit(&ws->effective_action_result, EFFECTIVE_RESULT_NOT_ATTEMPTED,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->action_sequence, 0, memory_order_relaxed);
    atomic_fetch_add_explicit(&ws->decision_version, 1u, memory_order_release);
}

static void record_worker_action_observation(worker_state_t *ws,
                                             const action_observation_t *observation,
                                             uint64_t sequence) {
    atomic_fetch_add_explicit(&ws->decision_version, 1u, memory_order_acq_rel);
    atomic_store_explicit(&ws->action_attempted, observation->action_attempted ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->action_attempt_result, observation->action_attempt_result,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->action_attempt_success,
                          observation->action_attempt_result == ACTION_ATTEMPT_SUCCEEDED ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->action_errno, observation->action_errno,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->cpu_before_action, observation->cpu_before_action,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->requested_cpu, observation->requested_cpu,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->requested_cpu_valid,
                          observation->requested_cpu_valid ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->cpu_after_action, observation->cpu_after_action,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->migration_observed,
                          observation->migration_observed ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->requested_sleep_ns, observation->requested_sleep_ns,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->observed_sleep_ns, observation->observed_sleep_ns,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->yield_attempted, observation->yield_attempted ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->throttle_attempted,
                          observation->throttle_attempted ? 1 : 0,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->fallback_reason, observation->fallback_reason,
                          memory_order_relaxed);
    atomic_store_explicit(&ws->effective_action_result,
                          observation->effective_action_result, memory_order_relaxed);
    atomic_store_explicit(&ws->action_sequence, sequence, memory_order_relaxed);
    atomic_fetch_add_explicit(&ws->decision_version, 1u, memory_order_release);
}

static bool read_worker_snapshot(const worker_state_t *ws, worker_snapshot_t *out) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        uint64_t v1 = atomic_load_explicit(&ws->decision_version, memory_order_acquire);
        if (v1 & 1u) continue;
        worker_snapshot_t snapshot = {
            .action = (action_t)atomic_load_explicit(&ws->action, memory_order_relaxed),
            .previous_action = (action_t)atomic_load_explicit(&ws->previous_action,
                                                               memory_order_relaxed),
            .has_previous_action = atomic_load_explicit(&ws->has_previous_action,
                                                        memory_order_relaxed) != 0,
            .state_index = atomic_load_explicit(&ws->state_index, memory_order_relaxed),
            .alive = atomic_load_explicit(&ws->alive, memory_order_relaxed) != 0,
            .exempt_rt = atomic_load_explicit(&ws->exempt_rt, memory_order_relaxed) != 0,
            .fallback_active = atomic_load_explicit(&ws->fallback_active,
                                                    memory_order_relaxed) != 0,
            .accepted_sequence = atomic_load_explicit(&ws->accepted_sequence,
                                                      memory_order_relaxed),
            .action_attempted = atomic_load_explicit(&ws->action_attempted,
                                                      memory_order_relaxed) != 0,
            .action_attempt_result = (action_attempt_result_t)atomic_load_explicit(
                &ws->action_attempt_result, memory_order_relaxed),
            .action_attempt_success = atomic_load_explicit(&ws->action_attempt_success,
                                                            memory_order_relaxed) != 0,
            .action_errno = atomic_load_explicit(&ws->action_errno, memory_order_relaxed),
            .cpu_before_action = atomic_load_explicit(&ws->cpu_before_action,
                                                      memory_order_relaxed),
            .requested_cpu = atomic_load_explicit(&ws->requested_cpu,
                                                  memory_order_relaxed),
            .requested_cpu_valid = atomic_load_explicit(&ws->requested_cpu_valid,
                                                        memory_order_relaxed) != 0,
            .cpu_after_action = atomic_load_explicit(&ws->cpu_after_action,
                                                     memory_order_relaxed),
            .migration_observed = atomic_load_explicit(&ws->migration_observed,
                                                        memory_order_relaxed) != 0,
            .requested_sleep_ns = atomic_load_explicit(&ws->requested_sleep_ns,
                                                        memory_order_relaxed),
            .observed_sleep_ns = atomic_load_explicit(&ws->observed_sleep_ns,
                                                       memory_order_relaxed),
            .yield_attempted = atomic_load_explicit(&ws->yield_attempted,
                                                     memory_order_relaxed) != 0,
            .throttle_attempted = atomic_load_explicit(&ws->throttle_attempted,
                                                        memory_order_relaxed) != 0,
            .fallback_reason = (fallback_reason_t)atomic_load_explicit(
                &ws->fallback_reason, memory_order_relaxed),
            .effective_action_result = (effective_action_result_t)atomic_load_explicit(
                &ws->effective_action_result, memory_order_relaxed),
            .action_sequence = atomic_load_explicit(&ws->action_sequence,
                                                    memory_order_relaxed)
        };
        atomic_thread_fence(memory_order_acquire);
        uint64_t v2 = atomic_load_explicit(&ws->decision_version, memory_order_acquire);
        if (v1 == v2 && !(v2 & 1u)) {
            *out = snapshot;
            return true;
        }
    }
    return false;
}

static bool workers_completed_sequence(const worker_block_t *workers,
                                       uint64_t sequence) {
    int eligible = 0;

    for (int i = 0; i < workers->worker_count; ++i) {
        worker_snapshot_t snapshot;

        if (!read_worker_snapshot(&workers->worker[i], &snapshot)) return false;
        if (snapshot.exempt_rt || !snapshot.alive) continue;
        eligible++;
        if (snapshot.accepted_sequence != sequence ||
            snapshot.action_sequence != sequence ||
            !snapshot.action_attempted)
            return false;
    }
    return eligible > 0;
}

static void wait_for_worker_sequence(const worker_block_t *workers,
                                     uint64_t sequence, uint64_t deadline_ns) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 500000};

    while (!g_stop && monotonic_ns() < deadline_ns) {
        if (workers_completed_sequence(workers, sequence)) return;
        (void)nanosleep(&pause, NULL);
    }
}

static bool read_cpu_sample(cpu_sample_t *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;
    cpu_sample_t parsed = {0};
    char label[16];
    int n = fscanf(f,
                   "%15s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                   label, &parsed.user, &parsed.nice, &parsed.system,
                   &parsed.idle, &parsed.iowait, &parsed.irq,
                   &parsed.softirq, &parsed.steal);
    fclose(f);
    if (n != 9 || strcmp(label, "cpu") != 0) return false;
    *s = parsed;
    return true;
}

static double cpu_usage_between(const cpu_sample_t *a, const cpu_sample_t *b) {
    uint64_t idle_a = a->idle + a->iowait;
    uint64_t idle_b = b->idle + b->iowait;
    uint64_t non_a = a->user + a->nice + a->system + a->irq + a->softirq + a->steal;
    uint64_t non_b = b->user + b->nice + b->system + b->irq + b->softirq + b->steal;
    uint64_t total_a = idle_a + non_a;
    uint64_t total_b = idle_b + non_b;
    if (total_b <= total_a || idle_b < idle_a) return 0.0;
    uint64_t total_delta = total_b - total_a;
    uint64_t idle_delta = idle_b - idle_a;
    if (idle_delta >= total_delta) return 0.0;
    return clamp01((double)(total_delta - idle_delta) / (double)total_delta);
}

static double read_memory_pressure(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0.0;
    char key[64], unit[16];
    unsigned long long value, total = 0, available = 0;
    while (fscanf(f, "%63s %llu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) total = value;
        else if (strcmp(key, "MemAvailable:") == 0) available = value;
        if (total && available) break;
    }
    fclose(f);
    return total ? clamp01(1.0 - (double)available / (double)total) : 0.0;
}

static uint64_t rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ull;
}

static double rng_uniform(uint64_t *state) {
    return (double)(rng_next(state) >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_normal(uint64_t *state) {
    double u1 = fmax(1e-12, rng_uniform(state));
    double u2 = rng_uniform(state);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static void busy_work_ms(int ms, uint64_t *rng) {
    uint64_t start = monotonic_ns();
    uint64_t duration_ns = (uint64_t)(unsigned int)ms * 1000000u;
    volatile double x = (double)((*rng & 0xffffu) + 1u);
    while ((monotonic_ns() - start) < duration_ns) {
        for (int i = 0; i < 2500; ++i) x = sin(x) * cos(x + 0.0001) + sqrt(fabs(x) + 1.0);
    }
    *rng ^= (uint64_t)(fabs(x) * 1000003.0) + 0x9e3779b97f4a7c15ull;
}

static bool action_sleep_ns(uint64_t requested_ns, uint64_t *observed_ns,
                            int *action_error) {
    if (requested_ns > MAX_ACTION_SLEEP_NS) {
        *observed_ns = 0;
        *action_error = EINVAL;
        return false;
    }
    struct timespec remaining = {
        .tv_sec = (time_t)(requested_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(requested_ns % UINT64_C(1000000000))
    };
    uint64_t start = monotonic_ns();
    int saved_error = 0;
    bool complete = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (nanosleep(&remaining, &remaining) == 0) {
            complete = true;
            break;
        }
        saved_error = errno;
        if (saved_error != EINTR || g_stop) break;
    }
    uint64_t end = monotonic_ns();
    *observed_ns = end >= start ? end - start : 0;
    *action_error = complete ? 0 : (saved_error != 0 ? saved_error : EIO);
    return complete;
}

static bool sleep_elapsed_is_undershoot(uint64_t requested_ns, uint64_t observed_ns) {
    return requested_ns > SLEEP_UNDERSLEEP_TOLERANCE_NS
        && observed_ns < requested_ns - SLEEP_UNDERSLEEP_TOLERANCE_NS;
}

static bool sleep_elapsed_is_excessive_overshoot(uint64_t requested_ns,
                                                  uint64_t observed_ns) {
    return requested_ns <= UINT64_MAX - SLEEP_OVERSLEEP_MAX_NS
        && observed_ns > requested_ns + SLEEP_OVERSLEEP_MAX_NS;
}

static effective_action_result_t classify_effective_action(
    const action_observation_t *observation) {
    if (observation->fallback_reason != FALLBACK_REASON_NONE)
        return EFFECTIVE_RESULT_FALLBACK;
    if (observation->selected_action < ACT_RUN || observation->selected_action >= ACTION_COUNT)
        return EFFECTIVE_RESULT_INVALID_ACTION;
    if (!observation->action_attempted)
        return EFFECTIVE_RESULT_NOT_ATTEMPTED;
    if (observation->selected_action == ACT_MIGRATE && !observation->requested_cpu_valid)
        return EFFECTIVE_RESULT_INVALID_REQUESTED_CPU;
    if (observation->action_attempt_result != ACTION_ATTEMPT_SUCCEEDED
        || observation->action_errno != 0)
        return EFFECTIVE_RESULT_ACTION_ERROR;
    if (observation->selected_action == ACT_SLEEP
        || observation->selected_action == ACT_THROTTLE) {
        if (sleep_elapsed_is_undershoot(observation->requested_sleep_ns,
                                        observation->observed_sleep_ns))
            return EFFECTIVE_RESULT_UNDERSLEEP;
        if (sleep_elapsed_is_excessive_overshoot(observation->requested_sleep_ns,
                                                 observation->observed_sleep_ns))
            return EFFECTIVE_RESULT_EXCESSIVE_OVERSLEEP;
    }
    if (observation->selected_action == ACT_MIGRATE && !observation->migration_observed)
        return EFFECTIVE_RESULT_MIGRATION_NOT_OBSERVED;
    return EFFECTIVE_RESULT_SUCCESS;
}

static int choose_migration_cpu(int current_cpu, int worker_index,
                                bool *requested_cpu_valid) {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        *requested_cpu_valid = false;
        return -1;
    }
    int first_allowed = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET((size_t)(unsigned int)cpu, &set)) continue;
        if (first_allowed < 0) first_allowed = cpu;
        if (current_cpu >= 0 && cpu != current_cpu
            && ((cpu + worker_index) % 2 == 0 || first_allowed == current_cpu)) {
            *requested_cpu_valid = true;
            return cpu;
        }
    }
    *requested_cpu_valid = first_allowed >= 0;
    return first_allowed;
}

static action_observation_t perform_action(action_t action, int worker_index,
                                            uint64_t *rng,
                                            fallback_reason_t fallback_reason) {
    action_observation_t observation = {
        .selected_action = action,
        .action_attempted = true,
        .action_attempt_result = ACTION_ATTEMPT_SUCCEEDED,
        .action_errno = 0,
        .cpu_before_action = sched_getcpu(),
        .requested_cpu = -1,
        .requested_cpu_valid = false,
        .cpu_after_action = -1,
        .migration_observed = false,
        .requested_sleep_ns = 0,
        .observed_sleep_ns = 0,
        .yield_attempted = false,
        .throttle_attempted = false,
        .fallback_reason = fallback_reason,
        .effective_action_result = EFFECTIVE_RESULT_NOT_ATTEMPTED
    };
    int sleep_error = 0;
    switch (action) {
        case ACT_RUN:
            busy_work_ms(10, rng);
            break;
        case ACT_SLEEP:
            observation.requested_sleep_ns = UINT64_C(35000000);
            if (!action_sleep_ns(observation.requested_sleep_ns,
                                 &observation.observed_sleep_ns, &sleep_error)) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = sleep_error;
            }
            break;
        case ACT_MIGRATE: {
            cpu_set_t original_set;
            bool affinity_changed = false;

            if (sched_getaffinity(0, sizeof(original_set), &original_set) != 0) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = errno;
                break;
            }
            observation.requested_cpu = choose_migration_cpu(observation.cpu_before_action,
                                                              worker_index,
                                                              &observation.requested_cpu_valid);
            if (!observation.requested_cpu_valid) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = EINVAL;
                break;
            }
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET((size_t)(unsigned int)observation.requested_cpu, &set);
            if (sched_setaffinity(0, sizeof(set), &set) != 0) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = errno;
                break;
            }
            affinity_changed = true;
            busy_work_ms(5, rng);
            if (sched_yield() != 0) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = errno;
            }
            observation.cpu_after_action = sched_getcpu();
            observation.migration_observed = observation.cpu_after_action
                == observation.requested_cpu;
            /* MIGRATE is one decision, not a permanent affinity policy. */
            if (affinity_changed) {
                int restore_error = 0;
                bool restored = false;
                for (int attempt = 0; attempt < 3; ++attempt) {
                    if (sched_setaffinity(0, sizeof(original_set),
                                          &original_set) == 0) {
                        restored = true;
                        break;
                    }
                    restore_error = errno;
                    if (restore_error != EINTR)
                        break;
                }
                if (!restored) {
                    /* Continuing would silently turn one MIGRATE decision
                     * into permanent affinity policy.  Stop the run after
                     * recording the failed action; process exit removes the
                     * narrowed affinity state. */
                    observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                    observation.action_errno = restore_error != 0 ?
                        restore_error : EIO;
                    observation.fatal_process_state = true;
                }
            }
            break;
        }
        case ACT_THROTTLE:
            observation.throttle_attempted = true;
            busy_work_ms(2, rng);
            observation.requested_sleep_ns = UINT64_C(25000000);
            if (!action_sleep_ns(observation.requested_sleep_ns,
                                 &observation.observed_sleep_ns, &sleep_error)) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = sleep_error;
            }
            break;
        case ACT_YIELD:
            busy_work_ms(3, rng);
            observation.yield_attempted = true;
            if (sched_yield() != 0) {
                observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
                observation.action_errno = errno;
            }
            observation.requested_sleep_ns = UINT64_C(4000000);
            (void)action_sleep_ns(observation.requested_sleep_ns,
                                  &observation.observed_sleep_ns, &sleep_error);
            break;
        default:
            observation.action_attempt_result = ACTION_ATTEMPT_FAILED;
            observation.action_errno = EINVAL;
            break;
    }
    observation.cpu_after_action = observation.cpu_after_action >= 0
        ? observation.cpu_after_action : sched_getcpu();
    observation.effective_action_result = classify_effective_action(&observation);
    return observation;
}

static action_t directive_from_signal(double cpu_pred, double memory, double thermal) {
    if (thermal > 0.90 || cpu_pred > 0.94) return ACT_THROTTLE;
    if (cpu_pred > 0.82 || memory > 0.90) return ACT_MIGRATE;
    if (cpu_pred > 0.68) return ACT_YIELD;
    if (cpu_pred < 0.20) return ACT_SLEEP;
    return ACT_RUN;
}

static int cpu_bucket(double cpu) {
    if (cpu < 0.20) return 0;
    if (cpu <= 0.68) return 1;
    if (cpu <= 0.82) return 2;
    if (cpu <= 0.94) return 3;
    return 4;
}

static int memory_bucket(double memory) {
    return memory > 0.90 ? 1 : 0;
}

static int thermal_bucket(double thermal) {
    if (thermal < 0.70) return 0;
    if (thermal <= 0.90) return 1;
    return 2;
}

static int state_index(double cpu, double memory, double thermal) {
    int cpu_state = cpu_bucket(cpu);
    int memory_state = memory_bucket(memory);
    return (cpu_state * MEMORY_BUCKETS + memory_state) * THERMAL_BUCKETS
         + thermal_bucket(thermal);
}

static int qindex(int state, int action) { return state * ACTION_COUNT + action; }

static double epsilon_for_sequence(uint64_t sequence) {
    double eps = EPSILON_START * pow(EPSILON_DECAY, (double)sequence);
    return eps < EPSILON_MIN ? EPSILON_MIN : eps;
}

static double decision_cpu_value(run_mode_t mode, double cpu_now,
                                 double cpu_pred, double confidence,
                                 uint32_t *prediction_used) {
    bool use_prediction = mode == MODE_ORCHESTRA
                       && isfinite(confidence)
                       && confidence >= PREDICTION_CONFIDENCE_MIN;
    *prediction_used = use_prediction ? 1u : 0u;
    return clamp01(use_prediction ? cpu_pred : cpu_now);
}

static double normalized_geometric_mean(const double *factors, size_t count) {
    if (count == 0) return 1.0;
    double log_sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double factor = clamp01(factors[i]);
        if (factor <= 0.0) return 0.0;
        log_sum += log(factor);
    }
    return clamp01(exp(log_sum / (double)count));
}

static action_t greedy_action(const double qtable[QTABLE_SIZE], int state, uint64_t *rng) {
    double best = qtable[qindex(state, 0)];
    int candidates[ACTION_COUNT];
    int n = 1;
    candidates[0] = 0;
    for (int a = 1; a < ACTION_COUNT; ++a) {
        double q = qtable[qindex(state, a)];
        if (q > best + 1e-12) { best = q; candidates[0] = a; n = 1; }
        else if (fabs(q - best) <= 1e-12) candidates[n++] = a;
    }
    return (action_t)candidates[(int)(rng_uniform(rng) * n) % n];
}

static action_t epsilon_greedy(const double qtable[QTABLE_SIZE], int state,
                               double epsilon, uint64_t *rng) {
    if (rng_uniform(rng) < epsilon) return (action_t)((int)(rng_uniform(rng) * ACTION_COUNT) % ACTION_COUNT);
    return greedy_action(qtable, state, rng);
}

static void derive_epoch_key(const uint8_t master[MASTER_KEY_SIZE], uint32_t epoch,
                             uint8_t key[HMAC_SIZE]) {
    uint8_t msg[4] = {
        (uint8_t)(epoch >> 24), (uint8_t)(epoch >> 16),
        (uint8_t)(epoch >> 8), (uint8_t)epoch
    };
    hmac_sha256(master, MASTER_KEY_SIZE, msg, sizeof(msg), key);
}

static void wire_put_u32(uint8_t **cursor, uint32_t value) {
    (*cursor)[0] = (uint8_t)(value >> 24);
    (*cursor)[1] = (uint8_t)(value >> 16);
    (*cursor)[2] = (uint8_t)(value >> 8);
    (*cursor)[3] = (uint8_t)value;
    *cursor += 4;
}

static void wire_put_u64(uint8_t **cursor, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *(*cursor)++ = (uint8_t)(value >> shift);
    }
}

static void wire_put_double(uint8_t **cursor, double value) {
    uint64_t bits;
    _Static_assert(sizeof(bits) == sizeof(value), "binary64 storage required");
    memcpy(&bits, &value, sizeof(bits));
    wire_put_u64(cursor, bits);
}

static void serialize_payload(const signal_payload_t *payload,
                              uint8_t wire[SIGNAL_WIRE_SIZE]) {
    uint8_t *cursor = wire;
    wire_put_u32(&cursor, payload->magic);
    wire_put_u32(&cursor, payload->schema_version);
    wire_put_u32(&cursor, payload->tier);
    wire_put_u32(&cursor, payload->source_id);
    wire_put_u64(&cursor, payload->sequence);
    wire_put_u64(&cursor, payload->monotonic_ns);
    wire_put_u64(&cursor, payload->max_age_ns);
    wire_put_u32(&cursor, payload->key_epoch);
    wire_put_u32(&cursor, payload->directive);
    wire_put_u32(&cursor, payload->state_schema_version);
    wire_put_u32(&cursor, payload->prediction_used);
    wire_put_double(&cursor, payload->cpu_now);
    wire_put_double(&cursor, payload->cpu_pred);
    wire_put_double(&cursor, payload->decision_cpu);
    wire_put_double(&cursor, payload->memory_pressure);
    wire_put_double(&cursor, payload->thermal_proxy);
    wire_put_double(&cursor, payload->confidence);
    wire_put_double(&cursor, payload->jitter_sigma);
    wire_put_double(&cursor, payload->switch_penalty);
    wire_put_double(&cursor, payload->consensus_blend);
    if ((size_t)(cursor - wire) != SIGNAL_WIRE_SIZE) abort();
}

static uint32_t wire_get_u32(const uint8_t **cursor) {
    uint32_t value = ((uint32_t)(*cursor)[0] << 24)
                   | ((uint32_t)(*cursor)[1] << 16)
                   | ((uint32_t)(*cursor)[2] << 8)
                   | (uint32_t)(*cursor)[3];
    *cursor += 4;
    return value;
}

static uint64_t wire_get_u64(const uint8_t **cursor) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | (*cursor)[i];
    *cursor += 8;
    return value;
}

static double wire_get_double(const uint8_t **cursor) {
    uint64_t bits = wire_get_u64(cursor);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void deserialize_payload(const uint8_t wire[SIGNAL_WIRE_SIZE],
                                signal_payload_t *payload) {
    const uint8_t *cursor = wire;
    memset(payload, 0, sizeof(*payload));
    payload->magic = wire_get_u32(&cursor);
    payload->schema_version = wire_get_u32(&cursor);
    payload->tier = wire_get_u32(&cursor);
    payload->source_id = wire_get_u32(&cursor);
    payload->sequence = wire_get_u64(&cursor);
    payload->monotonic_ns = wire_get_u64(&cursor);
    payload->max_age_ns = wire_get_u64(&cursor);
    payload->key_epoch = wire_get_u32(&cursor);
    payload->directive = wire_get_u32(&cursor);
    payload->state_schema_version = wire_get_u32(&cursor);
    payload->prediction_used = wire_get_u32(&cursor);
    payload->cpu_now = wire_get_double(&cursor);
    payload->cpu_pred = wire_get_double(&cursor);
    payload->decision_cpu = wire_get_double(&cursor);
    payload->memory_pressure = wire_get_double(&cursor);
    payload->thermal_proxy = wire_get_double(&cursor);
    payload->confidence = wire_get_double(&cursor);
    payload->jitter_sigma = wire_get_double(&cursor);
    payload->switch_penalty = wire_get_double(&cursor);
    payload->consensus_blend = wire_get_double(&cursor);
    if ((size_t)(cursor - wire) != SIGNAL_WIRE_SIZE) abort();
}

static void sign_payload(const signal_payload_t *payload,
                         const uint8_t master[MASTER_KEY_SIZE], uint8_t out[HMAC_SIZE]) {
    uint8_t epoch_key[HMAC_SIZE];
    uint8_t wire[SIGNAL_WIRE_SIZE];
    derive_epoch_key(master, payload->key_epoch, epoch_key);
    serialize_payload(payload, wire);
    hmac_sha256(epoch_key, sizeof(epoch_key), wire, sizeof(wire), out);
    explicit_bzero(epoch_key, sizeof(epoch_key));
}

typedef enum {
    SIGNAL_VALIDATION_OK = 0,
    SIGNAL_VALIDATION_FIELDS,
    SIGNAL_VALIDATION_SCHEMA,
    SIGNAL_VALIDATION_DIRECTIVE,
    SIGNAL_VALIDATION_TIER,
    SIGNAL_VALIDATION_SOURCE,
    SIGNAL_VALIDATION_SEQUENCE,
    SIGNAL_VALIDATION_STALE,
    SIGNAL_VALIDATION_KEY_EPOCH,
    SIGNAL_VALIDATION_HMAC
} signal_validation_result_t;

typedef enum {
    SIGNAL_SNAPSHOT_EMPTY = 0,
    SIGNAL_SNAPSHOT_COPIED = 1,
    SIGNAL_SNAPSHOT_UNSTABLE = -1
} signal_snapshot_result_t;

typedef enum {
    SIGNAL_PUBLISH_OK = 0,
    SIGNAL_PUBLISH_CONTENDED = 1,
    SIGNAL_PUBLISH_GENERATION_EXHAUSTED = 2
} signal_publish_result_t;

typedef enum {
    SIGNAL_HOOK_WRITER_LOCKED = 0,
    SIGNAL_HOOK_WRITER_ODD,
    SIGNAL_HOOK_WRITER_WORD,
    SIGNAL_HOOK_WRITER_COMPLETE,
    SIGNAL_HOOK_READER_TOKEN,
    SIGNAL_HOOK_READER_PINNED,
    SIGNAL_HOOK_READER_SEQUENCE,
    SIGNAL_HOOK_READER_WORD,
    SIGNAL_HOOK_READER_FINAL_CHECK
} signal_publication_hook_stage_t;

#ifdef ORCHESTRA_UNIT_TEST
typedef void (*signal_publication_test_hook_t)(signal_publication_hook_stage_t stage);
static signal_publication_test_hook_t g_signal_publication_test_hook = NULL;

static void signal_publication_test_hook(signal_publication_hook_stage_t stage) {
    if (g_signal_publication_test_hook != NULL)
        g_signal_publication_test_hook(stage);
}
#else
static void signal_publication_test_hook(signal_publication_hook_stage_t stage) {
    (void)stage;
}
#endif

static void signal_diagnostic_increment(_Atomic uint64_t *counter) {
    if (counter == NULL) return;
    uint64_t observed = atomic_load_explicit(counter, memory_order_relaxed);
    for (unsigned int attempt = 0; attempt < 4u; ++attempt) {
        if (observed == UINT64_MAX) return;
        uint64_t desired = observed + UINT64_C(1);
        if (atomic_compare_exchange_weak_explicit(counter, &observed, desired,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static signal_validation_result_t payload_field_validation(
    const signal_payload_t *payload) {
    const double normalized[] = {
        payload->cpu_now, payload->cpu_pred, payload->decision_cpu,
        payload->memory_pressure, payload->thermal_proxy, payload->confidence
    };
    if (payload->magic != SIGNAL_MAGIC
        || payload->schema_version != SIGNAL_SCHEMA_VERSION
        || payload->state_schema_version != STATE_SCHEMA_VERSION) {
        return SIGNAL_VALIDATION_SCHEMA;
    }
    if (payload->directive >= ACTION_COUNT)
        return SIGNAL_VALIDATION_DIRECTIVE;
    if (payload->prediction_used > 1u
        || payload->max_age_ns == 0
        || payload->max_age_ns > MAX_FRAME_AGE_NS) {
        return SIGNAL_VALIDATION_FIELDS;
    }
    for (size_t i = 0; i < sizeof(normalized) / sizeof(normalized[0]); ++i) {
        if (!isfinite(normalized[i]) || normalized[i] < 0.0 || normalized[i] > 1.0)
            return SIGNAL_VALIDATION_FIELDS;
    }
    if (!(isfinite(payload->jitter_sigma)
        && payload->jitter_sigma >= 0.0 && payload->jitter_sigma <= 0.20
        && isfinite(payload->switch_penalty)
        && payload->switch_penalty >= 0.0 && payload->switch_penalty <= 0.30
        && isfinite(payload->consensus_blend)
        && payload->consensus_blend >= 0.0 && payload->consensus_blend <= 0.15)) {
        return SIGNAL_VALIDATION_FIELDS;
    }
    return SIGNAL_VALIDATION_OK;
}

static bool payload_fields_valid(const signal_payload_t *payload) {
    return payload_field_validation(payload) == SIGNAL_VALIDATION_OK;
}

/* Canonical bytes are never represented by a native C structure in shared
 * storage.  These explicit shifts retain the established big-endian payload
 * and the separate HMAC byte sequence while avoiding aliasing or alignment
 * assumptions for the atomic 64-bit words. */
#if !ORCHESTRA_SIGNAL_PUBLICATION_LEGACY || defined(ORCHESTRA_UNIT_TEST)
static void signal_frame_bytes_to_words(
    const uint8_t bytes[SIGNAL_FRAME_SIZE], uint64_t words[SIGNAL_FRAME_WORD_COUNT]) {
    for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word) {
        uint64_t value = 0;
        for (size_t byte = 0; byte < SIGNAL_FRAME_WORD_SIZE; ++byte) {
            value = (value << 8u) | (uint64_t)bytes[word * SIGNAL_FRAME_WORD_SIZE + byte];
        }
        words[word] = value;
    }
}

static void signal_frame_words_to_bytes(
    const uint64_t words[SIGNAL_FRAME_WORD_COUNT], uint8_t bytes[SIGNAL_FRAME_SIZE]) {
    for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word) {
        for (size_t byte = 0; byte < SIGNAL_FRAME_WORD_SIZE; ++byte) {
            unsigned int shift = (unsigned int)((SIGNAL_FRAME_WORD_SIZE - UINT64_C(1) - byte)
                                                * UINT64_C(8));
            bytes[word * SIGNAL_FRAME_WORD_SIZE + byte] =
                (uint8_t)(words[word] >> shift);
        }
    }
}
#endif

static void prepare_canonical_signal_frame(
    const signal_payload_t *payload, const uint8_t master[MASTER_KEY_SIZE], bool tamper,
    uint8_t bytes[SIGNAL_FRAME_SIZE]) {
    signal_payload_t published = *payload;
    sign_payload(payload, master, bytes + SIGNAL_WIRE_SIZE);
    if (tamper) {
        published.cpu_pred = published.cpu_pred <= 0.50
                           ? published.cpu_pred + 0.33
                           : published.cpu_pred - 0.33;
    }
    serialize_payload(&published, bytes);
}

/* Slot-gate helpers are not needed by a production legacy-reference build;
 * unit builds retain both paths for exact equivalence coverage. */
#if !ORCHESTRA_SIGNAL_PUBLICATION_LEGACY || defined(ORCHESTRA_UNIT_TEST)
static bool signal_gate_try_pin_reader(signal_reader_gates_t *gates, size_t slot) {
    _Atomic uint64_t *access = &gates->access_state[slot].value;
    uint64_t observed = atomic_load_explicit(access, memory_order_acquire);
    for (unsigned int attempt = 0; attempt < 4u; ++attempt) {
        if ((observed & SIGNAL_SLOT_WRITER_LOCK) != 0u
            || observed >= SIGNAL_SLOT_READER_MAX) {
            return false;
        }
        uint64_t desired = observed + UINT64_C(1);
        if (atomic_compare_exchange_weak_explicit(access, &observed, desired,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

static void signal_gate_unpin_reader(signal_reader_gates_t *gates, size_t slot) {
    /* The reader acquired one low-bit pin before this matching release.  The
     * writer lease cannot coexist with a positive pin count, so this bounded
     * ownership invariant makes an underflow impossible in a correct caller.
     * fetch_sub avoids a retry loop while other readers leave concurrently. */
    uint64_t prior = atomic_fetch_sub_explicit(&gates->access_state[slot].value,
                                               UINT64_C(1), memory_order_release);
    if (prior == 0u || (prior & SIGNAL_SLOT_WRITER_LOCK) != 0u)
        abort();
}

static bool signal_gate_try_lock_writer(signal_reader_gates_t *gates, size_t slot) {
    uint64_t expected = 0;
    return atomic_compare_exchange_strong_explicit(
        &gates->access_state[slot].value, &expected, SIGNAL_SLOT_WRITER_LOCK,
        memory_order_acq_rel, memory_order_acquire);
}

static void signal_gate_unlock_writer(signal_reader_gates_t *gates, size_t slot) {
    atomic_store_explicit(&gates->access_state[slot].value, 0, memory_order_release);
}

static signal_publish_result_t generation_publish_canonical_frame(
    generation_signal_publication_t *publication, signal_reader_gates_t *gates,
    const uint8_t bytes[SIGNAL_FRAME_SIZE]) {
    uint64_t token = atomic_load_explicit(&publication->publication_token.value,
                                          memory_order_acquire);
    uint64_t generation = token >> SIGNAL_TOKEN_GENERATION_SHIFT;
    if (generation >= SIGNAL_MAX_GENERATION)
        return SIGNAL_PUBLISH_GENERATION_EXHAUSTED;

    size_t slot = token == 0u ? 0u
                              : (size_t)(UINT64_C(1) - (token & SIGNAL_TOKEN_SLOT_MASK));
    uint64_t next_generation = generation + UINT64_C(1);
    uint64_t completed_sequence = next_generation << SIGNAL_TOKEN_GENERATION_SHIFT;
    uint64_t next_token = completed_sequence | (uint64_t)slot;
    if (!signal_gate_try_lock_writer(gates, slot))
        return SIGNAL_PUBLISH_CONTENDED;

    signal_publication_test_hook(SIGNAL_HOOK_WRITER_LOCKED);
    generation_signal_slot_t *target = &publication->slot[slot];
    atomic_store_explicit(&target->sequence.value, completed_sequence | UINT64_C(1),
                          memory_order_release);
    signal_publication_test_hook(SIGNAL_HOOK_WRITER_ODD);

    uint64_t words[SIGNAL_FRAME_WORD_COUNT];
    signal_frame_bytes_to_words(bytes, words);
    for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word) {
        atomic_store_explicit(&target->words[word], words[word], memory_order_relaxed);
        signal_publication_test_hook(SIGNAL_HOOK_WRITER_WORD);
    }
    atomic_store_explicit(&target->sequence.value, completed_sequence,
                          memory_order_release);
    atomic_store_explicit(&publication->publication_token.value, next_token,
                          memory_order_release);
    signal_publication_test_hook(SIGNAL_HOOK_WRITER_COMPLETE);
    signal_gate_unlock_writer(gates, slot);
    return SIGNAL_PUBLISH_OK;
}

static signal_snapshot_result_t generation_copy_canonical_frame(
    const generation_signal_publication_t *publication, signal_reader_gates_t *gates,
    uint8_t bytes[SIGNAL_FRAME_SIZE], signal_reader_diagnostics_t *diagnostics) {
    signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->read_attempts);
    for (unsigned int attempt = 0; attempt < SIGNAL_PUBLICATION_RETRY_LIMIT; ++attempt) {
        uint64_t token_first = atomic_load_explicit(&publication->publication_token.value,
                                                    memory_order_acquire);
        signal_publication_test_hook(SIGNAL_HOOK_READER_TOKEN);
        if (token_first == 0u)
            return SIGNAL_SNAPSHOT_EMPTY;
        uint64_t generation = token_first >> SIGNAL_TOKEN_GENERATION_SHIFT;
        size_t slot = (size_t)(token_first & SIGNAL_TOKEN_SLOT_MASK);
        uint64_t expected_sequence = generation << SIGNAL_TOKEN_GENERATION_SHIFT;
        if (generation == 0u || !signal_gate_try_pin_reader(gates, slot)) {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->unstable_slot_observations);
            signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
            continue;
        }
        signal_publication_test_hook(SIGNAL_HOOK_READER_PINNED);
        uint64_t token_pinned = atomic_load_explicit(&publication->publication_token.value,
                                                     memory_order_acquire);
        if (token_pinned != token_first) {
            signal_gate_unpin_reader(gates, slot);
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->unstable_slot_observations);
            signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
            continue;
        }
        const generation_signal_slot_t *source = &publication->slot[slot];
        uint64_t sequence_first = atomic_load_explicit(&source->sequence.value,
                                                       memory_order_acquire);
        signal_publication_test_hook(SIGNAL_HOOK_READER_SEQUENCE);
        if (sequence_first != expected_sequence || (sequence_first & UINT64_C(1)) != 0u) {
            signal_gate_unpin_reader(gates, slot);
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->unstable_slot_observations);
            signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
            continue;
        }
        uint64_t words[SIGNAL_FRAME_WORD_COUNT];
        for (size_t word = 0; word < SIGNAL_FRAME_WORD_COUNT; ++word) {
            words[word] = atomic_load_explicit(&source->words[word], memory_order_relaxed);
            signal_publication_test_hook(SIGNAL_HOOK_READER_WORD);
        }
        signal_publication_test_hook(SIGNAL_HOOK_READER_FINAL_CHECK);
        uint64_t sequence_second = atomic_load_explicit(&source->sequence.value,
                                                        memory_order_acquire);
        uint64_t token_final = atomic_load_explicit(&publication->publication_token.value,
                                                    memory_order_acquire);
        signal_gate_unpin_reader(gates, slot);
        if (token_first != token_pinned || token_first != token_final
            || sequence_first != sequence_second
            || sequence_second != expected_sequence
            || (sequence_second & UINT64_C(1)) != 0u) {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->unstable_slot_observations);
            signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
            continue;
        }
        signal_frame_words_to_bytes(words, bytes);
        return SIGNAL_SNAPSHOT_COPIED;
    }
    signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retry_exhaustions);
    return SIGNAL_SNAPSHOT_UNSTABLE;
}
#endif

#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY || defined(ORCHESTRA_UNIT_TEST)
static signal_publish_result_t legacy_publish_canonical_frame(
    legacy_signal_publication_t *publication,
    const uint8_t bytes[SIGNAL_FRAME_SIZE]) {
    uint64_t current = atomic_load_explicit(&publication->publish_version,
                                            memory_order_acquire);
    if (current >= UINT64_MAX - UINT64_C(1))
        return SIGNAL_PUBLISH_GENERATION_EXHAUSTED;
    atomic_fetch_add_explicit(&publication->publish_version, UINT64_C(1),
                              memory_order_acq_rel);
    signal_publication_test_hook(SIGNAL_HOOK_WRITER_ODD);
    for (size_t i = 0; i < SIGNAL_WIRE_SIZE; ++i) {
        atomic_store_explicit(&publication->wire[i], bytes[i], memory_order_relaxed);
        signal_publication_test_hook(SIGNAL_HOOK_WRITER_WORD);
    }
    for (size_t i = 0; i < HMAC_SIZE; ++i) {
        atomic_store_explicit(&publication->hmac[i], bytes[SIGNAL_WIRE_SIZE + i],
                              memory_order_relaxed);
        signal_publication_test_hook(SIGNAL_HOOK_WRITER_WORD);
    }
    atomic_fetch_add_explicit(&publication->publish_version, UINT64_C(1),
                              memory_order_release);
    signal_publication_test_hook(SIGNAL_HOOK_WRITER_COMPLETE);
    return SIGNAL_PUBLISH_OK;
}

static signal_snapshot_result_t legacy_copy_canonical_frame(
    const legacy_signal_publication_t *publication, uint8_t bytes[SIGNAL_FRAME_SIZE],
    signal_reader_diagnostics_t *diagnostics) {
    signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->read_attempts);
    for (unsigned int attempt = 0; attempt < SIGNAL_PUBLICATION_RETRY_LIMIT; ++attempt) {
        uint64_t first = atomic_load_explicit(&publication->publish_version,
                                              memory_order_acquire);
        signal_publication_test_hook(SIGNAL_HOOK_READER_TOKEN);
        if ((first & UINT64_C(1)) != 0u) {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->unstable_slot_observations);
            signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
            continue;
        }
        for (size_t i = 0; i < SIGNAL_WIRE_SIZE; ++i) {
            bytes[i] = atomic_load_explicit(&publication->wire[i], memory_order_relaxed);
            signal_publication_test_hook(SIGNAL_HOOK_READER_WORD);
        }
        for (size_t i = 0; i < HMAC_SIZE; ++i) {
            bytes[SIGNAL_WIRE_SIZE + i] = atomic_load_explicit(&publication->hmac[i],
                                                                memory_order_relaxed);
            signal_publication_test_hook(SIGNAL_HOOK_READER_WORD);
        }
        signal_publication_test_hook(SIGNAL_HOOK_READER_FINAL_CHECK);
        atomic_thread_fence(memory_order_acquire);
        uint64_t second = atomic_load_explicit(&publication->publish_version,
                                               memory_order_acquire);
        if (first == second && (second & UINT64_C(1)) == 0u)
            return SIGNAL_SNAPSHOT_COPIED;
        signal_diagnostic_increment(diagnostics == NULL
                                    ? NULL : &diagnostics->unstable_slot_observations);
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retries);
    }
    signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->retry_exhaustions);
    return SIGNAL_SNAPSHOT_UNSTABLE;
}
#endif

static signal_publish_result_t selected_publish_canonical_frame(
    signal_bus_t *bus, signal_reader_gates_t *gates,
    const uint8_t bytes[SIGNAL_FRAME_SIZE]) {
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    (void)gates;
    return legacy_publish_canonical_frame(&bus->publication, bytes);
#else
    return generation_publish_canonical_frame(&bus->publication, gates, bytes);
#endif
}

static signal_snapshot_result_t selected_copy_canonical_frame(
    const signal_bus_t *bus, signal_reader_gates_t *gates,
    uint8_t bytes[SIGNAL_FRAME_SIZE], signal_reader_diagnostics_t *diagnostics) {
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    (void)gates;
    return legacy_copy_canonical_frame(&bus->publication, bytes, diagnostics);
#else
    return generation_copy_canonical_frame(&bus->publication, gates, bytes, diagnostics);
#endif
}

typedef enum {
    FRAME_UNSTABLE = -2,
    FRAME_INVALID = -1,
    FRAME_NO_NEW = 0,
    FRAME_VALID = 1
} frame_read_result_t;

static frame_read_result_t validate_copied_signal_frame(
    const uint8_t bytes[SIGNAL_FRAME_SIZE], const uint8_t master[MASTER_KEY_SIZE],
    uint64_t last_sequence, signal_payload_t *out,
    signal_reader_diagnostics_t *diagnostics) {
    signal_payload_t payload;
    uint8_t expected[HMAC_SIZE];
    deserialize_payload(bytes, &payload);
    signal_validation_result_t field_result = payload_field_validation(&payload);
    if (field_result != SIGNAL_VALIDATION_OK) {
        *out = payload;
        if (field_result == SIGNAL_VALIDATION_SCHEMA) {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->invalid_schema);
        } else if (field_result == SIGNAL_VALIDATION_DIRECTIVE) {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->invalid_directive);
        } else {
            signal_diagnostic_increment(diagnostics == NULL
                                        ? NULL : &diagnostics->invalid_fields);
        }
        return FRAME_INVALID;
    }
    if (payload.tier != SIGNAL_TIER_CORE_LOCAL) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->invalid_tier);
        return FRAME_INVALID;
    }
    if (payload.source_id != SIGNAL_SOURCE_LOCAL) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->invalid_source);
        return FRAME_INVALID;
    }
    if (payload.sequence == 0u || payload.sequence < last_sequence) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->invalid_sequence);
        return FRAME_INVALID;
    }
    if (payload.sequence == last_sequence)
        return FRAME_NO_NEW;
    uint64_t now = monotonic_ns();
    if (payload.monotonic_ns > now || now - payload.monotonic_ns > payload.max_age_ns) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->stale_frames);
        return FRAME_INVALID;
    }
    if (payload.key_epoch != (uint32_t)(payload.sequence / KEY_EPOCH_TICKS)) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->invalid_key_epochs);
        return FRAME_INVALID;
    }
    sign_payload(&payload, master, expected);
    if (!constant_time_equal(bytes + SIGNAL_WIRE_SIZE, expected, HMAC_SIZE)) {
        *out = payload;
        signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->hmac_failures);
        return FRAME_INVALID;
    }
    *out = payload;
    signal_diagnostic_increment(diagnostics == NULL ? NULL : &diagnostics->verified_reads);
    return FRAME_VALID;
}

static frame_read_result_t read_verified_frame_with_diagnostics(
    const signal_bus_t *bus, signal_reader_gates_t *gates,
    const uint8_t master[MASTER_KEY_SIZE], uint64_t last_sequence,
    signal_payload_t *out, signal_reader_diagnostics_t *diagnostics) {
    uint8_t bytes[SIGNAL_FRAME_SIZE];
    signal_snapshot_result_t snapshot = selected_copy_canonical_frame(bus, gates, bytes,
                                                                       diagnostics);
    if (snapshot == SIGNAL_SNAPSHOT_EMPTY)
        return FRAME_NO_NEW;
    if (snapshot == SIGNAL_SNAPSHOT_UNSTABLE)
        return FRAME_UNSTABLE;
    return validate_copied_signal_frame(bytes, master, last_sequence, out, diagnostics);
}

#ifdef ORCHESTRA_UNIT_TEST
static frame_read_result_t read_verified_frame(const signal_bus_t *bus,
                                               signal_reader_gates_t *gates,
                                               const uint8_t master[MASTER_KEY_SIZE],
                                               uint64_t last_sequence,
                                               signal_payload_t *out) {
    return read_verified_frame_with_diagnostics(bus, gates, master, last_sequence, out, NULL);
}
#endif

static signal_publish_result_t publish_frame(signal_bus_t *bus,
                                             signal_reader_gates_t *gates,
                                             const signal_payload_t *payload,
                                             const uint8_t master[MASTER_KEY_SIZE],
                                             bool tamper) {
    uint8_t bytes[SIGNAL_FRAME_SIZE];
    signal_diagnostic_increment(&bus->diagnostics.publication_attempts);
    prepare_canonical_signal_frame(payload, master, tamper, bytes);
    signal_publish_result_t result = selected_publish_canonical_frame(bus, gates, bytes);
    if (result == SIGNAL_PUBLISH_OK)
        signal_diagnostic_increment(&bus->diagnostics.publications);
    else if (result == SIGNAL_PUBLISH_CONTENDED)
        signal_diagnostic_increment(&bus->diagnostics.publication_contention);
    else
        signal_diagnostic_increment(&bus->diagnostics.generation_exhaustions);
    return result;
}

static bool realtime_bypass_loop(signal_bus_t *bus, worker_state_t *ws,
                                 int worker_index) {
    struct sched_param sp = { .sched_priority = 1 };
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        fprintf(stderr, "[rt-bypass] worker %d could not enter SCHED_FIFO: %s; worker remains ORCHESTRA-eligible\n",
                worker_index, strerror(errno));
        atomic_store(&ws->exempt_rt, 0);
        return false;
    }
    uint64_t rng = ((uint64_t)getpid() << 32) ^ monotonic_ns();
    atomic_store(&ws->exempt_rt, 1);
    atomic_store(&ws->alive, 1);
    while (!atomic_load(&bus->stop)) {
        atomic_store(&ws->previous_action, atomic_load(&ws->action));
        atomic_store(&ws->action, ACT_RUN);
        busy_work_ms(3, &rng);
        atomic_fetch_add(&ws->heartbeat, 1u);
        atomic_store(&ws->action, ACT_SLEEP);
        sleep_ms(17);
    }
    atomic_store(&ws->alive, 0);
    return true;
}

/*
 * Consensus owns every Q-table only after publishing consensus_lock and
 * observing every worker outside this critical section.  The second lock
 * check closes the race where consensus begins between the first check and
 * publication of q_update_active.
 */
static bool begin_qtable_access(const signal_bus_t *bus, worker_state_t *ws) {
    for (;;) {
        while (atomic_load_explicit(&bus->consensus_lock, memory_order_seq_cst)
               && !atomic_load_explicit(&bus->stop, memory_order_relaxed)) {
            sleep_ms(1);
        }
        if (atomic_load_explicit(&bus->stop, memory_order_relaxed)) return false;
        atomic_store_explicit(&ws->q_update_active, 1, memory_order_seq_cst);
        if (!atomic_load_explicit(&bus->consensus_lock, memory_order_seq_cst))
            return true;
        atomic_store_explicit(&ws->q_update_active, 0, memory_order_seq_cst);
    }
}

static void end_qtable_access(worker_state_t *ws) {
    atomic_store_explicit(&ws->q_update_active, 0, memory_order_seq_cst);
}

static void adaptive_worker_loop(signal_bus_t *bus, signal_reader_gates_t *gates,
                                 worker_state_t *ws,
                                 int worker_index,
                                 const uint8_t master[MASTER_KEY_SIZE],
                                 run_mode_t mode, uint64_t base_seed) {
    if (mprotect(bus, sizeof(*bus), PROT_READ) != 0) {
        fprintf(stderr, "worker %d: mprotect signal bus failed: %s\n", worker_index, strerror(errno));
        _exit(2);
    }

    uint64_t rng = base_seed
                 ? base_seed ^ (UINT64_C(0x9e3779b97f4a7c15)
                                * (uint64_t)(unsigned int)(worker_index + 1))
                 : ((uint64_t)(unsigned int)getpid() << 32) ^ monotonic_ns()
                   ^ (uint64_t)(unsigned int)worker_index;
    if (rng == 0) rng = UINT64_C(0x6a09e667f3bcc909);
    uint64_t last_sequence = 0;
    uint64_t last_rejected_sequence = 0;
    int last_state = 0;
    action_t last_action = ACT_SLEEP;
    bool have_transition = false;
    signal_payload_t last_good = {0};
    bool have_last_good = false;
    atomic_store(&ws->exempt_rt, 0);
    atomic_store(&ws->action, ACT_SLEEP);
    atomic_store(&ws->previous_action, ACT_SLEEP);
    atomic_store(&ws->alive, 1);

    while (!atomic_load(&bus->stop)) {
        /* The invalid-frame diagnostics may inspect the rejected snapshot. */
        signal_payload_t frame = {0};
        frame_read_result_t frame_result = read_verified_frame_with_diagnostics(
            bus, gates, master, last_sequence, &frame, &ws->publication_diagnostics);

        if (frame_result == FRAME_VALID) {
            uint64_t transition_sequence = last_sequence;
            last_sequence = frame.sequence;
            last_good = frame;
            have_last_good = true;
            double perceived_cpu = clamp01(frame.decision_cpu
                                          + rng_normal(&rng) * frame.jitter_sigma);
            double perceived_memory = clamp01(frame.memory_pressure
                                             + rng_normal(&rng)
                                               * frame.jitter_sigma);
            double perceived_thermal = clamp01(frame.thermal_proxy
                                              + rng_normal(&rng)
                                                * frame.jitter_sigma);
            int current_state = state_index(perceived_cpu, perceived_memory,
                                            perceived_thermal);
            atomic_store(&ws->next_state_index, current_state);

            action_t proposed;
            if (mode == MODE_ORCHESTRA) {
                if (!begin_qtable_access(bus, ws)) break;
                int allow_update = atomic_load_explicit(&bus->policy_update_allowed,
                                                        memory_order_relaxed);
                int allow_explore = atomic_load_explicit(&bus->policy_exploration_enabled,
                                                          memory_order_relaxed);
                uint64_t reward_sequence = atomic_load_explicit(
                    &ws->reward_sequence, memory_order_acquire);
                if (have_transition && reward_sequence == transition_sequence && allow_update) {
                    double reward = atomic_load(&ws->reward);
                    double max_next = ws->qtable[qindex(current_state, 0)];
                    for (int a = 1; a < ACTION_COUNT; ++a) {
                        double q = ws->qtable[qindex(current_state, a)];
                        if (q > max_next) max_next = q;
                    }
                    int qi = qindex(last_state, (int)last_action);
                    double old = ws->qtable[qi];
                    ws->qtable[qi] = old + RL_ALPHA * (reward + RL_GAMMA * max_next - old);
                    atomic_store_explicit(&ws->reward_sequence, 0,
                                          memory_order_release);
                }
                if (allow_explore) {
                    proposed = epsilon_greedy(ws->qtable, current_state,
                                              epsilon_for_sequence(frame.sequence), &rng);
                } else {
                    proposed = greedy_action(ws->qtable, current_state, &rng);
                }
                end_qtable_access(ws);
            } else {
                proposed = (action_t)frame.directive;
            }

            record_worker_decision(ws, proposed, current_state, false,
                                   FALLBACK_REASON_NONE,
                                   frame.sequence);

            last_state = current_state;
            last_action = proposed;
            have_transition = true;
        } else if (frame_result == FRAME_INVALID) {
            if (frame.sequence != last_rejected_sequence) {
                atomic_fetch_add(&ws->rejected_frames, 1u);
                last_rejected_sequence = frame.sequence;
            }
            sleep_ms(2);
        } else {
            sleep_ms(2);
        }

        if (frame_result != FRAME_VALID) {
            bool expired = !have_last_good;
            if (have_last_good) {
                uint64_t now = monotonic_ns();
                expired = now < last_good.monotonic_ns
                       || now - last_good.monotonic_ns > last_good.max_age_ns;
            }
            if (expired && !atomic_load(&ws->fallback_active)) {
                fallback_reason_t reason = have_last_good
                    ? FALLBACK_REASON_LAST_KNOWN_GOOD_EXPIRED
                    : FALLBACK_REASON_NO_VALID_FRAME;
                if (frame_result == FRAME_UNSTABLE) {
                    signal_diagnostic_increment(
                        &ws->publication_diagnostics.contention_safe_fallbacks);
                }
                record_worker_decision(ws, ACT_THROTTLE,
                                       atomic_load(&ws->state_index), true,
                                       reason,
                                       last_sequence);
            }
        }

        worker_snapshot_t action_snapshot;
        if (read_worker_snapshot(ws, &action_snapshot)) {
            action_observation_t observation;
            if (atomic_load_explicit(&bus->kernel_bridge_enabled,
                                     memory_order_relaxed)) {
                uint64_t wait_ns = frame_result == FRAME_VALID ? frame.max_age_ns
                    : (have_last_good ? last_good.max_age_ns
                                      : UINT64_C(100000000));
                uint64_t deadline = monotonic_ns() + wait_ns;
                while (atomic_load_explicit(&ws->kernel_publish_sequence,
                                            memory_order_acquire)
                           < action_snapshot.accepted_sequence
                       && monotonic_ns() < deadline
                       && !atomic_load_explicit(&bus->stop,
                                                memory_order_relaxed))
                    sleep_ms(1);
                bool accepted = atomic_load_explicit(
                    &ws->kernel_publish_sequence, memory_order_acquire)
                    == action_snapshot.accepted_sequence
                    && atomic_load_explicit(&ws->kernel_publish_status,
                                            memory_order_relaxed) == 0;
                observation = (action_observation_t) {
                    .selected_action = action_snapshot.action,
                    .action_attempted = accepted,
                    .action_attempt_result = accepted
                        ? ACTION_ATTEMPT_SUCCEEDED : ACTION_ATTEMPT_FAILED,
                    .action_errno = accepted ? 0 : EIO,
                    .cpu_before_action = sched_getcpu(),
                    .requested_cpu = -1,
                    .requested_cpu_valid = false,
                    .cpu_after_action = sched_getcpu(),
                    .fallback_reason = action_snapshot.fallback_active
                        ? action_snapshot.fallback_reason : FALLBACK_REASON_NONE,
                    /* Request acceptance is not kernel execution proof. */
                    .effective_action_result = EFFECTIVE_RESULT_NOT_ATTEMPTED
                };
            } else {
                observation = perform_action(
                    action_snapshot.action, worker_index, &rng,
                    action_snapshot.fallback_active ? action_snapshot.fallback_reason
                                                    : FALLBACK_REASON_NONE);
            }
            record_worker_action_observation(ws, &observation,
                                             action_snapshot.accepted_sequence);
            if (observation.fatal_process_state) {
                atomic_store_explicit(&bus->stop, 1, memory_order_release);
                break;
            }
        }
        atomic_fetch_add(&ws->heartbeat, 1u);
    }

    atomic_store(&ws->alive, 0);
    _exit(0);
}

/* ------------------------ Coordination and reward ---------------------- */

static double normalized_entropy_from_counts(const int counts[ACTION_COUNT], int total) {
    if (total <= 1) return 0.0;
    double h = 0.0;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (counts[i] == 0) continue;
        double p = (double)counts[i] / (double)total;
        h -= p * log(p);
    }
    return h / log((double)ACTION_COUNT);
}

static bool add_u64_bounded(uint64_t *total, uint64_t addend, uint64_t maximum) {
    if (addend > maximum || *total > maximum - addend) return false;
    *total += addend;
    return true;
}

static bool valid_action_attempt_result(action_attempt_result_t result) {
    return result >= ACTION_ATTEMPT_NOT_ATTEMPTED
        && result <= ACTION_ATTEMPT_FAILED;
}

static bool valid_effective_result(effective_action_result_t result) {
    return result >= EFFECTIVE_RESULT_NOT_ATTEMPTED
        && result <= EFFECTIVE_RESULT_INVALID_REQUESTED_CPU;
}

static double fraction_or_one(int numerator, int denominator) {
    if (denominator <= 0) return 1.0;
    return clamp01((double)numerator / (double)denominator);
}

/* The parent creates `frame`, while each accepted worker sequence proves that
 * the worker verified its authenticated wire frame.  This helper additionally
 * requires the parent-side representation to be structurally valid and fresh
 * before a directive transition can justify any burst. */
static bool metric_frame_is_valid_and_fresh(const signal_payload_t *frame) {
    if (!payload_fields_valid(frame)
        || frame->tier != SIGNAL_TIER_CORE_LOCAL
        || frame->source_id != SIGNAL_SOURCE_LOCAL
        || frame->sequence == 0) {
        return false;
    }
    uint64_t now = monotonic_ns();
    return frame->monotonic_ns <= now
        && now - frame->monotonic_ns <= frame->max_age_ns;
}

static void burst_tracker_record(burst_tracker_t *tracker,
                                 const signal_payload_t *frame,
                                 const coord_metrics_t *metrics) {
    if (tracker == NULL || !metrics->current_directive_valid) return;

    tracker->previous_directive_valid = true;
    tracker->previous_directive = (action_t)frame->directive;
    tracker->previous_sequence = frame->sequence;

    burst_history_entry_t *entry = &tracker->history[tracker->next_index];
    entry->valid = true;
    entry->large_burst = metrics->large_burst_event;
    entry->old_action = metrics->dominant_old_action;
    entry->new_action = metrics->dominant_new_action;
    entry->sequence = frame->sequence;
    tracker->next_index = (tracker->next_index + 1u) % BURST_HISTORY_SIZE;
}

static coord_metrics_t compute_metrics(worker_block_t *workers,
                                       const signal_payload_t *frame,
                                       double forecast_error,
                                       int interval_ms,
                                       const burst_tracker_t *burst_tracker) {
    coord_metrics_t m = {0};
    int eligible = 0, compliant = 0, changed = 0;
    int state_totals[STATE_COUNT] = {0};
    int state_actions[STATE_COUNT][ACTION_COUNT] = {{0}};
    int transition_counts[ACTION_COUNT][ACTION_COUNT] = {{0}};
    double signal_reach_sum = 0.0;
    m.dominant_old_action = -1;
    m.dominant_new_action = -1;
    for (int i = 0; i < workers->worker_count; ++i) {
        worker_state_t *w = &workers->worker[i];
        worker_snapshot_t snapshot;
        if (!read_worker_snapshot(w, &snapshot) || snapshot.exempt_rt || !snapshot.alive)
            continue;
        if (snapshot.action < ACT_RUN || snapshot.action >= ACTION_COUNT ||
            snapshot.state_index < 0 || snapshot.state_index >= STATE_COUNT ||
            (snapshot.has_previous_action &&
             (snapshot.previous_action < ACT_RUN ||
              snapshot.previous_action >= ACTION_COUNT))) {
            /* Corrupt state cannot index fixed metric tables. */
            continue;
        }
        action_t a = snapshot.action;
        action_t prev = snapshot.previous_action;
        if (a >= 0 && a < ACTION_COUNT) m.counts[a]++;
        if (snapshot.state_index >= 0 && snapshot.state_index < STATE_COUNT
            && a >= 0 && a < ACTION_COUNT) {
            state_totals[snapshot.state_index]++;
            state_actions[snapshot.state_index][a]++;
        }
        if (!snapshot.fallback_active && snapshot.accepted_sequence == frame->sequence
            && a == (action_t)frame->directive)
            compliant++;
        if (snapshot.has_previous_action && a != prev) changed++;
        if (snapshot.has_previous_action && a >= ACT_RUN && a < ACTION_COUNT
            && prev >= ACT_RUN && prev < ACTION_COUNT && a != prev) {
            transition_counts[prev][a]++;
            m.changed_eligible_workers++;
        }
        if (snapshot.fallback_active) {
            m.fallback_workers++;
            if (m.fallback_reason == FALLBACK_REASON_NONE) {
                m.fallback_reason = snapshot.fallback_reason;
            } else if (m.fallback_reason != snapshot.fallback_reason) {
                m.fallback_reason = FALLBACK_REASON_MULTIPLE;
            }
        }
        uint64_t accepted = snapshot.accepted_sequence;
        if (accepted == frame->sequence) m.accepted_workers++;
        uint64_t gap = frame->sequence > accepted ? frame->sequence - accepted : 0;
        signal_reach_sum += exp(-0.7 * (double)gap);

        /* A result is current only when a verified current frame selected it.
         * Rejected, stale, fallback, and not-yet-completed actions contribute
         * no effective success. */
        bool current_action = !snapshot.fallback_active
            && accepted == frame->sequence
            && snapshot.action_sequence == frame->sequence
            && snapshot.action_attempted
            && valid_action_attempt_result(snapshot.action_attempt_result)
            && (snapshot.action_attempt_success
                == (snapshot.action_attempt_result == ACTION_ATTEMPT_SUCCEEDED))
            && valid_effective_result(snapshot.effective_action_result);
        if (current_action) {
            m.action_attempt_count++;
            if (snapshot.action_errno != 0) m.action_error_count++;
            if (snapshot.effective_action_result == EFFECTIVE_RESULT_SUCCESS)
                m.effective_action_success_count++;
            if (a == ACT_MIGRATE) {
                m.migration_attempt_count++;
                if (snapshot.requested_cpu_valid) {
                    m.migration_valid_requested_cpu_count++;
                    if (snapshot.action_attempt_result == ACTION_ATTEMPT_SUCCEEDED
                        && snapshot.action_errno == 0)
                        m.migration_affinity_success_count++;
                    if (snapshot.migration_observed
                        && snapshot.cpu_after_action == snapshot.requested_cpu)
                        m.migration_observed_success_count++;
                }
            } else if (a == ACT_SLEEP) {
                m.sleep_attempt_count++;
                if (snapshot.effective_action_result == EFFECTIVE_RESULT_SUCCESS)
                    m.sleep_effective_success_count++;
                (void)add_u64_bounded(&m.requested_sleep_ns_total,
                                      snapshot.requested_sleep_ns,
                                      (uint64_t)MAX_WORKERS * MAX_ACTION_SLEEP_NS);
                (void)add_u64_bounded(&m.observed_sleep_ns_total,
                                      snapshot.observed_sleep_ns,
                                      (uint64_t)MAX_WORKERS
                                      * (MAX_ACTION_SLEEP_NS + SLEEP_OVERSLEEP_MAX_NS));
            } else if (a == ACT_YIELD) {
                m.yield_attempt_count++;
                if (snapshot.yield_attempted
                    && snapshot.effective_action_result == EFFECTIVE_RESULT_SUCCESS)
                    m.yield_call_success_count++;
            } else if (a == ACT_THROTTLE) {
                m.throttle_attempt_count++;
                if (snapshot.throttle_attempted
                    && snapshot.effective_action_result == EFFECTIVE_RESULT_SUCCESS)
                    m.throttle_operation_success_count++;
            }
        }
        eligible++;
    }

    double age_sec = fmax(0.0, (double)(monotonic_ns() - frame->monotonic_ns) / 1e9);
    double expected_age = (double)interval_ms / 1000.0;
    double freshness = exp(-2.0 * fmax(0.0, age_sec - expected_age));
    double accuracy = exp(-4.0 * forecast_error);
    double signal_reach = eligible ? signal_reach_sum / eligible : 1.0;
    m.s1 = clamp01(freshness * accuracy * frame->confidence * signal_reach);
    m.s2_selected = eligible ? (double)compliant / eligible : 1.0;
    /* Compliance is attributable only to the currently accepted frame. */
    m.s2 = m.s2_selected;
    m.s2_effective = eligible == 0 ? 1.0
        : clamp01((double)m.effective_action_success_count / (double)eligible);
    m.s3_global = clamp01(1.0 - normalized_entropy_from_counts(m.counts, eligible));
    double conditioned_sum = 0.0;
    int conditioned_workers = 0;
    for (int state = 0; state < STATE_COUNT; ++state) {
        int group_size = state_totals[state];
        if (group_size == 0) continue;
        /* A singleton is perfectly coherent within its observed state cohort. */
        double coherence = group_size == 1 ? 1.0
            : clamp01(1.0 - normalized_entropy_from_counts(state_actions[state], group_size));
        conditioned_sum += coherence * (double)group_size;
        conditioned_workers += group_size;
    }
    m.s3_conditioned = conditioned_workers > 0
        ? clamp01(conditioned_sum / (double)conditioned_workers) : 1.0;
    /* Canonical S3 is state-conditioned coherence. Global entropy remains
     * diagnostic only and cannot reward meaningless population uniformity. */
    m.s3 = m.s3_conditioned;
    m.s4 = eligible ? 1.0 - (double)changed / eligible : 1.0;
    m.eligible_workers = eligible;

    /* S4_burst uses only valid canonical action transitions.  In normal
     * emitted rows this count equals historical `changed`; retaining the
     * transition matrix makes malformed actions non-justifying rather than
     * indexing outside fixed storage. */
    m.change_fraction = eligible > 0
        ? clamp01((double)m.changed_eligible_workers / (double)eligible) : 0.0;

    m.current_directive_valid = metric_frame_is_valid_and_fresh(frame)
        && eligible > 0
        && m.accepted_workers == eligible
        && m.fallback_workers == 0;
    m.previous_directive_valid = burst_tracker != NULL
        && burst_tracker->previous_directive_valid;
    m.directive_transition_valid = m.current_directive_valid
        && m.previous_directive_valid
        && (action_t)frame->directive != burst_tracker->previous_directive;

    for (int old_action = ACT_RUN; old_action < ACTION_COUNT; ++old_action) {
        for (int new_action = ACT_RUN; new_action < ACTION_COUNT; ++new_action) {
            int count = transition_counts[old_action][new_action];
            if (count == 0) continue;
            if (count > m.dominant_transition_count) {
                m.dominant_transition_count = count;
                m.dominant_old_action = old_action;
                m.dominant_new_action = new_action;
            }
            if (m.directive_transition_valid
                && new_action == (int)frame->directive
                && old_action != (int)frame->directive) {
                m.justified_changed_workers += count;
            }
        }
    }
    m.dominant_transition_fraction = m.changed_eligible_workers > 0
        ? clamp01((double)m.dominant_transition_count
                  / (double)m.changed_eligible_workers) : 0.0;
    m.justified_change_fraction = m.changed_eligible_workers > 0
        ? clamp01((double)m.justified_changed_workers
                  / (double)m.changed_eligible_workers) : 0.0;
    m.large_burst_event = m.changed_eligible_workers > 1
        && m.change_fraction >= LARGE_BURST_CHANGE_FRACTION
        && m.dominant_transition_fraction >= LARGE_BURST_DOMINANT_FRACTION;

    if (m.large_burst_event) {
        m.rolling_window_burst_count = 1;
        /* A row without a current large burst has neutral rolling diagnostics.
         * This avoids reporting a stale prior burst as if it were current. */
    }
    if (m.large_burst_event && burst_tracker != NULL) {
        for (size_t i = 0; i < BURST_HISTORY_SIZE; ++i) {
            const burst_history_entry_t *entry = &burst_tracker->history[i];
            if (!entry->valid || entry->sequence >= frame->sequence) continue;
            uint64_t sequence_gap = frame->sequence - entry->sequence;
            if (sequence_gap > BURST_WINDOW_SEQUENCE_DISTANCE) continue;
            if (entry->large_burst) {
                m.rolling_window_burst_count++;
                if (m.large_burst_event
                    && entry->old_action == m.dominant_new_action
                    && entry->new_action == m.dominant_old_action) {
                    m.rolling_window_oscillation_count++;
                }
            }
        }
    }
    m.repeated_oscillation_event = m.large_burst_event
        && m.rolling_window_oscillation_count > 0;
    m.oscillation_penalty = m.repeated_oscillation_event
        ? fmin(MAX_OSCILLATION_PENALTY,
               OSCILLATION_PENALTY_WEIGHT
               * (double)m.rolling_window_oscillation_count) : 0.0;

    /* A lone changed worker cannot demonstrate synchronization.  This
     * population-scale term is zero for zero/one eligible worker and for a
     * single changed worker; it rises to one only for a full cohort switch. */
    double population_scale = (eligible > 1 && m.changed_eligible_workers > 1)
        ? (double)(m.changed_eligible_workers - 1) / (double)(eligible - 1) : 0.0;
    double justification_factor = JUSTIFIED_BURST_FACTOR
        + (1.0 - JUSTIFIED_BURST_FACTOR) * (1.0 - m.justified_change_fraction);
    double burst_penalty = BURST_PENALTY_WEIGHT * m.change_fraction
        * population_scale * m.dominant_transition_fraction * justification_factor;
    m.s4_burst = clamp01(1.0 - burst_penalty - m.oscillation_penalty);

    m.migration_observed_success_fraction = fraction_or_one(
        m.migration_observed_success_count, m.migration_attempt_count);
    m.sleep_effectiveness_fraction = fraction_or_one(
        m.sleep_effective_success_count, m.sleep_attempt_count);
    m.yield_call_success_fraction = fraction_or_one(
        m.yield_call_success_count, m.yield_attempt_count);
    m.throttle_operation_success_fraction = fraction_or_one(
        m.throttle_operation_success_count, m.throttle_attempt_count);
    m.fallback_fraction = eligible > 0
        ? clamp01((double)m.fallback_workers / (double)eligible) : 0.0;
    /* Burst-aware stability is now part of canonical S4/Q/control. The
     * conservative minimum preserves the exact all-stable/all-switch bounds. */
    if (m.s4_burst < m.s4) m.s4 = m.s4_burst;
    const double factors[] = {m.s1, m.s2, m.s3, m.s4};
    m.q = normalized_geometric_mean(factors, sizeof(factors) / sizeof(factors[0]));
    return m;
}

static bool coordination_sample_complete(const worker_block_t *workers,
                                         const coord_metrics_t *metrics) {
    (void)workers;
    return metrics->eligible_workers > 0
        && metrics->accepted_workers == metrics->eligible_workers;
}

#ifdef ORCHESTRA_UNIT_TEST
/* Kept as a test oracle for the aggregate implementation below. */
static double population_utility_reference(const action_t actions[MAX_WORKERS],
                                           const action_t previous[MAX_WORKERS],
                                           const int states[MAX_WORKERS],
                                           int n, action_t directive) {
    if (n <= 0) return 1.0;
    int state_totals[STATE_COUNT] = {0};
    int state_actions[STATE_COUNT][ACTION_COUNT] = {{0}};
    int compliant = 0, changed = 0;
    for (int i = 0; i < n; ++i) {
        state_totals[states[i]]++;
        state_actions[states[i]][actions[i]]++;
        if (actions[i] == directive) compliant++;
        if (actions[i] != previous[i]) changed++;
    }
    double s2 = (double)compliant / n;
    double coherent = 0.0;
    for (int state = 0; state < STATE_COUNT; ++state) {
        if (state_totals[state] == 0) continue;
        double state_coherence = state_totals[state] == 1 ? 1.0
            : clamp01(1.0 - normalized_entropy_from_counts(state_actions[state],
                                                            state_totals[state]));
        coherent += state_coherence * (double)state_totals[state];
    }
    double s3 = clamp01(coherent / (double)n);
    double s4 = 1.0 - (double)changed / n;
    const double factors[] = {s2, s3, s4};
    return normalized_geometric_mean(factors, sizeof(factors) / sizeof(factors[0]));
}
#endif

typedef struct {
    int eligible;
    int state_totals[STATE_COUNT];
    int state_actions[STATE_COUNT][ACTION_COUNT];
    int compliant;
    int changed;
} population_counters_t;

static double population_utility_from_counters(const population_counters_t *counters) {
    if (counters->eligible <= 0) return 1.0;
    double s2 = (double)counters->compliant / (double)counters->eligible;
    double coherent = 0.0;
    for (int state = 0; state < STATE_COUNT; ++state) {
        int count = counters->state_totals[state];
        if (count == 0) continue;
        double state_coherence = count == 1 ? 1.0
            : clamp01(1.0 - normalized_entropy_from_counts(
                counters->state_actions[state], count));
        coherent += state_coherence * (double)count;
    }
    double s3 = clamp01(coherent / (double)counters->eligible);
    double s4 = 1.0 - (double)counters->changed / (double)counters->eligible;
    const double factors[] = {s2, s3, s4};
    return normalized_geometric_mean(factors, sizeof(factors) / sizeof(factors[0]));
}

static void assign_difference_rewards(worker_block_t *workers,
                                      const signal_payload_t *frame) {
    action_t actions[MAX_WORKERS] = {0}, previous[MAX_WORKERS] = {0};
    int indices[MAX_WORKERS] = {0};
    int states[MAX_WORKERS] = {0};
    bool has_previous[MAX_WORKERS] = {false};
    int n = 0;
    for (int i = 0; i < workers->worker_count; ++i) {
        worker_state_t *w = &workers->worker[i];
        worker_snapshot_t snapshot;
        if (!read_worker_snapshot(w, &snapshot) || snapshot.exempt_rt || !snapshot.alive)
            continue;
        if (snapshot.action < ACT_RUN || snapshot.action >= ACTION_COUNT ||
            snapshot.state_index < 0 || snapshot.state_index >= STATE_COUNT ||
            (snapshot.has_previous_action &&
             (snapshot.previous_action < ACT_RUN ||
              snapshot.previous_action >= ACTION_COUNT)))
            continue;
        indices[n] = i;
        actions[n] = snapshot.action;
        previous[n] = snapshot.previous_action;
        states[n] = snapshot.state_index;
        has_previous[n] = snapshot.has_previous_action;
        n++;
    }
    population_counters_t counters = {.eligible = n};
    for (int j = 0; j < n; ++j) {
        counters.state_totals[states[j]]++;
        counters.state_actions[states[j]][actions[j]]++;
        if (actions[j] == (action_t)frame->directive) counters.compliant++;
        if (has_previous[j] && actions[j] != previous[j]) counters.changed++;
    }
    double global = population_utility_from_counters(&counters);
    for (int j = 0; j < n; ++j) {
        action_t original = actions[j];
        /* The fixed counterfactual alters only this worker's aggregate terms. */
        population_counters_t counterfactual = counters;
        counterfactual.state_actions[states[j]][original]--;
        counterfactual.state_actions[states[j]][ACT_YIELD]++;
        if (original == (action_t)frame->directive) counterfactual.compliant--;
        if (ACT_YIELD == (action_t)frame->directive) counterfactual.compliant++;
        if (has_previous[j] && original != previous[j]) counterfactual.changed--;
        if (has_previous[j] && ACT_YIELD != previous[j]) counterfactual.changed++;
        double counterfactual_utility = population_utility_from_counters(&counterfactual);
        double difference = (double)n * (global - counterfactual_utility);
        if (difference > LOCAL_REWARD_MATCH) difference = LOCAL_REWARD_MATCH;
        if (difference < LOCAL_REWARD_MISMATCH) difference = LOCAL_REWARD_MISMATCH;
        double local = (original == (action_t)frame->directive)
                     ? LOCAL_REWARD_MATCH : LOCAL_REWARD_MISMATCH;
        if (frame->thermal_proxy > 0.90 && original == ACT_RUN) local -= 0.40;
        if (has_previous[j] && original != previous[j])
            local -= frame->switch_penalty;
        double reward = (1.0 - DIFFERENCE_WEIGHT) * local + DIFFERENCE_WEIGHT * difference;
        atomic_store(&workers->worker[indices[j]].reward, reward);
        atomic_store_explicit(&workers->worker[indices[j]].reward_sequence,
                              frame->sequence, memory_order_release);
    }
}

static bool consensus_blend_qtables(signal_bus_t *bus, worker_block_t *workers, double blend) {
    if (blend <= 0.0) return false;
    atomic_store(&bus->consensus_lock, 1);
    bool active = false;
    for (int spin = 0; spin < 200; ++spin) {
        active = false;
        for (int i = 0; i < workers->worker_count; ++i) {
            if (atomic_load(&workers->worker[i].q_update_active)) { active = true; break; }
        }
        if (!active) break;
        sleep_ms(1);
    }
    if (active) {
        atomic_store(&bus->consensus_lock, 0);
        return false;
    }
    double mean[QTABLE_SIZE] = {0};
    int n = 0;
    for (int i = 0; i < workers->worker_count; ++i) {
        worker_state_t *w = &workers->worker[i];
        if (atomic_load(&w->exempt_rt)) continue;
        for (int q = 0; q < QTABLE_SIZE; ++q) mean[q] += w->qtable[q];
        n++;
    }
    if (!n) { atomic_store(&bus->consensus_lock, 0); return false; }
    for (int q = 0; q < QTABLE_SIZE; ++q) mean[q] /= n;
    for (int i = 0; i < workers->worker_count; ++i) {
        worker_state_t *w = &workers->worker[i];
        if (atomic_load(&w->exempt_rt)) continue;
        for (int q = 0; q < QTABLE_SIZE; ++q) {
            w->qtable[q] = (1.0 - blend) * w->qtable[q] + blend * mean[q];
        }
    }
    atomic_store(&bus->consensus_lock, 0);
    return true;
}

static const char* controller_state_name(controller_state_t s) {
    switch (s) {
        case CONTROL_STATE_NORMAL: return "NORMAL";
        case CONTROL_STATE_DEGRADED: return "DEGRADED";
        case CONTROL_STATE_SATURATED: return "SATURATED";
        case CONTROL_STATE_DISABLED: return "DISABLED";
        case CONTROL_STATE_ROLLBACK: return "ROLLBACK";
        case CONTROL_STATE_RECOVERY: return "RECOVERY";
        default: return "UNKNOWN";
    }
}

static const char* transition_reason_name(transition_reason_t r) {
    switch (r) {
        case TRANSITION_REASON_NONE: return "NONE";
        case TRANSITION_REASON_DEGRADED_COORDINATION: return "DEGRADED_COORDINATION";
        case TRANSITION_REASON_RECOVERED_COORDINATION: return "RECOVERED_COORDINATION";
        case TRANSITION_REASON_SATURATED_ACTUATOR: return "SATURATED_ACTUATOR";
        case TRANSITION_REASON_OSCILLATION: return "OSCILLATION";
        case TRANSITION_REASON_CRITICAL_FAULT: return "CRITICAL_FAULT";
        case TRANSITION_REASON_ROLLBACK: return "ROLLBACK";
        case TRANSITION_REASON_RECOVERY_COMPLETE: return "RECOVERY_COMPLETE";
        case TRANSITION_REASON_RECOVERY_FAILED: return "RECOVERY_FAILED";
        case TRANSITION_REASON_DEFAULT_RECOVERY: return "DEFAULT_RECOVERY";
        case TRANSITION_REASON_ROLLBACK_COMPLETE: return "ROLLBACK_COMPLETE";
        default: return "UNKNOWN";
    }
}

static void window_add(controller_window_t *w, double s3, double s4, double s4_burst) {
    w->s3[w->index] = s3;
    w->s4[w->index] = s4;
    w->s4_burst[w->index] = s4_burst;
    w->index = (w->index + 1) % CONTROLLER_WINDOW_SIZE;
    if (w->count < CONTROLLER_WINDOW_SIZE) w->count++;
}

static void osc_add(oscillation_window_t *w, actuator_vector_t vec) {
    w->history[w->index] = vec;
    w->index = (w->index + 1) % CONTROLLER_OSCILLATION_WINDOW;
    if (w->count < CONTROLLER_OSCILLATION_WINDOW) w->count++;
}

static double window_avg(const double *arr, size_t count) {
    if (count == 0) return 1.0;
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) sum += arr[i];
    return sum / (double)count;
}

static void apply_state_transition(controller_machine_t *machine, controller_state_t new_state, transition_reason_t reason) {
    if (machine->state == new_state) return;
    machine->previous_state = machine->state;
    machine->state = new_state;
    machine->transition_reason = reason;
    machine->state_residence_time = 0;
}

static controller_event_t controller_machine_update(signal_payload_t *next, const coord_metrics_t *m, double jitter_floor, uint64_t controller_step, controller_machine_t *machine, controller_window_t *window, oscillation_window_t *osc_window, bool frame_valid) {
    controller_event_t event = { .updated = false, .step = controller_step, .reason = CONTROL_REASON_NONE };
    
    machine->update_accepted = 0;
    machine->update_suppressed = 0;
    machine->suppression_reason = 0;
    machine->rollback_event = 0;
    machine->oscillation_event = 0;
    machine->state_residence_time++;

    if (!frame_valid) {
        machine->invalid_frame_fault_count++;
        machine->update_suppressed = 1;
        machine->suppression_reason = 1; /* invalid frame */
        if (machine->invalid_frame_fault_count > 10 && machine->state != CONTROL_STATE_DISABLED) {
            apply_state_transition(machine, CONTROL_STATE_DISABLED, TRANSITION_REASON_CRITICAL_FAULT);
        }
        return event;
    }
    
    machine->invalid_frame_fault_count = 0;
    machine->valid_control_history_count++;
    machine->update_accepted = 1;

    window_add(window, m->s3, m->s4, m->s4_burst);
    
    double avg_s3 = window_avg(window->s3, window->count);
    double avg_s4 = window_avg(window->s4, window->count);
    
    if (machine->state == CONTROL_STATE_NORMAL) {
        if (window->count == CONTROLLER_WINDOW_SIZE && (avg_s3 < CONTROLLER_THRESHOLD || avg_s4 < CONTROLLER_THRESHOLD) && machine->state_residence_time > 2) {
            apply_state_transition(machine, CONTROL_STATE_DEGRADED, TRANSITION_REASON_DEGRADED_COORDINATION);
        }
    } else if (machine->state == CONTROL_STATE_DEGRADED) {
        if (avg_s3 >= CONTROLLER_THRESHOLD && avg_s4 >= CONTROLLER_THRESHOLD && machine->state_residence_time > 3) {
            apply_state_transition(machine, CONTROL_STATE_NORMAL, TRANSITION_REASON_RECOVERED_COORDINATION);
        } else if (machine->saturation_persistence > 5) {
            apply_state_transition(machine, CONTROL_STATE_SATURATED, TRANSITION_REASON_SATURATED_ACTUATOR);
        }
    } else if (machine->state == CONTROL_STATE_SATURATED) {
        if (avg_s3 >= CONTROLLER_THRESHOLD && avg_s4 >= CONTROLLER_THRESHOLD) {
            apply_state_transition(machine, CONTROL_STATE_NORMAL, TRANSITION_REASON_RECOVERED_COORDINATION);
        } else if (machine->state_residence_time > 5) {
            apply_state_transition(machine, CONTROL_STATE_ROLLBACK, TRANSITION_REASON_SATURATED_ACTUATOR);
        }
    } else if (machine->state == CONTROL_STATE_ROLLBACK) {
        machine->rollback_event = 1;
        if (machine->last_known_good_available) {
            machine->requested = machine->last_known_good;
            machine->rollback_reason = TRANSITION_REASON_ROLLBACK;
        } else {
            machine->requested.jitter_sigma = JITTER_MULTIPLIER_DEFAULT * 0.02; 
            machine->requested.switch_penalty = 0.0;
            machine->requested.consensus_blend = 0.0;
            machine->rollback_reason = TRANSITION_REASON_DEFAULT_RECOVERY;
        }
        apply_state_transition(machine, CONTROL_STATE_RECOVERY, TRANSITION_REASON_ROLLBACK_COMPLETE);
    } else if (machine->state == CONTROL_STATE_RECOVERY) {
        machine->recovery_progress = clamp01((double)machine->state_residence_time / 10.0);
        if (machine->state_residence_time > 10) {
            if (avg_s3 >= CONTROLLER_THRESHOLD && avg_s4 >= CONTROLLER_THRESHOLD) {
                apply_state_transition(machine, CONTROL_STATE_NORMAL, TRANSITION_REASON_RECOVERY_COMPLETE);
            } else {
                apply_state_transition(machine, CONTROL_STATE_DISABLED, TRANSITION_REASON_RECOVERY_FAILED);
            }
        }
    }
    
    if (machine->state == CONTROL_STATE_NORMAL || machine->state == CONTROL_STATE_DEGRADED) {
        double beta = CONTROLLER_BETA0 / sqrt(1.0 + (double)controller_step);
        event.updated = true;
        event.beta = beta;
        if (m->s3 < CONTROLLER_THRESHOLD) event.reason |= CONTROL_REASON_S3;
        if (m->s4 < CONTROLLER_THRESHOLD) event.reason |= CONTROL_REASON_S4;

        /* Jitter may repair a herd only while directive compliance is healthy;
         * otherwise more perturbation would amplify the S2 failure. */
        bool jitter_can_increase = (event.reason & CONTROL_REASON_S4)
                                && m->s2 >= CONTROLLER_THRESHOLD;
        double raw_jitter = machine->applied.jitter_sigma
            + (jitter_can_increase ? beta * 0.20 : -beta * 0.05);
        
        double raw_switch = machine->applied.switch_penalty + ((event.reason != CONTROL_REASON_NONE) ? beta * 0.15 : -beta * 0.04);
        double raw_consensus = machine->applied.consensus_blend + ((event.reason & CONTROL_REASON_S3) ? beta * 0.10 : -beta * 0.03);

        machine->requested.jitter_sigma = raw_jitter;
        machine->requested.switch_penalty = raw_switch;
        machine->requested.consensus_blend = raw_consensus;
    } else if (machine->state == CONTROL_STATE_DISABLED) {
        machine->update_suppressed = 1;
        machine->suppression_reason = 2; /* disabled */
    }
    
    double jitter_min = clamp01(jitter_floor);
    if (jitter_min > 0.20) jitter_min = 0.20;
    
    machine->applied.jitter_sigma = machine->requested.jitter_sigma;
    if (machine->applied.jitter_sigma < jitter_min) machine->applied.jitter_sigma = jitter_min;
    if (machine->applied.jitter_sigma > 0.20) machine->applied.jitter_sigma = 0.20;
    
    machine->applied.switch_penalty = machine->requested.switch_penalty;
    if (machine->applied.switch_penalty < 0.0) machine->applied.switch_penalty = 0.0;
    if (machine->applied.switch_penalty > 0.30) machine->applied.switch_penalty = 0.30;
    
    machine->applied.consensus_blend = machine->requested.consensus_blend;
    if (machine->applied.consensus_blend < 0.0) machine->applied.consensus_blend = 0.0;
    if (machine->applied.consensus_blend > 0.15) machine->applied.consensus_blend = 0.15;
    
    event.jitter_saturated = (machine->requested.jitter_sigma < jitter_min || machine->requested.jitter_sigma > 0.20);
    event.switch_saturated = (machine->requested.switch_penalty < 0.0 || machine->requested.switch_penalty > 0.30);
    event.consensus_saturated = (machine->requested.consensus_blend < 0.0 || machine->requested.consensus_blend > 0.15);
    
    machine->saturation_bitmask = (event.jitter_saturated ? 1 : 0) | (event.switch_saturated ? 2 : 0) | (event.consensus_saturated ? 4 : 0);
    machine->saturation_direction = 0;
    if (machine->requested.jitter_sigma > 0.20
        || machine->requested.switch_penalty > 0.30
        || machine->requested.consensus_blend > 0.15)
        machine->saturation_direction = 1;
    if (machine->requested.jitter_sigma < jitter_min
        || machine->requested.switch_penalty < 0.0
        || machine->requested.consensus_blend < 0.0)
        machine->saturation_direction = machine->saturation_direction == 1 ? 0 : -1;
    if (machine->saturation_bitmask != 0 && event.reason != CONTROL_REASON_NONE) {
        machine->saturation_persistence++;
    } else {
        machine->saturation_persistence = 0;
    }

    osc_add(osc_window, machine->applied);
    if (osc_window->count == CONTROLLER_OSCILLATION_WINDOW) {
        int reversals = 0;
        for (size_t i = 2; i < CONTROLLER_OSCILLATION_WINDOW; i++) {
            double d1 = osc_window->history[(osc_window->index + i - 1) % CONTROLLER_OSCILLATION_WINDOW].jitter_sigma - osc_window->history[(osc_window->index + i - 2) % CONTROLLER_OSCILLATION_WINDOW].jitter_sigma;
            double d2 = osc_window->history[(osc_window->index + i) % CONTROLLER_OSCILLATION_WINDOW].jitter_sigma - osc_window->history[(osc_window->index + i - 1) % CONTROLLER_OSCILLATION_WINDOW].jitter_sigma;
            if (d1 * d2 < 0) reversals++;
        }
        machine->oscillation_score = (double)reversals / (double)CONTROLLER_OSCILLATION_WINDOW;
        if (reversals > 4) {
            machine->oscillation_event = 1;
            if (machine->state == CONTROL_STATE_DEGRADED && machine->state_residence_time > 4) {
                 apply_state_transition(machine, CONTROL_STATE_DISABLED, TRANSITION_REASON_OSCILLATION);
            }
        }
    }

    if (machine->state == CONTROL_STATE_NORMAL && machine->state_residence_time > 5 && machine->saturation_persistence == 0 && machine->oscillation_event == 0) {
        machine->last_known_good = machine->applied;
        machine->last_known_good_available = 1;
    }

    next->jitter_sigma = machine->applied.jitter_sigma;
    next->switch_penalty = machine->applied.switch_penalty;
    next->consensus_blend = machine->applied.consensus_blend;
    
    return event;
}

/* ------------------------ Policy persistence ------------------------- */

static int policy_update_is_allowed(policy_mode_t pmode, controller_state_t cstate,
                                    bool frame_valid) {
    if (!frame_valid) return 0;
    if (pmode == POLICY_MODE_EVALUATE) return 0;
    if (cstate == CONTROL_STATE_SATURATED) return 0;
    if (cstate == CONTROL_STATE_ROLLBACK) return 0;
    if (cstate == CONTROL_STATE_DISABLED) return 0;
    if (pmode == POLICY_MODE_ADAPT && cstate == CONTROL_STATE_DEGRADED) return 0;
    return 1;
}

static int policy_exploration_enabled_for_mode(policy_mode_t pmode) {
    return pmode == POLICY_MODE_TRAIN ? 1 : 0;
}

static int policy_consensus_enabled_for_mode(policy_mode_t pmode) {
    return pmode != POLICY_MODE_EVALUATE ? 1 : 0;
}

static void policy_sha256(const double qtable[QTABLE_SIZE], uint8_t out[32]) {
    uint8_t raw[QTABLE_SIZE * 8];
    for (int i = 0; i < QTABLE_SIZE; i++) {
        uint64_t bits;
        memcpy(&bits, &qtable[i], sizeof(bits));
        for (int b = 0; b < 8; b++)
            raw[i * 8 + b] = (uint8_t)(bits >> ((7 - b) * 8));
    }
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, raw, sizeof(raw));
    sha256_final(&ctx, out);
}

static int policy_serialize(const double qtable[QTABLE_SIZE], uint8_t *buf,
                            size_t buf_size, size_t *out_len, uint64_t generation,
                            uint64_t train_count, uint64_t adapt_count) {
    if (buf_size < POLICY_MAX_FILE_SIZE) return 0;
    memset(buf, 0, buf_size);
    uint8_t *cursor = buf;
    wire_put_u32(&cursor, POLICY_MAGIC);
    wire_put_u32(&cursor, POLICY_FORMAT_VERSION);
    wire_put_u32(&cursor, POLICY_SCHEMA_VERSION);
    wire_put_u32(&cursor, POLICY_HEADER_SIZE);
    wire_put_u32(&cursor, (uint32_t)((size_t)QTABLE_SIZE * 8u));
    wire_put_u32(&cursor, (uint32_t)STATE_COUNT);
    wire_put_u32(&cursor, (uint32_t)ACTION_COUNT);
    wire_put_u64(&cursor, generation);
    wire_put_u64(&cursor, train_count);
    wire_put_u64(&cursor, adapt_count);
    cursor = buf + 44u;
    uint8_t *payload = buf + POLICY_HEADER_SIZE;
    for (int i = 0; i < QTABLE_SIZE; i++) {
        uint64_t bits;
        memcpy(&bits, &qtable[i], sizeof(bits));
        wire_put_u64(&payload, bits);
    }
    uint8_t digest[32];
    policy_sha256(qtable, digest);
    memcpy(buf + POLICY_HEADER_SIZE + (size_t)QTABLE_SIZE * 8u, digest, 32u);
    *out_len = POLICY_HEADER_SIZE + (size_t)QTABLE_SIZE * 8u + 32u;
    return 1;
}

static policy_load_status_t policy_deserialize(const uint8_t *buf, size_t buf_len,
                                               double qtable[QTABLE_SIZE],
                                               uint64_t *out_generation,
                                               uint64_t *out_train_count,
                                               uint64_t *out_adapt_count) {
    if (buf_len < POLICY_HEADER_SIZE) return POLICY_LOAD_TRUNCATED;
    const uint8_t *cursor = buf;
    uint32_t magic = wire_get_u32(&cursor);
    if (magic != POLICY_MAGIC) return POLICY_LOAD_MAGIC;
    uint32_t fmt_ver = wire_get_u32(&cursor);
    if (fmt_ver != POLICY_FORMAT_VERSION) return POLICY_LOAD_VERSION;
    uint32_t schema_ver = wire_get_u32(&cursor);
    if (schema_ver != POLICY_SCHEMA_VERSION) return POLICY_LOAD_SCHEMA;
    uint32_t header_len = wire_get_u32(&cursor);
    if ((size_t)header_len != POLICY_HEADER_SIZE) return POLICY_LOAD_VERSION;
    uint32_t payload_len = wire_get_u32(&cursor);
    if ((uint64_t)payload_len > (uint64_t)QTABLE_SIZE * UINT64_C(8))
        return POLICY_LOAD_OVERSIZED;
    if (payload_len != (uint32_t)((size_t)QTABLE_SIZE * 8u))
        return POLICY_LOAD_DIMENSIONS;
    uint32_t state_count = wire_get_u32(&cursor);
    if (state_count != (uint32_t)STATE_COUNT) return POLICY_LOAD_STATE_COUNT;
    uint32_t action_count = wire_get_u32(&cursor);
    if (action_count != (uint32_t)ACTION_COUNT) return POLICY_LOAD_ACTION_COUNT;
    uint64_t generation = wire_get_u64(&cursor);
    uint64_t train_count = wire_get_u64(&cursor);
    uint64_t adapt_count = wire_get_u64(&cursor);
    size_t needed = (size_t)header_len + (size_t)payload_len + 32u;
    if (buf_len < needed) return POLICY_LOAD_TRUNCATED;
    if (buf_len > needed) return POLICY_LOAD_TRAILING;

    const uint8_t *payload = buf + header_len;
    double parsed[QTABLE_SIZE];
    for (int i = 0; i < QTABLE_SIZE; i++) {
        const uint8_t *fcursor = payload + (size_t)i * 8u;
        uint64_t bits = wire_get_u64(&fcursor);
        double val;
        memcpy(&val, &bits, sizeof(val));
        if (!isfinite(val)) return POLICY_LOAD_NON_FINITE;
        parsed[i] = val;
    }
    uint8_t expected[32];
    policy_sha256(parsed, expected);
    const uint8_t *stored_digest = buf + header_len + (size_t)payload_len;
    if (!constant_time_equal(stored_digest, expected, 32u))
        return POLICY_LOAD_DIGEST;

    memcpy(qtable, parsed, sizeof(parsed));
    *out_generation = generation;
    *out_train_count = train_count;
    *out_adapt_count = adapt_count;
    return POLICY_LOAD_OK;
}

static policy_save_status_t policy_save_atomic(const double qtable[QTABLE_SIZE],
                                               const char *path,
                                               uint64_t generation,
                                               uint64_t train_count,
                                               uint64_t adapt_count) {
    uint8_t buf[POLICY_MAX_FILE_SIZE];
    size_t out_len = 0;
    if (!policy_serialize(qtable, buf, sizeof(buf), &out_len,
                          generation, train_count, adapt_count))
        return POLICY_SAVE_VALIDATION_FAILED;

    char tmp_path[1024];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.XXXXXX", path);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) return POLICY_SAVE_WRITE_ERROR;
    int fd = mkstemp(tmp_path);
    if (fd < 0) return POLICY_SAVE_TEMP_FAILED;
    if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        close(fd); unlink(tmp_path); return POLICY_SAVE_WRITE_ERROR;
    }
    size_t offset = 0;
    while (offset < out_len) {
        ssize_t count = write(fd, buf + offset, out_len - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); unlink(tmp_path); return POLICY_SAVE_WRITE_ERROR; }
        offset += (size_t)count;
    }
    if (fsync(fd) != 0 || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd); unlink(tmp_path); return POLICY_SAVE_WRITE_ERROR;
    }
    uint8_t verify_buf[POLICY_MAX_FILE_SIZE];
    offset = 0;
    while (offset < out_len) {
        ssize_t count = read(fd, verify_buf + offset, out_len - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); unlink(tmp_path); return POLICY_SAVE_TEMP_FAILED; }
        offset += (size_t)count;
    }
    if (close(fd) != 0 || memcmp(buf, verify_buf, out_len) != 0) {
        unlink(tmp_path); return POLICY_SAVE_TEMP_FAILED;
    }

    if (rename(tmp_path, path) != 0) { unlink(tmp_path); return POLICY_SAVE_RENAME_ERROR; }
    return POLICY_SAVE_OK;
}

/* ----------------------------- Calibration ----------------------------- */

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *x, int n) {
    qsort(x, (size_t)n, sizeof(double), compare_double);
    return n % 2 ? x[n/2] : 0.5 * (x[n/2 - 1] + x[n/2]);
}

static double robust_noise_sigma(const double *x, int n) {
    if (n < 4) return 0.02;
    double diffs[MAX_CAL_SAMPLES], absdev[MAX_CAL_SAMPLES];
    int m = n - 1;
    for (int i = 0; i < m; ++i) diffs[i] = x[i+1] - x[i];
    double copy[MAX_CAL_SAMPLES];
    memcpy(copy, diffs, (size_t)m * sizeof(double));
    double med = median(copy, m);
    for (int i = 0; i < m; ++i) absdev[i] = fabs(diffs[i] - med);
    double mad = median(absdev, m);
    return fmax(0.001, mad * 1.4826 / sqrt(2.0));
}

static double calibrate_fixed_gain(const double *x, int n) {
    if (n < 4) return 0.90;
    double best_gain = 0.90, best_mse = HUGE_VAL;
    for (int gi = 1; gi <= 99; ++gi) {
        double k = gi / 100.0;
        double estimate = x[0];
        double sum = 0.0;
        int count = 0;
        for (int i = 1; i < n; ++i) {
            double forecast = estimate;
            double e = x[i] - forecast;
            sum += e * e;
            count++;
            estimate = estimate + k * e;
        }
        double mse = sum / fmax(1, count);
        if (mse < best_mse) { best_mse = mse; best_gain = k; }
    }
    return best_gain;
}

static int calibration_sample_target(int seconds) {
    if (seconds <= 0) return 0;
    uint64_t requested = (uint64_t)(unsigned int)seconds * UINT64_C(20);
    return requested > MAX_CAL_SAMPLES ? MAX_CAL_SAMPLES : (int)requested;
}

static double evaluate_fixed_gain_held_out(const double *calibration,
                                           int calibration_count,
                                           const double *evaluation,
                                           int evaluation_count,
                                           double gain) {
    if (calibration_count <= 0 || evaluation_count <= 0 ||
        !isfinite(gain) || gain <= 0.0 || gain >= 1.0)
        return HUGE_VAL;
    double estimate = calibration[calibration_count - 1];
    double sum = 0.0;
    for (int i = 0; i < evaluation_count; ++i) {
        /* Score the forecast before incorporating the held-out observation. */
        double error = evaluation[i] - estimate;
        sum += error * error;
        estimate += gain * error;
    }
    return sum / (double)evaluation_count;
}

/* Convert one-step error into a bounded validity score using only error scale
 * measured on the held-out calibration suffix.  This is deliberately not an
 * online covariance estimator: no runtime sample mutates the calibrated scale. */
static double calibrated_prediction_confidence(double absolute_error,
                                                double held_out_mse,
                                                double observation_sigma) {
    if (!isfinite(absolute_error) || absolute_error < 0.0 ||
        !isfinite(held_out_mse) || held_out_mse < 0.0 ||
        !isfinite(observation_sigma) || observation_sigma <= 0.0)
        return 0.0;
    double scale = fmax(sqrt(held_out_mse), observation_sigma);
    scale = fmax(scale, 0.001);
    double normalized = absolute_error / scale;
    if (!isfinite(normalized) || normalized >= 8.0) return 0.0;
    return clamp01(exp(-0.5 * normalized * normalized));
}

static int collect_calibration_trace(double samples[MAX_CAL_SAMPLES], int seconds) {
    cpu_sample_t prev, cur;
    if (!read_cpu_sample(&prev)) return 0;
    int target = calibration_sample_target(seconds);
    int n = 0;
    while (n < target && !g_stop) {
        sleep_ms(50);
        if (!read_cpu_sample(&cur)) continue;
        samples[n++] = cpu_usage_between(&prev, &cur);
        prev = cur;
    }
    return n;
}

static bool random_key(uint8_t key[MASTER_KEY_SIZE]) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < MASTER_KEY_SIZE) {
        ssize_t n = read(fd, key + off, MASTER_KEY_SIZE - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return false; }
        off += (size_t)n;
    }
    return close(fd) == 0;
}

static int aggregate_adaptive_policy(const worker_block_t *workers,
                                     double output[QTABLE_SIZE]) {
    int policy_workers = 0;
    memset(output, 0, sizeof(double) * (size_t)QTABLE_SIZE);
    for (int i = 0; i < workers->worker_count; ++i) {
        if (atomic_load_explicit(&workers->worker[i].exempt_rt,
                                 memory_order_relaxed))
            continue;
        for (int q = 0; q < QTABLE_SIZE; q++)
            output[q] += workers->worker[i].qtable[q];
        policy_workers++;
    }
    if (policy_workers > 0) {
        for (int q = 0; q < QTABLE_SIZE; q++)
            output[q] /= (double)policy_workers;
    }
    return policy_workers;
}

/* ------------------------------- Main --------------------------------- */

static int spawn_worker(signal_bus_t *bus, signal_reader_gates_t *gates,
                        worker_block_t *workers, int index,
                        const uint8_t master[MASTER_KEY_SIZE], run_mode_t mode,
                        uint64_t base_seed, pid_t *pid_out) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (index < workers->rt_exempt_count
            && realtime_bypass_loop(bus, &workers->worker[index], index))
            _exit(0);
        adaptive_worker_loop(bus, gates, &workers->worker[index], index, master, mode,
                             base_seed);
    }
    *pid_out = pid;
    return 0;
}

static bool wait_for_workers_ready(worker_block_t *workers, int timeout_ms) {
    uint64_t deadline = monotonic_ns()
                      + (uint64_t)(unsigned int)timeout_ms * 1000000u;
    while (monotonic_ns() < deadline) {
        int alive = 0;
        for (int i = 0; i < workers->worker_count; ++i)
            if (atomic_load(&workers->worker[i].alive)) alive++;
        if (alive == workers->worker_count) return true;
        sleep_ms(5);
    }
    return false;
}

static int actual_rt_exempt_count(const worker_block_t *workers) {
    int count = 0;
    for (int i = 0; i < workers->worker_count; ++i) {
        if (atomic_load_explicit(&workers->worker[i].exempt_rt,
                                 memory_order_relaxed))
            count++;
    }
    return count;
}

static int reap_workers_nonblocking(pid_t pids[MAX_WORKERS], int count) {
    int remaining = 0;
    for (int i = 0; i < count; ++i) {
        if (pids[i] <= 0) continue;
        pid_t result;
        do {
            result = waitpid(pids[i], NULL, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == pids[i] || (result < 0 && errno == ECHILD)) {
            pids[i] = 0;
        } else {
            remaining++;
        }
    }
    return remaining;
}

static void stop_and_reap_workers(signal_bus_t *bus,
                                  pid_t pids[MAX_WORKERS], int count) {
    atomic_store(&bus->stop, 1);
    for (int i = 0; i < count; ++i) {
        if (pids[i] > 0 && kill(pids[i], SIGTERM) != 0 && errno != ESRCH)
            perror("kill(SIGTERM)");
    }

    uint64_t deadline = monotonic_ns() + UINT64_C(2000000000);
    int remaining = reap_workers_nonblocking(pids, count);
    while (remaining > 0 && monotonic_ns() < deadline) {
        sleep_ms(10);
        remaining = reap_workers_nonblocking(pids, count);
    }

    if (remaining > 0) {
        for (int i = 0; i < count; ++i) {
            if (pids[i] > 0 && kill(pids[i], SIGKILL) != 0 && errno != ESRCH)
                perror("kill(SIGKILL)");
        }
        deadline = monotonic_ns() + UINT64_C(1000000000);
        while (remaining > 0 && monotonic_ns() < deadline) {
            sleep_ms(10);
            remaining = reap_workers_nonblocking(pids, count);
        }
    }

    if (remaining > 0)
        fprintf(stderr, "Warning: %d worker(s) could not be reaped within the bounded cleanup window.\n",
                remaining);
}

static void signal_diagnostic_add(uint64_t *total, uint64_t value) {
    if (*total > UINT64_MAX - value) {
        *total = UINT64_MAX;
        return;
    }
    *total += value;
}

/* Publication diagnostics deliberately stay out of metrics-v4.  They describe
 * bounded transport behavior, not scheduling outcomes, and are summarized
 * only after all worker writers have exited. */
static void print_signal_publication_diagnostics(const signal_bus_t *bus,
                                                 const worker_block_t *workers) {
    uint64_t read_attempts = 0;
    uint64_t verified_reads = 0;
    uint64_t retries = 0;
    uint64_t retry_exhaustions = 0;
    uint64_t unstable_slots = 0;
    uint64_t safe_fallbacks = 0;
    uint64_t invalid_fields = 0;
    uint64_t invalid_schema = 0;
    uint64_t invalid_tier = 0;
    uint64_t invalid_source = 0;
    uint64_t invalid_directive = 0;
    uint64_t invalid_sequence = 0;
    uint64_t stale_frames = 0;
    uint64_t invalid_epochs = 0;
    uint64_t hmac_failures = 0;
    for (int index = 0; index < workers->worker_count; ++index) {
        const signal_reader_diagnostics_t *diagnostics =
            &workers->worker[index].publication_diagnostics;
        signal_diagnostic_add(&read_attempts, atomic_load_explicit(
            &diagnostics->read_attempts, memory_order_relaxed));
        signal_diagnostic_add(&verified_reads, atomic_load_explicit(
            &diagnostics->verified_reads, memory_order_relaxed));
        signal_diagnostic_add(&retries, atomic_load_explicit(
            &diagnostics->retries, memory_order_relaxed));
        signal_diagnostic_add(&retry_exhaustions, atomic_load_explicit(
            &diagnostics->retry_exhaustions, memory_order_relaxed));
        signal_diagnostic_add(&unstable_slots, atomic_load_explicit(
            &diagnostics->unstable_slot_observations, memory_order_relaxed));
        signal_diagnostic_add(&safe_fallbacks, atomic_load_explicit(
            &diagnostics->contention_safe_fallbacks, memory_order_relaxed));
        signal_diagnostic_add(&invalid_fields, atomic_load_explicit(
            &diagnostics->invalid_fields, memory_order_relaxed));
        signal_diagnostic_add(&invalid_schema, atomic_load_explicit(
            &diagnostics->invalid_schema, memory_order_relaxed));
        signal_diagnostic_add(&invalid_tier, atomic_load_explicit(
            &diagnostics->invalid_tier, memory_order_relaxed));
        signal_diagnostic_add(&invalid_source, atomic_load_explicit(
            &diagnostics->invalid_source, memory_order_relaxed));
        signal_diagnostic_add(&invalid_directive, atomic_load_explicit(
            &diagnostics->invalid_directive, memory_order_relaxed));
        signal_diagnostic_add(&invalid_sequence, atomic_load_explicit(
            &diagnostics->invalid_sequence, memory_order_relaxed));
        signal_diagnostic_add(&stale_frames, atomic_load_explicit(
            &diagnostics->stale_frames, memory_order_relaxed));
        signal_diagnostic_add(&invalid_epochs, atomic_load_explicit(
            &diagnostics->invalid_key_epochs, memory_order_relaxed));
        signal_diagnostic_add(&hmac_failures, atomic_load_explicit(
            &diagnostics->hmac_failures, memory_order_relaxed));
    }
#if ORCHESTRA_SIGNAL_PUBLICATION_LEGACY
    const char *implementation = "legacy-bytewise-reference";
#else
    const char *implementation = "generation-stamped";
#endif
    fprintf(stderr,
            "[signal-publication] path=%s publications=%" PRIu64 "/%" PRIu64
            " contention=%" PRIu64 " generation-exhaustion=%" PRIu64
            " read-attempts=%" PRIu64 " verified=%" PRIu64 " retries=%" PRIu64
            " retry-exhaustion=%" PRIu64 " unstable-slot=%" PRIu64
            " safe-fallback=%" PRIu64 " invalid={fields:%" PRIu64
            ",schema:%" PRIu64 ",tier:%" PRIu64 ",source:%" PRIu64
            ",directive:%" PRIu64 ",sequence:%" PRIu64 ",stale:%" PRIu64
            ",epoch:%" PRIu64 ",hmac:%" PRIu64 "}\n",
            implementation,
            atomic_load_explicit(&bus->diagnostics.publications, memory_order_relaxed),
            atomic_load_explicit(&bus->diagnostics.publication_attempts,
                                 memory_order_relaxed),
            atomic_load_explicit(&bus->diagnostics.publication_contention,
                                 memory_order_relaxed),
            atomic_load_explicit(&bus->diagnostics.generation_exhaustions,
                                 memory_order_relaxed),
            read_attempts, verified_reads, retries, retry_exhaustions, unstable_slots,
            safe_fallbacks, invalid_fields, invalid_schema, invalid_tier, invalid_source,
            invalid_directive, invalid_sequence, stale_frames, invalid_epochs,
            hmac_failures);
}

static const char *controller_reason_name(uint32_t reason) {
    switch (reason) {
        case CONTROL_REASON_NONE: return "NONE";
        case CONTROL_REASON_S3: return "S3";
        case CONTROL_REASON_S4: return "S4";
        case CONTROL_REASON_S3 | CONTROL_REASON_S4: return "S3+S4";
        default: return "INVALID";
    }
}

static void print_header(void) {
    printf("tick,mode,cpu_now,cpu_pred,decision_cpu,prediction_used,confidence,forecast_error,frame_age_ms,mem,thermal,directive,run,sleep,migrate,throttle,yield,eligible_workers,fallback_workers,S1,S2,S3,S4,Q,jitter_sigma,switch_penalty,consensus_blend,next_jitter_sigma,next_switch_penalty,next_consensus_blend,controller_updated,controller_step,controller_reason,controller_beta,jitter_saturated,switch_saturated,consensus_saturated,consensus_applied,rejected_frames,missed_deadlines,metrics_schema,S3_global,S3_conditioned,S2_selected,S2_effective,action_attempt_count,effective_action_success_count,action_error_count,migration_attempt_count,migration_valid_requested_cpu_count,migration_affinity_success_count,migration_observed_success_count,migration_observed_success_fraction,sleep_attempt_count,sleep_effective_success_count,sleep_effectiveness_fraction,requested_sleep_ns_total,observed_sleep_ns_total,yield_attempt_count,yield_call_success_count,yield_call_success_fraction,throttle_attempt_count,throttle_operation_success_count,throttle_operation_success_fraction,fallback_fraction,fallback_reason,S4_burst,change_fraction,dominant_transition_fraction,justified_change_fraction,oscillation_penalty,dominant_old_action,dominant_new_action,changed_eligible_workers,justified_changed_workers,dominant_transition_count,rolling_window_burst_count,rolling_window_oscillation_count,current_directive_valid,previous_directive_valid,directive_transition_valid,large_burst_event,repeated_oscillation_event,controller_state,previous_state,transition_reason,state_residence_time,valid_control_history_count,invalid_frame_fault_count,saturation_bitmask,saturation_direction,saturation_persistence,oscillation_score,oscillation_event,rollback_event,rollback_reason,recovery_progress,last_known_good_available,requested_jitter,applied_jitter,requested_switch,applied_switch,requested_consensus,applied_consensus,update_accepted,update_suppressed,suppression_reason,policy_mode,policy_schema_version,policy_generation,policy_update_allowed,policy_update_applied,policy_update_suppression_reason,policy_exploration_enabled,policy_train_update_count,policy_adapt_update_count,policy_load_status,policy_save_status,policy_digest_prefix,policy_format_version,coordination_semantics_version\n");
}

static void print_row(uint64_t tick, run_mode_t mode, const signal_payload_t *applied,
                      const signal_payload_t *next, const coord_metrics_t *m,
                      const controller_event_t *event, double forecast_error,
                      uint64_t missed_deadlines, worker_block_t *workers,
                      const controller_machine_t *machine,
                      const signal_bus_t *bus) {
    uint64_t rejected = 0;
    for (int i = 0; i < workers->worker_count; ++i)
        rejected += atomic_load(&workers->worker[i].rejected_frames);
    double frame_age_ms = fmax(0.0,
        (double)(monotonic_ns() - applied->monotonic_ns) / 1000000.0);
    /* Metrics are a declared eight-decimal wire format.  Compute the reported
     * aggregate from the same rounded factors a downstream validator sees. */
    double emitted_s1 = round(m->s1 * 1e8) / 1e8;
    double emitted_s2 = round(m->s2 * 1e8) / 1e8;
    double emitted_s3 = round(m->s3 * 1e8) / 1e8;
    double emitted_s4 = round(m->s4 * 1e8) / 1e8;
    const double emitted_factors[] = {
        emitted_s1, emitted_s2, emitted_s3, emitted_s4
    };
    double emitted_q = normalized_geometric_mean(
        emitted_factors, sizeof(emitted_factors) / sizeof(emitted_factors[0]));
    printf("%llu,%s,%.8f,%.8f,%.8f,%u,%.8f,%.8f,%.8f,%.8f,%.8f,%s,"
           "%d,%d,%d,%d,%d,%d,%d,"
           "%.8f,%.8f,%.8f,%.8f,%.8f,"
           "%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,"
           "%d,%llu,%s,%.8f,%d,%d,%d,%d,%llu,%llu,%s,%.8f,%.8f,"
           "%.8f,%.8f,%d,%d,%d,%d,%d,%d,%d,%.8f,%d,%d,%.8f,%llu,%llu,"
           "%d,%d,%.8f,%d,%d,%.8f,%.8f,%s,"
           "%.8f,%.8f,%.8f,%.8f,%.8f,"
           "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
           "%s,%s,%s,%llu,%llu,%llu,%u,%d,%llu,%.8f,%d,%d,%s,%.8f,%d,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%d,%d,%d,%s,%u,%llu,%d,%d,%s,%d,%llu,%llu,%s,%s,%016llx,%u,%u\n",
           (unsigned long long)tick,
           mode == MODE_ORCHESTRA ? "orchestra" : "baseline",
           applied->cpu_now, applied->cpu_pred, applied->decision_cpu,
           applied->prediction_used, applied->confidence, forecast_error,
           frame_age_ms, applied->memory_pressure, applied->thermal_proxy,
           action_name((action_t)applied->directive),
           m->counts[ACT_RUN], m->counts[ACT_SLEEP], m->counts[ACT_MIGRATE],
           m->counts[ACT_THROTTLE], m->counts[ACT_YIELD],
           m->eligible_workers, m->fallback_workers,
           emitted_s1, emitted_s2, emitted_s3, emitted_s4, emitted_q,
           applied->jitter_sigma, applied->switch_penalty, applied->consensus_blend,
           next->jitter_sigma, next->switch_penalty, next->consensus_blend,
           event->updated ? 1 : 0, (unsigned long long)event->step,
           controller_reason_name(event->reason), event->beta,
           event->jitter_saturated ? 1 : 0,
           event->switch_saturated ? 1 : 0,
           event->consensus_saturated ? 1 : 0,
           event->consensus_applied ? 1 : 0,
           (unsigned long long)rejected,
           (unsigned long long)missed_deadlines,
           "orchestra.paper_cpu.metrics/v7", m->s3_global, m->s3_conditioned,
           m->s2_selected, m->s2_effective,
           m->action_attempt_count, m->effective_action_success_count,
           m->action_error_count, m->migration_attempt_count,
           m->migration_valid_requested_cpu_count,
           m->migration_affinity_success_count,
           m->migration_observed_success_count,
           m->migration_observed_success_fraction,
           m->sleep_attempt_count, m->sleep_effective_success_count,
           m->sleep_effectiveness_fraction,
           (unsigned long long)m->requested_sleep_ns_total,
           (unsigned long long)m->observed_sleep_ns_total,
           m->yield_attempt_count, m->yield_call_success_count,
           m->yield_call_success_fraction,
           m->throttle_attempt_count, m->throttle_operation_success_count,
           m->throttle_operation_success_fraction, m->fallback_fraction,
           fallback_reason_name(m->fallback_reason),
           m->s4_burst, m->change_fraction, m->dominant_transition_fraction,
           m->justified_change_fraction, m->oscillation_penalty,
           m->dominant_old_action, m->dominant_new_action,
           m->changed_eligible_workers, m->justified_changed_workers,
           m->dominant_transition_count, m->rolling_window_burst_count,
           m->rolling_window_oscillation_count,
           m->current_directive_valid ? 1 : 0,
           m->previous_directive_valid ? 1 : 0,
           m->directive_transition_valid ? 1 : 0,
           m->large_burst_event ? 1 : 0,
           m->repeated_oscillation_event ? 1 : 0,
           machine ? controller_state_name(machine->state) : "UNKNOWN",
           machine ? controller_state_name(machine->previous_state) : "UNKNOWN",
           machine ? transition_reason_name(machine->transition_reason) : "UNKNOWN",
           machine ? (unsigned long long)machine->state_residence_time : 0,
           machine ? (unsigned long long)machine->valid_control_history_count : 0,
           machine ? (unsigned long long)machine->invalid_frame_fault_count : 0,
           machine ? machine->saturation_bitmask : 0,
           machine ? machine->saturation_direction : 0,
           machine ? (unsigned long long)machine->saturation_persistence : 0,
           machine ? machine->oscillation_score : 0.0,
           machine ? machine->oscillation_event : 0,
           machine ? machine->rollback_event : 0,
           machine ? transition_reason_name(machine->rollback_reason) : "UNKNOWN",
           machine ? machine->recovery_progress : 0.0,
           machine ? machine->last_known_good_available : 0,
           machine ? machine->requested.jitter_sigma : 0.0,
           machine ? machine->applied.jitter_sigma : 0.0,
           machine ? machine->requested.switch_penalty : 0.0,
           machine ? machine->applied.switch_penalty : 0.0,
           machine ? machine->requested.consensus_blend : 0.0,
           machine ? machine->applied.consensus_blend : 0.0,
           machine ? machine->update_accepted : 0,
           machine ? machine->update_suppressed : 0,
           machine ? machine->suppression_reason : 0,
           policy_mode_name((policy_mode_t)atomic_load_explicit(&bus->policy_mode, memory_order_relaxed)),
           POLICY_SCHEMA_VERSION,
           (unsigned long long)atomic_load_explicit(&g_policy_generation, memory_order_relaxed),
           atomic_load_explicit(&bus->policy_update_allowed, memory_order_relaxed),
           atomic_load_explicit(&g_policy_update_applied, memory_order_relaxed),
           policy_suppress_reason_name((policy_suppress_reason_t)atomic_load_explicit(&g_policy_suppression_reason, memory_order_relaxed)),
           atomic_load_explicit(&bus->policy_exploration_enabled, memory_order_relaxed),
           (unsigned long long)atomic_load_explicit(&g_policy_train_count, memory_order_relaxed),
           (unsigned long long)atomic_load_explicit(&g_policy_adapt_count, memory_order_relaxed),
           policy_load_status_name((policy_load_status_t)atomic_load_explicit(&g_policy_load_status, memory_order_relaxed)),
           policy_save_status_name((policy_save_status_t)atomic_load_explicit(&g_policy_save_status, memory_order_relaxed)),
           (unsigned long long)atomic_load_explicit(&g_policy_digest_prefix, memory_order_relaxed),
           POLICY_FORMAT_VERSION, 7u);
    fflush(stdout);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --workers N          total real processes, 4..64 (default %d)\n"
        "  --rt-exempt N        hard-RT bypass processes (default %d)\n"
        "  --duration SEC       run time, 0 until Ctrl+C (default %d)\n"
        "  --interval-ms MS     signal tick, 50..2000 (default %d)\n"
        "  --calibration SEC    pre-run calibration trace (default %d)\n"
        "  --mode orchestra     paper adaptive mode (default)\n"
        "  --mode baseline      instrumented observed-state reactive reference\n"
        "  --tamper-every N     corrupt each Nth signal; 0 disables\n"
        "  --seed N             worker-policy RNG seed; 0 selects and records one\n"
        "  --policy-mode MODE   train|adapt|evaluate (default train)\n"
        "  --policy-in PATH     load policy file\n"
        "  --policy-out PATH    save policy file at exit\n"
        "  --kernel-bridge PATH publish decisions through bridge --stream; disables userspace action emulation\n"
        "  --help\n",
        prog, DEFAULT_WORKERS, DEFAULT_RT_EXEMPT, DEFAULT_DURATION_SEC,
        DEFAULT_INTERVAL_MS, DEFAULT_CALIBRATION_SEC);
}

static bool parse_int_arg(const char *text, int *out) {
    if (text == NULL || *text == '\0' || isspace((unsigned char)*text))
        return false;
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < INT_MIN || value > INT_MAX)
        return false;
    *out = (int)value;
    return true;
}

static bool parse_u64_arg(const char *text, uint64_t *out) {
    if (text == NULL || *text == '\0' || text[0] == '-' ||
        isspace((unsigned char)*text))
        return false;
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *out = (uint64_t)value;
    return true;
}

int main(int argc, char **argv) {
    int worker_count = DEFAULT_WORKERS;
    int rt_exempt = DEFAULT_RT_EXEMPT;
    int duration_sec = DEFAULT_DURATION_SEC;
    int interval_ms = DEFAULT_INTERVAL_MS;
    int calibration_sec = DEFAULT_CALIBRATION_SEC;
    int tamper_every = 0;
    uint64_t requested_seed = 0;
    run_mode_t mode = MODE_ORCHESTRA;
    policy_mode_t policy_mode = POLICY_MODE_TRAIN;
    const char *policy_in_path = NULL;
    const char *policy_out_path = NULL;
    const char *kernel_bridge_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--workers") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &worker_count)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--rt-exempt") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &rt_exempt)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--duration") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &duration_sec)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--interval-ms") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &interval_ms)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--calibration") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &calibration_sec)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--tamper-every") && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &tamper_every)) { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            if (!parse_u64_arg(argv[++i], &requested_seed)) { usage(argv[0]); return 2; }
        }
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "orchestra")) mode = MODE_ORCHESTRA;
            else if (!strcmp(m, "baseline")) mode = MODE_BASELINE;
            else { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--policy-mode") && i + 1 < argc) {
            const char *pm = argv[++i];
            if (!strcmp(pm, "train")) policy_mode = POLICY_MODE_TRAIN;
            else if (!strcmp(pm, "adapt")) policy_mode = POLICY_MODE_ADAPT;
            else if (!strcmp(pm, "evaluate")) policy_mode = POLICY_MODE_EVALUATE;
            else { usage(argv[0]); return 2; }
        } else if (!strcmp(argv[i], "--policy-in") && i + 1 < argc) {
            policy_in_path = argv[++i];
        } else if (!strcmp(argv[i], "--policy-out") && i + 1 < argc) {
            policy_out_path = argv[++i];
        } else if (!strcmp(argv[i], "--kernel-bridge") && i + 1 < argc) {
            kernel_bridge_path = argv[++i];
        } else if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    if (worker_count < 4 || worker_count > MAX_WORKERS || rt_exempt < 0 || rt_exempt >= worker_count ||
        duration_sec < 0 || interval_ms < 50 || interval_ms > 2000 || calibration_sec < 1 ||
        tamper_every < 0) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "ORCHESTRA-OS paper-aligned real-CPU userspace prototype\n");
    fprintf(stderr, "Collecting %d-second calibration trace...\n", calibration_sec);
    double cal[MAX_CAL_SAMPLES] = {0};
    int cal_n = collect_calibration_trace(cal, calibration_sec);
    if (cal_n < 4) {
        fprintf(stderr, "Calibration failed: /proc/stat unavailable or insufficient samples.\n");
        return 1;
    }
    int calibration_count = (cal_n * 7) / 10;
    if (calibration_count < 4) calibration_count = 4;
    if (calibration_count >= cal_n) calibration_count = cal_n - 1;
    int evaluation_count = cal_n - calibration_count;
    double gain = calibrate_fixed_gain(cal, calibration_count);
    double held_out_mse = evaluate_fixed_gain_held_out(
        cal, calibration_count, cal + calibration_count, evaluation_count, gain);
    double sigma_obs = robust_noise_sigma(cal, cal_n);
    if (!isfinite(held_out_mse)) {
        fprintf(stderr, "Predictor held-out evaluation failed.\n");
        return 1;
    }
    double jitter_floor = JITTER_MULTIPLIER_DEFAULT * sigma_obs;
    if (jitter_floor > 0.20) jitter_floor = 0.20;
    fprintf(stderr, "predictor_model=%u calibration_samples=%d evaluation_samples=%d fixed_gain=%.3f held_out_mse=%.8f sigma_obs=%.5f jitter_floor=%.5f\n",
            PREDICTOR_MODEL_VERSION, calibration_count, evaluation_count, gain,
            held_out_mse, sigma_obs, jitter_floor);

    uint64_t effective_seed = requested_seed;
    if (effective_seed == 0)
        effective_seed = monotonic_ns() ^ ((uint64_t)getpid() << 32);
    if (effective_seed == 0) effective_seed = 1;

    uint8_t master[MASTER_KEY_SIZE];
    if (!random_key(master)) {
        fprintf(stderr, "Cannot obtain cryptographic key from /dev/urandom.\n");
        return 1;
    }

    signal_bus_t *bus = mmap(NULL, sizeof(*bus), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    signal_reader_gates_t *gates = mmap(NULL, sizeof(*gates), PROT_READ | PROT_WRITE,
                                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    worker_block_t *workers = mmap(NULL, sizeof(*workers), PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (bus == MAP_FAILED || gates == MAP_FAILED || workers == MAP_FAILED) {
        perror("mmap");
        if (bus != MAP_FAILED) munmap(bus, sizeof(*bus));
        if (gates != MAP_FAILED) munmap(gates, sizeof(*gates));
        if (workers != MAP_FAILED) munmap(workers, sizeof(*workers));
        explicit_bzero(master, sizeof(master));
        return 1;
    }
    initialize_shared_state(bus, gates, workers, worker_count, rt_exempt);

    atomic_store_explicit(&bus->policy_mode, policy_mode, memory_order_release);
    atomic_store_explicit(&bus->policy_update_allowed,
        policy_update_is_allowed(policy_mode, CONTROL_STATE_NORMAL, true),
        memory_order_release);
    atomic_store_explicit(&bus->policy_exploration_enabled,
        policy_exploration_enabled_for_mode(policy_mode), memory_order_release);
    atomic_store_explicit(&bus->policy_consensus_enabled,
        policy_consensus_enabled_for_mode(policy_mode), memory_order_release);

    if (policy_in_path) {
        FILE *pf = fopen(policy_in_path, "rb");
        if (!pf) {
            atomic_store_explicit(&g_policy_load_status, POLICY_LOAD_MISSING,
                                  memory_order_release);
            fprintf(stderr, "Policy file not found: %s\n", policy_in_path);
            munmap(bus, sizeof(*bus));
            munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers));
            explicit_bzero(master, sizeof(master));
            return 1;
        }
        if (fseek(pf, 0, SEEK_END) != 0) {
            fclose(pf);
            fprintf(stderr, "Policy seek failed\n");
            munmap(bus, sizeof(*bus)); munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers)); explicit_bzero(master, sizeof(master));
            return 1;
        }
        long fsz = ftell(pf);
        if (fseek(pf, 0, SEEK_SET) != 0) {
            fclose(pf);
            fprintf(stderr, "Policy rewind failed\n");
            munmap(bus, sizeof(*bus)); munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers)); explicit_bzero(master, sizeof(master));
            return 1;
        }
        if (fsz <= 0 || (uint64_t)fsz > (uint64_t)POLICY_MAX_FILE_SIZE) {
            fclose(pf);
            atomic_store_explicit(&g_policy_load_status, POLICY_LOAD_OVERSIZED,
                                  memory_order_release);
            fprintf(stderr, "Policy file invalid size\n");
            munmap(bus, sizeof(*bus));
            munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers));
            explicit_bzero(master, sizeof(master));
            return 1;
        }
        uint8_t *fbuf = malloc((size_t)fsz);
        if (!fbuf) { fclose(pf); munmap(bus, sizeof(*bus)); munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers)); explicit_bzero(master, sizeof(master)); return 1; }
        size_t fr = fread(fbuf, 1u, (size_t)fsz, pf);
        fclose(pf);
        if (fr != (size_t)fsz) {
            free(fbuf); atomic_store_explicit(&g_policy_load_status, POLICY_LOAD_TRUNCATED,
                                               memory_order_release);
            fprintf(stderr, "Policy partial read\n");
            munmap(bus, sizeof(*bus)); munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers)); explicit_bzero(master, sizeof(master)); return 1;
        }
        uint64_t pgen = 0, ptrain = 0, padapt = 0;
        double loaded[QTABLE_SIZE];
        policy_load_status_t lstatus = policy_deserialize(fbuf, (size_t)fsz, loaded,
                                                           &pgen, &ptrain, &padapt);
        free(fbuf);
        atomic_store_explicit(&g_policy_load_status, lstatus, memory_order_release);
        if (lstatus != POLICY_LOAD_OK) {
            fprintf(stderr, "Policy load failed: %s\n", policy_load_status_name(lstatus));
            munmap(bus, sizeof(*bus)); munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers)); explicit_bzero(master, sizeof(master)); return 1;
        }
        for (int i = 0; i < worker_count; ++i)
            memcpy(workers->worker[i].qtable, loaded, sizeof(loaded));
        atomic_store_explicit(&g_policy_generation, pgen, memory_order_release);
        atomic_store_explicit(&g_policy_train_count, ptrain, memory_order_release);
        atomic_store_explicit(&g_policy_adapt_count, padapt, memory_order_release);
        uint64_t dp = 0;
        memcpy(&dp, loaded, sizeof(uint64_t));
        atomic_store_explicit(&g_policy_digest_prefix, dp, memory_order_release);
    } else {
        atomic_store_explicit(&g_policy_load_status, POLICY_LOAD_SKIP, memory_order_release);
    }

    if (!shared_atomics_are_lock_free(bus, gates, workers)) {
        fprintf(stderr, "Required process-shared atomics are not lock-free on this platform.\n");
        munmap(bus, sizeof(*bus));
        munmap(gates, sizeof(*gates));
        munmap(workers, sizeof(*workers));
        explicit_bzero(master, sizeof(master));
        return 1;
    }

    pid_t pids[MAX_WORKERS] = {0};
    int spawned_workers = 0;
    for (int i = 0; i < worker_count; ++i) {
        if (spawn_worker(bus, gates, workers, i, master, mode, effective_seed,
                         &pids[i]) != 0) {
            perror("fork");
            stop_and_reap_workers(bus, pids, spawned_workers);
            munmap(bus, sizeof(*bus));
            munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers));
            explicit_bzero(master, sizeof(master));
            return 1;
        }
        spawned_workers++;
    }

    if (!wait_for_workers_ready(workers, 1000)) {
        fprintf(stderr, "Workers did not become ready within 1000 ms.\n");
        stop_and_reap_workers(bus, pids, spawned_workers);
        munmap(bus, sizeof(*bus));
        munmap(gates, sizeof(*gates));
        munmap(workers, sizeof(*workers));
        explicit_bzero(master, sizeof(master));
        return 1;
    }

    kernel_bridge_session_t kernel_session = {
        .request_fd = -1, .response_fd = -1
    };
    if (kernel_bridge_path) {
        if (!kernel_bridge_start(&kernel_session, kernel_bridge_path)) {
            fprintf(stderr, "Cannot start kernel bridge stream %s: %s\n",
                    kernel_bridge_path, strerror(errno));
            stop_and_reap_workers(bus, pids, spawned_workers);
            munmap(bus, sizeof(*bus));
            munmap(gates, sizeof(*gates));
            munmap(workers, sizeof(*workers));
            explicit_bzero(master, sizeof(master));
            return 1;
        }
        atomic_store_explicit(&bus->kernel_bridge_enabled, 1,
                              memory_order_release);
    }

    int admitted_rt_exempt = actual_rt_exempt_count(workers);
    fprintf(stderr, "workers=%d eligible=%d rt-exempt-requested=%d rt-exempt-admitted=%d mode=%s tick=%dms tamper-every=%d seed=%llu signal-schema=%u state-schema=%u policy-mode=%s\n",
            worker_count, worker_count - admitted_rt_exempt, rt_exempt,
            admitted_rt_exempt,
            mode == MODE_ORCHESTRA ? "orchestra" : "baseline", interval_ms, tamper_every,
            (unsigned long long)effective_seed, SIGNAL_SCHEMA_VERSION, STATE_SCHEMA_VERSION,
            policy_mode_name(policy_mode));
    fprintf(stderr, "CSV metrics are written to stdout.\n\n");

    cpu_sample_t prev_cpu_sample, cur_cpu_sample;
    if (!read_cpu_sample(&prev_cpu_sample)) {
        fprintf(stderr, "Cannot read /proc/stat; Linux is required.\n");
        stop_and_reap_workers(bus, pids, spawned_workers);
        munmap(bus, sizeof(*bus));
        munmap(gates, sizeof(*gates));
        munmap(workers, sizeof(*workers));
        explicit_bzero(master, sizeof(master));
        kernel_bridge_stop(&kernel_session);
        return 1;
    }

    double estimate = cal[cal_n - 1];
    double thermal = 0.20;
    double last_prediction = estimate;
    uint64_t tick = 0;
    uint64_t controller_step = 0;
    uint64_t missed_deadlines = 0;
    burst_tracker_t burst_tracker = {0};
    controller_machine_t machine = {0};
    controller_window_t window = {0};
    oscillation_window_t osc_window = {0};
    
    machine.state = CONTROL_STATE_NORMAL;
    machine.applied.jitter_sigma = jitter_floor;
    machine.requested.jitter_sigma = jitter_floor;
    
    uint64_t interval_ns = (uint64_t)(unsigned int)interval_ms * 1000000ull;
    uint64_t start_ns = monotonic_ns();
    uint64_t next_publish_ns = start_ns + interval_ns;

    signal_payload_t frame = {
        .magic = SIGNAL_MAGIC,
        .schema_version = SIGNAL_SCHEMA_VERSION,
        .tier = SIGNAL_TIER_CORE_LOCAL,
        .source_id = SIGNAL_SOURCE_LOCAL,
        .sequence = 0,
        .max_age_ns = interval_ns * FRAME_MAX_AGE_MULTIPLIER,
        .key_epoch = 0,
        .directive = ACT_RUN,
        .state_schema_version = STATE_SCHEMA_VERSION,
        .prediction_used = 0,
        .cpu_now = estimate,
        .cpu_pred = estimate,
        .decision_cpu = estimate,
        .memory_pressure = read_memory_pressure(),
        .thermal_proxy = thermal,
        .confidence = 0.95,
        .jitter_sigma = jitter_floor,
        .switch_penalty = 0.0,
        .consensus_blend = 0.0
    };

    print_header();

    while (!g_stop && !atomic_load(&bus->stop)) {
        sleep_until_ns(next_publish_ns);
        if (g_stop || atomic_load(&bus->stop)) break;
        if (!read_cpu_sample(&cur_cpu_sample)) {
            next_publish_ns += interval_ns;
            continue;
        }
        double cpu_now = cpu_usage_between(&prev_cpu_sample, &cur_cpu_sample);
        prev_cpu_sample = cur_cpu_sample;

        double forecast_error = fabs(cpu_now - last_prediction);
        double innovation = cpu_now - estimate;
        estimate = clamp01(estimate + gain * innovation);
        double cpu_pred = estimate; /* fixed-gain, one-step local-level forecast */
        last_prediction = cpu_pred;
        thermal = clamp01(0.95 * thermal + 0.05 * cpu_now);
        double mem = read_memory_pressure();
        double confidence = calibrated_prediction_confidence(
            forecast_error, held_out_mse, sigma_obs);

        if (tick == UINT64_MAX) {
            fprintf(stderr, "Signal sequence exhausted; stopping without publication wrap.\n");
            break;
        }
        uint64_t candidate_sequence = tick + UINT64_C(1);
        frame.sequence = candidate_sequence;
        frame.monotonic_ns = monotonic_ns();
        frame.key_epoch = (uint32_t)(candidate_sequence / KEY_EPOCH_TICKS);
        frame.cpu_now = cpu_now;
        frame.cpu_pred = cpu_pred;
        frame.decision_cpu = decision_cpu_value(mode, cpu_now, cpu_pred, confidence,
                                                &frame.prediction_used);
        frame.memory_pressure = mem;
        frame.thermal_proxy = thermal;
        frame.confidence = mode == MODE_BASELINE ? 1.0 : confidence;
        {
            action_t canonical = directive_from_signal(frame.decision_cpu, mem,
                                                        thermal);
            uint32_t wire_action = ORCHESTRA_ACTION_RUN;
            if (!canonical_action_to_wire(canonical, &wire_action))
                wire_action = ORCHESTRA_ACTION_RUN;
            frame.directive = wire_action;
        }

        signal_payload_t applied = frame;

        bool tamper = tamper_every > 0
                   && (candidate_sequence % (uint64_t)tamper_every == 0u);
        signal_publish_result_t publication = publish_frame(bus, gates, &applied, master,
                                                            tamper);
        if (publication != SIGNAL_PUBLISH_OK) {
            if (publication == SIGNAL_PUBLISH_GENERATION_EXHAUSTED) {
                fprintf(stderr,
                        "Signal publication generation exhausted; entering bounded stop path.\n");
                break;
            }
            uint64_t failed_now = monotonic_ns();
            next_publish_ns += interval_ns;
            while (next_publish_ns <= failed_now) {
                next_publish_ns += interval_ns;
                missed_deadlines++;
            }
            if (duration_sec > 0
                && failed_now - start_ns
                       >= (uint64_t)(unsigned int)duration_sec * UINT64_C(1000000000)) {
                break;
            }
            continue;
        }
        tick = candidate_sequence;

        /* Let workers observe the new signal and choose/execute an action. */
        sleep_until_ns(next_publish_ns + interval_ns / 2u);
        /* Worker actions are asynchronous. Give every eligible worker the
         * remainder of this publication interval to complete the current
         * sequence before taking the coordination snapshot. A genuinely late
         * worker remains incomplete and is reflected by S1/S2 and deadline
         * telemetry; the wait never crosses the next publication deadline. */
        if (mode == MODE_BASELINE)
            wait_for_worker_sequence(workers, applied.sequence,
                                     next_publish_ns + interval_ns);

        double metric_forecast_error = mode == MODE_BASELINE ? 0.0 : forecast_error;
        coord_metrics_t metrics = compute_metrics(workers, &applied,
                                                  metric_forecast_error, interval_ms,
                                                  &burst_tracker);
        bool complete_accepted_sample = coordination_sample_complete(workers,
                                                                     &metrics);

        /* Publish the measured fixed-point signal before its per-task
         * directives.  The first stream record carries the signal; all
         * following records require that same current frame, so BPF ownership
         * is fail-closed if the signal is stale or incoherent. */
        if (kernel_session.active
            && !publish_kernel_decisions(&kernel_session, workers, pids,
                                         applied.sequence,
                                         applied.max_age_ns, bus,
                                         &applied, &metrics)) {
            fprintf(stderr, "Kernel bridge stream failed; reverting workers to userspace action semantics\n");
            atomic_store_explicit(&bus->kernel_bridge_enabled, 0,
                                  memory_order_release);
            kernel_bridge_stop(&kernel_session);
        }

        /* Invalid, stale, rejected, or partial frames cannot become a future
         * directive-transition justification or an oscillation history entry. */
        burst_tracker_record(&burst_tracker, &applied, &metrics);
        if (mode == MODE_ORCHESTRA && complete_accepted_sample)
            assign_difference_rewards(workers, &applied);

        signal_payload_t next = applied;
        controller_event_t event = {
            .updated = false,
            .step = controller_step,
            .reason = CONTROL_REASON_NONE
        };

         if (mode == MODE_ORCHESTRA && tick % CONTROLLER_PERIOD_TICKS == 0) {
             bool frame_valid = metric_frame_is_valid_and_fresh(&frame) && complete_accepted_sample;
             if (frame_valid) controller_step++;
             event = controller_machine_update(&next, &metrics, jitter_floor, controller_step, &machine, &window, &osc_window, frame_valid);
             if (event.updated) {
                 int consensus_ok = atomic_load_explicit(&bus->policy_consensus_enabled,
                                                         memory_order_relaxed);
                 event.consensus_applied = consensus_ok ? consensus_blend_qtables(bus, workers,
                                                       next.consensus_blend) : 0;
             }
             atomic_store_explicit(&bus->controller_state, machine.state, memory_order_release);
             {
                 policy_mode_t pm;
                 int allowed, suppr;
                 pm = (policy_mode_t)atomic_load_explicit(&bus->policy_mode,
                                                          memory_order_relaxed);
                 allowed = policy_update_is_allowed(pm, machine.state, frame_valid);
                 atomic_store_explicit(&bus->policy_update_allowed, allowed, memory_order_release);
                 atomic_store_explicit(&bus->policy_exploration_enabled,
                                        policy_exploration_enabled_for_mode(pm),
                                        memory_order_release);
                 atomic_store_explicit(&bus->policy_consensus_enabled,
                                        policy_consensus_enabled_for_mode(pm),
                                        memory_order_release);
                 suppr = POLICY_SUPPRESS_NONE;
                 if (pm == POLICY_MODE_EVALUATE) suppr = POLICY_SUPPRESS_MODE;
                 else if (!frame_valid) suppr = POLICY_SUPPRESS_FRAME_INVALID;
                 else if (!allowed) suppr = POLICY_SUPPRESS_STATE;
                 if (allowed && event.updated) {
                     atomic_store_explicit(&g_policy_update_applied, 1, memory_order_release);
                     atomic_fetch_add_explicit(&g_policy_generation, UINT64_C(1),
                                                memory_order_release);
                     if (pm == POLICY_MODE_TRAIN)
                         atomic_fetch_add_explicit(&g_policy_train_count, UINT64_C(1),
                                                    memory_order_release);
                     else if (pm == POLICY_MODE_ADAPT)
                         atomic_fetch_add_explicit(&g_policy_adapt_count, UINT64_C(1),
                                                    memory_order_release);
                 } else {
                     atomic_store_explicit(&g_policy_update_applied, 0, memory_order_release);
                 }
                 atomic_store_explicit(&g_policy_suppression_reason, suppr,
                                        memory_order_release);
             }
         }

        frame.jitter_sigma = next.jitter_sigma;
        frame.switch_penalty = next.switch_penalty;
        frame.consensus_blend = next.consensus_blend;

        uint64_t now = monotonic_ns();
        next_publish_ns += interval_ns;
        while (next_publish_ns <= now) {
            next_publish_ns += interval_ns;
            missed_deadlines++;
        }

        print_row(tick, mode, &applied, &next, &metrics, &event,
                  metric_forecast_error, missed_deadlines, workers, &machine, bus);

        now = monotonic_ns();
        if (duration_sec > 0
            && now - start_ns >= (uint64_t)(unsigned int)duration_sec * 1000000000ull)
            break;
    }

    stop_and_reap_workers(bus, pids, spawned_workers);
    kernel_bridge_stop(&kernel_session);
    print_signal_publication_diagnostics(bus, workers);

    if (policy_out_path) {
        double save_q[QTABLE_SIZE];
        int policy_workers = aggregate_adaptive_policy(workers, save_q);
        if (policy_workers == 0) {
            fprintf(stderr, "Policy save failed: no adaptive worker policy exists\n");
            atomic_store_explicit(&g_policy_save_status,
                                  POLICY_SAVE_VALIDATION_FAILED,
                                  memory_order_release);
        } else {
        uint64_t pgen = atomic_load_explicit(&g_policy_generation, memory_order_relaxed);
        uint64_t ptrain = atomic_load_explicit(&g_policy_train_count, memory_order_relaxed);
        uint64_t padapt = atomic_load_explicit(&g_policy_adapt_count, memory_order_relaxed);
        policy_save_status_t sstatus = policy_save_atomic(save_q, policy_out_path, pgen,
                                                           ptrain, padapt);
        atomic_store_explicit(&g_policy_save_status, sstatus, memory_order_release);
        if (sstatus != POLICY_SAVE_OK)
            fprintf(stderr, "Policy save failed: %s\n", policy_save_status_name(sstatus));
        else
            fprintf(stderr, "Policy saved: %s\n", policy_out_path);
        }
    }

    fprintf(stderr, "Policy mode=%s load=%s save=%s suppr=%s\n",
            policy_mode_name(policy_mode),
            policy_load_status_name((policy_load_status_t)atomic_load_explicit(
                &g_policy_load_status, memory_order_relaxed)),
            policy_save_status_name((policy_save_status_t)atomic_load_explicit(
                &g_policy_save_status, memory_order_relaxed)),
            policy_suppress_reason_name((policy_suppress_reason_t)atomic_load_explicit(
                &g_policy_suppression_reason, memory_order_relaxed)));

    munmap(bus, sizeof(*bus));
    munmap(gates, sizeof(*gates));
    munmap(workers, sizeof(*workers));
    explicit_bzero(master, sizeof(master));
    fprintf(stderr, "Finished.\n");
    return 0;
}
