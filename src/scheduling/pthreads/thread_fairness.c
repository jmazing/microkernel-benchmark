#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define DEFAULT_NUM_THREADS 4

// Global flag used to signal threads to stop work
volatile int stop = 0;

// Structure to hold per-thread data
typedef struct {
    int thread_id;
    unsigned long iterations;
} thread_data_t;

void* thread_function(void* arg) {
    thread_data_t *data = (thread_data_t *)arg;
    // Loop until the global stop flag is set
    while (!stop) {
        data->iterations++;
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
#ifdef __linux__  
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
#endif

    int num_threads = DEFAULT_NUM_THREADS;
    
    // Allow the number of threads to be specified via command line argument
    if (argc > 1) {
        num_threads = atoi(argv[1]);
        if (num_threads <= 0) {
            fprintf(stderr, "Invalid number of threads provided. Using default: %d\n", DEFAULT_NUM_THREADS);
            num_threads = DEFAULT_NUM_THREADS;
        }
    }

    // Allocate memory for thread handles and their data
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_data_t *thread_data = malloc(num_threads * sizeof(thread_data_t));
    if (!threads || !thread_data) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    // Create the threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].iterations = 0;
        if (pthread_create(&threads[i], NULL, thread_function, (void*)&thread_data[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // Let the threads run for 5 seconds
    // make this configurable
    sleep(5);
    stop = 1;

    // Wait for all threads to finish
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Report the results
    printf("Thread Fairness Test Results (2 seconds run):\n");
    for (int i = 0; i < num_threads; i++) {
        printf("Thread %d: %lu iterations\n", thread_data[i].thread_id, thread_data[i].iterations);
    }

    free(threads);
    free(thread_data);
    return 0;
}
