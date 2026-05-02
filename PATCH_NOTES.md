# Patch Notes

This repository preserves the original RFC patch as a research artifact. It is intentionally not presented as upstream-ready.

## Scope

- Patch file: `patches/0000-agent-latency-dvfs-rfc.patch`
- Intended usage: local experimental branches only
- Intended target: ARM/Linux systems where wakeup-to-run and DVFS/cpuidle interactions are measurable

## Plausibility Review

The patch structure is plausible for a kernel RFC skeleton:

- `include/uapi/linux/sched.h`
- `include/linux/sched.h`
- `include/linux/sched/agent_latency.h`
- `include/trace/events/agent_latency.h`
- `kernel/sched/core.c`
- `kernel/sched/fair.c`
- `kernel/sched/cpufreq_schedutil.c`
- `drivers/cpuidle/cpuidle.c`
- `tools/testing/selftests/agent_latency/agent_loop.c`

The comments already describe the feature as experimental and research-oriented, which is the right framing. No benchmark claims are embedded in the patch.

## Likely Rebase Points

Expect to revisit these files and their surrounding helpers on a real target tree:

- `kernel/sched/core.c`
- `kernel/sched/fair.c`
- `kernel/sched/cpufreq_schedutil.c`
- `drivers/cpuidle/cpuidle.c`
- `include/uapi/linux/sched.h`
- `include/linux/sched.h`

## Kernel-Version Sensitivity

Several hook points are kernel-version dependent:

- `__sched_setscheduler()` signature and validation flow can drift.
- `try_to_wake_up()` locking and wakeup path placement change over time.
- `select_task_rq_fair()` may gain or lose helper indirection.
- `schedutil` internals, `sugov_cpu`, and utility calculation helpers regularly move.
- cpuidle state-selection logic may be split across governor-specific paths rather than being best patched in `cpuidle_enter_state()`.

If rebasing, prefer preserving the design intent over preserving exact function placement.

## Open Engineering Concerns

- The runqueue metadata is intentionally minimal and does not yet define lifecycle reset semantics.
- The patch does not yet model cgroup budgets, fairness constraints, thermal limits, or abuse prevention.
- The cpuidle guard currently uses a static exit-latency limit. A production design would likely need per-policy or per-task tuning.
- The schedutil boost is simple by design. A production variant would need stronger reasoning around util clamping, PELT interaction, and multi-CPU behavior.

## Recommendation

Treat the patch as a conversation starter plus implementation seed. Rebase it onto a chosen kernel baseline, validate the hook locations, then iterate with tracing and measurement before considering any wider discussion.
