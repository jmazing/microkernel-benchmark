#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>

#define NSEC_PER_SEC 1000000000L
#define PERIOD_NS    1000000L  // 1 ms period
#define ITERATIONS   10000

// Helper: add nanoseconds to a timespec
struct timespec timespec_add(struct timespec t, long ns) {
    t.tv_nsec += ns;
    if (t.tv_nsec >= NSEC_PER_SEC) {
        t.tv_sec += t.tv_nsec / NSEC_PER_SEC;
        t.tv_nsec %= NSEC_PER_SEC;
    }
    return t;
}

// Helper: compute difference (in ns) between two timespecs
long timespec_diff_ns(struct timespec end, struct timespec start) {
    return (end.tv_sec - start.tv_sec) * NSEC_PER_SEC + (end.tv_nsec - start.tv_nsec);
}

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
#ifdef __QNX__
    if (strcmp(arg, "sporadic") == 0) return SCHED_SPORADIC;
#endif
    fprintf(stderr, "Unsupported or unknown policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [fifo|rr|sporadic]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int policy = parse_sched_policy(argv[1]);
    int prio = sched_get_priority_max(policy);

#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(15, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    struct rlimit mem_limit;
    mem_limit.rlim_cur = 512UL * 1024 * 1024;
    mem_limit.rlim_max = 512UL * 1024 * 1024;
    if (setrlimit(RLIMIT_AS, &mem_limit) != 0) {
        perror("setrlimit");
        exit(EXIT_FAILURE);
    }
#endif

    struct sched_param param = { .sched_priority = prio };

#ifdef __QNX__
    if (policy == SCHED_SPORADIC) {
        struct sched_param_sporadic sps = {
            .sched_priority = prio,
            .sched_ss_low_priority = 10,
            .sched_ss_max_repl = 5,
            .sched_ss_repl_period = { 0, 50000000 }, // 50 ms
            .sched_ss_init_budget = { 0, 5000000 }   // 5 ms
        };
        if (sched_setscheduler(0, policy, (struct sched_param *)&sps) != 0) {
            perror("sched_setscheduler (sporadic)");
            exit(EXIT_FAILURE);
        }
    } else
#endif
    {
        if (sched_setscheduler(0, policy, &param) != 0) {
            perror("sched_setscheduler");
            // Proceed anyway
        }
    }

    struct timespec next, now;
    long max_latency = 0;

    clock_gettime(CLOCK_MONOTONIC, &next);
    next = timespec_add(next, PERIOD_NS);

    for (int i = 0; i < ITERATIONS; i++) {
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) != 0) {
            perror("clock_nanosleep");
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        long latency = timespec_diff_ns(now, next);
        if (latency < 0) latency = -latency;
        if (latency > max_latency)
            max_latency = latency;

        next = timespec_add(next, PERIOD_NS);
    }

    printf("Max latency observed: %ld ns\n", max_latency);
    return 0;
}