#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sched.h>
#include <string.h>
#include <errno.h>

#define NUM_PROCS 10
#define RUN_TIME  5

int sched_policy = SCHED_FIFO;

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
#ifdef __QNX__
    if (strcmp(arg, "sporadic") == 0) return SCHED_SPORADIC;
#else
    if (strcmp(arg, "sporadic") == 0) {
        fprintf(stderr, "SCHED_SPORADIC is not supported on Linux.\n");
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

    int pipefd[NUM_PROCS][2];
    pid_t pids[NUM_PROCS];
    unsigned long long counts[NUM_PROCS];

    for (int i = 0; i < NUM_PROCS; i++) {
        if (pipe(pipefd[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) {
            // Child process
            close(pipefd[i][0]);

            int prio = sched_get_priority_max(sched_policy) - (i % 3); // stagger a bit
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
                    perror("sched_setscheduler (child)");
                }
            } else
#endif
            {
                struct sched_param sp = { .sched_priority = prio };
                if (sched_setscheduler(0, sched_policy, &sp) != 0) {
                    perror("sched_setscheduler (child)");
                }
            }

            struct timeval start, current;
            gettimeofday(&start, NULL);
            volatile unsigned long long counter = 0;

            while (1) {
                counter++;
                if (counter % 1000000 == 0) {
                    gettimeofday(&current, NULL);
                    double elapsed = (current.tv_sec - start.tv_sec) +
                                     (current.tv_usec - start.tv_usec) / 1e6;
                    if (elapsed >= RUN_TIME)
                        break;
                }
            }

            char buffer[64];
            int len = snprintf(buffer, sizeof(buffer), "%llu", counter);
            write(pipefd[i][1], buffer, len);
            close(pipefd[i][1]);
            exit(EXIT_SUCCESS);
        }
    }

    for (int i = 0; i < NUM_PROCS; i++) {
        close(pipefd[i][1]);
    }

    for (int i = 0; i < NUM_PROCS; i++) {
        wait(NULL);
    }

    for (int i = 0; i < NUM_PROCS; i++) {
        char buf[64];
        int n = read(pipefd[i][0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            counts[i] = strtoull(buf, NULL, 10);
        } else {
            counts[i] = 0;
        }
        close(pipefd[i][0]);
    }

    unsigned long long min = counts[0], max = counts[0], sum = 0;
    for (int i = 0; i < NUM_PROCS; i++) {
        if (counts[i] < min) min = counts[i];
        if (counts[i] > max) max = counts[i];
        sum += counts[i];
    }
    double avg = sum / (double)NUM_PROCS;

    const char *policy_name =
        (sched_policy == SCHED_FIFO) ? "FIFO" :
        (sched_policy == SCHED_RR)   ? "RR"   :
        "SPORADIC";

    printf("Process fairness test (%s) results after %d seconds:\n", policy_name, RUN_TIME);
    for (int i = 0; i < NUM_PROCS; i++) {
        printf("Process %d: %llu iterations\n", i, counts[i]);
    }
    printf("Min: %llu, Max: %llu, Avg: %.0f\n", min, max, avg);

    return 0;
}
