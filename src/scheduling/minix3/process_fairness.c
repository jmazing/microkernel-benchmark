#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sched.h>

#define DEFAULT_NUM_PROCESSES 4
#define TEST_DURATION 2  // Test duration in seconds

// Shared memory structure with a stop flag and a flexible array for counters.
typedef struct {
    volatile int stop;
    unsigned long counters[];  // One counter per process
} shared_data_t;

int main(int argc, char *argv[]) {
#ifdef __linux__
    // Enforce execution on a single CPU (CPU 0)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
#endif
    
    int num_processes = DEFAULT_NUM_PROCESSES;
    if (argc > 1) {
        num_processes = atoi(argv[1]);
        if (num_processes <= 0) {
            num_processes = DEFAULT_NUM_PROCESSES;
        }
    }

    // Allocate shared memory using /dev/zero (since MAP_ANONYMOUS isn't available)
    size_t size = sizeof(shared_data_t) + num_processes * sizeof(unsigned long);
    int fd = open("/dev/zero", O_RDWR);
    if (fd < 0) {
        perror("open /dev/zero");
        exit(EXIT_FAILURE);
    }
    shared_data_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(fd);

    data->stop = 0;
    for (int i = 0; i < num_processes; i++) {
        data->counters[i] = 0;
    }

    // Array to store child process IDs.
    pid_t *child_pids = malloc(num_processes * sizeof(pid_t));
    if (child_pids == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Fork child processes.
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            // Child process: continuously increment its own counter.
            while (!data->stop) {
                data->counters[i]++;
            }
            exit(EXIT_SUCCESS);
        } else {
            // Parent process: record child PID.
            child_pids[i] = pid;
        }
    }

    // Let the children run for the specified test duration.
    sleep(TEST_DURATION);
    data->stop = 1;

    // Wait for all child processes to finish.
    for (int i = 0; i < num_processes; i++) {
        waitpid(child_pids[i], NULL, 0);
    }

    // Report the results.
    printf("Process Scheduling Fairness Test Results (Duration: %d seconds):\n", TEST_DURATION);
    for (int i = 0; i < num_processes; i++) {
        printf("Process %d: %lu iterations\n", i, data->counters[i]);
    }

    free(child_pids);
    munmap(data, size);
    return 0;
}
