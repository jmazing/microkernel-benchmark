#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sched.h>
#include <sys/stat.h>

// Number of processes and test duration
#define DEFAULT_NUM_PROCESSES 4
#define TEST_DURATION 2  // in seconds

// Shared memory structure: a stop flag and an array of counters (one per process)
typedef struct {
    volatile int stop;
    unsigned long counters[];  // flexible array member
} shared_data_t;

int main(int argc, char *argv[]) {
    int num_processes = DEFAULT_NUM_PROCESSES;
    if (argc > 1) {
        num_processes = atoi(argv[1]);
        if (num_processes <= 0) {
            num_processes = DEFAULT_NUM_PROCESSES;
        }
    }

    // Enforce execution on a single CPU (CPU 0)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    // Calculate the size needed for shared memory
    size_t size = sizeof(shared_data_t) + num_processes * sizeof(unsigned long);
    const char *shm_name = "/my_shm_obj";

    // Create and open a shared memory object
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    // Set the shared memory size
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    // Map the shared memory object into memory
    shared_data_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(fd);
    // Unlink the shared memory object (it will persist until unmapped)
    shm_unlink(shm_name);

    // Initialize shared memory: clear the stop flag and counters.
    data->stop = 0;
    for (int i = 0; i < num_processes; i++) {
        data->counters[i] = 0;
    }

    // Allocate array to store child process IDs.
    pid_t *child_pids = malloc(num_processes * sizeof(pid_t));
    if (!child_pids) {
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
            // Each child process increments its own counter until the stop flag is set.
            while (!data->stop) {
                data->counters[i]++;
            }
            exit(EXIT_SUCCESS);
        } else {
            child_pids[i] = pid;
        }
    }

    // Let the children run for the test duration.
    sleep(TEST_DURATION);
    data->stop = 1;

    // Wait for all child processes to finish.
    for (int i = 0; i < num_processes; i++) {
        waitpid(child_pids[i], NULL, 0);
    }

    // Print out the counters for each process.
    printf("Process Scheduling Fairness Test Results (Duration: %d seconds):\n", TEST_DURATION);
    for (int i = 0; i < num_processes; i++) {
        printf("Process %d: %lu iterations\n", i, data->counters[i]);
    }

    free(child_pids);
    munmap(data, size);
    return 0;
}
