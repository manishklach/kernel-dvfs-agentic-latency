# Deferred Completion Paths

## Beyond Direct IRQ
The baseline agent latency patch assumes a synchronous path: a hardware interrupt directly wakes the waiting agent task. However, modern Linux offloads significant work to deferred contexts. Agent-visible latency often includes time spent in these asynchronous queues.

The full completion path is frequently:
`IRQ/Timer/Event → Deferred Context → Wakeup → Scheduler → DVFS/cpuidle → Userspace`

## The Two Deferred Bottlenecks

### 1. Workqueue / kworker
Many network, block, and asynchronous I/O (e.g., `io_uring`) completions are deferred to kernel worker threads (`kworker`).
* **The Problem**: A hardware IRQ fires, but the actual wakeup of the agent thread is delegated to a workqueue. If the CPU is in a deep C-state or running at minimum frequency, the `kworker` itself suffers latency before it can wake the agent.
* **Agent Impact**: The agent is punished twice. First, the `kworker` execution is delayed. Second, the agent's wakeup is delayed.
* **Control Plane Extension**: The agent's `SCHED_FLAG_AGENT_LATENCY` metadata must be transitively attributed to the `work_struct` in flight. When the `kworker` is scheduled, it should temporarily inherit the agent's latency constraints to rush the deferred wakeup.

### 2. Timers and hrtimers
Agents waiting on precise polling loops, timeouts, or network retransmissions rely on `hrtimer` or standard timers.
* **The Problem**: Timer expiry is handled in SoftIRQ context. If the CPU is asleep, the timer hardware interrupt wakes the CPU, but the timer callback must execute before the agent is woken. 
* **Agent Impact**: A timer programmed to expire in 100µs might fire, but the CPU takes 200µs just to exit C6, violating the agent's tight deadline.
* **Control Plane Extension**: When a timer is armed by an agent-latency task, the timer subsystem should bound the allowable C-state based on the timer's proximity. The cpuidle governor must refuse sleep states whose exit latency exceeds the time remaining until an agent's timer expires.

## Measuring Deferred Latency
Use the provided `bpftrace` tools to observe this gap:
* `tools/bpftrace/workqueue_latency.bt`: Measures time from work queueing to `kworker` execution.
* `tools/bpftrace/timer_wakeup_latency.bt`: Measures time from timer expiration to agent wakeup.
