/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ORCHESTRA-OS privileged sched_ext bridge, ABI v2.
 *
 * The bridge uses raw bpf(2) operations so its safety checks do not depend on
 * a particular libbpf helper version.  Writers serialize with flock(2), map
 * values are published/read with BPF_F_LOCK, and map IDs are pinned only when
 * the operator supplies every exact ID.  There is no global prefix scan.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/bpf.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "orchestra_bridge_v1.h"

#define BRIDGE_PIN_DIR       "/sys/fs/bpf/orchestra"
#define BRIDGE_CTL_PATH      BRIDGE_PIN_DIR "/" BRIDGE_CTL_MAP_NAME
#define BRIDGE_DIR_PATH      BRIDGE_PIN_DIR "/" BRIDGE_DIR_MAP_NAME
#define BRIDGE_ID_PATH       BRIDGE_PIN_DIR "/" BRIDGE_ID_MAP_NAME
#define BRIDGE_TASK_PATH     BRIDGE_PIN_DIR "/" BRIDGE_TASK_MAP_NAME
#define BRIDGE_TASK_TEL_PATH BRIDGE_PIN_DIR "/" BRIDGE_TASK_TEL_MAP_NAME
#define BRIDGE_TEL_PATH      BRIDGE_PIN_DIR "/" BRIDGE_TEL_MAP_NAME
#define BRIDGE_DEFER_PATH    BRIDGE_PIN_DIR "/" BRIDGE_DEFER_MAP_NAME
#define BRIDGE_RUNTIME_V8_PATH BRIDGE_PIN_DIR "/" BRIDGE_RUNTIME_V8_MAP_NAME
#define BRIDGE_POLICY_META_V8_PATH \
    BRIDGE_PIN_DIR "/" BRIDGE_POLICY_META_V8_MAP_NAME
#define BRIDGE_POLICY_ENTRY_V8_PATH \
    BRIDGE_PIN_DIR "/" BRIDGE_POLICY_ENTRY_V8_MAP_NAME
#define BRIDGE_TASK_V8_PATH BRIDGE_PIN_DIR "/" BRIDGE_TASK_V8_MAP_NAME
#define BRIDGE_DIAG_V8_PATH BRIDGE_PIN_DIR "/" BRIDGE_DIAG_V8_MAP_NAME
#define BRIDGE_TEL_V8_PATH BRIDGE_PIN_DIR "/" BRIDGE_TEL_V8_MAP_NAME
#define BRIDGE_LOCK_PATH     "/run/lock/orchestra_bridge.lock"
#define BRIDGE_OPS_NAME      "orchestra_scx_v8"
#define BRIDGE_LEGACY_OPS_NAME "orchestra_scx_stage7"
#define DEFAULT_EXPIRY_NS    UINT64_C(30000000000)
#define DEFAULT_LEASE_NS     UINT64_C(30000000000)
#define DEFAULT_SIGNAL_MAX_AGE_NS UINT64_C(1000000000)

enum exit_code {
    EXIT_OK = 0,
    EXIT_ARGS = 1,
    EXIT_NO_SCHED = 2,
    EXIT_MAP_MISSING = 3,
    EXIT_SCHEMA = 4,
    EXIT_POLICY = 5,
    EXIT_TASK = 6,
    EXIT_ACTION = 7,
    EXIT_PUB_FAIL = 8,
    EXIT_PERM = 9,
    EXIT_GEN_OVERFLOW = 10
};

enum map_role {
    MAP_CONTROL,
    MAP_DIRECTIVE,
    MAP_IDENTITY,
    MAP_TASK_STATE,
    MAP_TASK_TELEMETRY,
    MAP_TELEMETRY,
    MAP_DEFER_TIMER,
    MAP_SIGNAL,
    MAP_RUNTIME_V8,
    MAP_POLICY_META_V8,
    MAP_POLICY_ENTRY_V8,
    MAP_TASK_V8,
    MAP_DIAG_V8,
    MAP_TEL_V8,
    MAP_COORD_V10,
    MAP_COORD_CPU_V10,
    MAP_CONTROLLER_V10,
    MAP_CONTROLLER_TEL_V10,
    MAP_RUNTIME_V10,
    MAP_TASK_COORD_V10,
    MAP_ROLE_COUNT
};

struct map_spec {
    const char *name;
    const char *path;
    enum bpf_map_type type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
};

static const struct map_spec map_specs[MAP_ROLE_COUNT] = {
    [MAP_CONTROL] = { BRIDGE_CTL_MAP_NAME, BRIDGE_CTL_PATH,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t), sizeof(struct bridge_control), 1 },
    [MAP_DIRECTIVE] = { BRIDGE_DIR_MAP_NAME, BRIDGE_DIR_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct bridge_directive), BRIDGE_MAX_TASKS },
    [MAP_IDENTITY] = { BRIDGE_ID_MAP_NAME, BRIDGE_ID_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_pid_key),
        sizeof(struct bridge_identity_record), BRIDGE_MAX_TASKS },
    [MAP_TASK_STATE] = { BRIDGE_TASK_MAP_NAME, BRIDGE_TASK_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct bridge_task_state), BRIDGE_MAX_TASKS },
    [MAP_TASK_TELEMETRY] = { BRIDGE_TASK_TEL_MAP_NAME, BRIDGE_TASK_TEL_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct bridge_task_telemetry), BRIDGE_MAX_TASKS },
    [MAP_TELEMETRY] = { BRIDGE_TEL_MAP_NAME, BRIDGE_TEL_PATH,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct bridge_telemetry), 1 },
    [MAP_DEFER_TIMER] = { BRIDGE_DEFER_MAP_NAME, BRIDGE_DEFER_PATH,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct bridge_defer_timer), 1 },
    [MAP_SIGNAL] = { BRIDGE_SIGNAL_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_SIGNAL_MAP_NAME,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct bridge_signal_frame), 1 }
    , [MAP_RUNTIME_V8] = { BRIDGE_RUNTIME_V8_MAP_NAME, BRIDGE_RUNTIME_V8_PATH,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_runtime_state_v8), 1 }
    , [MAP_POLICY_META_V8] = { BRIDGE_POLICY_META_V8_MAP_NAME,
        BRIDGE_POLICY_META_V8_PATH, BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_policy_meta_v8), 1 }
    , [MAP_POLICY_ENTRY_V8] = { BRIDGE_POLICY_ENTRY_V8_MAP_NAME,
        BRIDGE_POLICY_ENTRY_V8_PATH, BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_policy_entry_v8),
        ORCHESTRA_KERNEL_POLICY_ENTRY_COUNT }
    , [MAP_TASK_V8] = { BRIDGE_TASK_V8_MAP_NAME, BRIDGE_TASK_V8_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct orchestra_task_hot_v8), BRIDGE_MAX_TASKS }
    , [MAP_DIAG_V8] = { BRIDGE_DIAG_V8_MAP_NAME, BRIDGE_DIAG_V8_PATH,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct orchestra_task_diag_v8), BRIDGE_MAX_TASKS }
    , [MAP_TEL_V8] = { BRIDGE_TEL_V8_MAP_NAME, BRIDGE_TEL_V8_PATH,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_telemetry_v8), 1 }
    , [MAP_COORD_V10] = { BRIDGE_COORD_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_COORD_V10_MAP_NAME,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_coordination_state_v10),
        ORCHESTRA_COORD_MAP_ENTRY_COUNT }
    , [MAP_COORD_CPU_V10] = { BRIDGE_COORD_CPU_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_COORD_CPU_V10_MAP_NAME,
        BPF_MAP_TYPE_PERCPU_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_coord_cpu_v10), 1 }
    , [MAP_CONTROLLER_V10] = { BRIDGE_CONTROLLER_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_CONTROLLER_V10_MAP_NAME,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_controller_state_v10), 1 }
    , [MAP_CONTROLLER_TEL_V10] = { BRIDGE_CONTROLLER_TEL_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_CONTROLLER_TEL_V10_MAP_NAME,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_controller_telemetry_v10), 1 }
    , [MAP_RUNTIME_V10] = { BRIDGE_RUNTIME_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_RUNTIME_V10_MAP_NAME,
        BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
        sizeof(struct orchestra_runtime_state_v10), 1 }
    , [MAP_TASK_COORD_V10] = { BRIDGE_TASK_COORD_V10_MAP_NAME,
        BRIDGE_PIN_DIR "/" BRIDGE_TASK_COORD_V10_MAP_NAME,
        BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
        sizeof(struct orchestra_task_coord_v10), BRIDGE_MAX_TASKS }
};

struct map_set {
    int fd[MAP_ROLE_COUNT];
};

struct task_proc_info {
    uint32_t tgid;
    uint64_t start_ticks;
};

struct options {
    bool status;
    bool publish;
    bool clear;
    bool opt_in;
    bool pin_maps;
    bool stream;
    bool policy_entry;
    bool policy_commit;
    bool signal_publish;
    bool require_signal;
    bool dry_run;
    bool quiet;
    bool target_set;
    bool policy_state_set;
    bool controller_set;
    bool policy_mode_set;
    enum orchestra_action_id action;
    bool action_set;
    uint32_t target_tid;
    uint32_t target_cpu;
    uint32_t policy_state_index;
    uint64_t slice_ns;
    uint64_t not_before_ns;
    uint64_t throttle_period_ns;
    uint64_t throttle_budget_ns;
    uint64_t expiry_duration_ns;
    uint64_t lease_ns;
    uint32_t controller_state;
    uint32_t policy_mode;
    uint64_t policy_generation;
    uint64_t signal_sequence;
    uint64_t signal_max_age_ns;
    uint32_t signal_tier;
    uint32_t signal_source_id;
    uint32_t signal_key_epoch;
    uint32_t signal_directive;
    uint32_t signal_state_schema_version;
    uint32_t signal_prediction_used;
    uint32_t signal_confidence_permille;
    uint32_t signal_cpu_now_permille;
    uint32_t signal_cpu_pred_permille;
    uint32_t signal_decision_cpu_permille;
    uint32_t signal_memory_pressure_permille;
    uint32_t signal_thermal_permille;
    uint32_t signal_s1_permille;
    uint32_t signal_s2_permille;
    uint32_t signal_s3_permille;
    uint32_t signal_s4_permille;
    uint32_t signal_q_permille;
    uint32_t map_ids[MAP_ROLE_COUNT];
};

static const char *const action_names[ORCHESTRA_ACTION_COUNT] = {
    [ORCHESTRA_ACTION_RUN] = "RUN",
    [ORCHESTRA_ACTION_SLEEP] = "SLEEP",
    [ORCHESTRA_ACTION_MIGRATE] = "MIGRATE",
    [ORCHESTRA_ACTION_THROTTLE] = "THROTTLE",
    [ORCHESTRA_ACTION_YIELD] = "YIELD"
};

static const char *const controller_names[ORCHESTRA_CTRL_COUNT] = {
    "NORMAL", "DEGRADED", "SATURATED", "DISABLED", "ROLLBACK", "RECOVERY"
};

static int bpf_call(enum bpf_cmd cmd, union bpf_attr *attr)
{
    return (int)syscall(__NR_bpf, cmd, attr, sizeof(*attr));
}

static int bpf_obj_get_raw(const char *path)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.pathname = (uint64_t)(uintptr_t)path;
    return bpf_call(BPF_OBJ_GET, &attr);
}

static int bpf_map_fd_by_id(uint32_t id)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_id = id;
    return bpf_call(BPF_MAP_GET_FD_BY_ID, &attr);
}

static int bpf_obj_pin_raw(int fd, const char *path)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.pathname = (uint64_t)(uintptr_t)path;
    attr.bpf_fd = (uint32_t)fd;
    return bpf_call(BPF_OBJ_PIN, &attr);
}

static int bpf_info_raw(int fd, struct bpf_map_info *info)
{
    union bpf_attr attr;
    uint32_t len = sizeof(*info);

    memset(info, 0, sizeof(*info));
    memset(&attr, 0, sizeof(attr));
    attr.info.bpf_fd = (uint32_t)fd;
    attr.info.info_len = len;
    attr.info.info = (uint64_t)(uintptr_t)info;
    return bpf_call(BPF_OBJ_GET_INFO_BY_FD, &attr);
}

