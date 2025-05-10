#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <string.h>
#include <sys/neutrino.h>

#define HOLD_NS (50LL * 1000000LL)   /* 50 ms */
#define MED_NS  (200LL * 1000000LL)  /* 200 ms */

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* coordinate thread startup */
sem_t sem_low_locked;
sem_t sem_high_ready;
sem_t sem_high_done;

/* shared mutex (QNX uses PTHREAD_PRIO_INHERIT by default) */
pthread_mutex_t mutex;

int sched_policy = SCHED_FIFO;

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
    if (strcmp(arg, "sporadic") == 0) return SCHED_SPORADIC;
    fprintf(stderr, "Unknown policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

void *low_task(void *_) {
    (void)_;
    pthread_mutex_lock(&mutex);
    sem_post(&sem_low_locked);

    uint64_t start = now_ns();
    while (now_ns() - start < HOLD_NS) {}

    pthread_mutex_unlock(&mutex);
    return NULL;
}

void *high_task(void *_) {
    (void)_;
    sem_post(&sem_high_ready);

    uint64_t t0 = now_ns();
    pthread_mutex_lock(&mutex);
    uint64_t t1 = now_ns();

    const char *pname = (sched_policy == SCHED_FIFO) ? "FIFO" :
                        (sched_policy == SCHED_RR) ? "RR" : "SPORADIC";
    printf("QNX (%s): high waited = %llu ms\n", pname,
           (unsigned long long)((t1 - t0) / 1000000ULL));

    pthread_mutex_unlock(&mutex);
    sem_post(&sem_high_done);
    return NULL;
}

void *med_task(void *_) {
    (void)_;
    uint64_t start = now_ns();
    while (now_ns() - start < MED_NS) {}
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        sched_policy = parse_sched_policy(argv[1]);
    }

    sem_init(&sem_low_locked,  0, 0);
    sem_init(&sem_high_ready,  0, 0);
    sem_init(&sem_high_done,   0, 0);
    pthread_mutex_init(&mutex, NULL);

    int pmax = sched_get_priority_max(sched_policy);
    int pmed = pmax - 10;
    int plow = pmed - 10;

    pthread_attr_t A[3];
    pthread_t threads[3];
    int prios[3] = { plow, pmed, pmax };

    for (int i = 0; i < 3; i++) {
        pthread_attr_init(&A[i]);
        pthread_attr_setschedpolicy(&A[i], sched_policy);

        if (sched_policy == SCHED_SPORADIC) {
            struct sched_param_sporadic sps = {
                .sched_priority = prios[i],
                .sched_ss_low_priority = 10,
                .sched_ss_max_repl = 5,
                .sched_ss_repl_period = { 0, 50000000 }, // 50 ms
                .sched_ss_init_budget = { 0, 5000000 }   // 5 ms
            };
            pthread_attr_setschedparam(&A[i], (struct sched_param *)&sps);
        } else {
            struct sched_param sp = { .sched_priority = prios[i] };
            pthread_attr_setschedparam(&A[i], &sp);
        }
    }

    pthread_create(&threads[0], &A[0], low_task,  NULL);
    sem_wait(&sem_low_locked);

    pthread_create(&threads[2], &A[2], high_task, NULL);
    sem_wait(&sem_high_ready);

    pthread_create(&threads[1], &A[1], med_task,  NULL);
    sem_wait(&sem_high_done);

    return 0;
}
