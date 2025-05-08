#include <stdio.h>
#include <stdlib.h>

#define NUM_LEAKS 1000
#define BLOCK_SIZE 1024  // 1 KB per leak

int main() {
    printf("Starting memory leak test...\n");

    // Leak memory by not freeing it
    for (int i = 0; i < NUM_LEAKS; i++) {
        void *leak = malloc(BLOCK_SIZE);
        if (leak == NULL) {
            fprintf(stderr, "Allocation failed at %d\n", i);
            return 1;
        }

        // Simulate usage
        ((char *)leak)[0] = 'a';

        // No free() = memory leak!
    }

    printf("Finished leaking memory. Exiting now.\n");
    return 0;
}