#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
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

#define MAX_WORKERS 128
#define GROUPS 4
#define DEFAULT_WORKERS 16
#define DEFAULT_INTERVAL_MS 500
#define DEFAULT_DURATION_SEC 30

typedef enum {
    ACT_RUN = 0,
    ACT_SLEEP,
    ACT_YIELD,
    ACT_MIGRATE,
    ACT_THROTTLE
} action_t;

typedef enum {
    GROUP_INTERACTIVE = 0,
    GROUP_COMPUTE,
    GROUP_IO,
    GROUP_BACKGROUND
} group_t;

typedef struct {
    _Atomic int action;
    _Atomic unsigned long heartbeat;
    _Atomic int alive;
    _Atomic int cpu_target;
    int group;
    int worker_index;
} worker_slot_t;

typedef struct {
    _Atomic unsigned long sequence;
    _Atomic int stop;
    double cpu_now;
    double cpu_pred;
    double memory_pressure;
    double thermal_proxy;
    double confidence;
    int directive;
    int worker_count;
    worker_slot_t workers[MAX_WORKERS];
} shared_state_t;

typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
} cpu_sample_t;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        if (g_stop) break;
    }
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static const char *action_name(action_t a) {
    switch (a) {
        case ACT_RUN: return "RUN";
        case ACT_SLEEP: return "SLEEP";
        case ACT_YIELD: return "YIELD";
        case ACT_MIGRATE: return "MIGRATE";
        case ACT_THROTTLE: return "THROTTLE";
        default: return "UNKNOWN";
    }
}

static const char *group_name(int g) {
    switch (g) {
        case GROUP_INTERACTIVE: return "interactive";
        case GROUP_COMPUTE: return "compute";
        case GROUP_IO: return "io";
        case GROUP_BACKGROUND: return "background";
        default: return "unknown";
    }
}

static bool read_cpu_sample(cpu_sample_t *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;
    char label[16];
    int n = fscanf(f, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                   label, &s->user, &s->nice, &s->system, &s->idle,
                   &s->iowait, &s->irq, &s->softirq, &s->steal);
    fclose(f);
    return n >= 5 && strcmp(label, "cpu") == 0;
}

static double cpu_usage_between(const cpu_sample_t *a, const cpu_sample_t *b) {
    unsigned long long idle_a = a->idle + a->iowait;
    unsigned long long idle_b = b->idle + b->iowait;
    unsigned long long non_a = a->user + a->nice + a->system + a->irq + a->softirq + a->steal;
    unsigned long long non_b = b->user + b->nice + b->system + b->irq + b->softirq + b->steal;
    unsigned long long total_a = idle_a + non_a;
    unsigned long long total_b = idle_b + non_b;
    unsigned long long total_delta = total_b - total_a;
    unsigned long long idle_delta = idle_b - idle_a;
    if (total_delta == 0) return 0.0;
    return clamp01((double)(total_delta - idle_delta) / (double)total_delta);
}

static double read_memory_pressure(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0.0;
    char key[64];
    unsigned long long value;
    char unit[16];
    unsigned long long total = 0, available = 0;
    while (fscanf(f, "%63s %llu %15s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) total = value;
        else if (strcmp(key, "MemAvailable:") == 0) available = value;
        if (total && available) break;
    }
    fclose(f);
    if (!total) return 0.0;
    return clamp01(1.0 - (double)available / (double)total);
}

static void busy_work_ms(int ms, unsigned long *state) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    volatile double x = (double)(*state + 1);
    for (;;) {
        for (int i = 0; i < 5000; ++i) {
            x = sin(x) * cos(x + 0.0001) + sqrt(fabs(x) + 1.0);
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L +
                          (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed_ms >= ms) break;
    }
    *state ^= (unsigned long)(fabs(x) * 1000003.0) + 0x9e3779b9UL;
}

static void do_io_work(unsigned long *state) {
    char path[] = "/tmp/orchestra_worker_XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        (void)fchmod(fd, S_IRUSR | S_IWUSR);
        (void)unlink(path);
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "%ld %lu\n", (long)time(NULL), *state);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            (void)write(fd, buf, (size_t)n);
            (void)fsync(fd);
        }
        close(fd);
    }
}

