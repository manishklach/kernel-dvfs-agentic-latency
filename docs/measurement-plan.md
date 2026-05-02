# Measurement Plan

Validating the agent latency patch requires observing microsecond-level interactions across multiple kernel boundaries.

## Experimental Configurations
To establish ground truth, we run the `agent_loop` microbenchmark under four configurations:

1. **Baseline**: Standard `schedutil` governor and `menu`/`teo` cpuidle governor. (Power optimized, high latency).
2. **Performance Governor**: `cpufreq` set to `performance`, C-states clamped via `/dev/cpu_dma_latency`. (Maximum power draw, minimum latency).
3. **uclamp**: Task sets a static `util_min=1024` via `sched_setattr`. (Power blind, high utilization).
4. **Agent Patch**: Task sets `SCHED_FLAG_AGENT_LATENCY`. (Dynamic active window).

## Metrics
1. **Wakeup Latency**: Time from `try_to_wake_up` execution to the task executing on a CPU.
2. **DVFS Ramp**: The delay between task wakeup and the CPU reaching target frequency.
3. **Idle Exit Latency**: The hardware delay imposed by C-state transition.
4. **p99 Total Latency**: The cumulative end-to-end loop latency amplification.

## Measurement Tooling

### bpftrace
Use eBPF to measure internal kernel latency without recompilation overhead:

* **Wakeup to run**:
  ```bash
  sudo bpftrace tools/bpftrace/wakeup_to_run.bt
  ```
* **DVFS state transitions**:
  ```bash
  sudo bpftrace tools/bpftrace/cpu_frequency.bt
  ```
* **C-state entry**:
  ```bash
  sudo bpftrace tools/bpftrace/cpu_idle.bt
  ```

### perf
Verify idle state occupancy using hardware counters:
```bash
sudo perf stat -e "power:cpu_idle" -a sleep 5
```

### cpupower
Control baseline frequency configurations:
```bash
# Set performance governor
sudo cpupower frequency-set -g performance

# View idle state statistics
sudo cpupower idle-info
```
