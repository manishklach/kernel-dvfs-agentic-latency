// SPDX-License-Identifier: GPL-2.0-only
/*
 * Standalone agent-loop latency microbenchmark.
 *
 * One producer periodically signals an eventfd. The main thread blocks in
 * epoll_wait(), wakes, performs a small parse loop, and records timing. This
 * models "wait for completion -> parse -> decide -> wait again".
 */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SCHED_FLAG_AGENT_LATENCY
#define SCHED_FLAG_AGENT_LATENCY 0x10000000
#endif

#ifndef SYS_sched_setattr
#if defined(__aarch64__)
#define SYS_sched_setattr 274
#elif defined(__x86_64__)
#define SYS_sched_setattr 314
#endif
#endif

static int g_event_fd = -1;
static unsigned int g_interval_us = 500;
static uint64_t g_steps = 10000;
static volatile bool g_stop = false;

static uint64_t nsec_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void busy_parse_work(void)
{
    volatile uint64_t acc = 0;
    unsigned int i;

    for (i = 0; i < 20000; i++)
        acc += i ^ (acc >> 3);

    (void)acc;
}

static int mark_agent_control_thread(void)
{
#ifdef SYS_sched_setattr
    struct sched_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.sched_policy = SCHED_NORMAL;
    attr.sched_flags = SCHED_FLAG_AGENT_LATENCY;
    attr.sched_nice = 0;

    return (int)syscall(SYS_sched_setattr, 0, &attr, 0);
#else
    errno = ENOSYS;
    return -1;
#endif
}

static void *producer_thread(void *arg)
{
    uint64_t one = 1;

    (void)arg;

    while (!g_stop) {
        if (usleep(g_interval_us) != 0 && errno != EINTR) {
            perror("usleep");
            break;
        }

        if (write(g_event_fd, &one, sizeof(one)) < 0 && errno != EAGAIN) {
            perror("write(eventfd)");
            break;
        }
    }

    return NULL;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--agent-latency] [--interval-us N] [--steps N]\n",
            prog);
}

static int parse_args(int argc, char **argv, bool *use_agent_latency)
{
    static const struct option long_opts[] = {
        {"agent-latency", no_argument, NULL, 'a'},
        {"interval-us", required_argument, NULL, 'i'},
        {"steps", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int opt;

    while ((opt = getopt_long(argc, argv, "ai:s:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'a':
            *use_agent_latency = true;
            break;
        case 'i':
            g_interval_us = (unsigned int)strtoul(optarg, NULL, 10);
            if (g_interval_us == 0) {
                fprintf(stderr, "--interval-us must be > 0\n");
                return -1;
            }
            break;
        case 's':
            g_steps = strtoull(optarg, NULL, 10);
            if (g_steps == 0) {
                fprintf(stderr, "--steps must be > 0\n");
                return -1;
            }
            break;
        case 'h':
            usage(argv[0]);
            return 1;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct epoll_event event;
    struct epoll_event out;
    pthread_t thread;
    bool use_agent_latency = false;
    uint64_t step;
    int epoll_fd;
    int rc;

    rc = parse_args(argc, argv, &use_agent_latency);
    if (rc != 0)
        return rc < 0 ? 1 : 0;

    if (use_agent_latency && mark_agent_control_thread() != 0)
        perror("sched_setattr(SCHED_FLAG_AGENT_LATENCY)");

    g_event_fd = eventfd(0, EFD_NONBLOCK);
    if (g_event_fd < 0) {
        perror("eventfd");
        return 1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(g_event_fd);
        return 1;
    }

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = g_event_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_event_fd, &event) != 0) {
        perror("epoll_ctl");
        close(epoll_fd);
        close(g_event_fd);
        return 1;
    }

    if (pthread_create(&thread, NULL, producer_thread, NULL) != 0) {
        perror("pthread_create");
        close(epoll_fd);
        close(g_event_fd);
        return 1;
    }

    printf("step,epoll_wait_us,total_step_us\n");

    for (step = 1; step <= g_steps; step++) {
        uint64_t wait_start_ns;
        uint64_t wake_ns;
        uint64_t done_ns;
        uint64_t val;

        wait_start_ns = nsec_now();
        rc = epoll_wait(epoll_fd, &out, 1, -1);
        wake_ns = nsec_now();
        if (rc < 0) {
            if (errno == EINTR) {
                step--;
                continue;
            }
            perror("epoll_wait");
            g_stop = true;
            break;
        }

        if (read(g_event_fd, &val, sizeof(val)) < 0) {
            perror("read(eventfd)");
            g_stop = true;
            break;
        }

        busy_parse_work();
        done_ns = nsec_now();

        printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
               step,
               (wake_ns - wait_start_ns) / 1000ULL,
               (done_ns - wait_start_ns) / 1000ULL);
    }

    g_stop = true;
    pthread_join(thread, NULL);
    close(epoll_fd);
    close(g_event_fd);
    return 0;
}