static int bpf_lookup_raw(int fd, const void *key, void *value, uint64_t flags)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = (uint32_t)fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.value = (uint64_t)(uintptr_t)value;
    attr.flags = flags;
    return bpf_call(BPF_MAP_LOOKUP_ELEM, &attr);
}

static int bpf_update_raw(int fd, const void *key, const void *value,
                          uint64_t flags)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = (uint32_t)fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.value = (uint64_t)(uintptr_t)value;
    attr.flags = flags;
    return bpf_call(BPF_MAP_UPDATE_ELEM, &attr);
}

static int bpf_delete_raw(int fd, const void *key)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = (uint32_t)fd;
    attr.key = (uint64_t)(uintptr_t)key;
    return bpf_call(BPF_MAP_DELETE_ELEM, &attr);
}

static int bpf_next_key_raw(int fd, const void *key, void *next_key)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = (uint32_t)fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.next_key = (uint64_t)(uintptr_t)next_key;
    return bpf_call(BPF_MAP_GET_NEXT_KEY, &attr);
}

static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (UINT64_MAX - a < b)
        return false;
    *out = a + b;
    return true;
}

static bool monotonic_ns(uint64_t *out)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0 || ts.tv_sec < 0)
        return false;
    if ((uint64_t)ts.tv_sec > UINT64_MAX / UINT64_C(1000000000))
        return false;
    return checked_add_u64((uint64_t)ts.tv_sec * UINT64_C(1000000000),
                           (uint64_t)ts.tv_nsec, out);
}

static bool parse_u64(const char *text, uint64_t min, uint64_t max,
                      uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (!text || !*text || isspace((unsigned char)text[0]) || text[0] == '-')
        return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max)
        return false;
    *out = (uint64_t)value;
    return true;
}

static bool parse_u32(const char *text, uint32_t min, uint32_t max,
                      uint32_t *out)
{
    uint64_t value;

    if (!parse_u64(text, min, max, &value))
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool canonical_to_wire(enum orchestra_action_id action, uint32_t *wire)
{
    switch (action) {
    case ORCHESTRA_ACTION_RUN:      *wire = ORCHESTRA_ACTION_RUN; return true;
    case ORCHESTRA_ACTION_SLEEP:    *wire = ORCHESTRA_ACTION_SLEEP; return true;
    case ORCHESTRA_ACTION_MIGRATE:  *wire = ORCHESTRA_ACTION_MIGRATE; return true;
    case ORCHESTRA_ACTION_THROTTLE: *wire = ORCHESTRA_ACTION_THROTTLE; return true;
    case ORCHESTRA_ACTION_YIELD:    *wire = ORCHESTRA_ACTION_YIELD; return true;
    default: return false;
    }
}

static bool parse_action(const char *text, enum orchestra_action_id *action)
{
    uint32_t i;

    for (i = 0; i < ORCHESTRA_ACTION_COUNT; i++) {
        if (action_names[i] && strcasecmp(text, action_names[i]) == 0) {
            *action = (enum orchestra_action_id)i;
            return true;
        }
    }
    return false;
}

static bool parse_controller(const char *text, uint32_t *state)
{
    uint32_t i;

    for (i = 0; i < ORCHESTRA_CTRL_COUNT; i++) {
        if (strcasecmp(text, controller_names[i]) == 0) {
            *state = i;
            return true;
        }
    }
    return false;
}

static void map_set_init(struct map_set *maps)
{
    size_t i;

    for (i = 0; i < MAP_ROLE_COUNT; i++)
        maps->fd[i] = -1;
}

static void map_set_close(struct map_set *maps)
{
    size_t i;

    for (i = 0; i < MAP_ROLE_COUNT; i++) {
        if (maps->fd[i] >= 0) {
            close(maps->fd[i]);
            maps->fd[i] = -1;
        }
    }
}

static bool map_matches(int fd, const struct map_spec *spec,
                        struct bpf_map_info *out)
{
    struct bpf_map_info info;

    if (bpf_info_raw(fd, &info) != 0)
        return false;
    if (strncmp((const char *)info.name, spec->name, BPF_OBJ_NAME_LEN) != 0 ||
        info.type != (uint32_t)spec->type || info.key_size != spec->key_size ||
        info.value_size != spec->value_size ||
        info.max_entries != spec->max_entries)
        return false;
    if (out)
        *out = info;
    return true;
}

static int open_checked(enum map_role role)
{
    int fd = bpf_obj_get_raw(map_specs[role].path);

    if (fd < 0)
        return -1;
    if (!map_matches(fd, &map_specs[role], NULL)) {
        close(fd);
        errno = EPROTO;
        return -1;
    }
    return fd;
}

static bool open_runtime_maps(struct map_set *maps, bool all)
{
    enum map_role last = all ? MAP_SIGNAL + 1 : MAP_IDENTITY + 1;
    enum map_role role;

    map_set_init(maps);
    for (role = MAP_CONTROL; role < last; role++) {
        maps->fd[role] = open_checked(role);
        if (maps->fd[role] < 0) {
            map_set_close(maps);
            return false;
        }
    }
    return true;
}

static bool open_v8_maps(struct map_set *maps)
{
    enum map_role role;

    for (role = MAP_RUNTIME_V8; role <= MAP_TEL_V8; role++) {
        maps->fd[role] = open_checked(role);
        if (maps->fd[role] < 0) {
            for (enum map_role cleanup = MAP_RUNTIME_V8; cleanup < role;
                 cleanup++) {
                if (maps->fd[cleanup] >= 0) {
                    close(maps->fd[cleanup]);
                    maps->fd[cleanup] = -1;
                }
            }
            return false;
        }
    }
    return true;
}

static bool open_v10_maps(struct map_set *maps)
{
    enum map_role role;

    for (role = MAP_COORD_V10; role < MAP_ROLE_COUNT; role++) {
        maps->fd[role] = open_checked(role);
        if (maps->fd[role] < 0) {
            for (enum map_role cleanup = MAP_COORD_V10; cleanup < role;
                 cleanup++) {
                if (maps->fd[cleanup] >= 0) {
                    close(maps->fd[cleanup]);
                    maps->fd[cleanup] = -1;
                }
            }
            return false;
        }
    }
    return true;
}

static bool read_control(int fd, struct bridge_control *control)
{
    uint32_t key = 0;

    memset(control, 0, sizeof(*control));
    return bpf_lookup_raw(fd, &key, control, BPF_F_LOCK) == 0;
}

static bool write_control(int fd, const struct bridge_control *control)
{
    uint32_t key = 0;

    return bpf_update_raw(fd, &key, control, BPF_EXIST | BPF_F_LOCK) == 0;
}

static void record_v8_policy_transition(int fd, bool rollback)
{
    struct orchestra_telemetry_v8 telemetry;
    uint32_t key = 0;

    if (bpf_lookup_raw(fd, &key, &telemetry, BPF_F_LOCK) != 0)
        return;
    if (rollback)
        telemetry.policy_rollback_count++;
    else
        telemetry.policy_commit_count++;
    (void)bpf_update_raw(fd, &key, &telemetry, BPF_EXIST | BPF_F_LOCK);
}

static bool valid_control(const struct bridge_control *control)
{
    return control->magic == ORCHESTRA_ABI_MAGIC &&
           control->abi_version == ORCHESTRA_ABI_VERSION &&
           control->value_size == sizeof(*control) &&
           control->scx_api_version == ORCHESTRA_SCX_API_VERSION &&
           control->scheduler_epoch != 0 &&
           control->controller_state < ORCHESTRA_CTRL_COUNT &&
           control->policy_mode < ORCHESTRA_POLICY_COUNT &&
           (control->capability_flags & BRIDGE_REQUIRED_CAPS) ==
               BRIDGE_REQUIRED_CAPS;
}

static bool valid_policy_meta_v8(const struct orchestra_policy_meta_v8 *meta,
                                 uint64_t epoch)
{
    return meta->magic == ORCHESTRA_ABI_MAGIC &&
           meta->abi_version == ORCHESTRA_KERNEL_ABI_VERSION &&
           meta->value_size == sizeof(*meta) &&
           meta->policy_schema_version == ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION &&
           meta->active_bank < ORCHESTRA_KERNEL_POLICY_BANK_COUNT &&
           meta->entry_count <= ORCHESTRA_KERNEL_MAX_POLICY_STATES &&
           meta->scheduler_epoch == epoch &&
           (meta->capability_flags & ORCHESTRA_KERNEL_REQUIRED_CAPS) ==
               ORCHESTRA_KERNEL_REQUIRED_CAPS;
}

static bool read_text_file(const char *path, char *buf, size_t size)
{
    FILE *file;

    if (size == 0)
        return false;
    file = fopen(path, "re");
    if (!file)
        return false;
    if (!fgets(buf, (int)size, file)) {
        fclose(file);
        return false;
    }
    if (fclose(file) != 0)
        return false;
    buf[strcspn(buf, "\r\n")] = '\0';
    return true;
}

static bool scheduler_loaded(void)
{
    char state[32];
    char ops[64];

    return read_text_file("/sys/kernel/sched_ext/state", state, sizeof(state)) &&
           strcmp(state, "enabled") == 0 &&
           read_text_file("/sys/kernel/sched_ext/root/ops", ops, sizeof(ops)) &&
           (strcmp(ops, BRIDGE_OPS_NAME) == 0 ||
            strcmp(ops, BRIDGE_LEGACY_OPS_NAME) == 0);
}

/* Correctly parses field 22 after the final ')' which terminates comm. */
static bool parse_proc_stat_start(const char *line, uint64_t *start_ticks)
{
    const char *right = strrchr(line, ')');
    char copy[4096];
    char *save = NULL;
    char *token;
    unsigned int field = 3;

    if (!right || right[1] != ' ' || strlen(right + 2) >= sizeof(copy))
        return false;
    strcpy(copy, right + 2);
    for (token = strtok_r(copy, " ", &save); token;
         token = strtok_r(NULL, " ", &save), field++) {
        if (field == 22)
            return parse_u64(token, 1, UINT64_MAX, start_ticks);
    }
    return false;
}

static bool get_task_proc_info(uint32_t tid, struct task_proc_info *info)
{
    char path[64];
    char line[4096];
    FILE *file;
    uint32_t tgid = 0;

    if (snprintf(path, sizeof(path), "/proc/%" PRIu32 "/stat", tid) < 0)
        return false;
    file = fopen(path, "re");
    if (!file)
        return false;
    if (!fgets(line, sizeof(line), file) || !parse_proc_stat_start(line, &info->start_ticks)) {
        fclose(file);
        return false;
    }
    if (fclose(file) != 0)
        return false;

    if (snprintf(path, sizeof(path), "/proc/%" PRIu32 "/status", tid) < 0)
        return false;
    file = fopen(path, "re");
    if (!file)
        return false;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Tgid:", 5) == 0) {
            char *text = line + 5;
            char *end;
            unsigned long value;

            errno = 0;
            value = strtoul(text, &end, 10);
            while (*end && isspace((unsigned char)*end))
                end++;
            if (errno == 0 && value > 0 && value <= UINT32_MAX && *end == '\0')
                tgid = (uint32_t)value;
            break;
        }
    }
    if (fclose(file) != 0 || tgid == 0)
        return false;
    info->tgid = tgid;
    return true;
}

static bool proc_info_equal(const struct task_proc_info *a,
                            const struct task_proc_info *b)
{
    return a->tgid == b->tgid && a->start_ticks == b->start_ticks;
}

static bool start_ns_matches_proc_ticks(uint64_t start_ns,
                                        uint64_t proc_ticks)
{
    long clock_ticks = sysconf(_SC_CLK_TCK);
    uint64_t hz;
    uint64_t seconds;
    uint64_t remainder;
    uint64_t converted;

    if (start_ns == 0 || clock_ticks <= 0)
        return false;
    hz = (uint64_t)clock_ticks;
    seconds = start_ns / UINT64_C(1000000000);
    remainder = start_ns % UINT64_C(1000000000);
    if (seconds > UINT64_MAX / hz || remainder > UINT64_MAX / hz)
        return false;
    converted = seconds * hz +
        (remainder * hz) / UINT64_C(1000000000);
    return converted == proc_ticks;
}

