#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_ALLOCATIONS 1000000   // Total number of allocations
#define BLOCK_SIZE 64             // Size of each memory block in bytes

double get_time_in_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
    void **pointers = malloc(NUM_ALLOCATIONS * sizeof(void *));
    if (pointers == NULL) {
        perror("Failed to allocate pointer array");
        return 1;
    }

    printf("Testing memory throughput with %d allocations of %d bytes each...\n", NUM_ALLOCATIONS, BLOCK_SIZE);

    double start_time = get_time_in_seconds();

    // Allocation phase
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        pointers[i] = malloc(BLOCK_SIZE);
        if (pointers[i] == NULL) {
            fprintf(stderr, "Failed to allocate memory at iteration %d\n", i);
            return 1;
        }
    }

    // Deallocation phase
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        free(pointers[i]);
    }

    double end_time = get_time_in_seconds();
    double total_time = end_time - start_time;

    printf("Total time: %.6f seconds\n", total_time);
    printf("Throughput: %.2f operations/second (alloc + free)\n", (NUM_ALLOCATIONS * 2) / total_time);

    free(pointers);
    return 0;
}