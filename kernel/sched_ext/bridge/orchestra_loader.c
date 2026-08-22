/* SPDX-License-Identifier: GPL-2.0 */
/* Load ORCHESTRA with every runtime map pinned before struct_ops attach. */
#define _GNU_SOURCE
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <linux/bpf.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "orchestra_bridge_v1.h"

#define PIN_DIR  "/sys/fs/bpf/orchestra"
#define LINK_PATH PIN_DIR "/orchestra_sched"
#define SCHED_EXT_STATE_PATH "/sys/kernel/sched_ext/state"
#define SCHED_EXT_OPS_PATH "/sys/kernel/sched_ext/root/ops"
#define ORCHESTRA_OPS_NAME "orchestra_scx_v8"
#define ORCHESTRA_LEGACY_OPS_NAME "orchestra_scx_stage7"
#define ORCHESTRA_OPS_MAP_NAME "orchestra_sched_ops"

struct loader_map_spec {
    const char *name;
    const char *path;
    enum bpf_map_type type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
};

#define MAP_PATH(name) PIN_DIR "/" name

static const struct loader_map_spec map_specs[] = {
    { BRIDGE_CTL_MAP_NAME, MAP_PATH(BRIDGE_CTL_MAP_NAME), BPF_MAP_TYPE_ARRAY,
      sizeof(uint32_t), sizeof(struct bridge_control), 1 },
    { BRIDGE_DIR_MAP_NAME, MAP_PATH(BRIDGE_DIR_MAP_NAME), BPF_MAP_TYPE_HASH,
      sizeof(struct orchestra_task_identity), sizeof(struct bridge_directive),
      BRIDGE_MAX_TASKS },
    { BRIDGE_ID_MAP_NAME, MAP_PATH(BRIDGE_ID_MAP_NAME), BPF_MAP_TYPE_HASH,
      sizeof(struct orchestra_pid_key), sizeof(struct bridge_identity_record),
      BRIDGE_MAX_TASKS },
    { BRIDGE_TASK_MAP_NAME, MAP_PATH(BRIDGE_TASK_MAP_NAME), BPF_MAP_TYPE_HASH,
      sizeof(struct orchestra_task_identity), sizeof(struct bridge_task_state),
      BRIDGE_MAX_TASKS },
    { BRIDGE_TASK_TEL_MAP_NAME, MAP_PATH(BRIDGE_TASK_TEL_MAP_NAME),
      BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
      sizeof(struct bridge_task_telemetry), BRIDGE_MAX_TASKS },
    { BRIDGE_TEL_MAP_NAME, MAP_PATH(BRIDGE_TEL_MAP_NAME), BPF_MAP_TYPE_ARRAY,
      sizeof(uint32_t), sizeof(struct bridge_telemetry), 1 },
    { BRIDGE_DEFER_MAP_NAME, MAP_PATH(BRIDGE_DEFER_MAP_NAME), BPF_MAP_TYPE_ARRAY,
      sizeof(uint32_t), sizeof(struct bridge_defer_timer), 1 },
    { BRIDGE_SIGNAL_MAP_NAME, MAP_PATH(BRIDGE_SIGNAL_MAP_NAME), BPF_MAP_TYPE_ARRAY,
      sizeof(uint32_t), sizeof(struct bridge_signal_frame), 1 },
    { BRIDGE_RUNTIME_V8_MAP_NAME, MAP_PATH(BRIDGE_RUNTIME_V8_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_runtime_state_v8), 1 },
    { BRIDGE_POLICY_META_V8_MAP_NAME, MAP_PATH(BRIDGE_POLICY_META_V8_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_policy_meta_v8), 1 },
    { BRIDGE_POLICY_ENTRY_V8_MAP_NAME, MAP_PATH(BRIDGE_POLICY_ENTRY_V8_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_policy_entry_v8),
      ORCHESTRA_KERNEL_POLICY_ENTRY_COUNT },
    { BRIDGE_TASK_V8_MAP_NAME, MAP_PATH(BRIDGE_TASK_V8_MAP_NAME),
      BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
      sizeof(struct orchestra_task_hot_v8), BRIDGE_MAX_TASKS },
    { BRIDGE_DIAG_V8_MAP_NAME, MAP_PATH(BRIDGE_DIAG_V8_MAP_NAME),
      BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
      sizeof(struct orchestra_task_diag_v8), BRIDGE_MAX_TASKS },
    { BRIDGE_TEL_V8_MAP_NAME, MAP_PATH(BRIDGE_TEL_V8_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_telemetry_v8), 1 },
    { BRIDGE_COORD_V10_MAP_NAME, MAP_PATH(BRIDGE_COORD_V10_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_coordination_state_v10),
      ORCHESTRA_COORD_MAP_ENTRY_COUNT },
    { BRIDGE_COORD_CPU_V10_MAP_NAME, MAP_PATH(BRIDGE_COORD_CPU_V10_MAP_NAME),
      BPF_MAP_TYPE_PERCPU_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_coord_cpu_v10), 1 },
    { BRIDGE_CONTROLLER_V10_MAP_NAME, MAP_PATH(BRIDGE_CONTROLLER_V10_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_controller_state_v10), 1 },
    { BRIDGE_CONTROLLER_TEL_V10_MAP_NAME,
      MAP_PATH(BRIDGE_CONTROLLER_TEL_V10_MAP_NAME), BPF_MAP_TYPE_ARRAY,
      sizeof(uint32_t), sizeof(struct orchestra_controller_telemetry_v10), 1 },
    { BRIDGE_RUNTIME_V10_MAP_NAME, MAP_PATH(BRIDGE_RUNTIME_V10_MAP_NAME),
      BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
      sizeof(struct orchestra_runtime_state_v10), 1 },
    { BRIDGE_TASK_COORD_V10_MAP_NAME, MAP_PATH(BRIDGE_TASK_COORD_V10_MAP_NAME),
      BPF_MAP_TYPE_HASH, sizeof(struct orchestra_task_identity),
      sizeof(struct orchestra_task_coord_v10), BRIDGE_MAX_TASKS },
};

