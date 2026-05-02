# Agent Latency Architecture

## The Agent Loop
Agentic AI workloads differ fundamentally from traditional server applications. Instead of processing massive batches or handling thousands of concurrent requests, an agentic loop executes tightly coupled sequential steps:

1. **Wait**: The agent submits an async request (LLM inference, database query, tool execution) and sleeps (e.g., `epoll_wait`, `io_uring_enter`).
2. **Wake**: An IRQ signals completion, waking the agent thread.
3. **Compute**: The agent parses the result, updates its state, and determines the next action.
4. **Repeat**: The loop starts over.

## Kernel Interaction
When the agent enters the **Wait** phase, the Linux kernel aggressively attempts to save power. Over a gap of even a few milliseconds, the following occurs:
* **cpuidle**: The CPU transitions from C0 into deeper sleep states (e.g., C3/C6).
* **DVFS (CPUFreq / schedutil)**: The scheduler utilization signal decays, causing the frequency governor to drop the CPU frequency to its minimum.
* **Scheduler**: The agent task is removed from the runqueue.

When the **Wake** phase begins, the kernel must undo all of this:
* Pay the exit latency to bring the CPU back to C0.
* Wake the task and place it on a runqueue (often migrating it to a cold CPU to balance load).
* Slowly ramp the CPU frequency back up as utilization builds.

This leads to latency amplification across thousands of steps.

## Why Existing Mechanisms Fail

### 1. schedutil (Reactive)
`schedutil` relies on the PELT (Per-Entity Load Tracking) signal. PELT is explicitly designed as a low-pass filter to smooth out transients. By definition, a short, bursty agent loop will not generate a high sustained utilization signal. `schedutil` is always reacting *after* the compute phase is over.

### 2. uclamp (Static)
`uclamp` allows userspace to set a static minimum performance floor (`util_min`). However, clamping utilization high prevents the CPU from *ever* saving power, even if the agent is blocked for seconds waiting on an external API. It is energy-blind.

### 3. cpuidle (Energy Optimized)
The cpuidle governor (like `menu` or `teo`) predicts idle duration based on past events. It routinely selects deep C-states for agent waits, prioritizing power over the microsecond-level wake latency required by the loop.

## The Patch: A Control Plane for Agents
This research patch introduces an "agent active window". When a thread marked with `SCHED_FLAG_AGENT_LATENCY` is woken by an IRQ:

1. **Wakeup Path**: The scheduler immediately opens a short, timestamped window on the runqueue (`rq->agent_active_until_ns`).
2. **Scheduler**: The load balancer prefers keeping the task on its previous CPU to preserve L1/L2 cache locality, avoiding cold-start migrations.
3. **DVFS (schedutil)**: If the agent window is active, `schedutil` applies a temporary `util_min` floor, bypassing PELT decay. The frequency ramps instantly.
4. **cpuidle**: If the agent window is active, the idle governor drops any C-state whose `exit_latency` exceeds the agent's tight budget, keeping the core warm.

Once the window expires, standard Linux power management resumes safely.
