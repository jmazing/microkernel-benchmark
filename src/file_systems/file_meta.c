#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define NUM_FILES 10000
#define DIR_NAME "meta_test_dir"

double get_time_in_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void cleanup() {
    char command[512];
    snprintf(command, sizeof(command), "rm -rf %s", DIR_NAME);
    system(command);
}

int main() {
    char filepath[256];
    FILE *fp;

    // Cleanup from previous runs
    cleanup();

    printf("Starting metadata performance test with %d files...\n", NUM_FILES);

    mkdir(DIR_NAME, 0755);

    double start_create = get_time_in_seconds();

    // Create NUM_FILES small empty files
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filepath, sizeof(filepath), "%s/file_%d.txt", DIR_NAME, i);
        fp = fopen(filepath, "w");
        if (fp == NULL) {
            perror("File creation failed");
            return 1;
        }
        fclose(fp);
    }

    double end_create = get_time_in_seconds();

    double start_rename = get_time_in_seconds();

    // Rename all files
    for (int i = 0; i < NUM_FILES; i++) {
        char newpath[256];
        snprintf(filepath, sizeof(filepath), "%s/file_%d.txt", DIR_NAME, i);
        snprintf(newpath, sizeof(newpath), "%s/renamed_%d.txt", DIR_NAME, i);
        rename(filepath, newpath);
    }

    double end_rename = get_time_in_seconds();

    double start_delete = get_time_in_seconds();

    // Delete all files
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filepath, sizeof(filepath), "%s/renamed_%d.txt", DIR_NAME, i);
        unlink(filepath);
    }

    double end_delete = get_time_in_seconds();

    rmdir(DIR_NAME);

    printf("Create time: %.4f seconds\n", end_create - start_create);
    printf("Rename time: %.4f seconds\n", end_rename - start_rename);
    printf("Delete time: %.4f seconds\n", end_delete - start_delete);

    return 0;
}