// linux_priority_inversion_orch.c
// Build with:
//   gcc -O2 -o linux_pinv_orch linux_priority_inversion_orch.c -pthread
// Run with RT privileges:
//   sudo chrt -f 99 ./linux_pinv_orch

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define HOLD_NS  (50LL  * 1000000LL)   // 50 ms
#define MED_NS   (200LL * 1000000LL)   // 200 ms

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Synchronization semaphores
sem_t sem_low_start, sem_med_start, sem_high_start;

// Shared mutex (default: no PI on Linux)
pthread_mutex_t mutex;

// Low‑priority thread: wait to start, then lock+hold
void* low_task(void*) {
    sem_wait(&sem_low_start);
    pthread_mutex_lock(&mutex);

    uint64_t t0 = now_ns();
    while (now_ns() - t0 < HOLD_NS) { /* busy spin 50ms */ }

    pthread_mutex_unlock(&mutex);
    return NULL;
}

// Medium‑priority thread: wait to start, then spin
void* med_task(void*) {
    sem_wait(&sem_med_start);

    uint64_t t0 = now_ns();
    while (now_ns() - t0 < MED_NS) { /* busy spin 200ms */ }

    return NULL;
}

// High‑priority thread: wait to start, then measure lock wait
void* high_task(void*) {
    sem_wait(&sem_high_start);

    uint64_t t0 = now_ns();
    pthread_mutex_lock(&mutex);
    uint64_t t1 = now_ns();

    printf("Linux: high waited = %llu ms\n",
        (unsigned long long)((t1 - t0)/1000000ULL));

    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main() {
    // 1) Pin main to CPU0
    cpu_set_t cpus; CPU_ZERO(&cpus); CPU_SET(0, &cpus);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) != 0)
        perror("affinity");

    // 2) Promote main to highest RT priority
    int prio_max = sched_get_priority_max(SCHED_FIFO);
    struct sched_param sp = { .sched_priority = prio_max };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        perror("setschedparam(main)");

    // 3) Init semaphores and mutex
    sem_init(&sem_low_start, 0, 0);
    sem_init(&sem_med_start, 0, 0);
    sem_init(&sem_high_start,0, 0);
    pthread_mutex_init(&mutex, NULL);

    // 4) Prepare thread attrs for low/med/high with EXPLICIT_SCHED
    pthread_attr_t attr_low, attr_med, attr_high;
    int prio_low = prio_max - 2;
    int prio_med = prio_max - 1;

    pthread_attr_init(&attr_low);
    pthread_attr_setinheritsched(&attr_low, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_low, SCHED_FIFO);
    sp.sched_priority = prio_low;
    pthread_attr_setschedparam(&attr_low, &sp);

    pthread_attr_init(&attr_med);
    pthread_attr_setinheritsched(&attr_med, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_med, SCHED_FIFO);
    sp.sched_priority = prio_med;
    pthread_attr_setschedparam(&attr_med, &sp);

    pthread_attr_init(&attr_high);
    pthread_attr_setinheritsched(&attr_high, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_high, SCHED_FIFO);
    sp.sched_priority = prio_max;
    pthread_attr_setschedparam(&attr_high, &sp);

    // 5) Spawn all three
    pthread_t low, med, high;
    pthread_create(&low,  &attr_low,  low_task,  NULL);
    pthread_create(&med,  &attr_med,  med_task,  NULL);
    pthread_create(&high, &attr_high, high_task, NULL);

    // 6) Sequence their starts
    sem_post(&sem_low_start);      // start low
    usleep(1000);                  // let low grab lock
    sem_post(&sem_med_start);      // start med (preempts low)
    usleep(1000);                  // let med actually spin
    sem_post(&sem_high_start);     // start high (preempts med, blocks)

    // 7) Drop main out of the way so med can run
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);

    // 8) Wait for high to print
    pthread_join(high, NULL);
    return 0;
}