static int ensure_pin_dir(void)
{
    struct stat state;

    if (mkdir(PIN_DIR, 0700) != 0 && errno != EEXIST)
        return -errno;
    if (lstat(PIN_DIR, &state) != 0)
        return -errno;
    if (!S_ISDIR(state.st_mode) || state.st_uid != geteuid() ||
        (state.st_mode & (S_IWGRP | S_IWOTH)) != 0)
        return -EPERM;
    return 0;
}

static bool read_sysfs_line(const char *path, char *buffer, size_t size)
{
    FILE *file;

    if (size == 0)
        return false;
    file = fopen(path, "re");
    if (!file)
        return false;
    if (!fgets(buffer, (int)size, file)) {
        fclose(file);
        return false;
    }
    if (fclose(file) != 0)
        return false;
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return true;
}

static bool scheduler_owned(void)
{
    char state[32];
    char ops[64];

    if (!read_sysfs_line(SCHED_EXT_STATE_PATH, state, sizeof(state)) ||
        !read_sysfs_line(SCHED_EXT_OPS_PATH, ops, sizeof(ops)))
        return false;
    return strcmp(state, "enabled") == 0 &&
           (strcmp(ops, ORCHESTRA_OPS_NAME) == 0 ||
            strcmp(ops, ORCHESTRA_LEGACY_OPS_NAME) == 0);
}

static bool pinned_map_matches(const struct loader_map_spec *spec)
{
    struct bpf_map_info info;
    __u32 info_len = sizeof(info);
    int fd;
    bool matches;

    fd = bpf_obj_get(spec->path);
    if (fd < 0)
        return false;
    memset(&info, 0, sizeof(info));
    matches = bpf_map_get_info_by_fd(fd, &info, &info_len) == 0 &&
              strncmp((const char *)info.name, spec->name, BPF_OBJ_NAME_LEN) == 0 &&
              info.type == (__u32)spec->type &&
              info.key_size == spec->key_size &&
              info.value_size == spec->value_size &&
              info.max_entries == spec->max_entries;
    close(fd);
    return matches;
}

static bool pinned_link_matches(void)
{
    struct bpf_link_info link_info;
    struct bpf_map_info map_info;
    __u32 link_info_len = sizeof(link_info);
    __u32 map_info_len = sizeof(map_info);
    int link_fd;
    int map_fd;
    bool matches = false;

    link_fd = bpf_obj_get(LINK_PATH);
    if (link_fd < 0)
        return false;
    memset(&link_info, 0, sizeof(link_info));
    if (bpf_link_get_info_by_fd(link_fd, &link_info, &link_info_len) != 0 ||
        link_info.type != BPF_LINK_TYPE_STRUCT_OPS ||
        link_info.struct_ops.map_id == 0) {
        close(link_fd);
        return false;
    }
    map_fd = bpf_map_get_fd_by_id(link_info.struct_ops.map_id);
    if (map_fd >= 0) {
        memset(&map_info, 0, sizeof(map_info));
        matches = bpf_map_get_info_by_fd(map_fd, &map_info, &map_info_len) == 0 &&
                  strncmp((const char *)map_info.name, ORCHESTRA_OPS_MAP_NAME,
                          BPF_OBJ_NAME_LEN) == 0;
        close(map_fd);
    }
    close(link_fd);
    return matches;
}

