// SPDX-License-Identifier: GPL-2.0
/*
 * Agent Loop Microbenchmark
 *
 * Simulates an agentic control loop: wait -> wake -> compute -> repeat.
 * Measures latency amplification at each step.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#ifndef SCHED_FLAG_AGENT_LATENCY
#define SCHED_FLAG_AGENT_LATENCY 0x10000000
#endif

static int efd;
static unsigned long interval_us = 500;
static unsigned long max_steps = 0;
static int use_agent_latency = 0;

static int mark_agent_control_thread(void)
{
    struct sched_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.sched_policy = SCHED_NORMAL;
    attr.sched_flags = SCHED_FLAG_AGENT_LATENCY;
    attr.sched_nice = -5;

    return syscall(SYS_sched_setattr, 0, &attr, 0);
}

static uint64_t nsec_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void busy_parse_work(void)
{
    volatile uint64_t x = 0;
    int i;
    for (i = 0; i < 20000; i++)
        x += i;
}

static void *producer(void *arg)
{
    uint64_t one = 1;

    for (;;) {
        usleep(interval_us);
        if (write(efd, &one, sizeof(one)) < 0 && errno != EAGAIN)
            perror("write");
    }
    return NULL;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --agent-latency    Enable experimental scheduler flag\n");
    fprintf(stderr, "  --interval-us <N>  Wait interval in microseconds (default: 500)\n");
    fprintf(stderr, "  --steps <N>        Stop after N steps (default: infinite)\n");
    exit(1);
}

static void parse_args(int argc, char **argv)
{
    static struct option long_options[] = {
        {"agent-latency", no_argument, 0, 'a'},
        {"interval-us", required_argument, 0, 'i'},
        {"steps", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "ai:s:", long_options, NULL)) != -1) {
        switch (c) {
        case 'a': use_agent_latency = 1; break;
        case 'i': interval_us = strtoul(optarg, NULL, 10); break;
        case 's': max_steps = strtoul(optarg, NULL, 10); break;
        default: print_usage(argv[0]);
        }
    }
}

int main(int argc, char **argv)
{
    struct epoll_event ev, out;
    pthread_t t;
    uint64_t step = 0;
    int ep;

    parse_args(argc, argv);

    if (use_agent_latency) {
        if (mark_agent_control_thread())
            perror("sched_setattr(SCHED_FLAG_AGENT_LATENCY)");
    }

    efd = eventfd(0, EFD_NONBLOCK);
    if (efd < 0) {
        perror("eventfd");
        return 1;
    }

    ep = epoll_create1(0);
    if (ep < 0) {
        perror("epoll_create1");
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = efd;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev)) {
        perror("epoll_ctl");
        return 1;
    }

    pthread_create(&t, NULL, producer, NULL);

    printf("step,epoll_wait_us,total_step_us\n");

    for (;;) {
        uint64_t val, t_start = nsec_now(), t_wake, t_end;

        if (epoll_wait(ep, &out, 1, -1) < 0) {
            perror("epoll_wait");
            break;
        }
        
        t_wake = nsec_now();
        if (read(efd, &val, sizeof(val)) < 0)
            perror("read");
            
        busy_parse_work();
        t_end = nsec_now();

        step++;
        
        printf("%llu,%llu,%llu\n",
               (unsigned long long)step,
               (unsigned long long)((t_wake - t_start) / 1000),
               (unsigned long long)((t_end - t_start) / 1000));

        if (max_steps > 0 && step >= max_steps)
            break;
    }

    return 0;
}
