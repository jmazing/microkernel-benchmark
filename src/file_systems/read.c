#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define FILE_NAME "testfile.bin"
#define FILE_SIZE_MB 1024 // 100MB file
#define BUFFER_SIZE 4096 // 4KB buffer

void write_test() {
    FILE *file = fopen(FILE_NAME, "wb");
    if (!file) {
        perror("File open failed");
        return;
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE);
    size_t total_bytes = FILE_SIZE_MB * 1024 * 1024;
    clock_t start = clock();

    for (size_t i = 0; i < total_bytes / BUFFER_SIZE; i++) {
        fwrite(buffer, 1, BUFFER_SIZE, file);
    }

    clock_t end = clock();
    fclose(file);
    printf("Write Time: %lf seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
}

void read_test() {
    FILE *file = fopen(FILE_NAME, "rb");
    if (!file) {
        perror("File open failed");
        return;
    }

    char buffer[BUFFER_SIZE];
    clock_t start = clock();

    while (fread(buffer, 1, BUFFER_SIZE, file) > 0);

    clock_t end = clock();
    fclose(file);
    printf("Read Time: %lf seconds\n", (double)(end - start) / CLOCKS_PER_SEC);
}

int main() {
    write_test();
    read_test();
    remove(FILE_NAME);
    return 0;
}
