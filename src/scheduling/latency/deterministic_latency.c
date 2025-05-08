// deterministic_latency.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#ifdef __QNX__
  #include <sys/neutrino.h>    // TimerTimeout(), ThreadCtl(), EOK
#else
  #include <sched.h>           // CPU_ZERO, CPU_SET, sched_setaffinity
#endif

#define NSEC_PER_SEC 1000000000L
#define PERIOD_NS    1000000L   // 1 ms
#define ITERATIONS   10000      // number of deadlines
#define LOAD_THREADS 3          // background load threads

static volatile int running = 1;

void* load_thread(void* arg) {
    while (running) {
        for (volatile int i = 0; i < 1000000; ++i) { }
        usleep(100);
    }
    return NULL;
}

void* rt_thread(void* arg) {
    struct timespec now, next;
    long max_latency = 0;

    clock_gettime(CLOCK_MONOTONIC, &next);

    for (int i = 0; i < ITERATIONS; ++i) {
        next.tv_nsec += PERIOD_NS;
        if (next.tv_nsec >= NSEC_PER_SEC) {
            next.tv_sec += next.tv_nsec / NSEC_PER_SEC;
            next.tv_nsec %= NSEC_PER_SEC;
        }

    #ifdef __QNX__
        // QNX raw kernel nanosleep until absolute time 'next'
        {
            uint64_t abs_ns = next.tv_sec * NSEC_PER_SEC + next.tv_nsec;
            int rc = TimerTimeout(
                        CLOCK_MONOTONIC,
                        _NTO_TIMEOUT_NANOSLEEP,
                        NULL,
                       &abs_ns,
                        NULL
                     );
            if (rc != EOK) {
                perror("TimerTimeout");
                break;
            }
        }
    #else
        // Linux fallback
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    #endif

        clock_gettime(CLOCK_MONOTONIC, &now);
        long dsec  = now.tv_sec  - next.tv_sec;
        long dnsec = now.tv_nsec - next.tv_nsec;
        long latency = llabs(dsec * NSEC_PER_SEC + dnsec);
        if (latency > max_latency)
            max_latency = latency;
    }

    printf("Max wake‑up latency = %ld ns\n", max_latency);
    running = 0;
    return NULL;
}

int main(void) {
    pthread_t loaders[LOAD_THREADS];
    pthread_t rt;
    int ret;

    // spawn background load
    for (int i = 0; i < LOAD_THREADS; ++i) {
        ret = pthread_create(&loaders[i], NULL, load_thread, NULL);
        if (ret != 0) {
            fprintf(stderr,
                "pthread_create load[%d] failed: %s\n",
                i, strerror(ret));
            exit(EXIT_FAILURE);
        }
    }

    // setup real‑time thread attributes
    pthread_attr_t rt_attr;
    pthread_attr_init(&rt_attr);
    pthread_attr_setschedpolicy(&rt_attr, SCHED_FIFO);
    struct sched_param sp = { .sched_priority = 80 };
    pthread_attr_setschedparam(&rt_attr, &sp);
    pthread_attr_setinheritsched(&rt_attr, PTHREAD_EXPLICIT_SCHED);

    // create RT thread (needs sudo or cap_sys_nice on Linux)
    ret = pthread_create(&rt, &rt_attr, rt_thread, NULL);
    if (ret != 0) {
        fprintf(stderr,
            "pthread_create rt failed: %s\n",
            strerror(ret));
        exit(EXIT_FAILURE);
    }

    // wait and clean up
    pthread_join(rt, NULL);
    for (int i = 0; i < LOAD_THREADS; ++i) {
        pthread_join(loaders[i], NULL);
    }

    return 0;
}