static void migrate_to_cpu(int cpu_count, int cpu) {
    int selected_cpu;

    if (cpu_count <= 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    selected_cpu = cpu % cpu_count;
    if (selected_cpu < 0) selected_cpu += cpu_count;
    CPU_SET((size_t)selected_cpu, &set);
    (void)sched_setaffinity(0, sizeof(set), &set);
}

static void worker_loop(shared_state_t *shared, int index) {
    worker_slot_t *slot = &shared->workers[index];
    unsigned long rng = (unsigned long)getpid() ^ (unsigned long)time(NULL);
    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);

    if (slot->group == GROUP_BACKGROUND) {
        (void)setpriority(PRIO_PROCESS, 0, 10);
    } else if (slot->group == GROUP_IO) {
        (void)setpriority(PRIO_PROCESS, 0, 5);
    }

    atomic_store(&slot->alive, 1);

    while (!atomic_load(&shared->stop)) {
        action_t action = (action_t)atomic_load(&slot->action);
        int target = atomic_load(&slot->cpu_target);

        switch (action) {
            case ACT_RUN:
                busy_work_ms(slot->group == GROUP_COMPUTE ? 35 : 15, &rng);
                if (slot->group == GROUP_IO && (rng % 8 == 0)) do_io_work(&rng);
                sleep_ms(slot->group == GROUP_INTERACTIVE ? 2 : 5);
                break;
            case ACT_SLEEP:
                sleep_ms(120);
                break;
            case ACT_YIELD:
                busy_work_ms(4, &rng);
                sched_yield();
                sleep_ms(8);
                break;
            case ACT_MIGRATE:
                migrate_to_cpu(cpu_count, target);
                busy_work_ms(8, &rng);
                sched_yield();
                sleep_ms(6);
                break;
            case ACT_THROTTLE:
                busy_work_ms(2, &rng);
                sleep_ms(45);
                break;
            default:
                sleep_ms(50);
                break;
        }
        atomic_fetch_add(&slot->heartbeat, 1UL);
    }

    atomic_store(&slot->alive, 0);
    _exit(0);
}

static action_t directive_from_signal(double cpu_pred, double memory, double thermal) {
    if (thermal > 0.90 || cpu_pred > 0.94) return ACT_THROTTLE;
    if (cpu_pred > 0.82 || memory > 0.90) return ACT_MIGRATE;
    if (cpu_pred > 0.68) return ACT_YIELD;
    if (cpu_pred < 0.20) return ACT_SLEEP;
    return ACT_RUN;
}

static uint32_t lcg(uint32_t x) {
    return x * 1664525u + 1013904223u;
}

static action_t local_recommendation(int worker_index, int group, double cpu_pred,
                                     double memory, double thermal, unsigned long seq) {
    uint32_t r = lcg((uint32_t)worker_index * UINT32_C(2654435761) ^
                     (uint32_t)seq);
    double jitter = ((double)(r % 1000) / 1000.0 - 0.5) * 0.08;
    double perceived = clamp01(cpu_pred + jitter);

    if (thermal > 0.90 || perceived > 0.94) return ACT_THROTTLE;
    if (perceived > 0.82 || memory > 0.90) {
        return (group == GROUP_COMPUTE || group == GROUP_IO) ? ACT_MIGRATE : ACT_YIELD;
    }
    if (perceived > 0.68) {
        return group == GROUP_INTERACTIVE ? ACT_RUN : ACT_YIELD;
    }
    if (perceived < 0.20 && group != GROUP_INTERACTIVE) return ACT_SLEEP;
    if (group == GROUP_BACKGROUND && perceived > 0.55) return ACT_YIELD;
    return ACT_RUN;
}

