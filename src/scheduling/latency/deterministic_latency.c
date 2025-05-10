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
  #include <sys/neutrino.h>
  #include <sched.h>
#else
  #include <sched.h>
#endif

#define NSEC_PER_SEC 1000000000L
#define PERIOD_NS    1000000L   // 1 ms
#define ITERATIONS   10000
#define LOAD_THREADS 3

static volatile int running = 1;

void* load_thread(void* arg) {
    while (running) {
        for (volatile int i = 0; i < 1000000; ++i) {}
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
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
#endif

        clock_gettime(CLOCK_MONOTONIC, &now);
        long dsec = now.tv_sec - next.tv_sec;
        long dnsec = now.tv_nsec - next.tv_nsec;
        long latency = llabs(dsec * NSEC_PER_SEC + dnsec);
        if (latency > max_latency)
            max_latency = latency;
    }

    printf("Max wake-up latency = %ld ns\n", max_latency);
    running = 0;
    return NULL;
}

int parse_sched_policy(const char* str) {
    if (strcmp(str, "fifo") == 0)
        return SCHED_FIFO;
    if (strcmp(str, "rr") == 0)
        return SCHED_RR;
#ifdef __QNX__
    if (strcmp(str, "sporadic") == 0)
        return SCHED_SPORADIC;
#endif
    fprintf(stderr, "Unknown or unsupported policy: %s\n", str);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [fifo|rr|sporadic]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int policy = parse_sched_policy(argv[1]);

    pthread_t loaders[LOAD_THREADS];
    pthread_t rt;
    int ret;

    for (int i = 0; i < LOAD_THREADS; ++i) {
        ret = pthread_create(&loaders[i], NULL, load_thread, NULL);
        if (ret != 0) {
            fprintf(stderr, "pthread_create load[%d] failed: %s\n", i, strerror(ret));
            exit(EXIT_FAILURE);
        }
    }

    pthread_attr_t rt_attr;
    pthread_attr_init(&rt_attr);
    pthread_attr_setschedpolicy(&rt_attr, policy);

    struct sched_param sp = { .sched_priority = 80 };
#ifdef __QNX__
    if (policy == SCHED_SPORADIC) {
        struct sched_param_sporadic sparam = {
            .sched_priority = 80,
            .sched_ss_low_priority = 10,
            .sched_ss_max_repl = 5,
            .sched_ss_repl_period = { 0, 50000000 }, // 50ms
            .sched_ss_init_budget = { 0, 5000000 }   // 5ms
        };
        ret = pthread_attr_setschedparam(&rt_attr, (struct sched_param*)&sparam);
    } else
#endif
    {
        ret = pthread_attr_setschedparam(&rt_attr, &sp);
    }

    pthread_attr_setinheritsched(&rt_attr, PTHREAD_EXPLICIT_SCHED);

    ret = pthread_create(&rt, &rt_attr, rt_thread, NULL);
    if (ret != 0) {
        fprintf(stderr, "pthread_create rt failed: %s\n", strerror(ret));
        exit(EXIT_FAILURE);
    }

    pthread_join(rt, NULL);
    for (int i = 0; i < LOAD_THREADS; ++i)
        pthread_join(loaders[i], NULL);

    return 0;
}