static bool validate_existing_pins(void)
{
    struct stat directory;

    if (lstat(PIN_DIR, &directory) != 0 || !S_ISDIR(directory.st_mode) ||
        directory.st_uid != geteuid() ||
        (directory.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
        !pinned_link_matches())
        return false;
    for (size_t i = 0; i < sizeof(map_specs) / sizeof(map_specs[0]); i++) {
        if (!pinned_map_matches(&map_specs[i]))
            return false;
    }
    return true;
}

static bool map_schema_matches(const struct bpf_map *map,
                               const struct loader_map_spec *spec)
{
    return strcmp(bpf_map__name(map), spec->name) == 0 &&
           bpf_map__type(map) == spec->type &&
           bpf_map__key_size(map) == spec->key_size &&
           bpf_map__value_size(map) == spec->value_size &&
           bpf_map__max_entries(map) == spec->max_entries;
}

static void unlink_created(size_t count)
{
    while (count > 0) {
        count--;
        if (unlink(map_specs[count].path) != 0 && errno != ENOENT)
            fprintf(stderr, "warning: cannot remove %s: %s\n",
                    map_specs[count].path, strerror(errno));
    }
}

static int load_scheduler(const char *object_path)
{
    struct bpf_object *object = NULL;
    struct bpf_link *link = NULL;
    struct bpf_map *ops_map = NULL;
    struct bpf_map *map;
    size_t pinned = 0;
    int err;

    if (geteuid() != 0)
        return -EPERM;
    err = ensure_pin_dir();
    if (err)
        return err;
    if (access(LINK_PATH, F_OK) == 0)
        return -EEXIST;

    object = bpf_object__open_file(object_path, NULL);
    err = (int)libbpf_get_error(object);
    if (err) {
        object = NULL;
        goto out;
    }
    for (size_t i = 0; i < sizeof(map_specs) / sizeof(map_specs[0]); i++) {
        map = bpf_object__find_map_by_name(object, map_specs[i].name);
        if (!map || !map_schema_matches(map, &map_specs[i])) {
            fprintf(stderr, "missing or incompatible map %s\n", map_specs[i].name);
            err = -EPROTO;
            goto out;
        }
        if (access(map_specs[i].path, F_OK) == 0) {
            err = -EEXIST;
            goto out;
        }
    }
    bpf_object__for_each_map(map, object) {
        if (bpf_map__type(map) == BPF_MAP_TYPE_STRUCT_OPS) {
            if (ops_map) {
                err = -EPROTO;
                goto out;
            }
            ops_map = map;
        }
    }
    if (!ops_map) {
        err = -EPROTO;
        goto out;
    }
    err = bpf_object__load(object);
    if (err)
        goto out;

    /* The timer map must have a userspace pin before attach invokes init(). */
    for (size_t i = 0; i < sizeof(map_specs) / sizeof(map_specs[0]); i++) {
        map = bpf_object__find_map_by_name(object, map_specs[i].name);
        err = bpf_map__pin(map, map_specs[i].path);
        if (err)
            goto out;
        pinned++;
    }

    link = bpf_map__attach_struct_ops(ops_map);
    err = (int)libbpf_get_error(link);
    if (err) {
        link = NULL;
        goto out;
    }
    err = bpf_link__pin(link, LINK_PATH);
    if (err)
        goto out;
    printf("loaded %s; maps pinned before attach; link=%s\n",
           object_path, LINK_PATH);
out:
    if (err) {
        if (link)
            bpf_link__destroy(link);
        unlink_created(pinned);
        if (rmdir(PIN_DIR) != 0 && errno != ENOTEMPTY && errno != ENOENT)
            fprintf(stderr, "warning: cannot remove %s: %s\n",
                    PIN_DIR, strerror(errno));
    } else if (link) {
        bpf_link__destroy(link);
    }
    bpf_object__close(object);
    return err;
}

static int unload_scheduler(void)
{
    bool disabled = false;
    int err = 0;

    if (geteuid() != 0)
        return -EPERM;
    if (!scheduler_owned())
        return -EPERM;
    if (!validate_existing_pins())
        return -EPERM;
    if (unlink(LINK_PATH) != 0 && errno != ENOENT)
        return -errno;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        FILE *state = fopen("/sys/kernel/sched_ext/state", "re");
        char text[32] = {0};

        if (state && fgets(text, sizeof(text), state) &&
            strncmp(text, "disabled", 8) == 0) {
            fclose(state);
            disabled = true;
            break;
        }
        if (state)
            fclose(state);
        usleep(10000);
    }
    if (!disabled)
        return -EBUSY;
    for (size_t i = 0; i < sizeof(map_specs) / sizeof(map_specs[0]); i++) {
        if (unlink(map_specs[i].path) != 0 && errno != ENOENT && err == 0)
            err = -errno;
    }
    if (rmdir(PIN_DIR) != 0 && errno != ENOENT && err == 0)
        err = -errno;
    return err;
}

int main(int argc, char **argv)
{
    int err;

    if (argc == 3 && strcmp(argv[1], "--load") == 0)
        err = load_scheduler(argv[2]);
    else if (argc == 2 && strcmp(argv[1], "--unload") == 0)
        err = unload_scheduler();
    else {
        fprintf(stderr, "usage: %s --load BPF_OBJECT | --unload\n", argv[0]);
        return 2;
    }
    if (err) {
        fprintf(stderr, "%s failed: %s\n", argv[1], strerror(-err));
        return 1;
    }
    return 0;
}
