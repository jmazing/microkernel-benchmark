// linux_avg_jitter_nanosleep.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>

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

        if (clock_nanosleep(CLOCK_MONOTONIC,
                            TIMER_ABSTIME,
                            &expected, NULL) != 0) {
            perror("clock_nanosleep"); break;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t a = ts_ns(&now), b = ts_ns(&expected);
        uint64_t diff = a > b ? a - b : b - a;
        if (diff > max_j) max_j = diff;
    }
    return max_j;
}

int main(void) {
    // Promote to FIFO and pin to CPU0, as before...
    struct sched_param sp = { .sched_priority = sched_get_priority_max(SCHED_FIFO) };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    cpu_set_t cpus; 
    CPU_ZERO(&cpus); 
    CPU_SET(0, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);

    uint64_t sum = 0;
    for (int r = 1; r <= RUNS; r++) {
        uint64_t mj = measure_once();
        printf("Run %3d: max_jitter = %5llu ns\n", r, (unsigned long long)mj);
        sum += mj;
    }
    printf("\nAverage over %d runs: %llu ns\n",
           RUNS, (unsigned long long)(sum / RUNS));
    return 0;
}
