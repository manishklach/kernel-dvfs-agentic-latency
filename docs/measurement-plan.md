# Measurement Plan

This measurement plan is designed to compare existing mechanisms against the experimental patch without inventing results. Use it as a reproducible workflow for an ARM/Linux evaluation branch.

## Test Matrix

### A. Baseline

- Governor: `schedutil`
- cpuidle: default platform configuration
- Placement: no task pinning
- Task hint: no agent flag

Purpose: establish the default behavior of the target system.

### B. Performance Governor

- Governor: `performance`
- cpuidle: unchanged unless the platform couples policies
- Task hint: none

Purpose: provide an upper-bound latency reference at higher energy cost.

### C. uclamp.min

- Governor: `schedutil`
- cpuidle: default
- Task hint: use existing `uclamp.min` controls where possible

Purpose: test an existing Linux performance-hint mechanism before introducing new logic.

### D. Agent Latency Patch

- Kernel: patched
- `CONFIG_AGENT_LATENCY=y`
- Task hint: `SCHED_FLAG_AGENT_LATENCY` enabled in the selftest
- Governor: `schedutil`

Purpose: test whether a short agent-active window reduces repeated wakeup-related penalties.

### E. Agent Latency + IRQ-Aware Placement

This repository does not implement this yet. Treat it as future work:

- preserve or improve CPU locality for the waking task
- avoid CPUs heavily loaded with recent IRQ/softirq completion work
- correlate placement with cache warmth and wakeup-to-run latency

## Metrics

Collect at least:

- `p50`, `p99`, `p999` step latency
- wakeup-to-run latency
- frequency transition count
- frequency ramp after wakeup
- idle-state residency
- `epoll_wait()` blocked duration
- `io_uring_enter()` blocked duration
- energy/latency tradeoff if RAPL, PMU, or board sensors are available

## Derived Metrics

Useful derived values:

- `freq_ramp_after_wakeup_us`
- idle exit observed near wakeup
- transitions per thousand agent steps
- latency reduction versus increased high-frequency residency

## Suggested Procedure

1. Boot the target kernel and confirm the governor state.
2. Run the selftest without the agent flag.
3. Capture `bpftrace` logs for wakeups, frequency changes, idle transitions, and userspace gate times.
4. Repeat with `performance`.
5. Repeat with `uclamp.min` where supported.
6. Repeat with the patched kernel and `--agent-latency`.
7. Postprocess logs into percentile summaries and event correlations.

## Example Commands

Inspect CPUFreq state:

```bash
cpupower frequency-info
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

Build and run the selftest:

```bash
make -C selftests
./selftests/agent_loop --steps 20000 --interval-us 500
./selftests/agent_loop --agent-latency --steps 20000 --interval-us 500
```

Run trace collection:

```bash
sudo bpftrace tools/bpftrace/wakeup_to_run.bt
sudo bpftrace tools/bpftrace/cpu_frequency.bt
sudo bpftrace tools/bpftrace/cpu_idle.bt
sudo bpftrace tools/bpftrace/epoll_gate.bt
sudo bpftrace tools/bpftrace/io_uring_gate.bt
```

Optional scheduler inspection:

```bash
sudo perf sched record -- ./selftests/agent_loop --steps 5000 --interval-us 500
sudo perf sched report
```

Postprocess:

```bash
python3 tools/postprocess/correlate_dvfs_wakeup.py \
  --wakeup wakeup.log \
  --frequency cpu_frequency.log \
  --idle cpu_idle.log
```

## Control Variables

Try to keep these stable across runs:

- board model and firmware
- CPU governor and thermal settings
- background load
- task interval and number of steps
- storage and network device state
- kernel config and debug options

## Interpretation Guidance

- If `performance` collapses latency tails but the patch does not, the issue may be broader than short-window policy.
- If `uclamp.min` gives similar wins, the patch must justify why time-bounded semantics are still better.
- If the patch reduces latency but raises idle-state churn or energy cost sharply, that tradeoff needs to be made explicit.
- If there is no measurable reduction in `freq_ramp_after_wakeup_us`, the schedutil hook may not be landing at the right place for the target kernel.