static bool resolve_identity(const struct map_set *maps, uint32_t tid,
                             uint64_t scheduler_epoch,
                             struct orchestra_task_identity *identity)
{
    struct task_proc_info before, after;
    struct orchestra_pid_key key;
    struct bridge_identity_record record;

    if (!get_task_proc_info(tid, &before))
        return false;
    key.tgid = before.tgid;
    key.tid = tid;
    memset(&record, 0, sizeof(record));
    if (bpf_lookup_raw(maps->fd[MAP_IDENTITY], &key, &record, 0) != 0 ||
        record.start_boottime_ns == 0 || record.scheduler_epoch != scheduler_epoch ||
        !start_ns_matches_proc_ticks(record.start_boottime_ns,
                                    before.start_ticks))
        return false;
    if (!get_task_proc_info(tid, &after) || !proc_info_equal(&before, &after))
        return false;
    identity->tgid = before.tgid;
    identity->tid = tid;
    identity->start_boottime_ns = record.start_boottime_ns;
    return true;
}

static int acquire_writer_lock(void)
{
    int fd = open(BRIDGE_LOCK_PATH,
                  O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat state;

    if (fd < 0)
        return -1;
    if (fstat(fd, &state) != 0 || !S_ISREG(state.st_mode) ||
        state.st_uid != geteuid() || (state.st_mode & 077u) != 0) {
        close(fd);
        errno = EPERM;
        return -1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool cpu_allowed_for_task(uint32_t tid, uint32_t cpu)
{
    cpu_set_t allowed;

    if (cpu >= CPU_SETSIZE)
        return false;
    CPU_ZERO(&allowed);
    return sched_getaffinity((pid_t)tid, sizeof(allowed), &allowed) == 0 &&
           CPU_ISSET_S(cpu, sizeof(allowed), &allowed);
}

static bool scheduling_policy_is_rt(int policy)
{
    return policy == SCHED_FIFO || policy == SCHED_RR ||
           policy == SCHED_DEADLINE;
}

static bool directive_equal(const struct bridge_directive *a,
                            const struct bridge_directive *b)
{
    return a->abi_version == b->abi_version &&
           a->value_size == b->value_size && a->flags == b->flags &&
           a->scheduler_epoch == b->scheduler_epoch &&
           a->generation == b->generation &&
           memcmp(&a->identity, &b->identity, sizeof(a->identity)) == 0 &&
           a->action == b->action && a->target_cpu == b->target_cpu &&
           a->slice_ns == b->slice_ns &&
           a->not_before_ns == b->not_before_ns &&
           a->throttle_period_ns == b->throttle_period_ns &&
           a->throttle_budget_ns == b->throttle_budget_ns &&
           a->expiry_ns == b->expiry_ns &&
           a->controller_state == b->controller_state &&
           a->policy_mode == b->policy_mode &&
           a->policy_generation == b->policy_generation;
}

static bool signal_equal(const struct bridge_signal_frame *a,
                         const struct bridge_signal_frame *b)
{
    /* The leading map lock is synchronization metadata, not payload. */
    return memcmp((const uint8_t *)a + sizeof(a->lock),
                  (const uint8_t *)b + sizeof(b->lock),
                  sizeof(*a) - sizeof(a->lock)) == 0;
}

static bool signal_permille_valid(uint32_t value)
{
    return value <= BRIDGE_SIGNAL_SCALE;
}

static bool stream_signal_fields_valid(const struct bridge_stream_request *request)
{
    return (request->stream_flags &
            ~(BRIDGE_STREAM_F_PUBLISH_SIGNAL | BRIDGE_STREAM_F_REQUIRE_SIGNAL)) == 0 &&
           request->signal_sequence != 0 &&
           request->signal_max_age_ns != 0 &&
           request->signal_max_age_ns <= BRIDGE_SIGNAL_MAX_AGE_NS &&
           request->signal_directive < ORCHESTRA_ACTION_COUNT &&
           request->signal_state_schema_version != 0 &&
           request->signal_prediction_used <= 1 &&
           signal_permille_valid(request->signal_confidence_permille) &&
           signal_permille_valid(request->signal_cpu_now_permille) &&
           signal_permille_valid(request->signal_cpu_pred_permille) &&
           signal_permille_valid(request->signal_decision_cpu_permille) &&
           signal_permille_valid(request->signal_memory_pressure_permille) &&
           signal_permille_valid(request->signal_thermal_permille) &&
           signal_permille_valid(request->signal_s1_permille) &&
           signal_permille_valid(request->signal_s2_permille) &&
           signal_permille_valid(request->signal_s3_permille) &&
           signal_permille_valid(request->signal_s4_permille) &&
           signal_permille_valid(request->signal_q_permille);
}

static uint32_t prune_expired_directives(int directive_fd, uint64_t epoch,
                                         uint64_t now)
{
    struct orchestra_task_identity key, next;
    bool have_key = false;
    uint32_t removed = 0;
    uint32_t scanned = 0;

    /* Deletion restarts hash iteration, so no entry is skipped. */
    while (scanned++ < BRIDGE_MAX_TASKS &&
           bpf_next_key_raw(directive_fd, have_key ? &key : NULL, &next) == 0) {
        struct bridge_directive directive;

        memset(&directive, 0, sizeof(directive));
        if (bpf_lookup_raw(directive_fd, &next, &directive, BPF_F_LOCK) != 0) {
            key = next;
            have_key = true;
            continue;
        }
        if (directive.abi_version == ORCHESTRA_ABI_VERSION &&
            directive.value_size == sizeof(directive) &&
            directive.scheduler_epoch == epoch && directive.expiry_ns > now) {
            key = next;
            have_key = true;
        } else if (bpf_delete_raw(directive_fd, &next) == 0) {
            removed++;
            have_key = false;
        } else {
            key = next;
            have_key = true;
        }
    }
    return removed;
}

static int publish_directive(const struct options *opts,
                             uint64_t *published_generation)
{
    struct map_set maps;
    struct bridge_control control, verify_control;
    struct bridge_directive directive, verify;
    struct orchestra_task_identity identity, identity_after;
    uint64_t now, expiry;
    uint32_t wire_action;
    int lock_fd = -1;
    int update_error = 0;
    int result = EXIT_PUB_FAIL;

    if (!scheduler_loaded()) {
        fprintf(stderr, "the active scheduler is not %s\n", BRIDGE_OPS_NAME);
        return EXIT_NO_SCHED;
    }
    if (!canonical_to_wire(opts->action, &wire_action))
        return EXIT_ACTION;
    if (!open_runtime_maps(&maps, false)) {
        fprintf(stderr, "missing or ABI-incompatible bridge maps: %s\n", strerror(errno));
        return errno == EPROTO ? EXIT_SCHEMA : EXIT_MAP_MISSING;
    }
    lock_fd = acquire_writer_lock();
    if (lock_fd < 0) {
        fprintf(stderr, "cannot serialize publisher: %s\n", strerror(errno));
        result = EXIT_PERM;
        goto out;
    }
    if (!read_control(maps.fd[MAP_CONTROL], &control) || !valid_control(&control)) {
        fprintf(stderr, "invalid control ABI\n");
        result = EXIT_SCHEMA;
        goto out;
    }
    if (!resolve_identity(&maps, opts->target_tid, control.scheduler_epoch,
                          &identity)) {
        fprintf(stderr, "TID %" PRIu32 " has no current kernel identity record\n",
                opts->target_tid);
        result = EXIT_TASK;
        goto out;
    }
    {
        int policy = sched_getscheduler((pid_t)opts->target_tid);

        if (policy < 0) {
            fprintf(stderr, "cannot read scheduling policy for TID %" PRIu32 ": %s\n",
                    opts->target_tid, strerror(errno));
            result = EXIT_TASK;
            goto out;
        }
        if (scheduling_policy_is_rt(policy)) {
            fprintf(stderr,
                    "refusing adaptive directive for RT policy TID %" PRIu32
                    " (policy=%d)\n", opts->target_tid, policy);
            result = EXIT_ACTION;
            goto out;
        }
    }
    if (!monotonic_ns(&now) ||
        !checked_add_u64(now, opts->expiry_duration_ns, &expiry)) {
        fprintf(stderr, "monotonic time overflow\n");
        goto out;
    }
    if (opts->action == ORCHESTRA_ACTION_MIGRATE &&
        (opts->target_cpu == ORCHESTRA_CPU_ANY ||
         !cpu_allowed_for_task(opts->target_tid, opts->target_cpu))) {
        fprintf(stderr, "CPU %" PRIu32 " is not currently allowed for TID %" PRIu32 "\n",
                opts->target_cpu, opts->target_tid);
        result = EXIT_ACTION;
        goto out;
    }
    if (opts->action == ORCHESTRA_ACTION_SLEEP &&
        (opts->not_before_ns <= now || opts->not_before_ns - now > BRIDGE_SLEEP_MAX_NS)) {
        fprintf(stderr, "SLEEP not-before must be in the next five seconds\n");
        result = EXIT_ACTION;
        goto out;
    }
    if (opts->action == ORCHESTRA_ACTION_THROTTLE &&
        (opts->throttle_period_ns < BRIDGE_SLICE_MIN_NS ||
         opts->throttle_period_ns > BRIDGE_THROTTLE_MAX_NS ||
         opts->throttle_budget_ns < BRIDGE_SLICE_MIN_NS ||
         opts->throttle_budget_ns >= opts->throttle_period_ns)) {
        fprintf(stderr, "THROTTLE requires 100us <= budget < period <= 1s\n");
        result = EXIT_ACTION;
        goto out;
    }
    if (control.last_generation == UINT64_MAX) {
        fprintf(stderr, "generation exhausted for this scheduler epoch\n");
        result = EXIT_GEN_OVERFLOW;
        goto out;
    }

    memset(&directive, 0, sizeof(directive));
    directive.abi_version = ORCHESTRA_ABI_VERSION;
    directive.value_size = sizeof(directive);
    directive.scheduler_epoch = control.scheduler_epoch;
    directive.generation = control.last_generation + 1;
    directive.identity = identity;
    directive.flags = opts->require_signal ? BRIDGE_DIRECTIVE_F_REQUIRE_SIGNAL : 0;
    directive.action = wire_action;
    directive.target_cpu = opts->target_cpu;
    directive.slice_ns = opts->slice_ns;
    directive.not_before_ns = opts->not_before_ns;
    directive.throttle_period_ns = opts->throttle_period_ns;
    directive.throttle_budget_ns = opts->throttle_budget_ns;
    directive.expiry_ns = expiry;
    directive.controller_state = opts->controller_state;
    directive.policy_mode = opts->policy_mode;
    directive.policy_generation = opts->policy_generation;

    if (opts->dry_run) {
        if (!opts->quiet)
            printf("DRY-RUN identity=%" PRIu32 ":%" PRIu32 ":%" PRIu64
                   " action=%s generation=%" PRIu64 "\n",
                   identity.tgid, identity.tid, identity.start_boottime_ns,
                   action_names[opts->action], directive.generation);
        if (published_generation)
            *published_generation = directive.generation;
        result = EXIT_OK;
        goto out;
    }

    /* Reserve a never-reused generation before making the directive visible. */
    control.last_generation = directive.generation;
    control.publisher_heartbeat_ns = now;
    control.publisher_lease_ns = opts->lease_ns;
    control.controller_state = opts->controller_state;
    control.policy_mode = opts->policy_mode;
    control.policy_generation = opts->policy_generation;
    control.publication_status = BRIDGE_PUB_OK;
    if (!write_control(maps.fd[MAP_CONTROL], &control)) {
        fprintf(stderr, "control update failed: %s\n", strerror(errno));
        goto out;
    }
    if (bpf_update_raw(maps.fd[MAP_DIRECTIVE], &identity, &directive,
                       BPF_ANY | BPF_F_LOCK) != 0) {
        update_error = errno;
        if (update_error == ENOSPC &&
            prune_expired_directives(maps.fd[MAP_DIRECTIVE],
                                     control.scheduler_epoch, now) > 0) {
            if (bpf_update_raw(maps.fd[MAP_DIRECTIVE], &identity, &directive,
                               BPF_ANY | BPF_F_LOCK) == 0)
                update_error = 0;
            else
                update_error = errno;
        }
    }
    if (update_error != 0) {
        control.publication_status = update_error == ENOSPC ?
            BRIDGE_PUB_MAP_FULL : BRIDGE_PUB_MAP_ERROR;
        (void)write_control(maps.fd[MAP_CONTROL], &control);
        fprintf(stderr, "directive update failed: %s\n",
                strerror(update_error));
        goto out;
    }
    memset(&verify, 0, sizeof(verify));
    if (bpf_lookup_raw(maps.fd[MAP_DIRECTIVE], &identity, &verify,
                       BPF_F_LOCK) != 0 || !directive_equal(&directive, &verify) ||
        !read_control(maps.fd[MAP_CONTROL], &verify_control) ||
        !valid_control(&verify_control) ||
        verify_control.scheduler_epoch != control.scheduler_epoch ||
        verify_control.last_generation != directive.generation ||
        verify_control.publisher_heartbeat_ns != now ||
        !resolve_identity(&maps, opts->target_tid, control.scheduler_epoch,
                          &identity_after) ||
        memcmp(&identity, &identity_after, sizeof(identity)) != 0) {
        int saved = errno;
        (void)bpf_delete_raw(maps.fd[MAP_DIRECTIVE], &identity);
        control.publication_status = BRIDGE_PUB_READBACK_FAIL;
        (void)write_control(maps.fd[MAP_CONTROL], &control);
        errno = saved;
        fprintf(stderr, "full publication readback or identity revalidation failed\n");
        goto out;
    }
    if (!opts->quiet)
        printf("published generation=%" PRIu64 " identity=%" PRIu32 ":%" PRIu32
               ":%" PRIu64 " action=%s\n",
               directive.generation, identity.tgid, identity.tid,
               identity.start_boottime_ns, action_names[opts->action]);
    if (published_generation)
        *published_generation = directive.generation;
    result = EXIT_OK;

out:
    if (lock_fd >= 0)
        close(lock_fd);
    map_set_close(&maps);
    return result;
}

static int publish_signal(const struct options *opts)
{
    struct map_set maps;
    struct bridge_control control, previous_control, rollback_control;
    struct bridge_signal_frame frame, existing, verify;
    uint32_t zero = 0;
    uint64_t now, expiry;
    int lock_fd = -1;
    int result = EXIT_PUB_FAIL;

    if (!scheduler_loaded()) {
        fprintf(stderr, "the active scheduler is not %s\n", BRIDGE_OPS_NAME);
        return EXIT_NO_SCHED;
    }
    if (opts->signal_sequence == 0) {
        fprintf(stderr, "signal sequence must be non-zero\n");
        return EXIT_ARGS;
    }
    if (!signal_permille_valid(opts->signal_confidence_permille) ||
        !signal_permille_valid(opts->signal_cpu_now_permille) ||
        !signal_permille_valid(opts->signal_cpu_pred_permille) ||
        !signal_permille_valid(opts->signal_decision_cpu_permille) ||
        !signal_permille_valid(opts->signal_memory_pressure_permille) ||
        !signal_permille_valid(opts->signal_thermal_permille) ||
        !signal_permille_valid(opts->signal_s1_permille) ||
        !signal_permille_valid(opts->signal_s2_permille) ||
        !signal_permille_valid(opts->signal_s3_permille) ||
        !signal_permille_valid(opts->signal_s4_permille) ||
        !signal_permille_valid(opts->signal_q_permille) ||
        opts->signal_directive >= ORCHESTRA_ACTION_COUNT ||
        opts->signal_prediction_used > 1 ||
        opts->signal_state_schema_version == 0) {
        fprintf(stderr, "signal fields are outside the fixed-point ABI bounds\n");
        return EXIT_ARGS;
    }
    if (!open_runtime_maps(&maps, true)) {
        fprintf(stderr, "missing or ABI-incompatible bridge maps: %s\n", strerror(errno));
        return errno == EPROTO ? EXIT_SCHEMA : EXIT_MAP_MISSING;
    }
    lock_fd = acquire_writer_lock();
    if (lock_fd < 0) {
        fprintf(stderr, "cannot serialize signal publisher: %s\n", strerror(errno));
        result = EXIT_PERM;
        goto out;
    }
    if (!read_control(maps.fd[MAP_CONTROL], &control) || !valid_control(&control)) {
        fprintf(stderr, "invalid control ABI\n");
        result = EXIT_SCHEMA;
        goto out;
    }
    previous_control = control;
    memset(&existing, 0, sizeof(existing));
    if (bpf_lookup_raw(maps.fd[MAP_SIGNAL], &zero, &existing, BPF_F_LOCK) == 0 &&
        existing.scheduler_epoch == control.scheduler_epoch &&
        existing.sequence >= opts->signal_sequence) {
        fprintf(stderr, "signal sequence must increase within scheduler epoch\n");
        result = EXIT_GEN_OVERFLOW;
        goto out;
    }
    if (!monotonic_ns(&now) ||
        !checked_add_u64(now, opts->signal_max_age_ns, &expiry) ||
        opts->signal_max_age_ns == 0 ||
        opts->signal_max_age_ns > BRIDGE_SIGNAL_MAX_AGE_NS) {
        fprintf(stderr, "signal max age is outside the bounded freshness window\n");
        result = EXIT_ACTION;
        goto out;
    }

    memset(&frame, 0, sizeof(frame));
    frame.magic = ORCHESTRA_ABI_MAGIC;
    frame.abi_version = ORCHESTRA_ABI_VERSION;
    frame.value_size = sizeof(frame);
    frame.flags = BRIDGE_SIGNAL_F_METRICS_VALID |
        BRIDGE_SIGNAL_F_CONTROLLER_VALID |
        (opts->signal_prediction_used ? BRIDGE_SIGNAL_F_PREDICTION_VALID : 0);
    frame.tier = opts->signal_tier;
    frame.source_id = opts->signal_source_id;
    frame.scheduler_epoch = control.scheduler_epoch;
    frame.sequence = opts->signal_sequence;
    frame.published_ns = now;
    frame.expires_ns = expiry;
    frame.key_epoch = opts->signal_key_epoch;
    frame.directive = opts->signal_directive;
    frame.state_schema_version = opts->signal_state_schema_version;
    frame.prediction_used = opts->signal_prediction_used;
    frame.confidence_permille = opts->signal_confidence_permille;
    frame.cpu_now_permille = opts->signal_cpu_now_permille;
    frame.cpu_pred_permille = opts->signal_cpu_pred_permille;
    frame.decision_cpu_permille = opts->signal_decision_cpu_permille;
    frame.memory_pressure_permille = opts->signal_memory_pressure_permille;
    frame.thermal_permille = opts->signal_thermal_permille;
    frame.s1_permille = opts->signal_s1_permille;
    frame.s2_permille = opts->signal_s2_permille;
    frame.s3_permille = opts->signal_s3_permille;
    frame.s4_permille = opts->signal_s4_permille;
    frame.q_permille = opts->signal_q_permille;
    frame.controller_state = control.controller_state;
    frame.policy_mode = control.policy_mode;
    frame.policy_generation = control.policy_generation;

    /* Refresh the common lease so a signal-only publication can feed a
     * directive that already exists.  Roll it back if frame publication fails. */
    control.publisher_heartbeat_ns = now;
    control.publisher_lease_ns = opts->lease_ns;
    control.publication_status = BRIDGE_PUB_OK;
    if (!write_control(maps.fd[MAP_CONTROL], &control)) {
        fprintf(stderr, "control update failed: %s\n", strerror(errno));
        goto out;
    }
    if (bpf_update_raw(maps.fd[MAP_SIGNAL], &zero, &frame,
                       BPF_EXIST | BPF_F_LOCK) != 0) {
        int saved = errno;
        rollback_control = previous_control;
        rollback_control.publication_status = BRIDGE_PUB_MAP_ERROR;
        (void)write_control(maps.fd[MAP_CONTROL], &rollback_control);
        fprintf(stderr, "signal frame update failed: %s\n", strerror(saved));
        goto out;
    }
    memset(&verify, 0, sizeof(verify));
    if (bpf_lookup_raw(maps.fd[MAP_SIGNAL], &zero, &verify, BPF_F_LOCK) != 0 ||
        !signal_equal(&frame, &verify) ||
        !read_control(maps.fd[MAP_CONTROL], &rollback_control) ||
        !valid_control(&rollback_control) ||
        rollback_control.scheduler_epoch != control.scheduler_epoch ||
        rollback_control.publisher_heartbeat_ns != now ||
        rollback_control.publisher_lease_ns != opts->lease_ns) {
        rollback_control = previous_control;
        rollback_control.publication_status = BRIDGE_PUB_READBACK_FAIL;
        (void)bpf_delete_raw(maps.fd[MAP_SIGNAL], &zero);
        (void)write_control(maps.fd[MAP_CONTROL], &rollback_control);
        fprintf(stderr, "signal publication readback failed\n");
        goto out;
    }
    if (!opts->quiet)
        printf("published signal sequence=%" PRIu64 " epoch=%" PRIu64
               " expires_ns=%" PRIu64 " confidence_permille=%" PRIu32 "\n",
               frame.sequence, frame.scheduler_epoch, frame.expires_ns,
               frame.confidence_permille);
    result = EXIT_OK;
out:
    if (lock_fd >= 0)
        close(lock_fd);
    map_set_close(&maps);
    return result;
}

static int policy_command(const struct options *opts)
{
    struct map_set maps;
    struct bridge_control control, rollback_control;
    struct orchestra_policy_meta_v8 meta, previous_meta, verify_meta;
    struct orchestra_policy_entry_v8 entry, verify_entry;
    uint32_t key;
    uint64_t now;
    uint64_t next_generation;
    uint32_t inactive_bank;
    uint32_t zero = 0;
    bool lifecycle_transition;
    bool rollback_request;
    int lock_fd = -1;
    int result = EXIT_PUB_FAIL;

    if (!scheduler_loaded()) {
        fprintf(stderr, "the active scheduler is not %s\n", BRIDGE_OPS_NAME);
        return EXIT_NO_SCHED;
    }
    if (!open_runtime_maps(&maps, true) || !open_v8_maps(&maps)) {
        map_set_close(&maps);
        return errno == EPROTO ? EXIT_SCHEMA : EXIT_MAP_MISSING;
    }
    lock_fd = acquire_writer_lock();
    if (lock_fd < 0) {
        result = EXIT_PERM;
        goto out;
    }
    if (!read_control(maps.fd[MAP_CONTROL], &control) ||
        !valid_control(&control)) {
        result = EXIT_SCHEMA;
        goto out;
    }
    memset(&meta, 0, sizeof(meta));
    if (bpf_lookup_raw(maps.fd[MAP_POLICY_META_V8], &zero, &meta,
                       BPF_F_LOCK) != 0 ||
        !valid_policy_meta_v8(&meta, control.scheduler_epoch)) {
        result = EXIT_SCHEMA;
        goto out;
    }
    previous_meta = meta;
    rollback_control = control;
    lifecycle_transition = opts->policy_mode_set &&
        opts->policy_mode != ORCHESTRA_POLICY_EVALUATE;
    rollback_request = opts->controller_set &&
        opts->controller_state == ORCHESTRA_CTRL_ROLLBACK;
    if (opts->policy_entry && rollback_request) {
        fprintf(stderr, "cannot stage a new policy while requesting rollback\n");
        result = EXIT_ARGS;
        goto out;
    }
    if ((opts->policy_entry || opts->policy_commit) &&
        meta.policy_mode == ORCHESTRA_POLICY_EVALUATE &&
        !lifecycle_transition && !rollback_request) {
        fprintf(stderr,
                "EVALUATE freezes the active policy; explicitly transition "
                "to TRAIN or ADAPT before committing\n");
        result = EXIT_ARGS;
        goto out;
    }
    if (!monotonic_ns(&now)) {
        result = EXIT_PUB_FAIL;
        goto out;
    }
    inactive_bank = meta.active_bank ^ 1u;
    next_generation = meta.policy_generation == UINT64_MAX ? 0 :
        meta.policy_generation + 1;
    if (next_generation == 0) {
        result = EXIT_GEN_OVERFLOW;
        goto out;
    }

    if (opts->policy_entry) {
        if (!opts->policy_state_set || !opts->action_set ||
            opts->policy_state_index >= ORCHESTRA_KERNEL_MAX_POLICY_STATES) {
            result = EXIT_ARGS;
            goto out;
        }
        key = inactive_bank * ORCHESTRA_KERNEL_MAX_POLICY_STATES +
            opts->policy_state_index;
        memset(&entry, 0, sizeof(entry));
        entry.magic = ORCHESTRA_ABI_MAGIC;
        entry.abi_version = ORCHESTRA_KERNEL_ABI_VERSION;
        entry.value_size = sizeof(entry);
        entry.policy_schema_version = ORCHESTRA_KERNEL_POLICY_SCHEMA_VERSION;
        entry.state_index = opts->policy_state_index;
        entry.action = (uint32_t)opts->action;
        entry.flags = ORCHESTRA_POLICY_V8_F_VALID;
        entry.controller_state = opts->controller_state;
        entry.capability_mask = 1u << entry.action;
        entry.target_cpu = opts->target_cpu;
        entry.slice_ns = opts->slice_ns == 0 ? ORCHESTRA_V8_DEFAULT_SLICE_NS :
            opts->slice_ns;
        entry.not_before_ns = opts->not_before_ns;
        entry.throttle_period_ns = opts->throttle_period_ns;
        entry.throttle_budget_ns = opts->throttle_budget_ns;
        entry.policy_generation = next_generation;
        if (bpf_update_raw(maps.fd[MAP_POLICY_ENTRY_V8], &key, &entry,
                            BPF_ANY | BPF_F_LOCK) != 0 ||
            bpf_lookup_raw(maps.fd[MAP_POLICY_ENTRY_V8], &key, &verify_entry,
                           BPF_F_LOCK) != 0 ||
            verify_entry.magic != entry.magic ||
            verify_entry.state_index != entry.state_index ||
            verify_entry.action != entry.action ||
            verify_entry.policy_generation != entry.policy_generation) {
            result = EXIT_PUB_FAIL;
            goto out;
        }
        if (!opts->policy_commit) {
            if (!opts->quiet)
                printf("staged policy bank=%" PRIu32 " state=%" PRIu32
                       " action=%s generation=%" PRIu64 "\n",
                       inactive_bank, entry.state_index,
                       entry.action < ORCHESTRA_ACTION_COUNT ?
                           action_names[entry.action] : "INVALID",
                       entry.policy_generation);
            result = EXIT_OK;
            goto out;
        }
    }

    if (opts->policy_commit) {
        meta.active_bank = inactive_bank;
        if (rollback_request) {
            if (previous_meta.previous_generation == 0) {
                fprintf(stderr, "no previous policy generation is available for rollback\n");
                result = EXIT_ARGS;
                goto out;
            }
            /* The inactive bank is the last known-good bank retained by the
             * previous commit.  Rollback intentionally rewinds the active
             * generation; the next normal commit allocates a fresh one. */
            meta.previous_generation = previous_meta.policy_generation;
            meta.policy_generation = previous_meta.previous_generation;
        } else {
            meta.previous_generation = meta.policy_generation;
            meta.policy_generation = next_generation;
        }
        meta.policy_mode = opts->policy_mode_set ? opts->policy_mode :
            meta.policy_mode;
        meta.controller_state = opts->controller_set ? opts->controller_state :
            meta.controller_state;
        meta.controller_schema_version =
            ORCHESTRA_KERNEL_CONTROLLER_SCHEMA_VERSION;
        meta.flags = ORCHESTRA_POLICY_META_V8_F_ACTIVE_VALID |
            (1u << (1u + meta.policy_mode));
        if (meta.controller_state == ORCHESTRA_CTRL_ROLLBACK)
            meta.flags |= ORCHESTRA_POLICY_META_V8_F_ROLLBACK;
        if (meta.controller_state == ORCHESTRA_CTRL_RECOVERY)
            meta.flags |= ORCHESTRA_POLICY_META_V8_F_RECOVERY;
        meta.scheduler_epoch = control.scheduler_epoch;
        meta.published_ns = now;
        meta.capability_flags = ORCHESTRA_KERNEL_REQUIRED_CAPS;
        if (bpf_update_raw(maps.fd[MAP_POLICY_META_V8], &zero, &meta,
                           BPF_EXIST | BPF_F_LOCK) != 0 ||
            bpf_lookup_raw(maps.fd[MAP_POLICY_META_V8], &zero, &verify_meta,
                           BPF_F_LOCK) != 0 ||
            verify_meta.active_bank != meta.active_bank ||
            verify_meta.policy_generation != meta.policy_generation) {
            result = EXIT_PUB_FAIL;
            goto out;
        }
        control.controller_state = meta.controller_state;
        control.policy_mode = meta.policy_mode;
        control.policy_generation = meta.policy_generation;
        control.publication_status = BRIDGE_PUB_OK;
        if (!write_control(maps.fd[MAP_CONTROL], &control)) {
            /* The old bank remains the safe active policy if the auxiliary
             * control record cannot be updated after the meta flip. */
            (void)bpf_update_raw(maps.fd[MAP_POLICY_META_V8], &zero,
                                 &previous_meta, BPF_EXIST | BPF_F_LOCK);
            (void)write_control(maps.fd[MAP_CONTROL], &rollback_control);
            result = EXIT_PUB_FAIL;
            goto out;
        }
        record_v8_policy_transition(maps.fd[MAP_TEL_V8], rollback_request);
        if (!opts->quiet)
            printf("committed policy bank=%" PRIu32 " generation=%" PRIu64
                   " mode=%" PRIu32 " controller=%s\n",
                   meta.active_bank, meta.policy_generation, meta.policy_mode,
                   meta.controller_state < ORCHESTRA_CTRL_COUNT ?
                       controller_names[meta.controller_state] : "INVALID");
        result = EXIT_OK;
    }
out:
    if (lock_fd >= 0)
        close(lock_fd);
    map_set_close(&maps);
    return result;
}

static int clear_directives(const struct options *opts)
{
    struct map_set maps;
    struct bridge_control control;
    struct orchestra_task_identity identity, next;
    int lock_fd;
    int result = EXIT_OK;

    if (!open_runtime_maps(&maps, false))
        return EXIT_MAP_MISSING;
    lock_fd = acquire_writer_lock();
    if (lock_fd < 0) {
        map_set_close(&maps);
        return EXIT_PERM;
    }
    if (!read_control(maps.fd[MAP_CONTROL], &control) || !valid_control(&control)) {
        result = EXIT_SCHEMA;
        goto out;
    }
    if (opts->target_set) {
        if (!resolve_identity(&maps, opts->target_tid, control.scheduler_epoch,
                              &identity) ||
            (bpf_delete_raw(maps.fd[MAP_DIRECTIVE], &identity) != 0 &&
             errno != ENOENT))
            result = EXIT_TASK;
    } else {
        bool have_key = false;

        while (bpf_next_key_raw(maps.fd[MAP_DIRECTIVE],
                                have_key ? &identity : NULL, &next) == 0) {
            if (bpf_delete_raw(maps.fd[MAP_DIRECTIVE], &next) != 0 && errno != ENOENT) {
                result = EXIT_PUB_FAIL;
                break;
            }
            /* Deletion restarts iteration; this cannot skip a hash entry. */
            have_key = false;
        }
    }
    /* Never reset last_generation: clear cannot create an ABA identity. */
    if (result == EXIT_OK)
        puts("cleared; scheduler epoch and generation were preserved");
out:
    close(lock_fd);
    map_set_close(&maps);
    return result;
}

static int status_command(const struct options *opts)
{
    struct map_set maps;
    struct bridge_control control;
    struct bridge_telemetry telemetry;
    struct orchestra_task_identity target_identity;
    struct orchestra_task_identity key, next;
    bool have_key = false;
    bool have_v8 = false;
    bool have_v10 = false;
    uint32_t zero = 0;

    printf("scheduler: %s\n", scheduler_loaded() ? BRIDGE_OPS_NAME : "not active");
    if (!open_runtime_maps(&maps, true)) {
        fprintf(stderr, "bridge maps missing or schema-invalid: %s\n", strerror(errno));
        return errno == EPROTO ? EXIT_SCHEMA : EXIT_MAP_MISSING;
    }
    have_v8 = open_v8_maps(&maps);
    have_v10 = open_v10_maps(&maps);
    if (!read_control(maps.fd[MAP_CONTROL], &control) || !valid_control(&control)) {
        map_set_close(&maps);
        return EXIT_SCHEMA;
    }
    if (opts->target_set &&
        !resolve_identity(&maps, opts->target_tid, control.scheduler_epoch,
                          &target_identity)) {
        fprintf(stderr, "TID %" PRIu32 " has no current kernel identity record\n",
                opts->target_tid);
        map_set_close(&maps);
        return EXIT_TASK;
    }
    printf("abi=%u scx_api=%u epoch=%" PRIu64 " generation=%" PRIu64
           " policy_generation=%" PRIu64 " controller=%s lease=%" PRIu64
           "ns\n",
           control.abi_version, control.scx_api_version,
           control.scheduler_epoch, control.last_generation,
           control.policy_generation,
           control.controller_state < ORCHESTRA_CTRL_COUNT ?
               controller_names[control.controller_state] : "INVALID",
           control.publisher_lease_ns);
    {
        struct bridge_signal_frame signal;

        memset(&signal, 0, sizeof(signal));
        if (bpf_lookup_raw(maps.fd[MAP_SIGNAL], &zero, &signal, BPF_F_LOCK) == 0) {
            printf("signal sequence=%" PRIu64 " epoch=%" PRIu64
                   " published_ns=%" PRIu64 " expires_ns=%" PRIu64
                   " tier=%" PRIu32 " source_id=%" PRIu32
                   " key_epoch=%" PRIu32
                   " directive=%s confidence_permille=%" PRIu32
                   " cpu_now_permille=%" PRIu32
                   " cpu_pred_permille=%" PRIu32
                   " decision_cpu_permille=%" PRIu32
                   " memory_pressure_permille=%" PRIu32
                   " thermal_permille=%" PRIu32
                   " s1_permille=%" PRIu32 " s2_permille=%" PRIu32
                   " s3_permille=%" PRIu32 " s4_permille=%" PRIu32
                   " q_permille=%" PRIu32 " controller=%s policy_mode=%" PRIu32
                   " policy_generation=%" PRIu64 " flags=%" PRIu32 "\n",
                   signal.sequence, signal.scheduler_epoch,
                   signal.published_ns, signal.expires_ns, signal.tier,
                   signal.source_id, signal.key_epoch,
                   signal.directive < ORCHESTRA_ACTION_COUNT ?
                       action_names[signal.directive] : "INVALID",
                   signal.confidence_permille, signal.cpu_now_permille,
                   signal.cpu_pred_permille, signal.decision_cpu_permille,
                   signal.memory_pressure_permille, signal.thermal_permille,
                   signal.s1_permille, signal.s2_permille, signal.s3_permille,
                   signal.s4_permille, signal.q_permille,
                   signal.controller_state < ORCHESTRA_CTRL_COUNT ?
                       controller_names[signal.controller_state] : "INVALID",
                   signal.policy_mode, signal.policy_generation, signal.flags);
        } else {
            puts("signal=unpublished");
        }
    }
    if (have_v8) {
        struct orchestra_runtime_state_v8 runtime;
        struct orchestra_policy_meta_v8 meta;

        memset(&runtime, 0, sizeof(runtime));
        if (bpf_lookup_raw(maps.fd[MAP_RUNTIME_V8], &zero, &runtime,
                           BPF_F_LOCK) == 0) {
            printf("runtime_v8 state_index=%" PRIu32 " cpu_now=%" PRIu32
                   " cpu_pred=%" PRIu32 " memory=%" PRIu32
                   " thermal=%" PRIu32 " q=%" PRIu32
                   " s1=%" PRIu32 " s2=%" PRIu32 " s3=%" PRIu32
                   " s4=%" PRIu32 " confidence=%" PRIu32
                   " prediction_generation=%" PRIu64
                   " prediction_fallback=%" PRIu32 " flags=%" PRIu32 "\n",
                   runtime.state_index, runtime.cpu_now_permille,
                   runtime.cpu_pred_permille, runtime.memory_pressure_permille,
                   runtime.thermal_permille, runtime.q_permille,
                   runtime.s1_permille, runtime.s2_permille,
                   runtime.s3_permille, runtime.s4_permille,
                   runtime.prediction_confidence_permille,
                   runtime.prediction_generation,
                   runtime.prediction_fallback_reason, runtime.flags);
        }
        memset(&meta, 0, sizeof(meta));
        if (bpf_lookup_raw(maps.fd[MAP_POLICY_META_V8], &zero, &meta,
                           BPF_F_LOCK) == 0) {
            printf("policy_v8 active_bank=%" PRIu32 " entries=%" PRIu32
                   " generation=%" PRIu64 " previous=%" PRIu64
                   " mode=%" PRIu32 " controller=%s\n",
                   meta.active_bank, meta.entry_count,
                   meta.policy_generation, meta.previous_generation,
                   meta.policy_mode,
                   meta.controller_state < ORCHESTRA_CTRL_COUNT ?
                       controller_names[meta.controller_state] : "INVALID");
        }
    }
    if (have_v10) {
        struct orchestra_runtime_state_v10 runtime;
        struct orchestra_controller_state_v10 controller;

        memset(&runtime, 0, sizeof(runtime));
        if (bpf_lookup_raw(maps.fd[MAP_RUNTIME_V10], &zero, &runtime,
                           BPF_F_LOCK) == 0) {
            printf("runtime_v10 window=%" PRIu64 " scope=%" PRIu32
                   " domain=%" PRIu32 " eligible=%" PRIu64
                   " executed=%" PRIu64 " s1=%" PRIu32 " s2=%" PRIu32
                   " s3=%" PRIu32 " s4=%" PRIu32 " q=%" PRIu32
                   " deficit_class=%" PRIu32 " primary=%" PRIu32
                   " secondary=%" PRIu32 " severity=%" PRIu32
                   " persistence=%" PRIu32 " controller=%s"
                   " controller_generation=%" PRIu64 " flags=%" PRIu32 "\n",
                   runtime.window_generation, runtime.scope, runtime.domain_id,
                   runtime.eligible_observations, runtime.executed_observations,
                   runtime.s1_permille, runtime.s2_permille,
                   runtime.s3_permille, runtime.s4_permille,
                   runtime.q_permille, runtime.deficit_class,
                   runtime.primary_deficit, runtime.secondary_deficit,
                   runtime.deficit_severity, runtime.deficit_persistence,
                   runtime.controller_state < ORCHESTRA_CTRL_COUNT ?
                       controller_names[runtime.controller_state] : "INVALID",
                   runtime.controller_generation, runtime.flags);
        }
        memset(&controller, 0, sizeof(controller));
        if (bpf_lookup_raw(maps.fd[MAP_CONTROLLER_V10], &zero, &controller,
                           BPF_F_LOCK) == 0) {
            printf("controller_v10 state=%s generation=%" PRIu64
                   " staging_generation=%" PRIu64
                   " previous_good_generation=%" PRIu64
                   " last_q=%" PRIu64 " next_update_ns=%" PRIu64
                   " saturation=%" PRIu64 " rollback=%" PRIu64
                   " recovery=%" PRIu64 " overrides=%" PRIu64
                   " no_op=%" PRIu64 "\n",
                   controller.active_state < ORCHESTRA_CTRL_COUNT ?
                       controller_names[controller.active_state] : "INVALID",
                   controller.active_generation,
                   controller.staging_generation,
                   controller.previous_good_generation,
                   controller.last_q_permille, controller.next_update_ns,
                   controller.saturation_count, controller.rollback_count,
                   controller.recovery_count, controller.override_count,
                   controller.no_op_count);
        }
    }
    while (bpf_next_key_raw(maps.fd[MAP_DIRECTIVE],
                            have_key ? &key : NULL, &next) == 0) {
        struct bridge_directive directive;

        memset(&directive, 0, sizeof(directive));
        if ((!opts->target_set ||
             memcmp(&next, &target_identity, sizeof(next)) == 0) &&
            bpf_lookup_raw(maps.fd[MAP_DIRECTIVE], &next, &directive,
                           BPF_F_LOCK) == 0) {
            printf("directive identity=%" PRIu32 ":%" PRIu32 ":%" PRIu64
                   " generation=%" PRIu64 " action=%s cpu=%" PRIu32
                   " flags=%" PRIu32
                   " slice_ns=%" PRIu64 " not_before_ns=%" PRIu64
                   " throttle_period_ns=%" PRIu64
                   " throttle_budget_ns=%" PRIu64
                   " expiry_ns=%" PRIu64 "\n",
                   next.tgid, next.tid, next.start_boottime_ns,
                   directive.generation,
                   directive.action < ORCHESTRA_ACTION_COUNT ?
                       action_names[directive.action] : "INVALID",
                   directive.target_cpu, directive.flags, directive.slice_ns,
                   directive.not_before_ns, directive.throttle_period_ns,
                   directive.throttle_budget_ns, directive.expiry_ns);
        }
        key = next;
        have_key = true;
    }
    have_key = false;
    while (bpf_next_key_raw(maps.fd[MAP_TASK_TELEMETRY],
                            have_key ? &key : NULL, &next) == 0) {
        struct bridge_task_telemetry task;

        memset(&task, 0, sizeof(task));
        if ((!opts->target_set ||
             memcmp(&next, &target_identity, sizeof(next)) == 0) &&
            bpf_lookup_raw(maps.fd[MAP_TASK_TELEMETRY], &next, &task,
                           BPF_F_LOCK) == 0) {
            printf("task identity=%" PRIu32 ":%" PRIu32 ":%" PRIu64
                   " generation=%" PRIu64 " action=%s requested_cpu=%" PRIu32
                   " dispatched_cpu=%" PRId32 " actual_cpu=%" PRId32
                   " accepted_ns=%" PRIu64 " dispatched_ns=%" PRIu64
                   " running_ns=%" PRIu64 " stopped_ns=%" PRIu64
                   " runtime_ns=%" PRIu64
                   " accepted=%" PRIu32 " dispatched=%" PRIu32
                   " running=%" PRIu32 " effective=%" PRIu32
                   " fallback=%" PRIu32 " errors=%" PRIu32
                   " fallback_reason=%" PRIu32 "\n",
                   next.tgid, next.tid, next.start_boottime_ns,
                   task.generation,
                   task.action < ORCHESTRA_ACTION_COUNT ?
                       action_names[task.action] : "INVALID",
                   task.requested_cpu, task.dispatched_cpu, task.actual_cpu,
                   task.accepted_ns, task.dispatched_ns, task.running_ns,
                   task.stopped_ns, task.runtime_ns,
                   task.accepted_count, task.dispatched_count,
                   task.running_count, task.effective_count,
                   task.fallback_count, task.error_count,
                   task.fallback_reason);
        }
        key = next;
        have_key = true;
    }
    memset(&telemetry, 0, sizeof(telemetry));
    if (bpf_lookup_raw(maps.fd[MAP_TELEMETRY], &zero, &telemetry, 0) == 0) {
        printf("telemetry accepted=%" PRIu64 " dispatched=%" PRIu64
               " running=%" PRIu64 " fallback=%" PRIu64
               " migrate_target=%" PRIu64 " migrate_other=%" PRIu64
               " timer_ticks=%" PRIu64 " deferred_scans=%" PRIu64
               " deferred_future=%" PRIu64 " deferred_release_fail=%" PRIu64
               " deferred_cpu_fail=%" PRIu64
               " enqueue=%" PRIu64 " run_disp=%" PRIu64 " yield_disp=%" PRIu64
               " mig_acc=%" PRIu64 " mig_disp=%" PRIu64
               " throttle_acc=%" PRIu64 " throttle_def=%" PRIu64
               " sleep_acc=%" PRIu64 " sleep_def=%" PRIu64
               " deferred=%" PRIu64 " deferred_rel=%" PRIu64
               " stale_lease=%" PRIu64 " bad_id=%" PRIu64 " expired=%" PRIu64
               " bad_cpu=%" PRIu64 " map_err=%" PRIu64
               " signal_acc=%" PRIu64 " signal_invalid=%" PRIu64
               " signal_stale=%" PRIu64 "\n",
               telemetry.accepted_directive_count,
               telemetry.dispatched_action_count, telemetry.running_count,
               telemetry.fallback_count, telemetry.migrate_running_target_count,
               telemetry.migrate_running_other_count,
               telemetry.deferred_timer_tick_count,
               telemetry.deferred_timer_scanned_count,
               telemetry.deferred_timer_future_count,
               telemetry.deferred_release_failure_count,
               telemetry.deferred_cpu_failure_count,
               telemetry.enqueue_callback_count,
               telemetry.run_dispatched_count,
               telemetry.yield_dispatched_count,
               telemetry.migrate_accepted_count,
               telemetry.migrate_dispatched_count,
               telemetry.throttle_accepted_count,
               telemetry.throttle_deferred_count,
               telemetry.sleep_accepted_count,
               telemetry.sleep_deferred_count,
               telemetry.deferred_count,
               telemetry.deferred_release_count,
               telemetry.stale_lease_count,
               telemetry.invalid_identity_count,
               telemetry.expired_directive_count,
               telemetry.invalid_cpu_count,
               telemetry.map_error_count,
               telemetry.signal_accepted_count,
               telemetry.signal_invalid_count,
               telemetry.signal_stale_count);
    }
    if (have_v8) {
        struct orchestra_telemetry_v8 kernel_telemetry;

        memset(&kernel_telemetry, 0, sizeof(kernel_telemetry));
        if (bpf_lookup_raw(maps.fd[MAP_TEL_V8], &zero, &kernel_telemetry,
                           BPF_F_LOCK) == 0) {
            printf("telemetry_v8 policy_lookup=%" PRIu64
                   " cache_hit=%" PRIu64 " cache_miss=%" PRIu64
                   " generation_mismatch=%" PRIu64
                   " state_gen_change=%" PRIu64
                   " policy_gen_change=%" PRIu64
                   " signal_gen_change=%" PRIu64
                   " invalid_index=%" PRIu64 " invalid_action=%" PRIu64
                   " unsupported=%" PRIu64 " policy_fallback=%" PRIu64
                   " controller_override=%" PRIu64
                   " prediction_fallback=%" PRIu64
                   " policy_commit=%" PRIu64 " policy_rollback=%" PRIu64
                   " task_create=%" PRIu64 " task_update=%" PRIu64 "\n",
                   kernel_telemetry.policy_lookup_count,
                   kernel_telemetry.policy_cache_hit_count,
                   kernel_telemetry.policy_cache_miss_count,
                   kernel_telemetry.policy_generation_mismatch_count,
                   kernel_telemetry.state_generation_change_count,
                   kernel_telemetry.policy_generation_change_count,
                   kernel_telemetry.signal_generation_change_count,
                   kernel_telemetry.invalid_policy_index_count,
                   kernel_telemetry.invalid_policy_action_count,
                   kernel_telemetry.unsupported_action_count,
                   kernel_telemetry.policy_fallback_count,
                   kernel_telemetry.controller_override_count,
                   kernel_telemetry.prediction_fallback_count,
                   kernel_telemetry.policy_commit_count,
                   kernel_telemetry.policy_rollback_count,
                   kernel_telemetry.task_state_create_count,
                   kernel_telemetry.task_state_update_count);
        }
    }
    map_set_close(&maps);
    return EXIT_OK;
}

static int opt_in_command(uint32_t tid)
{
    struct map_set maps;
    struct bridge_control control;
    struct orchestra_task_identity identity;
    int result = EXIT_TASK;

    if (!open_runtime_maps(&maps, false))
        return EXIT_MAP_MISSING;
    if (read_control(maps.fd[MAP_CONTROL], &control) && valid_control(&control) &&
        resolve_identity(&maps, tid, control.scheduler_epoch, &identity)) {
        printf("admitted exact TID identity=%" PRIu32 ":%" PRIu32 ":%" PRIu64
               "; full-switch mode does not change its Linux policy\n",
               identity.tgid, identity.tid, identity.start_boottime_ns);
        result = EXIT_OK;
    }
    map_set_close(&maps);
    return result;
}

static int pin_one(enum map_role role, uint32_t id, bool *created)
{
    struct bpf_map_info wanted, existing;
    int fd = bpf_map_fd_by_id(id);
    int old_fd;

    *created = false;
    if (fd < 0)
        return -1;
    if (!map_matches(fd, &map_specs[role], &wanted)) {
        close(fd);
        errno = EPROTO;
        return -1;
    }
    old_fd = bpf_obj_get_raw(map_specs[role].path);
    if (old_fd >= 0) {
        bool same = bpf_info_raw(old_fd, &existing) == 0 && existing.id == wanted.id;
        close(old_fd);
        close(fd);
        if (!same)
            errno = EEXIST;
        return same ? 0 : -1;
    }
    if (bpf_obj_pin_raw(fd, map_specs[role].path) != 0) {
        close(fd);
        return -1;
    }
    *created = true;
    close(fd);
    return 0;
}

static int pin_maps_command(const struct options *opts)
{
    enum map_role role;
    bool created[MAP_ROLE_COUNT] = {false};
    struct stat directory;
    struct bpf_map_info timer_info;
    uint32_t object_btf_id = 0;
    int timer_fd;

    if (!scheduler_loaded())
        return EXIT_NO_SCHED;
    /* A BPF timer is cancelled when its map loses all userspace references.
     * Pinning it after a generic loader exits cannot resurrect that timer.
     * Require a loader (or equivalent) to have pinned this exact map before
     * struct_ops attach; orchestra_loader implements the required order. */
    timer_fd = bpf_obj_get_raw(BRIDGE_DEFER_PATH);
    if (timer_fd < 0 ||
        !map_matches(timer_fd, &map_specs[MAP_DEFER_TIMER], &timer_info) ||
        timer_info.id != opts->map_ids[MAP_DEFER_TIMER]) {
        if (timer_fd >= 0)
            close(timer_fd);
        fprintf(stderr, "deferred timer map was not pinned before attach; use orchestra_loader\n");
        return EXIT_POLICY;
    }
    close(timer_fd);
    if (mkdir(BRIDGE_PIN_DIR, 0700) != 0 && errno != EEXIST)
        return EXIT_PERM;
    if (lstat(BRIDGE_PIN_DIR, &directory) != 0 ||
        !S_ISDIR(directory.st_mode) || directory.st_uid != geteuid() ||
        (directory.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        fprintf(stderr, "unsafe bridge pin directory ownership or mode\n");
        return EXIT_PERM;
    }
    for (role = MAP_CONTROL; role < MAP_ROLE_COUNT; role++) {
        struct bpf_map_info info;
        int fd;

        if (opts->map_ids[role] == 0) {
            fprintf(stderr, "every exact map ID is required; no global discovery is performed\n");
            return EXIT_ARGS;
        }
        fd = bpf_map_fd_by_id(opts->map_ids[role]);
        if (fd < 0 || !map_matches(fd, &map_specs[role], &info)) {
            if (fd >= 0) close(fd);
            return EXIT_SCHEMA;
        }
        close(fd);
        if (info.btf_id == 0 || (object_btf_id != 0 && info.btf_id != object_btf_id)) {
            fprintf(stderr, "map IDs do not share one BTF object identity\n");
            return EXIT_SCHEMA;
        }
        object_btf_id = info.btf_id;
    }
    for (role = MAP_CONTROL; role < MAP_ROLE_COUNT; role++) {
        if (pin_one(role, opts->map_ids[role], &created[role]) != 0) {
            int saved_errno = errno;
            fprintf(stderr, "cannot pin role %s ID %" PRIu32 ": %s\n",
                    map_specs[role].name, opts->map_ids[role],
                    strerror(saved_errno));
            for (enum map_role cleanup = MAP_CONTROL; cleanup < role; cleanup++) {
                if (created[cleanup] && unlink(map_specs[cleanup].path) != 0)
                    fprintf(stderr, "warning: cannot remove partial pin %s: %s\n",
                            map_specs[cleanup].path, strerror(errno));
            }
            return saved_errno == EPROTO ? EXIT_SCHEMA : EXIT_MAP_MISSING;
        }
    }
    return EXIT_OK;
}

/* 1=record, 0=clean EOF before a record, -1=error/truncated record. */
static int read_full_record(int fd, void *buffer, size_t size)
{
    uint8_t *cursor = buffer;
    size_t offset = 0;

    while (offset < size) {
        ssize_t count = read(fd, cursor + offset, size - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count == 0)
            return offset == 0 ? 0 : -1;
        if (count < 0)
            return -1;
        offset += (size_t)count;
    }
    return 1;
}

static bool write_full(int fd, const void *buffer, size_t size)
{
    const uint8_t *cursor = buffer;
    size_t offset = 0;

    while (offset < size) {
        ssize_t count = write(fd, cursor + offset, size - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        offset += (size_t)count;
    }
    return true;
}

static int stream_command(void)
{
    struct bridge_stream_request request;
    int read_status;
    uint64_t last_sequence = 0;

    while ((read_status = read_full_record(STDIN_FILENO, &request,
                                           sizeof(request))) == 1) {
        struct bridge_stream_response response = {
            .magic = BRIDGE_STREAM_MAGIC,
            .abi_version = ORCHESTRA_ABI_VERSION,
            .value_size = sizeof(response),
            .sequence = request.sequence
        };
        struct options opts;
        uint64_t generation = 0;

        memset(&opts, 0, sizeof(opts));
        opts.quiet = true;
        opts.publish = true;
        opts.action_set = true;
        opts.target_set = true;
        opts.target_cpu = ORCHESTRA_CPU_ANY;
        opts.expiry_duration_ns = DEFAULT_EXPIRY_NS;
        opts.lease_ns = DEFAULT_LEASE_NS;
        opts.controller_state = ORCHESTRA_CTRL_NORMAL;
        opts.policy_mode = ORCHESTRA_POLICY_EVALUATE;
        if (request.magic != BRIDGE_STREAM_MAGIC ||
            request.abi_version != ORCHESTRA_ABI_VERSION ||
            request.value_size != sizeof(request) ||
            request.sequence == 0 || request.sequence <= last_sequence ||
            request.action >= ORCHESTRA_ACTION_COUNT ||
            request.target_tid == 0 ||
            request.controller_state >= ORCHESTRA_CTRL_COUNT ||
            request.policy_mode >= ORCHESTRA_POLICY_COUNT ||
            request.expiry_duration_ns == 0 ||
            request.expiry_duration_ns > BRIDGE_EXPIRY_MAX_NS ||
            ((request.stream_flags & BRIDGE_STREAM_F_PUBLISH_SIGNAL) != 0 &&
             !stream_signal_fields_valid(&request))) {
            response.status = EXIT_ARGS;
        } else {
            last_sequence = request.sequence;
            opts.action = (enum orchestra_action_id)request.action;
            opts.target_tid = request.target_tid;
            opts.target_cpu = request.target_cpu;
            opts.slice_ns = request.slice_ns;
            opts.not_before_ns = request.not_before_ns;
            opts.throttle_period_ns = request.throttle_period_ns;
            opts.throttle_budget_ns = request.throttle_budget_ns;
            opts.expiry_duration_ns = request.expiry_duration_ns;
            opts.controller_state = request.controller_state;
            opts.policy_mode = request.policy_mode;
            opts.policy_generation = request.policy_generation;
            opts.require_signal =
                (request.stream_flags & BRIDGE_STREAM_F_REQUIRE_SIGNAL) != 0;
            if ((request.stream_flags & BRIDGE_STREAM_F_PUBLISH_SIGNAL) != 0) {
                opts.signal_publish = true;
                opts.signal_sequence = request.signal_sequence;
                opts.signal_max_age_ns = request.signal_max_age_ns;
                opts.signal_tier = request.signal_tier;
                opts.signal_source_id = request.signal_source_id;
                opts.signal_key_epoch = request.signal_key_epoch;
                opts.signal_directive = request.signal_directive;
                opts.signal_state_schema_version =
                    request.signal_state_schema_version;
                opts.signal_prediction_used = request.signal_prediction_used;
                opts.signal_confidence_permille =
                    request.signal_confidence_permille;
                opts.signal_cpu_now_permille = request.signal_cpu_now_permille;
                opts.signal_cpu_pred_permille = request.signal_cpu_pred_permille;
                opts.signal_decision_cpu_permille =
                    request.signal_decision_cpu_permille;
                opts.signal_memory_pressure_permille =
                    request.signal_memory_pressure_permille;
                opts.signal_thermal_permille = request.signal_thermal_permille;
                opts.signal_s1_permille = request.signal_s1_permille;
                opts.signal_s2_permille = request.signal_s2_permille;
                opts.signal_s3_permille = request.signal_s3_permille;
                opts.signal_s4_permille = request.signal_s4_permille;
                opts.signal_q_permille = request.signal_q_permille;
                response.status = publish_signal(&opts);
            } else {
                response.status = EXIT_OK;
            }
            if (response.status == EXIT_OK)
                response.status = publish_directive(&opts, &generation);
            response.generation = generation;
        }
        if (!write_full(STDOUT_FILENO, &response, sizeof(response)))
            return EXIT_PUB_FAIL;
    }
    return read_status == 0 ? EXIT_OK : EXIT_PUB_FAIL;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s COMMAND [OPTIONS]\n"
        "  --status [--target-pid TID]\n"
        "  --stream   (binary canonical-engine request/response stream)\n"
        "  --policy-entry --policy-state-index N --action RUN|SLEEP|MIGRATE|THROTTLE|YIELD\n"
        "      [--policy-commit] [--policy-mode ID] [--controller-state STATE]\n"
        "  --policy-commit   (atomically activate the inactive v8 policy bank)\n"
        "  --publish --action RUN|SLEEP|MIGRATE|THROTTLE|YIELD --target-pid TID\n"
        "      [--target-cpu CPU] [--slice-ns NS] [--not-before-ns MONO_NS]\n"
        "      [--throttle-period-ns NS] [--throttle-budget-ns NS]\n"
        "      [--expiry-ns DURATION_NS] [--lease-ns NS] [--dry-run]\n"
        "      [--controller-state STATE] [--policy-mode ID] [--policy-generation N]\n"
        "      [--require-signal]\n"
        "  --signal-publish --signal-sequence N\n"
        "      [--signal-max-age-ns NS] [--signal-tier N] [--signal-source-id N]\n"
        "      [--signal-key-epoch N] [--signal-directive ACTION]\n"
        "      [--signal-state-schema-version N] [--signal-prediction-used 0|1]\n"
        "      [--signal-confidence-permille N] [--signal-cpu-now-permille N]\n"
        "      [--signal-cpu-pred-permille N] [--signal-decision-cpu-permille N]\n"
        "      [--signal-memory-pressure-permille N] [--signal-thermal-permille N]\n"
        "      [--signal-s1-permille N] [--signal-s2-permille N]\n"
        "      [--signal-s3-permille N] [--signal-s4-permille N]\n"
        "      [--signal-q-permille N]\n"
        "  --clear [--target-pid TID]\n"
        "  --opt-in --target-pid TID   (identity admission only; exact TID)\n"
        "  --pin-maps --control-map-id ID --directive-map-id ID\n"
        "      --identity-map-id ID --task-map-id ID --task-telemetry-map-id ID\n"
        "      --telemetry-map-id ID --defer-timer-map-id ID --signal-map-id ID\n"
        "      --runtime-v8-map-id ID --policy-meta-v8-map-id ID\n"
        "      --policy-entry-v8-map-id ID --task-v8-map-id ID\n"
        "      --diag-v8-map-id ID --tel-v8-map-id ID\n"
        "      --coord-v10-map-id ID --coord-cpu-v10-map-id ID\n"
        "      --controller-v10-map-id ID --controller-tel-v10-map-id ID\n"
        "      --runtime-v10-map-id ID --task-coord-v10-map-id ID\n"
        "      (timer map must already be pinned before attach; use orchestra_loader)\n",
        program);
}

static bool need_value(int argc, char **argv, int *index, const char **value)
{
    if (*index + 1 >= argc)
        return false;
    *value = argv[++*index];
    return true;
}

static bool parse_options(int argc, char **argv, struct options *opts)
{
    int i;
    unsigned int commands = 0;

    memset(opts, 0, sizeof(*opts));
    opts->action = ORCHESTRA_ACTION_RUN;
    opts->target_cpu = ORCHESTRA_CPU_ANY;
    opts->expiry_duration_ns = DEFAULT_EXPIRY_NS;
    opts->lease_ns = DEFAULT_LEASE_NS;
    opts->controller_state = ORCHESTRA_CTRL_NORMAL;
    opts->policy_mode = ORCHESTRA_POLICY_EVALUATE;
    opts->signal_max_age_ns = DEFAULT_SIGNAL_MAX_AGE_NS;
    opts->signal_directive = ORCHESTRA_ACTION_RUN;
    opts->signal_state_schema_version = 1;
    opts->signal_confidence_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_cpu_now_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_cpu_pred_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_decision_cpu_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_memory_pressure_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_thermal_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_s1_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_s2_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_s3_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_s4_permille = BRIDGE_SIGNAL_SCALE;
    opts->signal_q_permille = BRIDGE_SIGNAL_SCALE;

    for (i = 1; i < argc; i++) {
        const char *value = NULL;
        uint32_t parsed32;

        if (strcmp(argv[i], "--status") == 0) opts->status = true;
        else if (strcmp(argv[i], "--publish") == 0) opts->publish = true;
        else if (strcmp(argv[i], "--policy-entry") == 0) opts->policy_entry = true;
        else if (strcmp(argv[i], "--policy-commit") == 0) opts->policy_commit = true;
        else if (strcmp(argv[i], "--signal-publish") == 0) opts->signal_publish = true;
        else if (strcmp(argv[i], "--clear") == 0) opts->clear = true;
        else if (strcmp(argv[i], "--opt-in") == 0) opts->opt_in = true;
        else if (strcmp(argv[i], "--pin-maps") == 0) opts->pin_maps = true;
        else if (strcmp(argv[i], "--stream") == 0) opts->stream = true;
        else if (strcmp(argv[i], "--dry-run") == 0) opts->dry_run = true;
        else if (strcmp(argv[i], "--require-signal") == 0) opts->require_signal = true;
        else if (strcmp(argv[i], "--help") == 0) return false;
        else if (strcmp(argv[i], "--action") == 0) {
            if (!need_value(argc, argv, &i, &value) || !parse_action(value, &opts->action))
                return false;
            opts->action_set = true;
        } else if (strcmp(argv[i], "--target-pid") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 1, INT32_MAX, &opts->target_tid))
                return false;
            opts->target_set = true;
        } else if (strcmp(argv[i], "--target-cpu") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, UINT32_MAX - 1, &opts->target_cpu))
                return false;
        } else if (strcmp(argv[i], "--policy-state-index") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, ORCHESTRA_KERNEL_MAX_POLICY_STATES - 1,
                           &opts->policy_state_index))
                return false;
            opts->policy_state_set = true;
        } else if (strcmp(argv[i], "--slice-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 0, BRIDGE_SLICE_MAX_NS, &opts->slice_ns))
                return false;
        } else if (strcmp(argv[i], "--not-before-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, UINT64_MAX, &opts->not_before_ns))
                return false;
        } else if (strcmp(argv[i], "--throttle-period-ns") == 0 ||
                   strcmp(argv[i], "--throttle-interval-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, BRIDGE_THROTTLE_MAX_NS,
                           &opts->throttle_period_ns))
                return false;
        } else if (strcmp(argv[i], "--throttle-budget-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, BRIDGE_THROTTLE_MAX_NS,
                           &opts->throttle_budget_ns))
                return false;
        } else if (strcmp(argv[i], "--expiry-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, BRIDGE_EXPIRY_MAX_NS,
                           &opts->expiry_duration_ns))
                return false;
        } else if (strcmp(argv[i], "--lease-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, BRIDGE_LEASE_MAX_NS, &opts->lease_ns))
                return false;
        } else if (strcmp(argv[i], "--controller-state") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_controller(value, &opts->controller_state))
                return false;
            opts->controller_set = true;
        } else if (strcmp(argv[i], "--policy-mode") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, ORCHESTRA_POLICY_COUNT - 1, &opts->policy_mode))
                return false;
            opts->policy_mode_set = true;
        } else if (strcmp(argv[i], "--policy-generation") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 0, UINT64_MAX, &opts->policy_generation))
                return false;
        } else if (strcmp(argv[i], "--signal-sequence") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, UINT64_MAX, &opts->signal_sequence))
                return false;
        } else if (strcmp(argv[i], "--signal-max-age-ns") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u64(value, 1, BRIDGE_SIGNAL_MAX_AGE_NS,
                           &opts->signal_max_age_ns))
                return false;
        } else if (strcmp(argv[i], "--signal-tier") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, UINT32_MAX, &opts->signal_tier))
                return false;
        } else if (strcmp(argv[i], "--signal-source-id") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, UINT32_MAX, &opts->signal_source_id))
                return false;
        } else if (strcmp(argv[i], "--signal-key-epoch") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, UINT32_MAX, &opts->signal_key_epoch))
                return false;
        } else if (strcmp(argv[i], "--signal-directive") == 0) {
            enum orchestra_action_id action;
            if (!need_value(argc, argv, &i, &value) ||
                !parse_action(value, &action))
                return false;
            opts->signal_directive = (uint32_t)action;
        } else if (strcmp(argv[i], "--signal-state-schema-version") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 1, UINT32_MAX,
                           &opts->signal_state_schema_version))
                return false;
        } else if (strcmp(argv[i], "--signal-prediction-used") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, 1, &opts->signal_prediction_used))
                return false;
        } else if (strcmp(argv[i], "--signal-confidence-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_confidence_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-cpu-now-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_cpu_now_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-cpu-pred-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_cpu_pred_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-decision-cpu-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_decision_cpu_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-memory-pressure-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_memory_pressure_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-thermal-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_thermal_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-s1-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_s1_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-s2-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_s2_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-s3-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_s3_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-s4-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_s4_permille))
                return false;
        } else if (strcmp(argv[i], "--signal-q-permille") == 0) {
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 0, BRIDGE_SIGNAL_SCALE,
                           &opts->signal_q_permille))
                return false;
        } else if (strcmp(argv[i], "--control-map-id") == 0 ||
                   strcmp(argv[i], "--directive-map-id") == 0 ||
                   strcmp(argv[i], "--identity-map-id") == 0 ||
                   strcmp(argv[i], "--task-map-id") == 0 ||
                   strcmp(argv[i], "--task-telemetry-map-id") == 0 ||
                   strcmp(argv[i], "--telemetry-map-id") == 0 ||
                   strcmp(argv[i], "--defer-timer-map-id") == 0 ||
                   strcmp(argv[i], "--signal-map-id") == 0 ||
                   strcmp(argv[i], "--runtime-v8-map-id") == 0 ||
                   strcmp(argv[i], "--policy-meta-v8-map-id") == 0 ||
                   strcmp(argv[i], "--policy-entry-v8-map-id") == 0 ||
                   strcmp(argv[i], "--task-v8-map-id") == 0 ||
                   strcmp(argv[i], "--diag-v8-map-id") == 0 ||
                   strcmp(argv[i], "--tel-v8-map-id") == 0 ||
                   strcmp(argv[i], "--coord-v10-map-id") == 0 ||
                   strcmp(argv[i], "--coord-cpu-v10-map-id") == 0 ||
                   strcmp(argv[i], "--controller-v10-map-id") == 0 ||
                   strcmp(argv[i], "--controller-tel-v10-map-id") == 0 ||
                   strcmp(argv[i], "--runtime-v10-map-id") == 0 ||
                   strcmp(argv[i], "--task-coord-v10-map-id") == 0) {
            enum map_role role = strcmp(argv[i], "--control-map-id") == 0 ? MAP_CONTROL :
                strcmp(argv[i], "--directive-map-id") == 0 ? MAP_DIRECTIVE :
                strcmp(argv[i], "--identity-map-id") == 0 ? MAP_IDENTITY :
                strcmp(argv[i], "--task-map-id") == 0 ? MAP_TASK_STATE :
                strcmp(argv[i], "--task-telemetry-map-id") == 0 ? MAP_TASK_TELEMETRY :
                strcmp(argv[i], "--telemetry-map-id") == 0 ? MAP_TELEMETRY :
                strcmp(argv[i], "--defer-timer-map-id") == 0 ? MAP_DEFER_TIMER :
                strcmp(argv[i], "--signal-map-id") == 0 ? MAP_SIGNAL :
                strcmp(argv[i], "--runtime-v8-map-id") == 0 ? MAP_RUNTIME_V8 :
                strcmp(argv[i], "--policy-meta-v8-map-id") == 0 ? MAP_POLICY_META_V8 :
                strcmp(argv[i], "--policy-entry-v8-map-id") == 0 ? MAP_POLICY_ENTRY_V8 :
                strcmp(argv[i], "--task-v8-map-id") == 0 ? MAP_TASK_V8 :
                strcmp(argv[i], "--diag-v8-map-id") == 0 ? MAP_DIAG_V8 :
                strcmp(argv[i], "--tel-v8-map-id") == 0 ? MAP_TEL_V8 :
                strcmp(argv[i], "--coord-v10-map-id") == 0 ? MAP_COORD_V10 :
                strcmp(argv[i], "--coord-cpu-v10-map-id") == 0 ? MAP_COORD_CPU_V10 :
                strcmp(argv[i], "--controller-v10-map-id") == 0 ? MAP_CONTROLLER_V10 :
                strcmp(argv[i], "--controller-tel-v10-map-id") == 0 ? MAP_CONTROLLER_TEL_V10 :
                strcmp(argv[i], "--runtime-v10-map-id") == 0 ? MAP_RUNTIME_V10 :
                MAP_TASK_COORD_V10;
            if (!need_value(argc, argv, &i, &value) ||
                !parse_u32(value, 1, UINT32_MAX, &parsed32))
                return false;
            opts->map_ids[role] = parsed32;
        } else {
            return false;
        }
    }

    commands = (unsigned int)opts->status + (unsigned int)opts->publish
             + (unsigned int)opts->clear + (unsigned int)opts->opt_in
             + (unsigned int)opts->pin_maps + (unsigned int)opts->stream
             + (unsigned int)opts->signal_publish +
             (unsigned int)(opts->policy_entry || opts->policy_commit);
    if (commands != 1)
        return false;
    if ((opts->publish && (!opts->action_set || !opts->target_set)) ||
        (opts->opt_in && !opts->target_set) ||
        (opts->signal_publish && opts->signal_sequence == 0) ||
        (opts->require_signal && !opts->publish))
        return false;
    if (opts->action == ORCHESTRA_ACTION_SLEEP && opts->not_before_ns == 0) {
        uint64_t now;
        if (!monotonic_ns(&now) || !checked_add_u64(now, UINT64_C(20000000),
                                                    &opts->not_before_ns))
            return false;
    }
    if (opts->action == ORCHESTRA_ACTION_THROTTLE) {
        if (opts->throttle_period_ns == 0)
            opts->throttle_period_ns = UINT64_C(10000000);
        if (opts->throttle_budget_ns == 0)
            opts->throttle_budget_ns = UINT64_C(2000000);
    }
    if (opts->policy_entry && !opts->policy_state_set)
        return false;
    return true;
}

#ifndef ORCHESTRA_BRIDGE_UNIT_TEST
int main(int argc, char **argv)
{
    struct options opts;

    if (!parse_options(argc, argv, &opts)) {
        usage(argv[0]);
        return EXIT_ARGS;
    }
    if (opts.status)
        return status_command(&opts);
    if (opts.stream)
        return stream_command();
    if (opts.publish)
        return publish_directive(&opts, NULL);
    if (opts.policy_entry || opts.policy_commit)
        return policy_command(&opts);
    if (opts.signal_publish)
        return publish_signal(&opts);
    if (opts.clear)
        return clear_directives(&opts);
    if (opts.opt_in)
        return opt_in_command(opts.target_tid);
    if (opts.pin_maps)
        return pin_maps_command(&opts);
    return EXIT_ARGS;
}
#endif
