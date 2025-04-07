#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

#define DEFAULT_NUM_SLEEP_PROCS 2
#define DEFAULT_NUM_LOAD_PROCS  2
#define SLEEP_INTERVAL_NS 10000000L  // 10 milliseconds in nanoseconds
#define TEST_DURATION 5             // Run test for 5 seconds

// Data structure to hold sleep process results.
typedef struct {
    int proc_id;
    long total_jitter_ns;
    long iterations;
} sleep_proc_data_t;

// Function for sleep measurement process.
void sleep_proc_function(int pipe_fd, int proc_id) {
    sleep_proc_data_t data;
    data.proc_id = proc_id;
    data.total_jitter_ns = 0;
    data.iterations = 0;

    struct timespec test_start, now, req;
    req.tv_sec = 0;
    req.tv_nsec = SLEEP_INTERVAL_NS;
    if (clock_gettime(CLOCK_MONOTONIC, &test_start) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    while (1) {
        struct timespec before, after;
        if (clock_gettime(CLOCK_MONOTONIC, &before) != 0) {
            perror("clock_gettime");
            exit(EXIT_FAILURE);
        }
        if (nanosleep(&req, NULL) != 0) {
            perror("nanosleep");
            // Even if interrupted, we continue the test.
        }
        if (clock_gettime(CLOCK_MONOTONIC, &after) != 0) {
            perror("clock_gettime");
            exit(EXIT_FAILURE);
        }
        long elapsed_ns = (after.tv_sec - before.tv_sec) * 1000000000L +
                          (after.tv_nsec - before.tv_nsec);
        long jitter = elapsed_ns - SLEEP_INTERVAL_NS;
        if (jitter < 0)
            jitter = 0; // ignore if sleep was early

        data.total_jitter_ns += jitter;
        data.iterations++;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime");
            exit(EXIT_FAILURE);
        }
        double total_elapsed = (now.tv_sec - test_start.tv_sec) +
                               (now.tv_nsec - test_start.tv_nsec) / 1e9;
        if (total_elapsed >= TEST_DURATION) {
            break;
        }
    }

    // Write results as text: "proc_id total_jitter_ns iterations"
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer), "%d %ld %ld", 
                       data.proc_id, data.total_jitter_ns, data.iterations);
    if (write(pipe_fd, buffer, len) != len) {
        perror("write");
    }
    close(pipe_fd);
    exit(EXIT_SUCCESS);
}

// Function for load process: busy loop to generate CPU contention.
void load_proc_function(void) {
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    volatile unsigned long counter = 0;
    while (1) {
        counter++; // Busy work
        if (counter % 1000000 == 0) {
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
                perror("clock_gettime");
                exit(EXIT_FAILURE);
            }
            double elapsed = (now.tv_sec - start.tv_sec) +
                             (now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed >= TEST_DURATION) {
                break;
            }
        }
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
#ifdef __linux__
    // Optional: pin the process to CPU 0 to reduce variability.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
#endif

    int num_sleep_procs = DEFAULT_NUM_SLEEP_PROCS;
    int num_load_procs = DEFAULT_NUM_LOAD_PROCS;

    // Optionally override defaults via command-line arguments.
    if (argc > 1) {
        num_sleep_procs = atoi(argv[1]);
        if (num_sleep_procs <= 0)
            num_sleep_procs = DEFAULT_NUM_SLEEP_PROCS;
    }
    if (argc > 2) {
        num_load_procs = atoi(argv[2]);
        if (num_load_procs < 0)
            num_load_procs = DEFAULT_NUM_LOAD_PROCS;
    }

    // Allocate pipes for sleep processes.
    int (*sleep_pipes)[2] = malloc(num_sleep_procs * sizeof(int[2]));
    if (!sleep_pipes) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Fork sleep measurement processes.
    for (int i = 0; i < num_sleep_procs; i++) {
        if (pipe(sleep_pipes[i]) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            // Child process: close the read end.
            close(sleep_pipes[i][0]);
            sleep_proc_function(sleep_pipes[i][1], i);
            // Not reached.
        }
        // Parent process: continue and eventually read from sleep_pipes[i][0].
        // Close the write end in the parent after forking all children.
    }

    // Fork load processes.
    for (int i = 0; i < num_load_procs; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            load_proc_function();
            // Not reached.
        }
    }

    // In parent: close the write ends of sleep pipes.
    for (int i = 0; i < num_sleep_procs; i++) {
        close(sleep_pipes[i][1]);
    }

    // Wait for all child processes (both sleep and load) to finish.
    while (wait(NULL) > 0)
        ;

    // Read and report results from sleep processes.
    printf("Sleep/Wake Precision Test Results (over %d seconds):\n", TEST_DURATION);
    for (int i = 0; i < num_sleep_procs; i++) {
        char buf[128];
        ssize_t n = read(sleep_pipes[i][0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            sleep_proc_data_t result;
            // Expecting format: "proc_id total_jitter_ns iterations"
            if (sscanf(buf, "%d %ld %ld", &result.proc_id, &result.total_jitter_ns, &result.iterations) == 3) {
                double avg_jitter = (result.iterations > 0) ?
                                    (double)result.total_jitter_ns / result.iterations : 0.0;
                printf("Sleep Process %d: %ld iterations, Average Jitter: %.2f ns\n",
                       result.proc_id, result.iterations, avg_jitter);
            } else {
                fprintf(stderr, "Error parsing result from process %d: %s\n", i, buf);
            }
        } else {
            fprintf(stderr, "No data read from sleep process %d\n", i);
        }
        close(sleep_pipes[i][0]);
    }

    free(sleep_pipes);
    return 0;
}
