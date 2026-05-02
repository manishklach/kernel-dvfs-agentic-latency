# Deferred Completion Paths

## The Asynchronous Bottleneck
The baseline agent latency patch assumes a synchronous path: a hardware interrupt directly wakes the waiting agent task. However, modern Linux offloads significant work to deferred contexts. Agent-visible latency often includes time spent in these asynchronous execution queues.

The full completion path is frequently:
`IRQ/Timer/Event → Workqueue/SoftIRQ → Wakeup → Scheduler → DVFS/cpuidle → Userspace`

## Phase 2: Deferred Context Attribution

### 1. Workqueue / kworker
Workqueues are asynchronous execution contexts serviced by kernel worker threads (`kworker`).
* **The Problem**: A hardware IRQ fires, but the actual wakeup of the agent thread is delegated to a workqueue. If the CPU drops into a deep C-state or minimum frequency, the `kworker` itself suffers latency before it can wake the agent.
* **Control Plane Extension**: The agent's latency guard must be transitively applied. Using `agent_work_ctx` (or a BPF map keyed by `work_struct`), we attribute specific work items to the agent step ID. When the `kworker` begins execution, it refreshes the agent latency window to protect the upcoming wakeup.

### 2. Timers and hrtimers
Timers (`hrtimers`) often gate retry logic, timeouts, and tight polling loops.
* **The Problem**: Timer expiry is handled in SoftIRQ context. The timer interrupt wakes the CPU, but the timer callback must execute before the agent is woken. 
* **Control Plane Extension**: When an agent's timer expires, the SoftIRQ context can refresh the agent latency window immediately, ensuring the core stays warm while transitioning from the timer callback back to userspace.

## Phase 2 Tracepoints
We introduce new experimental tracepoints to observe deferred context delays without heavy task-struct modifications:
* `agent_workqueue_queue`: Fires when work is queued for an agent.
* `agent_workqueue_start` / `agent_workqueue_finish`: Bounds the execution of the `kworker`.
* `agent_timer_expire`: Fires when an attributed timer begins execution.
* `agent_timer_wakeup`: Fires when the timer successfully wakes the agent.

## Measuring Deferred Latency
Use the provided `bpftrace` scripts to analyze these paths:
* `tools/bpftrace/workqueue_latency.bt`: Measures time from work queueing to `kworker` execution.
* `tools/bpftrace/timer_latency.bt`: Measures time from timer expiration to agent wakeup.
* `tools/bpftrace/kworker_runtime.bt`: Measures how long the `kworker` blocks the agent.
