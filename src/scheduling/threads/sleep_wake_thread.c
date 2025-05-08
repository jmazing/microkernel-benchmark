#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>

#define DEFAULT_NUM_SLEEP_THREADS 2
#define DEFAULT_NUM_LOAD_THREADS 2
#define SLEEP_INTERVAL_NS 10000000L  // 10 milliseconds in nanoseconds
#define TEST_DURATION 5             // Run test for 5 seconds

// Global flag to signal threads to stop
volatile int stop = 0;

// Data structure for sleep measurement threads
typedef struct {
    int thread_id;
    long total_jitter_ns;
    long iterations;
} sleep_thread_data_t;

// Thread function for measuring sleep/wake precision
void* sleep_thread_function(void* arg) {
    sleep_thread_data_t *data = (sleep_thread_data_t *)arg;
    struct timespec start, end;
    struct timespec req = { .tv_sec = 0, .tv_nsec = SLEEP_INTERVAL_NS };
    while (!stop) {
        // Record start time
        clock_gettime(CLOCK_MONOTONIC, &start);
        // Sleep for the requested interval
        nanosleep(&req, NULL);
        // Record end time
        clock_gettime(CLOCK_MONOTONIC, &end);

        // Calculate elapsed time in nanoseconds
        long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                          (end.tv_nsec - start.tv_nsec);
        // Jitter: the extra time beyond the requested sleep duration
        long jitter = elapsed_ns - SLEEP_INTERVAL_NS;
        if (jitter < 0) jitter = 0; // Only consider delays

        data->total_jitter_ns += jitter;
        data->iterations++;
    }
    pthread_exit(NULL);
}

// Thread function to generate CPU load
void* load_thread_function(void* arg) {
    (void)arg; // Unused parameter
    volatile unsigned long counter = 0;
    while (!stop) {
        counter++; // Busy work
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
#ifdef __linux__
    // use only 1 cpu
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }

    // use only 512 mb of memory
    struct rlimit mem_limit;
    // Set both soft and hard limits to 512 MB.
    mem_limit.rlim_cur = 512UL * 1024 * 1024;
    mem_limit.rlim_max = 512UL * 1024 * 1024;
    
    if (setrlimit(RLIMIT_AS, &mem_limit) != 0) {
        perror("setrlimit");
        exit(EXIT_FAILURE);
    }
#endif

    int num_sleep_threads = DEFAULT_NUM_SLEEP_THREADS;
    int num_load_threads = DEFAULT_NUM_LOAD_THREADS;

    // Optional command line parameters: number of sleep threads, load threads
    if (argc > 1) {
        num_sleep_threads = atoi(argv[1]);
        if (num_sleep_threads <= 0) num_sleep_threads = DEFAULT_NUM_SLEEP_THREADS;
    }
    if (argc > 2) {
        num_load_threads = atoi(argv[2]);
        if (num_load_threads < 0) num_load_threads = DEFAULT_NUM_LOAD_THREADS;
    }

    pthread_t *sleep_threads = malloc(num_sleep_threads * sizeof(pthread_t));
    sleep_thread_data_t *sleep_data = malloc(num_sleep_threads * sizeof(sleep_thread_data_t));
    pthread_t *load_threads = malloc(num_load_threads * sizeof(pthread_t));

    if (!sleep_threads || !sleep_data || !load_threads) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Create sleep measurement threads
    for (int i = 0; i < num_sleep_threads; i++) {
        sleep_data[i].thread_id = i;
        sleep_data[i].total_jitter_ns = 0;
        sleep_data[i].iterations = 0;
        if (pthread_create(&sleep_threads[i], NULL, sleep_thread_function, &sleep_data[i]) != 0) {
            perror("pthread_create (sleep thread)");
            exit(EXIT_FAILURE);
        }
    }

    // Only tests basic CPU contention, and not context switches, delys, interrupts, etc.
    // Create load threads to generate CPU contention
    for (int i = 0; i < num_load_threads; i++) {
        if (pthread_create(&load_threads[i], NULL, load_thread_function, NULL) != 0) {
            perror("pthread_create (load thread)");
            exit(EXIT_FAILURE);
        }
    }

    // Run the test for the specified duration
    sleep(TEST_DURATION);
    stop = 1;

    // Wait for sleep threads to finish
    for (int i = 0; i < num_sleep_threads; i++) {
        pthread_join(sleep_threads[i], NULL);
    }
    // Wait for load threads to finish
    for (int i = 0; i < num_load_threads; i++) {
        pthread_join(load_threads[i], NULL);
    }

    // Report the results: average jitter per sleep thread
    printf("Sleep/Wake Precision Test Results (over %d seconds):\n", TEST_DURATION);
    for (int i = 0; i < num_sleep_threads; i++) {
        if (sleep_data[i].iterations > 0) {
            double avg_jitter = (double)sleep_data[i].total_jitter_ns / sleep_data[i].iterations;
            printf("Sleep Thread %d: %ld iterations, Average Jitter: %.2f ns\n",
                   sleep_data[i].thread_id, sleep_data[i].iterations, avg_jitter);
        }
    }

    free(sleep_threads);
    free(sleep_data);
    free(load_threads);
    return 0;
}
