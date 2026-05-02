# Agent Latency Architecture

## The Agentic Control Loop
Agentic AI workloads execute tight, sequential control loops. Rather than sustaining massive throughput or processing large data batches, they perform repeated cycles of:

1. **Wait**: The agent submits an async request (e.g., LLM inference, vector DB lookup, hardware tool execution) and blocks in `epoll_wait` or `io_uring_enter`.
2. **Wake**: An IRQ signals completion; the kernel wakes the agent thread.
3. **Compute**: The agent quickly parses the result, updates its context state, and determines the next action.
4. **Repeat**: The cycle restarts immediately.

## Latency Amplification
Because the "Compute" phase is often fast relative to the "Wait" phase, the CPU routinely drops into deep sleep states or lowers its frequency during the wait.

This results in a systemic **latency amplification** effect:
`Latency Penalty = (disk + IRQ + scheduler + DVFS + idle exit) × N steps`

Over a chain of thousands of iterative reasoning or retrieval steps, these sub-millisecond wakeup penalties compound into massive user-facing delays.

## The Kernel Control Path
To understand the bottleneck, we must analyze the complete wakeup path:

`IRQ → wakeup → scheduler → DVFS → cpuidle → userspace`

```text
 +-----------------------------------------------------------+
 |                     Userspace Agent                       |
 |  [ Wait (epoll) ] ---> [ Wake ] ---> [ Compute/Parse ]    |
 +--------^------------------|------------------|------------+
          |                  |                  |
 +--------|------------------v------------------v------------+
 |                       Kernel Space                        |
 |                                                           |
 |  [ IRQ ] -> [ Scheduler Wakeup ] -> [ DVFS / cpuidle ]    |
 |                                                           |
 +-----------------------------------------------------------+
```

### 1. IRQ and Wakeup
* **Linux Today**: When an IRQ fires, `try_to_wake_up()` places the blocked task on a runqueue.
* **Why it fails**: The kernel does not know the task is part of a tight control loop. It treats this as an isolated event.

### 2. Scheduler
* **Linux Today**: The load balancer may migrate the task to an idle (cold) CPU to balance runqueue depth.
* **Why it fails**: Moving the task discards its L1/L2 cache state. For short compute phases, cache-miss latency outweighs queue-wait latency.

### 3. DVFS (schedutil)
* **Linux Today**: `schedutil` relies on PELT (Per-Entity Load Tracking) to determine CPU utilization.
* **Why it fails**: PELT is a low-pass filter. Short bursts of compute following a sleep period produce a low PELT signal. The CPU remains at its minimum frequency during the critical compute phase.

### 4. cpuidle
* **Linux Today**: The idle governor (e.g., `menu` or `teo`) predicts sleep duration. Because the agent waits on external API calls, the governor selects deep C-states (C3/C6).
* **Why it fails**: Deep C-states incur exit latencies of hundreds of microseconds. The agent thread must wait for hardware to physically wake the core before it can even begin execution.
