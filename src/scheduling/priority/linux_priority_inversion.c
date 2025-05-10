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
#include <stdlib.h>

#define HOLD_NS  (50LL  * 1000000LL)
#define MED_NS   (200LL * 1000000LL)

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Semaphores for thread orchestration
sem_t sem_low_start, sem_med_start, sem_high_start;
pthread_mutex_t mutex;

int sched_policy = SCHED_FIFO;

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
    if (strcmp(arg, "sporadic") == 0) {
        fprintf(stderr, "SCHED_SPORADIC is not supported on Linux.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Unknown policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

void* low_task(void*) {
    sem_wait(&sem_low_start);
    pthread_mutex_lock(&mutex);

    uint64_t t0 = now_ns();
    while (now_ns() - t0 < HOLD_NS) {}

    pthread_mutex_unlock(&mutex);
    return NULL;
}

void* med_task(void*) {
    sem_wait(&sem_med_start);

    uint64_t t0 = now_ns();
    while (now_ns() - t0 < MED_NS) {}

    return NULL;
}

void* high_task(void*) {
    sem_wait(&sem_high_start);

    uint64_t t0 = now_ns();
    pthread_mutex_lock(&mutex);
    uint64_t t1 = now_ns();

    printf("Linux (%s): high waited = %llu ms\n",
           (sched_policy == SCHED_FIFO) ? "FIFO" :
           (sched_policy == SCHED_RR)   ? "RR" : "UNKNOWN",
           (unsigned long long)((t1 - t0)/1000000ULL));

    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [fifo|rr]\n", argv[0]);
        return EXIT_FAILURE;
    }

    sched_policy = parse_sched_policy(argv[1]);

    // Pin main to CPU0
    cpu_set_t cpus;
    CPU_ZERO(&cpus); CPU_SET(0, &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus);

    // Promote main to highest priority for setup
    int prio_max = sched_get_priority_max(sched_policy);
    struct sched_param sp = { .sched_priority = prio_max };
    if (pthread_setschedparam(pthread_self(), sched_policy, &sp) != 0)
        perror("setschedparam(main)");

    // Init semaphores and mutex
    sem_init(&sem_low_start, 0, 0);
    sem_init(&sem_med_start, 0, 0);
    sem_init(&sem_high_start, 0, 0);
    pthread_mutex_init(&mutex, NULL);

    // Prepare thread attributes
    pthread_attr_t attr_low, attr_med, attr_high;
    pthread_attr_init(&attr_low);
    pthread_attr_init(&attr_med);
    pthread_attr_init(&attr_high);

    pthread_attr_setinheritsched(&attr_low, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_low, sched_policy);
    sp.sched_priority = prio_max - 2;
    pthread_attr_setschedparam(&attr_low, &sp);

    pthread_attr_setinheritsched(&attr_med, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_med, sched_policy);
    sp.sched_priority = prio_max - 1;
    pthread_attr_setschedparam(&attr_med, &sp);

    pthread_attr_setinheritsched(&attr_high, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr_high, sched_policy);
    sp.sched_priority = prio_max;
    pthread_attr_setschedparam(&attr_high, &sp);

    // Spawn threads
    pthread_t low, med, high;
    pthread_create(&low,  &attr_low,  low_task,  NULL);
    pthread_create(&med,  &attr_med,  med_task,  NULL);
    pthread_create(&high, &attr_high, high_task, NULL);

    // Sequence the starts
    sem_post(&sem_low_start);
    usleep(1000);
    sem_post(&sem_med_start);
    usleep(1000);
    sem_post(&sem_high_start);

    // Lower main's priority
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);

    pthread_join(high, NULL);
    return 0;
}