static void compute_group_budgets(int worker_count, double pressure, int budgets[GROUPS]) {
    static const double weights[GROUPS] = {1.30, 1.00, 0.95, 0.60};
    int total_budget = (int)floor((1.0 - 0.70 * clamp01(pressure)) * worker_count);
    if (total_budget < 2) total_budget = 2;
    if (total_budget > worker_count) total_budget = worker_count;

    double sum = 0.0;
    for (int g = 0; g < GROUPS; ++g) sum += weights[g];
    int allocated = 0;
    for (int g = 0; g < GROUPS; ++g) {
        budgets[g] = (int)floor(total_budget * weights[g] / sum);
        if (budgets[g] < 1) budgets[g] = 1;
        allocated += budgets[g];
    }
    while (allocated > total_budget) {
        for (int g = GROUPS - 1; g >= 0 && allocated > total_budget; --g) {
            if (budgets[g] > 1) {
                budgets[g]--;
                allocated--;
            }
        }
        if (allocated <= GROUPS) break;
    }
}

static void assign_actions(shared_state_t *shared, bool group_layer) {
    int budgets[GROUPS] = {0};
    int used[GROUPS] = {0};
    double pressure = fmax(shared->cpu_pred, fmax(shared->memory_pressure, shared->thermal_proxy));
    compute_group_budgets(shared->worker_count, pressure, budgets);

    unsigned long seq = atomic_load(&shared->sequence);
    int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);

    for (int i = 0; i < shared->worker_count; ++i) {
        worker_slot_t *slot = &shared->workers[i];
        int group = slot->group;
        action_t rec = local_recommendation(i, group, shared->cpu_pred,
                                            shared->memory_pressure,
                                            shared->thermal_proxy, seq);
        action_t final = rec;

        if (group_layer && rec == ACT_RUN) {
            if (used[group] < budgets[group]) {
                used[group]++;
            } else {
                final = ((i + (int)seq) % 2 == 0) ? ACT_YIELD : ACT_SLEEP;
            }
        }

        if (rec == ACT_MIGRATE) {
            int current = sched_getcpu();
            int target = cpu_count > 0 ? (current + 1 + i) % cpu_count : 0;
            atomic_store(&slot->cpu_target, target);
        }
        atomic_store(&slot->action, (int)final);
    }
}

static double normalized_entropy(const int counts[5], int total) {
    if (total <= 0) return 0.0;
    double h = 0.0;
    int active_types = 0;
    for (int i = 0; i < 5; ++i) {
        if (counts[i] == 0) continue;
        active_types++;
        double p = (double)counts[i] / (double)total;
        h -= p * log(p);
    }
    if (active_types <= 1) return 0.0;
    return h / log(5.0);
}

static bool action_complies(action_t directive, action_t actual) {
    switch (directive) {
        case ACT_RUN: return actual == ACT_RUN || actual == ACT_YIELD;
        case ACT_SLEEP: return actual == ACT_SLEEP || actual == ACT_YIELD;
        case ACT_YIELD: return actual == ACT_YIELD || actual == ACT_SLEEP || actual == ACT_THROTTLE;
        case ACT_MIGRATE: return actual == ACT_MIGRATE || actual == ACT_YIELD || actual == ACT_THROTTLE;
        case ACT_THROTTLE: return actual == ACT_THROTTLE || actual == ACT_SLEEP || actual == ACT_YIELD;
        default: return false;
    }
}

static void print_header(void) {
    printf("tick,cpu_now,cpu_pred,mem,thermal,directive,run,sleep,yield,migrate,throttle,S1,S2,S3,S4,Q\n");
}

