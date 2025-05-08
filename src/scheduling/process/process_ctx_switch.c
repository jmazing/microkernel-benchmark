// file: ctxswitch.c
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

static inline long diff_nsec(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000000000L
         + (end->tv_nsec - start->tv_nsec);
}

int main(int argc, char **argv) {
    int iterations = 100000;
    if (argc > 1) iterations = atoi(argv[1]);

    int p2c[2], c2p[2];
    if (pipe(p2c) == -1 || pipe(c2p) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        // Child: read from p2c, write to c2p
        close(p2c[1]);
        close(c2p[0]);
        unsigned char buf;
        for (int i = 0; i < iterations; i++) {
            if (read(p2c[0], &buf, 1) != 1) {
                perror("child read");
                exit(1);
            }
            if (write(c2p[1], &buf, 1) != 1) {
                perror("child write");
                exit(1);
            }
        }
        _exit(0);
    }

    // Parent: write to p2c, read from c2p
    close(p2c[0]);
    close(c2p[1]);

    struct timespec t_start, t_end;
    unsigned char buf = 0;

    // synchronize start
    if (clock_gettime(CLOCK_MONOTONIC, &t_start) == -1) {
        perror("clock_gettime");
        return 1;
    }

    for (int i = 0; i < iterations; i++) {
        if (write(p2c[1], &buf, 1) != 1) {
            perror("parent write");
            return 1;
        }
        if (read(c2p[0], &buf, 1) != 1) {
            perror("parent read");
            return 1;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t_end) == -1) {
        perror("clock_gettime");
        return 1;
    }

    long total_ns = diff_nsec(&t_start, &t_end);
    double avg_roundtrip_ns = (double)total_ns / iterations;
    printf("Iterations: %d\n", iterations);
    printf("Total time: %.3f ms\n", total_ns / 1e6);
    printf("Avg round-trip latency: %.3f µs\n", avg_roundtrip_ns / 1e3);
    printf("Est. one-way context switch:  %.3f µs\n",
           (avg_roundtrip_ns / 2) / 1e3);

    // wait for child to finish
    wait(NULL);
    return 0;
}
