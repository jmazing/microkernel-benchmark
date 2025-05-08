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
#include <sys/neutrino.h>    // for QNX ThreadCtl
#endif
#include <mosquitto.h>

#define BROKER_CMD   "mosquitto"
#define CONF_FILE    "mosquitto.conf"
#define BROKER_HOST  "127.0.0.1"
#define TOPIC        "test/topic"
#define COUNT        1000            // messages per QoS level

static pid_t broker_pid = 0;
static long long send_times[COUNT];
static long long recv_times[COUNT];
static int recv_count = 0;
static pthread_mutex_t recv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t recv_cond   = PTHREAD_COND_INITIALIZER;

// Monotonic timestamp in nanoseconds
static long long now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Mosquitto message callback
static void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
    if (recv_count < COUNT) {
        recv_times[recv_count++] = now_ns();
        if (recv_count == COUNT) {
            pthread_mutex_lock(&recv_mutex);
            pthread_cond_signal(&recv_cond);
            pthread_mutex_unlock(&recv_mutex);
        }
    }
}

// Start the Mosquitto broker
static void start_broker() {
    broker_pid = fork();
    if (broker_pid == 0) {
        execlp(BROKER_CMD, BROKER_CMD, "-c", CONF_FILE, NULL);
        perror("execlp mosquitto failed");
        exit(EXIT_FAILURE);
    }
    sleep(1);
}

// Stop the Mosquitto broker
static void stop_broker() {
    if (broker_pid > 0) {
        kill(broker_pid, SIGTERM);
        waitpid(broker_pid, NULL, 0);
    }
}

// Run a single burst test at the given QoS level
static void run_qos_test(int qos) {
    char label[64];
    snprintf(label, sizeof(label), "Burst pub/sub QoS %d", qos);
    printf("\n=== %s ===\n", label);

    recv_count = 0;
    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        fprintf(stderr, "Error: mosquitto_new() failed\n");
        exit(EXIT_FAILURE);
    }
    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_connect(mosq, BROKER_HOST, 1883, 60);
    mosquitto_subscribe(mosq, NULL, TOPIC, qos);

    // Optional real-time boost if compiled with -DUSE_RT
#ifdef USE_RT
    // Boost subscriber to real-time priority
    #ifdef __linux__
    struct sched_param sp_linux = { .sched_priority = 80 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp_linux) != 0) {
        perror("pthread_setschedparam");
    }
    #elif defined(__QNX__)
    ThreadCtl(_NTO_TCTL_IO, NULL);
    struct sched_param sp_qnx = { .sched_priority = 80 };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp_qnx) != 0) {
        perror("pthread_setschedparam");
    }
    #endif
#endif

    mosquitto_loop_start(mosq);
    sleep(1);

    // publish COUNT messages in a tight burst
    for (int i = 0; i < COUNT; i++) {
        send_times[i] = now_ns();
        mosquitto_publish(mosq, NULL, TOPIC,
                          (int)strlen("msg"), "msg",
                          qos, false);
    }

    // wait for all messages to arrive
    pthread_mutex_lock(&recv_mutex);
    while (recv_count < COUNT) {
        pthread_cond_wait(&recv_cond, &recv_mutex);
    }
    pthread_mutex_unlock(&recv_mutex);

    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    // compute latency stats
    long long total = 0, min = LLONG_MAX, max = 0;
    for (int i = 0; i < COUNT; i++) {
        long long d = recv_times[i] - send_times[i];
        total += d;
        if (d < min) min = d;
        if (d > max) max = d;
    }
    double avg_ms = (double)total / COUNT / 1e6;
    double min_ms = (double)min / 1e6;
    double max_ms = (double)max / 1e6;

    printf("Messages: %d\n", COUNT);
    printf("Avg Latency: %.2f ms\n", avg_ms);
    printf("Min Latency: %.2f ms\n", min_ms);
    printf("Max Latency: %.2f ms\n", max_ms);
}

int main(void) {
    // prevent paging jitter
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
    }

    printf("Starting Mosquitto broker...\n");
    start_broker();

    // sweep QoS levels 0,1,2
    for (int qos = 0; qos <= 2; qos++) {
        run_qos_test(qos);
    }

    printf("Stopping broker...\n");
    stop_broker();
    printf("Done.\n");
    return 0;
}