static void print_metrics(shared_state_t *shared, const int prev_actions[MAX_WORKERS], unsigned long tick) {
    int counts[5] = {0};
    int compliant = 0;
    int changed = 0;

    for (int i = 0; i < shared->worker_count; ++i) {
        action_t a = (action_t)atomic_load(&shared->workers[i].action);
        if (a >= 0 && a < 5) counts[a]++;
        if (action_complies((action_t)shared->directive, a)) compliant++;
        if (prev_actions[i] != (int)a) changed++;
    }

    double s1 = clamp01(shared->confidence);
    double s2 = shared->worker_count ? (double)compliant / shared->worker_count : 1.0;
    double s3 = clamp01(1.0 - normalized_entropy(counts, shared->worker_count));
    double s4 = shared->worker_count ? 1.0 - (double)changed / shared->worker_count : 1.0;
    double q = pow(fmax(1e-9, s1 * s2 * s3 * s4), 0.25);

    printf("%lu,%.4f,%.4f,%.4f,%.4f,%s,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f\n",
           tick, shared->cpu_now, shared->cpu_pred, shared->memory_pressure,
           shared->thermal_proxy, action_name((action_t)shared->directive),
           counts[ACT_RUN], counts[ACT_SLEEP], counts[ACT_YIELD],
           counts[ACT_MIGRATE], counts[ACT_THROTTLE], s1, s2, s3, s4, q);
    fflush(stdout);
}

static int spawn_worker(shared_state_t *shared, int index, pid_t *pid_out) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) worker_loop(shared, index);
    *pid_out = pid;
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--workers N] [--duration SEC] [--interval-ms MS] [--direct] [--fault-after SEC]\n"
            "  --workers N       4..128 worker processes (default %d)\n"
            "  --duration SEC    test duration, 0 = until Ctrl+C (default %d)\n"
            "  --interval-ms MS  controller interval 100..5000 (default %d)\n"
            "  --direct          disable group budgets; per-process direct mode\n"
            "  --fault-after SEC kill one worker and let the coordinator restart it\n",
            prog, DEFAULT_WORKERS, DEFAULT_DURATION_SEC, DEFAULT_INTERVAL_MS);
}

