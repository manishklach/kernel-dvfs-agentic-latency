# Kernel Design and Patch Breakdown

This document provides a precise technical breakdown of `0000-kernel-agent-latency-control-plane-rfc.patch`.

## The Control Plane Approach
The core goal is to inject an "active window" signal into the kernel power management subsystems. The design cleanly extends the scheduler, `schedutil`, and `cpuidle` without bypassing them.

### `SCHED_FLAG_AGENT_LATENCY`
A new userspace scheduling flag added to `include/uapi/linux/sched.h`. Userspace agents opt-in via `sched_setattr(2)`. This flag asserts that the task is operating in an iterative control loop that requires latency protection.

### `task_struct` Additions
We add minimal tracking metadata to the task structure:
* `agent_window_ns`: The duration (in nanoseconds) that the control-loop window remains "open" following a wakeup.
* `agent_min_util`: The minimum effective utilization floor requested during the active window.

### `rq` (Runqueue) Additions
The runqueue is the synchronization point between the scheduler and the per-CPU power management subsystems:
* `agent_active_until_ns`: A moving absolute timestamp marking when the latency window expires.
* `agent_min_util`: The currently asserted utilization floor for the CPU.

Storing this state on the `rq` ensures `schedutil` and `cpuidle` can evaluate the agent latency floor in $O(1)$ time without walking the task list.

### IRQ / Wakeup Interaction
In `core.c:try_to_wake_up()`, when an agent task is woken, we execute:
```c
task_agent_refresh_window(rq, p);
```
This updates `rq->agent_active_until_ns`. By arming the window *during* the wake path (often within hard/soft IRQ context), we pre-warm the CPU state before the context switch completes.

### Schedutil DVFS Hook
In `cpufreq_schedutil.c:sugov_update_single_freq()`, we intercept the raw PELT utilization:
```c
if (rq_agent_window_active(rq, now)) {
    util = max(util, rq->agent_min_util);
}
```
If the agent window is open, the utilization is boosted. The `schedutil` governor translates this directly into an immediate frequency ramp, bypassing the PELT decay filter.

### cpuidle Guard
In `drivers/cpuidle/cpuidle.c:cpuidle_enter_state()`, we evaluate the C-state chosen by the idle governor:
```c
if (rq_agent_window_active(rq, now)) {
    while (index > 0 && drv->states[index].exit_latency > AGENT_LATENCY_IDLE_EXIT_LIMIT_US)
        index--;
}
```
If the selected C-state exceeds our strict microsecond budget, we fall back to a shallower C-state (e.g., C1 or C0). This eliminates idle exit latency for the *next* step of the control loop.
