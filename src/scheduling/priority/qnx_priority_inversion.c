// qnx_pinv_fixed.c
// Build with:
//   qcc -Vgcc_ntox86_64 -o qnx_pinv_fixed qnx_pinv_fixed.c -lrt -lpthread

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
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

void *low_task(void *_) {
    (void)_;
    pthread_mutex_lock(&mutex);
    sem_post(&sem_low_locked);    // tell main “I hold the lock now”

    /* busy‑wait for HOLD_NS */
    uint64_t start = now_ns();
    while (now_ns() - start < HOLD_NS) { /* spin */ }

    pthread_mutex_unlock(&mutex);
    return NULL;
}

void *high_task(void *_) {
    (void)_;
    sem_post(&sem_high_ready);    // tell main “I’m about to block on the mutex”

    uint64_t t0 = now_ns();
    pthread_mutex_lock(&mutex);   // this will block until low releases
    uint64_t t1 = now_ns();

    printf("QNX: high waited = %llu ms\n",
           (unsigned long long)((t1 - t0) / 1000000ULL));

    pthread_mutex_unlock(&mutex);
    sem_post(&sem_high_done);     // signal done
    return NULL;
}

void *med_task(void *_) {
    (void)_;
    /* busy‑spin for MED_NS */
    uint64_t start = now_ns();
    while (now_ns() - start < MED_NS) { /* spin */ }
    return NULL;
}

int main(void) {
    /* init semaphores */
    sem_init(&sem_low_locked,  0, 0);
    sem_init(&sem_high_ready,  0, 0);
    sem_init(&sem_high_done,   0, 0);

    /* default mutex already PTHREAD_PRIO_INHERIT on QNX */
    pthread_mutex_init(&mutex, NULL);

    /* pick three distinct RT priorities */
    int pmax = sched_get_priority_max(SCHED_FIFO);
    int pmed = pmax - 10;
    int plow = pmed - 10;

    /* common attr inits */
    pthread_attr_t A[3];
    struct sched_param sp;

    pthread_attr_init(&A[0]);
    pthread_attr_setschedpolicy(&A[0], SCHED_FIFO);
    sp.sched_priority = plow;
    pthread_attr_setschedparam(&A[0], &sp);

    pthread_attr_init(&A[1]);
    pthread_attr_setschedpolicy(&A[1], SCHED_FIFO);
    sp.sched_priority = pmed;
    pthread_attr_setschedparam(&A[1], &sp);

    pthread_attr_init(&A[2]);
    pthread_attr_setschedpolicy(&A[2], SCHED_FIFO);
    sp.sched_priority = pmax;
    pthread_attr_setschedparam(&A[2], &sp);

    pthread_t low, med, high;

    /* 1) start low */
    pthread_create(&low,  &A[0], low_task,  NULL);
    sem_wait(&sem_low_locked);    // wait until low holds the mutex

    /* 2) start high next */
    pthread_create(&high, &A[2], high_task, NULL);
    sem_wait(&sem_high_ready);    // wait until high is definitely blocked

    /* 3) now start med */
    pthread_create(&med,  &A[1], med_task,  NULL);

    /* 4) wait until high finishes measuring */
    sem_wait(&sem_high_done);

    return 0;
}
