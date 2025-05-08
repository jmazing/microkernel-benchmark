#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>

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

int main() {
#ifdef __linux__
    // Optional: pin the process to CPU 0 to reduce variability.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(15, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    // use only 512 mb of memory
    struct rlimit mem_limit;
    // Set both soft and hard limits to 512 MB.
    mem_limit.rlim_cur = 512UL * 1024 * 1024;
    mem_limit.rlim_max = 512UL * 1024 * 1024;
    
    if (setrlimit(RLIMIT_AS, &mem_limit) != 0) {
        perror("setrlimit");
        exit(EXIT_FAILURE);
    }
#endif

    struct timespec next, now;
    long max_latency = 0;
    int i;

    // Set high priority scheduling: SCHED_FIFO is ideal for real-time tasks.
    struct sched_param param;
    param.sched_priority = 80; // Choose a high, but safe, priority value.
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("sched_setscheduler");
        // Continue even if setting real-time priority fails.
    }

    // Get current time and set the first deadline.
    clock_gettime(CLOCK_MONOTONIC, &next);
    next = timespec_add(next, PERIOD_NS);

    for (i = 0; i < ITERATIONS; i++) {
        // Sleep until the next deadline
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) != 0) {
            perror("clock_nanosleep");
        }

        // Measure actual wake time.
        clock_gettime(CLOCK_MONOTONIC, &now);

        // Calculate absolute latency: the difference between expected and actual wake time.
        long latency = timespec_diff_ns(now, next);
        if (latency < 0) latency = -latency;

        if (latency > max_latency)
            max_latency = latency;

        // Set the next deadline.
        next = timespec_add(next, PERIOD_NS);
    }

    printf("Max latency observed: %ld ns\n", max_latency);
    return 0;
}
