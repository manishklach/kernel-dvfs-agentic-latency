# Measurement Plan

Validating the agent latency patch requires observing microsecond-level interactions across multiple kernel boundaries.

## Experiments
We run the `agent_loop` microbenchmark under four configurations to establish ground truth:

1. **Baseline**: Standard `schedutil` and `menu`/`teo` cpuidle governor.
2. **Performance Governor**: `cpufreq` set to `performance`, C-states clamped via `/dev/cpu_dma_latency`. (Maximum power draw).
3. **uclamp**: Task sets `util_min=1024` via `sched_setattr`.
4. **Agent Patch**: Task sets `SCHED_FLAG_AGENT_LATENCY`.

## Metrics
1. **wakeup-to-run**: Time from `try_to_wake_up` to the task actually entering CPU execution.
2. **DVFS ramp delay**: Time taken to reach maximum frequency after wake.
3. **Idle exit latency**: Delay imposed by the C-state transition.
4. **epoll / io_uring wait duration**: Application-perceived block time.
5. **p99 total loop latency**: The cumulative latency amplification.

## eBPF Tooling (`tools/bpftrace/`)
We use `bpftrace` to gather high-fidelity data without kernel recompilation.

### Wakeup Latency
Measure scheduler delay:
```bash
sudo bpftrace tools/bpftrace/wakeup_to_run.bt
```

### CPU Frequency Transitions
Monitor schedutil decisions:
```bash
sudo bpftrace tools/bpftrace/cpu_frequency.bt
```

### CPU Idle State Selection
Observe C-state guards:
```bash
sudo bpftrace tools/bpftrace/cpu_idle.bt
```

### Application-Level Wait
```bash
sudo bpftrace tools/bpftrace/epoll_latency.bt
sudo bpftrace tools/bpftrace/io_uring_latency.bt
```

## System Commands for Baselines
Use `cpupower` and `perf` to control and verify baselines:

```bash
# Set performance governor
sudo cpupower frequency-set -g performance

# Verify C-state usage
sudo perf stat -e "power:cpu_idle" -a sleep 5
```
