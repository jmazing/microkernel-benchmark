#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sched.h>
#include <limits.h>
#include <sys/mman.h>
#ifdef __QNX__
#include <sys/neutrino.h>
#endif
#include <mosquitto.h>

#define BROKER_CMD   "mosquitto"
#define CONF_FILE    "mosquitto.conf"
#define BROKER_HOST  "127.0.0.1"
#define TOPIC        "test/topic"
#define COUNT        100
#define LOAD_PROCS   2

static pid_t broker_pid = 0;
static pid_t load_pids[LOAD_PROCS];
static long long send_times[COUNT];
static long long recv_times[COUNT];
static int recv_count = 0;
static pthread_mutex_t recv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t recv_cond   = PTHREAD_COND_INITIALIZER;

int sched_policy = SCHED_OTHER;

long long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (recv_count < COUNT) {
        recv_times[recv_count++] = now_ns();
        if (recv_count == COUNT) {
            pthread_mutex_lock(&recv_mutex);
            pthread_cond_signal(&recv_cond);
            pthread_mutex_unlock(&recv_mutex);
        }
    }
}

int parse_sched_policy(const char *arg) {
    if (strcmp(arg, "fifo") == 0) return SCHED_FIFO;
    if (strcmp(arg, "rr") == 0) return SCHED_RR;
#ifdef __QNX__
    if (strcmp(arg, "sporadic") == 0) return SCHED_SPORADIC;
#else
    if (strcmp(arg, "sporadic") == 0) {
        fprintf(stderr, "SCHED_SPORADIC not supported on Linux.\n");
        exit(EXIT_FAILURE);
    }
#endif
    fprintf(stderr, "Unknown scheduling policy: %s\n", arg);
    exit(EXIT_FAILURE);
}

void start_broker() {
    broker_pid = fork();
    if (broker_pid == 0) {
        execlp(BROKER_CMD, BROKER_CMD, "-c", CONF_FILE, NULL);
        perror("execlp mosquitto failed");
        exit(1);
    }
    sleep(1);
}

void stop_broker() {
    if (broker_pid > 0) {
        kill(broker_pid, SIGTERM);
        waitpid(broker_pid, NULL, 0);
    }
}

void run_test(const char *label) {
    printf("\n=== %s ===\n", label);
    recv_count = 0;

    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) { perror("mosquitto_new"); exit(1); }

    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_connect(mosq, BROKER_HOST, 1883, 60);

    // Set subscriber thread scheduling policy
    int prio = sched_get_priority_max(sched_policy);
#ifdef __QNX__
    ThreadCtl(_NTO_TCTL_IO, NULL);
    if (sched_policy == SCHED_SPORADIC) {
        struct sched_param_sporadic sps = {
            .sched_priority = prio,
            .sched_ss_low_priority = 10,
            .sched_ss_max_repl = 5,
            .sched_ss_repl_period = { 0, 50000000 },
            .sched_ss_init_budget = { 0, 5000000 }
        };
        pthread_setschedparam(pthread_self(), sched_policy, (struct sched_param *)&sps);
    } else
#endif
    {
        struct sched_param sp = { .sched_priority = prio };
        if (sched_policy != SCHED_OTHER)
            pthread_setschedparam(pthread_self(), sched_policy, &sp);
    }

    mosquitto_subscribe(mosq, NULL, TOPIC, 0);
    mosquitto_loop_start(mosq);
    sleep(1);

    for (int i = 0; i < COUNT; i++) {
        send_times[i] = now_ns();
        mosquitto_publish(mosq, NULL, TOPIC, strlen("msg"), "msg", 0, false);
        usleep(230000);
    }

    pthread_mutex_lock(&recv_mutex);
    while (recv_count < COUNT)
        pthread_cond_wait(&recv_cond, &recv_mutex);
    pthread_mutex_unlock(&recv_mutex);

    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    long long total = 0, min = LLONG_MAX, max = 0;
    for (int i = 0; i < COUNT; i++) {
        long long d = recv_times[i] - send_times[i];
        total += d; min = d < min ? d : min; max = d > max ? d : max;
    }
    printf("Messages: %d\n", COUNT);
    printf("Avg Latency: %.2f ms\n", (double)total / COUNT / 1e6);
    printf("Min Latency: %.2f ms\n", (double)min / 1e6);
    printf("Max Latency: %.2f ms\n", (double)max / 1e6);
}

void start_load() {
    for (int i = 0; i < LOAD_PROCS; i++) {
        pid_t p = fork();
        if (p == 0) {
            int prio = 1;
#ifdef __QNX__
            ThreadCtl(_NTO_TCTL_IO, NULL);
            if (sched_policy == SCHED_SPORADIC) {
                struct sched_param_sporadic sps = {
                    .sched_priority = prio,
                    .sched_ss_low_priority = 1,
                    .sched_ss_max_repl = 1,
                    .sched_ss_repl_period = { 0, 50000000 },
                    .sched_ss_init_budget = { 0, 1000000 }
                };
                sched_setscheduler(0, sched_policy, (struct sched_param *)&sps);
            } else
#endif
            {
                struct sched_param sp = { .sched_priority = prio };
                sched_setscheduler(0, sched_policy, &sp);
            }
            while (1) {}
        } else {
            load_pids[i] = p;
        }
    }
    sleep(1);
}

void stop_load() {
    for (int i = 0; i < LOAD_PROCS; i++) {
        kill(load_pids[i], SIGTERM);
        waitpid(load_pids[i], NULL, 0);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        sched_policy = parse_sched_policy(argv[1]);
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        perror("mlockall failed");

    printf("Starting broker...\n");
    start_broker();

    run_test("Baseline burst");

    printf("\nSpinning up %d busy-loop workers...\n", LOAD_PROCS);
    start_load();

    run_test("Under CPU load");

    printf("\nStopping CPU workers...\n");
    stop_load();

    printf("Stopping broker...\n");
    stop_broker();

    printf("Done.\n");
    return 0;
}
