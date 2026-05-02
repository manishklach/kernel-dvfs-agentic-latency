# Architecture Overview

This project studies a narrow but increasingly common control path: an agent waits for an external completion, wakes briefly, performs a small amount of CPU work, then waits again. The working hypothesis is that modern ARM/Linux power-management policies can add repeated latency to that pattern because the machine is optimized for average utilization, not for repeated sub-millisecond decision steps.

## Core Model

Agent loop:

```text
issue tool call / disk read / local RPC
wait in epoll_wait() or io_uring_enter()
completion arrives through IRQ / softirq / blk-mq / timer
agent wakes
parse result
decide next step
repeat
```

This repository is concerned with the latency of the wait-to-run transition, not with language-model token generation throughput.

## Why The Path Matters

If the CPU has downscaled frequency or entered a deep idle state during the wait, the next step can inherit extra delay before userspace meaningfully resumes. For a single step that delay may be small. Across many steps it can dominate wall-clock completion time.

### Latency Amplification

Per-step delay:

```text
IRQ/completion handling
+ softirq or blk-mq completion
+ wakeup-to-run latency
+ cpuidle exit latency
+ CPUFreq ramp latency
+ userspace gate return
```

Total delay:

```text
total delay = per-step delay * number_of_steps
```

For agentic loops, the multiplication factor can be large even when each individual penalty is modest.

## End-To-End Path

```text
userspace agent step
    |
    | issue async work
    v
epoll_wait() / io_uring_enter()
    |
    | sleep
    v
device completion / timer / network IRQ
    |
    v
IRQ handler -> softirq / blk-mq completion
    |
    v
task wakeup -> scheduler CPU selection
    |
    +--> cpuidle exit from selected CPU
    |
    +--> schedutil / cpufreq picks next performance point
    |
    v
userspace resumes -> parse -> decide -> next wait
```

## Research Hypothesis

The patch explores whether a short "agent active window" can reduce repeated warmup costs without pinning the system into permanently high performance. The idea is deliberately modest:

- Do not replace scheduler policy.
- Do not bypass CPUFreq or cpuidle.
- Do not assume all interactive tasks are agentic.
- Do keep performance warm briefly after a relevant wakeup.

## Diagram References

- [Agent DVFS window diagram](../diagrams/agent-dvfs-window.svg)
- [IRQ to scheduler to DVFS path diagram](../diagrams/irq-scheduler-dvfs-path.svg)

## Conceptual Timing View

```text
baseline:
  wait -> idle/downscale -> completion -> wakeup -> ramp/exit -> run

proposed:
  wait -> keep short active window -> completion -> wakeup -> run sooner
```

The goal is not to eliminate all latency. It is to reduce repeated warmup penalties when the workload shape strongly suggests another short agent step is imminent.
