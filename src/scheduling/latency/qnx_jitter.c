// avg_qnx8_jitter_spin.c
// Build with:
//   qcc -Vgcc_ntox86_64 -o avg_qnx8_jitter_spin avg_qnx8_jitter_spin.c -lrt -lpthread
//
// This version locks memory, pins to CPU 0, runs at FIFO RT max priority,
// and busy‑spins in user‑space to minimize context‑switch jitter.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <time.h>            // clock_gettime()
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>        // mlockall()
#include <sys/neutrino.h>    // ThreadCtl()
#include <pthread.h>
#include <sched.h>

#define NSEC_PER_SEC 1000000000LL
#define PERIOD_NS    1000000LL    /* 1 ms */
#define ITERATIONS   10000
#define RUNS         100

// convert a timespec to ns
static inline uint64_t ts_ns(const struct timespec *t) {
    return (uint64_t)t->tv_sec * NSEC_PER_SEC + t->tv_nsec;
}

// return CLOCK_MONOTONIC now in ns
static inline uint64_t now_ns(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return ts_ns(&tv);
}

/*
 * Run one ITERATIONS‐long busy‐spin test:
 *   - seed 'expected' from clock_gettime()
 *   - each iteration:
 *       • bump expected by PERIOD_NS
 *       • busy‑spin until now_ns() ≥ expected
 *       • timestamp and record |now−expected|
 */
static uint64_t measure_once(void) {
    struct timespec expected;
    uint64_t max_jitter = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &expected) == -1) {
        perror("clock_gettime");
        return 0;
    }

    for (int i = 0; i < ITERATIONS; i++) {
        // compute next deadline
        expected.tv_nsec += PERIOD_NS;
        if (expected.tv_nsec >= NSEC_PER_SEC) {
            expected.tv_sec++;
            expected.tv_nsec -= NSEC_PER_SEC;
        }

        // busy‑spin until that absolute time
        uint64_t deadline = ts_ns(&expected);
        while (now_ns() < deadline) {
            // tight spin
        }

        // measure actual time
        uint64_t actual = now_ns();
        uint64_t diff = (actual > deadline) ? (actual - deadline)
                                            : (deadline - actual);
        if (diff > max_jitter) {
            max_jitter = diff;
        }
    }

    return max_jitter;
}

int main(void) {
    // 1) lock all pages into RAM
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall");
    }

    // 2) print current tick period
    struct _clockperiod old;
    if (ClockPeriod(CLOCK_MONOTONIC, NULL, &old, 0) == -1) {
        perror("ClockPeriod");
    } else {
        printf("Current tick = %u ns\n", old.nsec);
    }

    // 3) gain RT privileges
    if (ThreadCtl(_NTO_TCTL_IO, NULL) == -1) {
        perror("ThreadCtl(_NTO_TCTL_IO)");
    }

    // 4) promote main to FIFO @ max priority
    struct sched_param sp = {
        .sched_priority = sched_get_priority_max(SCHED_FIFO)
    };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        perror("pthread_setschedparam");
    }

    // 5) pin to CPU 0
    unsigned runmask = 1u << 0;
    if (ThreadCtl(_NTO_TCTL_RUNMASK, (void*)(uintptr_t)runmask) == -1) {
        perror("ThreadCtl(_NTO_TCTL_RUNMASK)");
    }

    // 6) run several trials and average
    uint64_t sum = 0;
    printf("Average jitter over %d runs:\n", RUNS);
    for (int r = 1; r <= RUNS; r++) {
        uint64_t mj = measure_once();
        printf(" Run %3d: max_jitter = %8llu ns\n",
               r, (unsigned long long)mj);
        sum += mj;
    }
    printf("\nOverall average: %llu ns\n",
           (unsigned long long)(sum / RUNS));

    return 0;
}
