#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define NSEC_PER_SEC 1000000000LL
#define PERIOD_NS     1000000LL
#define ITERATIONS    10000
#define RUNS          100

static inline uint64_t ts_ns(const struct timespec *t) {
    return (uint64_t)t->tv_sec * NSEC_PER_SEC + t->tv_nsec;
}

static uint64_t measure_once(void) {
    struct timespec expected, now;
    clock_gettime(CLOCK_MONOTONIC, &expected);
    uint64_t max_j = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        expected.tv_nsec += PERIOD_NS;
        if (expected.tv_nsec >= NSEC_PER_SEC) {
            expected.tv_sec++;
            expected.tv_nsec -= NSEC_PER_SEC;
        }

        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &expected, NULL) != 0) {
            perror("clock_nanosleep");
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t a = ts_ns(&now), b = ts_ns(&expected);
        uint64_t diff = a > b ? a - b : b - a;
        if (diff > max_j) max_j = diff;
    }

    return max_j;
}

int parse_sched_policy(const char *str) {
    if (strcmp(str, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(str, "rr") == 0) return SCHED_RR;
#ifdef __QNX__
    if (strcmp(str, "sporadic") == 0) return SCHED_SPORADIC;
#endif
    fprintf(stderr, "Unsupported or unknown policy: %s\n", str);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [fifo|rr|sporadic]\n", argv[0]);
        return 1;
    }

    int policy = parse_sched_policy(argv[1]);

    // Set scheduling policy and priority
    struct sched_param sp = { .sched_priority = sched_get_priority_max(policy) };
#ifdef __QNX__
    if (policy == SCHED_SPORADIC) {
        struct sched_param_sporadic sps = {
            .sched_priority = sp.sched_priority,
            .sched_ss_low_priority = 10,
            .sched_ss_max_repl = 5,
            .sched_ss_repl_period = { 0, 50000000 }, // 50 ms
            .sched_ss_init_budget = { 0, 5000000 }   // 5 ms
        };
        if (pthread_setschedparam(pthread_self(), policy, (struct sched_param *)&sps) != 0) {
            perror("pthread_setschedparam (sporadic)");
            return 1;
        }
    } else
#endif
    {
        if (pthread_setschedparam(pthread_self(), policy, &sp) != 0) {
            perror("pthread_setschedparam");
            return 1;
        }
    }

    // Pin to CPU 0
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(0, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);

    // Run the jitter measurements
    uint64_t sum = 0;
    for (int r = 1; r <= RUNS; r++) {
        uint64_t mj = measure_once();
        printf("Run %3d: max_jitter = %5llu ns\n", r, (unsigned long long)mj);
        sum += mj;
    }
    printf("\nAverage over %d runs: %llu ns\n", RUNS, (unsigned long long)(sum / RUNS));
    return 0;
}
