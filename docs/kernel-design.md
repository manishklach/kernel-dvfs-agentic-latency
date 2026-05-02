# Kernel Design

This document explains the RFC patch series as a systems research design. It is intentionally conservative and builds on existing kernel mechanisms rather than inventing a parallel scheduler or power-management subsystem.

## CONFIG_AGENT_LATENCY

`CONFIG_AGENT_LATENCY` is the compile-time gate for the experiment.

- Location: `kernel/sched/Kconfig`
- Dependency: `CPU_FREQ_GOV_SCHEDUTIL`
- Intent: keep the feature clearly optional and easy to disable
- Status: research-only

At a high level, the Kconfig option enables task metadata, runqueue metadata, a schedutil boost hook, a cpuidle guard, and a few tracepoints for instrumentation.

## SCHED_FLAG_AGENT_LATENCY

`SCHED_FLAG_AGENT_LATENCY` is the userspace-visible opt-in hint.

- Location: `include/uapi/linux/sched.h`
- Intended use: normal tasks that participate in bursty agent control loops
- Explicit non-goal: stacking the hint onto RT or deadline classes in this RFC

The patch uses the flag as an admission point for default agent metadata. A more complete design would likely need stronger policy controls and perhaps cgroup-level governance.

## task_struct Metadata

The RFC adds experimental fields to `task_struct`:

- `agent_window_ns`
- `agent_min_util`
- `agent_id`
- `agent_step_id`

Purpose of each field:

- `agent_window_ns`: duration of the short active window after relevant wakeups
- `agent_min_util`: minimum effective utilization exposed to `schedutil` during the active window
- `agent_id`: optional attribution field for tracing multi-agent experiments
- `agent_step_id`: optional per-step sequence number for timeline correlation

These are deliberately small and do not yet define a complete policy model.

## rq Metadata

The RFC adds experimental fields to `struct rq`:

- `agent_active_until_ns`
- `agent_min_util`
- `agent_home_cpu`

Purpose of each field:

- `agent_active_until_ns`: deadline for the active window on the runqueue
- `agent_min_util`: runqueue-local effective minimum util during the active window
- `agent_home_cpu`: placeholder for locality-oriented heuristics and future placement work

The current design is runqueue-scoped because the desired effect is local to the CPU that may otherwise downscale or enter deeper idle.

## schedutil Hook

The patch adds a small schedutil helper:

- `agent_boost_util()` in design terms
- named `agent_latency_boost_util()` in the current RFC patch

Behavior:

1. Read current runqueue state.
2. Check whether `agent_active_until_ns > now`.
3. If active, raise effective util to at least `agent_min_util`.
4. Clamp to the governor's `max`.
5. Emit a tracepoint if the value actually changed.

This keeps the design aligned with the existing CPUFreq core/governor/driver model. The patch does not replace governor decisions; it only biases the utilization input during a short window.

## cpuidle Guard

The cpuidle side is intentionally simple. During an active agent window, the patch avoids deep idle states whose `exit_latency` exceeds `AGENT_IDLE_EXIT_LIMIT_US`.

Design intent:

- Preserve shallow-idle behavior where possible
- Avoid obviously expensive idle exits during an expected near-future agent wakeup
- Keep the mechanism bounded and measurable

This is not a new cpuidle governor. It is a small guard layered onto existing cpuidle behavior.

## Wakeup Path

The RFC refreshes the runqueue's active window on task wakeup. Conceptually:

```text
agent task wakes
-> scheduler observes agent flag
-> runqueue active window extended
-> schedutil sees temporary min-util floor
-> cpuidle guard can reject overly deep idle states
```

The current patch places this in the normal wakeup path, which is why rebasing against a real kernel tree matters: locking, helper names, and exact wakeup flow differ by version.

## Tracepoints

The RFC defines three dedicated tracepoints:

- `agent_window_refresh`
- `agent_schedutil_boost`
- `agent_cpuidle_guard`

These are important because the patch should be judged by trace evidence, not intuition. They let you correlate:

- when the active window was refreshed
- whether schedutil util was actually boosted
- whether cpuidle selection was constrained

## Selftest

The patch ships a minimal selftest built around an `eventfd` + `epoll` loop.

High-level behavior:

- Producer thread signals an `eventfd` periodically.
- Main thread blocks in `epoll_wait()`.
- Wakeup returns through the scheduler path.
- Main thread performs a small CPU parse loop.
- Test repeats for many short steps.

This is not intended to be a full agent runtime. It is a controlled latency probe for the wait-wakeup-run pattern.

## Existing Linux Mechanisms This Builds On

- CPUFreq core/governor/driver model
- `schedutil` governor
- `uclamp` as an existing performance-hint mechanism
- cpuidle governors and idle-state metadata
- scheduler wakeup path and fair scheduling

The project is best understood as a thin experimental layer across these mechanisms, not a competing subsystem.
