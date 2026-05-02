# kernel-dvfs-agentic-latency

Agent-aware latency optimizations for the Linux kernel (DVFS, IRQ, scheduler) targeting agentic AI workloads.

This repository packages an experimental RFC kernel patch, measurement tooling, a standalone selftest, and research notes around a simple thesis: agentic AI control loops are not throughput-shaped workloads. They alternate between short waits on completions and short bursts of compute, so a small per-step delay in IRQ completion, scheduler wakeup, cpuidle exit, or CPUFreq ramp can get amplified across hundreds or thousands of agent steps.

## Thesis

Agentic AI workloads often look like: issue a tool call, wait in `epoll_wait()` or `io_uring_enter()`, wake on an IRQ-backed completion, parse the result, decide the next step, and block again. On ARM Linux systems using `schedutil` and deep idle states, those short waits can be enough to cool the CPU between steps. The next step then repays idle-exit and DVFS-ramp latency, not because the machine is saturated, but because the control loop is bursty.

## Why Agentic AI Is Different

Traditional throughput workloads benefit when the scheduler and power-management stack optimize for sustained utilization over longer windows. Agentic loops are different:

- They are completion-driven rather than continuously CPU-bound.
- They often spend most of their time blocked in userspace gates such as `epoll_wait()` or `io_uring_enter()`.
- They execute many short decision steps where per-step latency compounds into visible wall-clock slowdown.
- They may be bottlenecked by control-path wakeup behavior rather than raw compute throughput.

## What The Patch Does

The RFC patch in [`patches/0000-agent-latency-dvfs-rfc.patch`](patches/0000-agent-latency-dvfs-rfc.patch) introduces an experimental `CONFIG_AGENT_LATENCY` mode:

- Adds `SCHED_FLAG_AGENT_LATENCY` as an opt-in task hint.
- Tracks a bounded agent activity window on the runqueue after wakeups.
- Lets `schedutil` apply a temporary minimum effective utilization during that window.
- Lets `cpuidle` avoid deep idle states whose exit latency is above a small guard threshold during the active window.
- Adds tracepoints so the behavior can be measured instead of guessed.

## What It Does Not Do

- It is not upstream-ready.
- It is not a replacement for CPUFreq, `schedutil`, `uclamp`, or `cpuidle`.
- It does not create a new real-time scheduler class.
- It does not promise correctness across kernel versions without rebasing.
- It does not include cgroup policy, thermal governance, admission control, or abuse prevention.
- It does not include benchmark claims in this repository.

## Relationship To Existing Kernel Mechanisms

- CPUFreq: the patch keeps the CPUFreq core/governor/driver model intact and only influences utilization seen by `schedutil`.
- `schedutil`: the proposed hook raises effective util during a short agent-active window.
- `uclamp`: `uclamp.min` already exists as a performance hint; this patch models a time-bounded control-loop window rather than a persistent clamp.
- `cpuidle`: the RFC keeps cpuidle governors in place but adds a guard against deep idle states with excessive exit latency during active windows.
- IRQ completions and wakeups: the motivating path is `IRQ/completion -> softirq/blk-mq/timer -> wakeup -> runqueue selection -> userspace resumes`.
- Scheduler wakeups: the patch is intentionally conservative and leans on existing wakeup and fair-scheduler logic.

## Repository Layout

```text
kernel-dvfs-agentic-latency/
  README.md
  LICENSE
  PATCH_NOTES.md
  patches/
    0000-agent-latency-dvfs-rfc.patch
  docs/
    architecture.md
    kernel-design.md
    measurement-plan.md
    risks-and-upstreaming.md
  tools/
    bpftrace/
      cpu_frequency.bt
      cpu_idle.bt
      wakeup_to_run.bt
      epoll_gate.bt
      io_uring_gate.bt
    postprocess/
      correlate_dvfs_wakeup.py
  selftests/
    agent_loop.c
    Makefile
  diagrams/
    agent-dvfs-window.svg
    irq-scheduler-dvfs-path.svg
  site/
    index.html
```

## Research Warning

This repository is research code. The patch is an RFC skeleton for local experimentation only. It is not production-ready, not security-reviewed, not thermal-policy complete, and not suitable for LKML submission in its current form.

## Quickstart

1. Unpack or clone this repository.
2. Apply the RFC patch to a Linux kernel tree:

```bash
cd /path/to/linux
git am /path/to/kernel-dvfs-agentic-latency/patches/0000-agent-latency-dvfs-rfc.patch
```

3. Enable `CONFIG_AGENT_LATENCY`:

```bash
scripts/config --enable AGENT_LATENCY
```

4. Build and boot the kernel using your normal ARM kernel workflow.
5. Build the standalone selftest:

```bash
make -C selftests
```

6. Run the selftest:

```bash
./selftests/agent_loop --steps 10000 --interval-us 500
./selftests/agent_loop --agent-latency --steps 10000 --interval-us 500
```

7. Run measurement tools while the selftest is active:

```bash
sudo bpftrace tools/bpftrace/wakeup_to_run.bt
sudo bpftrace tools/bpftrace/cpu_frequency.bt
sudo bpftrace tools/bpftrace/cpu_idle.bt
sudo bpftrace tools/bpftrace/epoll_gate.bt
sudo bpftrace tools/bpftrace/io_uring_gate.bt
```

8. Correlate trace logs:

```bash
python3 tools/postprocess/correlate_dvfs_wakeup.py \
  --wakeup wakeup.log \
  --frequency cpu_frequency.log \
  --idle cpu_idle.log
```

## Expected Measurements

- Wakeup-to-run latency
- CPU frequency transitions
- CPU idle state transitions
- `epoll_wait()` blocked duration
- `io_uring_enter()` blocked duration
- Derived `freq-ramp-after-wakeup` metric
- Step latency percentiles such as `p50`, `p99`, and `p999`

## Key Documents

- [Architecture overview](docs/architecture.md)
- [Kernel design details](docs/kernel-design.md)
- [Measurement plan](docs/measurement-plan.md)
- [Risks and upstreaming notes](docs/risks-and-upstreaming.md)
- [Patch rebase notes](PATCH_NOTES.md)

## Status

Use this repo as a systems-research artifact: patch skeleton, selftest, tracing scripts, documentation, and a measurement plan. Do not treat it as a productized kernel feature branch.