int main(int argc, char **argv) {
    int worker_count = DEFAULT_WORKERS;
    int duration_sec = DEFAULT_DURATION_SEC;
    int interval_ms = DEFAULT_INTERVAL_MS;
    int fault_after_sec = -1;
    bool group_layer = true;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            worker_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fault-after") == 0 && i + 1 < argc) {
            fault_after_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--direct") == 0) {
            group_layer = false;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (worker_count < 4 || worker_count > MAX_WORKERS || interval_ms < 100 || interval_ms > 5000 || duration_sec < 0) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    shared_state_t *shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    memset(shared, 0, sizeof(*shared));
    shared->worker_count = worker_count;
    shared->confidence = 0.95;

    pid_t pids[MAX_WORKERS] = {0};
    unsigned long last_heartbeat[MAX_WORKERS] = {0};
    int stale_ticks[MAX_WORKERS] = {0};
    int prev_actions[MAX_WORKERS] = {0};

    for (int i = 0; i < worker_count; ++i) {
        int group = i % GROUPS;
        shared->workers[i].group = group;
        shared->workers[i].worker_index = i;
        atomic_store(&shared->workers[i].action, ACT_SLEEP);
        atomic_store(&shared->workers[i].cpu_target, 0);
        if (spawn_worker(shared, i, &pids[i]) != 0) {
            perror("fork");
            atomic_store(&shared->stop, 1);
            return 1;
        }
    }

    cpu_sample_t previous, current;
    if (!read_cpu_sample(&previous)) {
        fprintf(stderr, "Cannot read /proc/stat. This demo requires Linux.\n");
        atomic_store(&shared->stop, 1);
        return 1;
    }

    fprintf(stderr, "ORCHESTRA real-CPU userspace prototype\n");
    fprintf(stderr, "workers=%d, interval=%d ms, mode=%s, duration=%d sec\n",
            worker_count, interval_ms, group_layer ? "group" : "direct", duration_sec);
    for (int g = 0; g < GROUPS; ++g) {
        int count = 0;
        for (int i = 0; i < worker_count; ++i) if (shared->workers[i].group == g) count++;
        fprintf(stderr, "  group %-11s : %d workers\n", group_name(g), count);
    }
    fprintf(stderr, "CSV metrics follow on stdout. Redirect them to a file for analysis.\n\n");

    print_header();
    unsigned long tick = 0;
    double prev_cpu = 0.0;
    double thermal = 0.20;
    bool fault_injected = false;
    time_t start_time = time(NULL);

    while (!g_stop && !atomic_load(&shared->stop)) {
        sleep_ms(interval_ms);
        if (!read_cpu_sample(&current)) {
            fprintf(stderr, "Warning: failed to read /proc/stat\n");
            continue;
        }

        double cpu = cpu_usage_between(&previous, &current);
        previous = current;
        double slope = cpu - prev_cpu;
        prev_cpu = cpu;
        double pred = clamp01(cpu + 0.70 * slope);
        double mem = read_memory_pressure();
        thermal = clamp01(0.92 * thermal + 0.08 * cpu);
        double confidence = clamp01(0.98 - fabs(slope) * 1.6);

        shared->cpu_now = cpu;
        shared->cpu_pred = pred;
        shared->memory_pressure = mem;
        shared->thermal_proxy = thermal;
        shared->confidence = confidence;
        shared->directive = directive_from_signal(pred, mem, thermal);
        atomic_fetch_add(&shared->sequence, 1UL);

        for (int i = 0; i < worker_count; ++i) {
            prev_actions[i] = atomic_load(&shared->workers[i].action);
        }

        assign_actions(shared, group_layer);
        tick++;

        // Detect missing worker heartbeats and restart the failed process.
        for (int i = 0; i < worker_count; ++i) {
            unsigned long hb = atomic_load(&shared->workers[i].heartbeat);
            if (hb == last_heartbeat[i]) stale_ticks[i]++;
            else stale_ticks[i] = 0;
            last_heartbeat[i] = hb;

            int status = 0;
            pid_t result = waitpid(pids[i], &status, WNOHANG);
            bool exited = result == pids[i];
            if (exited || stale_ticks[i] >= 8) {
                if (!exited && pids[i] > 0) {
                    kill(pids[i], SIGKILL);
                    waitpid(pids[i], NULL, 0);
                }
                fprintf(stderr, "[fault-handler] restarting worker %d (pid=%ld)\n", i, (long)pids[i]);
                atomic_store(&shared->workers[i].heartbeat, 0UL);
                atomic_store(&shared->workers[i].alive, 0);
                stale_ticks[i] = 0;
                last_heartbeat[i] = 0;
                if (spawn_worker(shared, i, &pids[i]) != 0) {
                    perror("fork restart");
                    g_stop = 1;
                    break;
                }
            }
        }

        time_t elapsed = time(NULL) - start_time;
        if (!fault_injected && fault_after_sec >= 0 && elapsed >= fault_after_sec) {
            int victim = worker_count / 2;
            fprintf(stderr, "[test] killing worker %d (pid=%ld) to test recovery\n", victim, (long)pids[victim]);
            kill(pids[victim], SIGKILL);
            fault_injected = true;
        }

        print_metrics(shared, prev_actions, tick);

        if (duration_sec > 0 && elapsed >= duration_sec) break;
    }

    atomic_store(&shared->stop, 1);
    for (int i = 0; i < worker_count; ++i) {
        if (pids[i] > 0) kill(pids[i], SIGTERM);
    }
    for (int i = 0; i < worker_count; ++i) {
        if (pids[i] > 0) waitpid(pids[i], NULL, 0);
    }
    munmap(shared, sizeof(*shared));
    fprintf(stderr, "Test finished.\n");
    return 0;
}
