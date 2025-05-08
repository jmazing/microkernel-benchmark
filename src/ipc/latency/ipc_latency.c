#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sched.h>
#include <sys/resource.h>

#define NSEC_PER_SEC 1000000000L
#define ITERATIONS   10000
#define QUEUE_NAME   "/ipc_test_queue"
#define MSG_SIZE     64
#define NUM_LOAD_THREADS 3

// Helper: add nanoseconds to a timespec (not used here, but available)
struct timespec timespec_add(struct timespec t, long ns) {
    t.tv_nsec += ns;
    if (t.tv_nsec >= NSEC_PER_SEC) {
        t.tv_sec += t.tv_nsec / NSEC_PER_SEC;
        t.tv_nsec %= NSEC_PER_SEC;
    }
    return t;
}

// Helper: compute the difference in nanoseconds between two timespecs
long timespec_diff_ns(struct timespec end, struct timespec start) {
    return (end.tv_sec - start.tv_sec) * NSEC_PER_SEC + (end.tv_nsec - start.tv_nsec);
}

// Background load: busy loop to stress the CPU
void *load_thread_func(void *arg) {
    volatile unsigned long counter = 0;
    while (1) {
        counter++;  // busy work
    }
    return NULL;
}

int main() {
    mqd_t mq;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    // Ensure any previous instance is removed.
    mq_unlink(QUEUE_NAME);
    mq = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // Create background load threads to stress the system.
    pthread_t load_threads[NUM_LOAD_THREADS];
    for (int i = 0; i < NUM_LOAD_THREADS; i++) {
        if (pthread_create(&load_threads[i], NULL, load_thread_func, NULL) != 0) {
            perror("pthread_create for load thread");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        mq_close(mq);
        mq_unlink(QUEUE_NAME);
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process acts as an echo server.
        char buffer[MSG_SIZE];
        for (int i = 0; i < ITERATIONS; i++) {
            if (mq_receive(mq, buffer, MSG_SIZE, NULL) == -1) {
                perror("mq_receive in child");
                exit(EXIT_FAILURE);
            }
            // Immediately echo the message back.
            if (mq_send(mq, buffer, strlen(buffer) + 1, 0) == -1) {
                perror("mq_send in child");
                exit(EXIT_FAILURE);
            }
        }
        mq_close(mq);
        exit(EXIT_SUCCESS);
    } else {
        // Parent process: sends messages and measures round-trip latency.
        struct timespec start, end;
        long max_latency = 0;
        char buffer[MSG_SIZE];
        snprintf(buffer, MSG_SIZE, "ping");

        // Set real-time scheduling for the parent process.
        struct sched_param param;
        param.sched_priority = 80; // Adjust priority as needed.
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            perror("sched_setscheduler in parent");
        }

        for (int i = 0; i < ITERATIONS; i++) {
            clock_gettime(CLOCK_MONOTONIC, &start);
            if (mq_send(mq, buffer, strlen(buffer) + 1, 0) == -1) {
                perror("mq_send in parent");
                break;
            }
            if (mq_receive(mq, buffer, MSG_SIZE, NULL) == -1) {
                perror("mq_receive in parent");
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            long latency = timespec_diff_ns(end, start);
            if (latency > max_latency)
                max_latency = latency;
        }

        printf("Max IPC round-trip latency observed: %ld ns\n", max_latency);

        // Cleanup: kill child process and load threads.
        kill(pid, SIGKILL);
        for (int i = 0; i < NUM_LOAD_THREADS; i++) {
            pthread_cancel(load_threads[i]);
            pthread_join(load_threads[i], NULL);
        }
        mq_close(mq);
        mq_unlink(QUEUE_NAME);
    }
    return 0;
}
