#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_ALLOCS 10000
#define ALLOC_SIZE 2048 // 1KB

int main() {
    void *ptrs[NUM_ALLOCS];
    clock_t start, end;
    
    // Measure allocation time
    start = clock();
    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = malloc(ALLOC_SIZE);
        if (!ptrs[i]) {
            printf("Memory allocation failed at %d\n", i);
            break;
        }
    }
    end = clock();
    printf("Allocation Time: %lf seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    // Measure deallocation time
    start = clock();
    for (int i = 0; i < NUM_ALLOCS; i++) {
        free(ptrs[i]);
    }
    end = clock();
    printf("Deallocation Time: %lf seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    return 0;
}
