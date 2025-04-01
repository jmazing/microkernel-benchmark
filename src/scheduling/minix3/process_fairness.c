#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sched.h>

#define NUM_PROCS 10   /* Number of child processes */
#define RUN_TIME 5     /* Run time in seconds for each child */

int main(void) {

    int pipefd[NUM_PROCS][2];
    pid_t pids[NUM_PROCS];
    unsigned long long counts[NUM_PROCS];

    /* Create children and a dedicated pipe for each */
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
        if (pids[i] == 0) {  /* Child process */
            /* Close the read end; we only need to write the result */
            close(pipefd[i][0]);

            struct timeval start, current;
            if (gettimeofday(&start, NULL) < 0) {
                perror("gettimeofday");
                exit(EXIT_FAILURE);
            }

            /* Use a volatile counter to prevent compiler optimizations */
            volatile unsigned long long counter = 0;
            while (1) {
                counter++;
                /* Check time every 1,000,000 iterations to reduce overhead */
                if (counter % 1000000 == 0) {
                    if (gettimeofday(&current, NULL) < 0) {
                        perror("gettimeofday");
                        exit(EXIT_FAILURE);
                    }
                    double elapsed = (current.tv_sec - start.tv_sec) +
                                     (current.tv_usec - start.tv_usec) / 1e6;
                    if (elapsed >= RUN_TIME) {
                        break;
                    }
                }
            }

            /* Write the counter value as text to the pipe */
            char buffer[64];
            int len = snprintf(buffer, sizeof(buffer), "%llu", counter);
            if (write(pipefd[i][1], buffer, len) < 0) {
                perror("write");
            }
            close(pipefd[i][1]);
            exit(EXIT_SUCCESS);
        }
        /* Parent continues to fork the next child */
    }

    /* Parent process: close write ends for all pipes */
    for (int i = 0; i < NUM_PROCS; i++) {
        close(pipefd[i][1]);
    }

    /* Wait for all child processes to finish */
    for (int i = 0; i < NUM_PROCS; i++) {
        wait(NULL);
    }

    /* Read each child's result from its pipe */
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

    /* Compute basic statistics */
    unsigned long long min = counts[0], max = counts[0], sum = 0;
    for (int i = 0; i < NUM_PROCS; i++) {
        if (counts[i] < min)
            min = counts[i];
        if (counts[i] > max)
            max = counts[i];
        sum += counts[i];
    }
    double avg = sum / (double)NUM_PROCS;

    /* Print the results */
    printf("Process fairness test results after %d seconds:\n", RUN_TIME);
    for (int i = 0; i < NUM_PROCS; i++) {
        printf("Process %d: %llu iterations\n", i, counts[i]);
    }
    printf("Min: %llu, Max: %llu, Avg: %.0f\n", min, max, avg);

    return 0;
}
