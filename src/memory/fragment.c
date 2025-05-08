#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_ALLOCS 1000000
#define ALLOC_SIZE 2048

int main() {
    void *ptrs[NUM_ALLOCS];
    srand(time(NULL));

    // Step 1: Allocate memory
    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = malloc(ALLOC_SIZE);
        if (!ptrs[i]) break;
    }

    // Step 2: Free random blocks
    for (int i = 0; i < NUM_ALLOCS / 2; i++) {
        int index = rand() % NUM_ALLOCS;
        free(ptrs[index]);
        ptrs[index] = NULL;
    }

    // Step 3: Allocate again and measure success rate
    int failed_allocs = 0;
    for (int i = 0; i < NUM_ALLOCS / 2; i++) {
        void *temp = malloc(ALLOC_SIZE);
        if (!temp) failed_allocs++;
    }

    printf("Failed allocations due to fragmentation: %d\n", failed_allocs);
    return 0;
}
