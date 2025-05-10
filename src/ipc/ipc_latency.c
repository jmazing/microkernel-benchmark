#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#define NSEC_PER_SEC 1000000000L
#define ITERATIONS   10000
#define QUEUE_NAME   "/ipc_test_queue"
#define MSG_SIZE     64
#define NUM_LOAD_THREADS 3

int sched_policy = SCHED_FIFO;

struct timespec timespec_add(struct timespec t, long ns) {
    t.tv_nsec += ns;
    if (t.tv_nsec >= NSEC_PER_SEC) {
        t.tv_sec += t.tv_nsec / NSEC_PER_SEC;
        t.tv_nsec %= NSEC_PER_SEC;
    }
    return t;
}

long timespec_diff_ns(struct timespec end, struct timespec start) {
    return (end.tv_sec - start.tv_sec) * NSEC_PER_SEC + (end.tv_nsec - start.tv_nsec);
}

void *load_thread_func(void *arg) {
    volatile unsigned long counter = 0;
    while (1) {
        counter++;
    }
    return NULL;
}

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
#ifdef __QNX__
    if (strcmp(arg, "sporadic") == 0) return SCHED_SPORADIC;
#else
    if (strcmp(arg, "sporadic") == 0) {
        fprintf(stderr, "SCHED_SPORADIC not supported on Linux.\n");
        exit(EXIT_FAILURE);
    }
#endif
    fprintf(stderr, "Unknown policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        sched_policy = parse_sched_policy(argv[1]);
    }

    mqd_t mq;
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = MSG_SIZE,
        .mq_curmsgs = 0
    };

    mq_unlink(QUEUE_NAME);
    mq = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    pthread_t load_threads[NUM_LOAD_THREADS];
    for (int i = 0; i < NUM_LOAD_THREADS; i++) {
        if (pthread_create(&load_threads[i], NULL, load_thread_func, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mq_close(mq);
        mq_unlink(QUEUE_NAME);
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char buffer[MSG_SIZE];
        for (int i = 0; i < ITERATIONS; i++) {
            if (mq_receive(mq, buffer, MSG_SIZE, NULL) == -1) {
                perror("mq_receive in child");
                exit(EXIT_FAILURE);
            }
            if (mq_send(mq, buffer, strlen(buffer) + 1, 0) == -1) {
                perror("mq_send in child");
                exit(EXIT_FAILURE);
            }
        }
        mq_close(mq);
        exit(EXIT_SUCCESS);
    } else {
        // Set real-time scheduling for the parent
        int prio = sched_get_priority_max(sched_policy);
        struct sched_param param = { .sched_priority = prio };

#ifdef __QNX__
        if (sched_policy == SCHED_SPORADIC) {
            struct sched_param_sporadic sps = {
                .sched_priority = prio,
                .sched_ss_low_priority = 10,
                .sched_ss_max_repl = 5,
                .sched_ss_repl_period = { 0, 50000000 },
                .sched_ss_init_budget = { 0, 5000000 }
            };
            if (sched_setscheduler(0, sched_policy, (struct sched_param *)&sps) != 0) {
                perror("sched_setscheduler (sporadic)");
            }
        } else
#endif
        {
            if (sched_setscheduler(0, sched_policy, &param) != 0) {
                perror("sched_setscheduler");
            }
        }

        // Measure round-trip latency
        struct timespec start, end;
        long max_latency = 0;
        char buffer[MSG_SIZE];
        snprintf(buffer, MSG_SIZE, "ping");

        for (int i = 0; i < ITERATIONS; i++) {
            clock_gettime(CLOCK_MONOTONIC, &start);
            if (mq_send(mq, buffer, strlen(buffer) + 1, 0) == -1) {
                perror("mq_send");
                break;
            }
            if (mq_receive(mq, buffer, MSG_SIZE, NULL) == -1) {
                perror("mq_receive");
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency = timespec_diff_ns(end, start);
            if (latency > max_latency)
                max_latency = latency;
        }

        const char *policy_str = (sched_policy == SCHED_FIFO) ? "FIFO" :
                                 (sched_policy == SCHED_RR)   ? "RR" :
                                 "SPORADIC";
        printf("Max IPC round-trip latency (%s): %ld ns\n", policy_str, max_latency);

        kill(pid, SIGKILL);
        for (int i = 0; i < NUM_LOAD_THREADS; i++) {
            pthread_cancel(load_threads[i]);
            pthread_join(load_threads[i], NULL);
        }

        mq_close(mq);
        mq_unlink(QUEUE_NAME);
    }

    return 0;
}
