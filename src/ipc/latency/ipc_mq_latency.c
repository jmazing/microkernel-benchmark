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


#define MSG_SIZE 64
#define ITERATIONS 10000
#define NSEC_PER_SEC 1000000000L

// Helper: compute difference in nanoseconds between two timespecs.
long timespec_diff_ns(struct timespec end, struct timespec start) {
    return (end.tv_sec - start.tv_sec) * NSEC_PER_SEC + (end.tv_nsec - start.tv_nsec);
}

int main() {

    mqd_t mq_ptoc, mq_ctop;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    // Unlink the queues in case they already exist.
    mq_unlink("/mq_ptoc");
    mq_unlink("/mq_ctop");

    mq_ptoc = mq_open("/mq_ptoc", O_CREAT | O_RDWR, 0644, &attr);
    if (mq_ptoc == (mqd_t)-1) {
        perror("mq_open /mq_ptoc");
        exit(EXIT_FAILURE);
    }
    mq_ctop = mq_open("/mq_ctop", O_CREAT | O_RDWR, 0644, &attr);
    if (mq_ctop == (mqd_t)-1) {
        perror("mq_open /mq_ctop");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // Child process acts as an echo server.
        // Optionally, set a real-time scheduling policy (with lower priority than the parent)
        struct sched_param child_param;
        child_param.sched_priority = 70;
        if (sched_setscheduler(0, SCHED_FIFO, &child_param) != 0) {
            perror("sched_setscheduler in child");
        }
        
        char buf[MSG_SIZE];
        for (int i = 0; i < ITERATIONS; i++) {
            ssize_t n = mq_receive(mq_ptoc, buf, MSG_SIZE, NULL);
            if (n == -1) {
                perror("Child mq_receive");
                exit(EXIT_FAILURE);
            }
            if (mq_send(mq_ctop, buf, MSG_SIZE, 0) == -1) {
                perror("Child mq_send");
                exit(EXIT_FAILURE);
            }
        }
        mq_close(mq_ptoc);
        mq_close(mq_ctop);
        exit(EXIT_SUCCESS);
    } else {
        // Parent process: send messages and measure round-trip latency.
        char buf[MSG_SIZE];
        memset(buf, 'M', MSG_SIZE - 1);
        buf[MSG_SIZE - 1] = '\0';

        struct timespec start, end;
        long max_latency = 0;

        // Set high real-time priority for the parent.
        struct sched_param param;
        param.sched_priority = 80;
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            perror("sched_setscheduler in parent");
        }

        for (int i = 0; i < ITERATIONS; i++) {
            if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
                perror("Parent clock_gettime start");
            }
            if (mq_send(mq_ptoc, buf, MSG_SIZE, 0) == -1) {
                perror("Parent mq_send");
                break;
            }
            ssize_t n = mq_receive(mq_ctop, buf, MSG_SIZE, NULL);
            if (n == -1) {
                perror("Parent mq_receive");
                break;
            }
            if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
                perror("Parent clock_gettime end");
            }
            long latency = timespec_diff_ns(end, start);
            if (latency > max_latency)
                max_latency = latency;
        }
        printf("Max IPC round-trip latency using POSIX message queues: %ld ns\n", max_latency);

        mq_close(mq_ptoc);
        mq_close(mq_ctop);
        mq_unlink("/mq_ptoc");
        mq_unlink("/mq_ctop");

        kill(pid, SIGKILL);
        wait(NULL);
    }
    return 0;
}
