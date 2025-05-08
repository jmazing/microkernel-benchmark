// file: thread_ctx_switch.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define ITERATIONS_DEFAULT 100000

static pthread_mutex_t lock     = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond_ping = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  cond_pong = PTHREAD_COND_INITIALIZER;
static int             turn      = 0;          // 0: ping’s turn, 1: pong’s turn
static int             iterations = ITERATIONS_DEFAULT;

void* pong_thread(void *arg) {
    for (int i = 0; i < iterations; i++) {
        pthread_mutex_lock(&lock);
        while (turn != 1) {
            pthread_cond_wait(&cond_pong, &lock);
        }
        // Respond: flip turn back to ping and wake it
        turn = 0;
        pthread_cond_signal(&cond_ping);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        iterations = atoi(argv[1]);
        if (iterations <= 0) iterations = ITERATIONS_DEFAULT;
    }

    pthread_t thr;
    struct timespec start, end;

    // Create the pong thread
    if (pthread_create(&thr, NULL, pong_thread, NULL) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    // Give the pong thread a moment to start and block on cond_pong
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
    nanosleep(&ts, NULL);

    // Measure ping-pong
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        pthread_mutex_lock(&lock);
        // Initiate ping → set turn to 1 and signal pong
        turn = 1;
        pthread_cond_signal(&cond_pong);
        // Wait for pong to flip turn back to 0
        while (turn != 0) {
            pthread_cond_wait(&cond_ping, &lock);
        }
        pthread_mutex_unlock(&lock);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Join and cleanup
    pthread_join(thr, NULL);

    long total_ns = (end.tv_sec - start.tv_sec) * 1000000000L
                  + (end.tv_nsec - start.tv_nsec);
    double avg_rt_us  = (double)total_ns / iterations / 1000.0;
    double avg_1w_us  = avg_rt_us / 2.0;

    printf("Iterations:        %d\n", iterations);
    printf("Total time:        %.3f ms\n", total_ns / 1e6);
    printf("Avg round-trip:    %.3f µs\n", avg_rt_us);
    printf("Est. one-way cost: %.3f µs\n", avg_1w_us);

    return 0;
}
