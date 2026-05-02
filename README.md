# kernel-dvfs-agentic-latency

**A kernel-level latency control plane for agentic workloads.**

Agentic AI workloads expose a new kernel latency bottleneck across DVFS, cpuidle, the IRQ path, and scheduler wakeups. This research repository introduces a bounded "agent active window" to eliminate latency amplification in tight control loops.

## Thesis
Agent workloads do not behave like traditional batch processing or high-throughput servers. They run in extremely tight, sequential control loops: **wait → wake → compute → repeat**. This pattern actively fights modern Linux power management and scheduling subsystems, leading to massive latency amplification.

## The Problem: Latency Amplification
Because the "compute" step is often very fast, the CPU routinely drops into deep sleep or lowers frequency during the "wait" step. When the next completion arrives, the kernel pays a latency penalty at every stage of the wake path.

The result is **latency amplification**:
`(disk + IRQ + scheduler + DVFS + idle exit) × N steps`

Across thousands of reasoning/retrieval steps, this sub-millisecond penalty accumulates into significant user-facing delays.

## The Kernel Control Path
To understand the bottleneck, we must look at the complete path a wakeup takes. This repository targets the entire chain:

`IRQ → wakeup → scheduler → DVFS → cpuidle → userspace`

## Deferred Completion Paths
Agent-visible latency isn't just direct hardware IRQs. Often, completion logic is deferred:

`IRQ/Timer → Workqueue/SoftIRQ → Wakeup → Scheduler → DVFS → cpuidle → Userspace`

To address this, our research extends latency attribution into deferred contexts. We track delays caused by `kworker` threads executing deferred I/O and `hrtimer` callbacks in SoftIRQ context. If a `kworker` is processing an agent's completion, it must inherit the agent's latency guard to rush the wakeup. (See [docs/deferred-completion-paths.md](docs/deferred-completion-paths.md) for details).

## I/O and blk-mq Attribution

Agent loops frequently block on local storage and async I/O. This repo now includes RFC tracepoints and bpftrace tools to measure io_uring submit/complete and blk-mq request issue/complete latency.

## MM and Page Cache Attribution

Agent loops often stall on mmap/page-cache-backed data. This repo now includes RFC tracepoints and bpftrace tools for page fault and filemap/page-cache attribution.

## VFS Metadata Attribution

Agentic coding and retrieval workloads frequently scan directories and issue many open/stat/readdir operations. This repo now includes RFC tracepoints and bpftrace tools to measure VFS metadata latency.

## Latency Budgeting

To prevent abuse, agent latency behavior is gated by a simple cgroup token budget. This ensures that latency-sensitive execution remains bounded and does not permanently override power management.
## What This Repo Does
This is **not** a generic AI repo. This is **not** a "performance tuning" guide. This is a cross-subsystem Linux kernel research project exploring a new scheduler abstraction that spans 4 kernel subsystems:

1. **Scheduler**: Introduces `SCHED_FLAG_AGENT_LATENCY` to identify tight control loop threads and preserve cache locality on wakeup.
2. **DVFS (schedutil)**: Enforces an effective minimum utilization floor during an active "agent window," preventing the CPU from dropping frequency between rapid steps.
3. **cpuidle**: Adds an agent latency guard that prevents the CPU from entering deep sleep states whose exit latency exceeds a defined budget.
4. **IRQ/Wakeup**: Refreshes the agent window precisely at wakeup, eliminating frequency ramp delays.

## Key Insight
This is **not a DVFS tweak**. Existing mechanisms like `schedutil` are purely reactive, while `uclamp` is static and energy-blind over long periods. The key insight is modeling the workload as a short, moving **"active window"**. If the loop closes within the window, the state remains warm. If the process stops, power management resumes immediately.

## Architecture
```text
 +-----------------------------------------------------------+
 |                     Userspace Agent                       |
 |  [ Wait (epoll) ] ---> [ Wake ] ---> [ Compute/Parse ]    |
 +--------^------------------|------------------|------------+
          |                  |                  |
 +--------|------------------v------------------v------------+
 |                       Kernel Space                        |
 |  IRQ Delivery --->  Scheduler Wakeup ---> schedutil/DVFS  |
 |  (Refresh Window)   (Keep Cache Hot)      (Min Util Guard)|
 |                                                 |         |
 |                                           cpuidle         |
 |                                           (Block C-states)|
 +-----------------------------------------------------------+
```

## Contents
* `patches/0000-kernel-agent-latency-control-plane-rfc.patch` - The core kernel implementation.
* `docs/architecture.md` - Subsystem interaction and problem definition.
* `docs/kernel-design.md` - Line-by-line patch breakdown and data structures.
* `docs/measurement-plan.md` - Experimental setup and latency measurement.
* `tools/bpftrace/` - eBPF scripts for observability.
* `selftests/agent_loop.c` - Microbenchmark for validation.

## Measurement Plan
We measure wakeup-to-run, DVFS ramp, idle exit, and p99 loop latency using a rigorous eBPF methodology. See [docs/measurement-plan.md](docs/measurement-plan.md) for full details.

## Quickstart
1. Review the [Architecture](docs/architecture.md) and [Kernel Design](docs/kernel-design.md).
2. Run the selftest:
   ```bash
   cd selftests && make
   ./agent_loop --agent-latency --steps 1000 --interval-us 500
   ```
3. Attach eBPF tracing:
   ```bash
   sudo bpftrace tools/bpftrace/wakeup_to_run.bt
   ```

## Status
**RFC (Request for Comments).** This patch series is experimental. It is designed for systems research, artifact development, and latency analysis. It is not upstream-ready and requires cgroup budget controls before production deployment.

## Future Work
* Integrating with eBPF schedulers (sched-ext).
* Dynamic agent-window sizing based on historical loop duration.
* Cgroup-level token buckets to prevent power abuse.

## Takeaway
**Agentic loops expose the gap between reactive DVFS and static uclamp; we need a moving control-plane window.**
