# Kernel Design and Patch Breakdown

This document details the internal mechanics of `0000-kernel-agent-latency-control-plane-rfc.patch`.

## Foundation
This patch does not invent a new frequency governor or scheduler class. It cleanly extends existing subsystems:
* **CPUFreq & schedutil**: Hooks into the utilization calculation rather than writing raw hardware registers.
* **uclamp**: Complements uclamp's static hints with a dynamic, moving window.
* **Scheduler**: Integrates directly into `try_to_wake_up()` and `select_task_rq_fair()`.

## Data Structures

### `SCHED_FLAG_AGENT_LATENCY`
A new userspace-facing flag added to `include/uapi/linux/sched.h`. Tasks opt-in by setting this flag via `sched_setattr(2)`.

### `task_struct` Additions
```c
u64 agent_window_ns;
unsigned int agent_min_util;
u32 agent_id;
u64 agent_step_id;
```
Tasks carry their requested window duration and utilization floor. The ID fields exist purely for eBPF observability and correlation.

### `rq` (Runqueue) Additions
```c
u64 agent_active_until_ns;
unsigned int agent_min_util;
```
The runqueue holds the actual active window state. This ensures that `schedutil` (which operates on a per-CPU basis) can observe the agent requirement instantly, without walking task lists.

## Hook Implementation

### 1. The IRQ Refresh Concept
In `core.c:try_to_wake_up()`, when an agent task is woken, we execute:
```c
task_agent_refresh_window(rq, p);
```
This updates `rq->agent_active_until_ns = ktime_get_ns() + p->agent_window_ns`. 
By doing this in the wake path (which is often within the hard/soft IRQ context of a completion), we arm the CPU *before* the context switch even happens.

### 2. The schedutil Hook
In `cpufreq_schedutil.c:sugov_update_single_freq()`, we filter the raw PELT utilization:
```c
util = agent_latency_boost_util(sg_cpu, util, max);
```
If the runqueue's agent window is active, `util` is boosted to `agent_min_util`. The governor then selects a high frequency immediately.

### 3. The cpuidle Guard
In `drivers/cpuidle/cpuidle.c:cpuidle_enter_state()`, we evaluate the selected idle state:
```c
if (rq_agent_window_active(rq, now)) {
    while (index > 0 && drv->states[index].exit_latency > AGENT_LATENCY_IDLE_EXIT_LIMIT_US)
        index--;
}
```
If the governor chose C6 (exit latency: 800us) but the limit is 50us, we walk backwards until we find C1 or C0, ensuring lightning-fast wakeups.
