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

#define MSG_SIZE 64
#define ITERATIONS 10000
#define NSEC_PER_SEC 1000000000L

int sched_policy = SCHED_FIFO;

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
    fprintf(stderr, "Unknown scheduling policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

long timespec_diff_ns(struct timespec end, struct timespec start) {
    return (end.tv_sec - start.tv_sec) * NSEC_PER_SEC + (end.tv_nsec - start.tv_nsec);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        sched_policy = parse_sched_policy(argv[1]);
    }

    mqd_t mq_ptoc, mq_ctop;
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = MSG_SIZE,
        .mq_curmsgs = 0
    };

    mq_unlink("/mq_ptoc");
    mq_unlink("/mq_ctop");

    mq_ptoc = mq_open("/mq_ptoc", O_CREAT | O_RDWR, 0644, &attr);
    if (mq_ptoc == (mqd_t)-1) {
        perror("mq_open /mq_ptoc");
        exit(EXIT_FAILURE);
    }
    mq_ctop = mq_open("/mq_ctop", O_CREAT | O_RDWR, 0644, &attr);
    if (mq_ctop == (mqd_t)-1) {
        perror("mq_open /mq_ctop");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process: echo server
        int child_prio = sched_get_priority_max(sched_policy) - 10;
#ifdef __QNX__
        if (sched_policy == SCHED_SPORADIC) {
            struct sched_param_sporadic sps = {
                .sched_priority = child_prio,
                .sched_ss_low_priority = 10,
                .sched_ss_max_repl = 5,
                .sched_ss_repl_period = { 0, 50000000 },
                .sched_ss_init_budget = { 0, 5000000 }
            };
            sched_setscheduler(0, sched_policy, (struct sched_param *)&sps);
        } else
#endif
        {
            struct sched_param sp = { .sched_priority = child_prio };
            sched_setscheduler(0, sched_policy, &sp);
        }

        char buf[MSG_SIZE];
        for (int i = 0; i < ITERATIONS; i++) {
            ssize_t n = mq_receive(mq_ptoc, buf, MSG_SIZE, NULL);
            if (n == -1) {
                perror("Child mq_receive");
                exit(EXIT_FAILURE);
            }
            if (mq_send(mq_ctop, buf, MSG_SIZE, 0) == -1) {
                perror("Child mq_send");
                exit(EXIT_FAILURE);
            }
        }
        mq_close(mq_ptoc);
        mq_close(mq_ctop);
        exit(EXIT_SUCCESS);
    } else {
        // Parent: sender + timer
        int parent_prio = sched_get_priority_max(sched_policy);
#ifdef __QNX__
        if (sched_policy == SCHED_SPORADIC) {
            struct sched_param_sporadic sps = {
                .sched_priority = parent_prio,
                .sched_ss_low_priority = 10,
                .sched_ss_max_repl = 5,
                .sched_ss_repl_period = { 0, 50000000 },
                .sched_ss_init_budget = { 0, 5000000 }
            };
            sched_setscheduler(0, sched_policy, (struct sched_param *)&sps);
        } else
#endif
        {
            struct sched_param sp = { .sched_priority = parent_prio };
            sched_setscheduler(0, sched_policy, &sp);
        }

        char buf[MSG_SIZE];
        memset(buf, 'M', MSG_SIZE - 1);
        buf[MSG_SIZE - 1] = '\0';

        struct timespec start, end;
        long max_latency = 0;

        for (int i = 0; i < ITERATIONS; i++) {
            clock_gettime(CLOCK_MONOTONIC, &start);
            if (mq_send(mq_ptoc, buf, MSG_SIZE, 0) == -1) {
                perror("Parent mq_send");
                break;
            }
            if (mq_receive(mq_ctop, buf, MSG_SIZE, NULL) == -1) {
                perror("Parent mq_receive");
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency = timespec_diff_ns(end, start);
            if (latency > max_latency)
                max_latency = latency;
        }

        const char *policy_str =
            (sched_policy == SCHED_FIFO) ? "FIFO" :
            (sched_policy == SCHED_RR)   ? "RR" :
            "SPORADIC";

        printf("Max IPC round-trip latency (%s): %ld ns\n", policy_str, max_latency);

        mq_close(mq_ptoc);
        mq_close(mq_ctop);
        mq_unlink("/mq_ptoc");
        mq_unlink("/mq_ctop");

        kill(pid, SIGKILL);
        wait(NULL);
    }

    return 0;
}
