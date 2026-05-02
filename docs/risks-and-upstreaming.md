# Risks And Upstreaming

## Current Risk Profile

This patch is a research skeleton. The main risks are design incompleteness, kernel-version sensitivity, and policy abuse if the hint is too easy to set broadly.

## Technical Risks

- Fairness risk: a util floor on one runqueue can distort scheduling and frequency behavior if overused.
- Thermal risk: keeping CPUs warm can increase sustained temperature and reduce headroom elsewhere.
- Energy risk: reduced latency may come at the cost of more shallow-idle residency and higher average frequency.
- Multi-tenant risk: without cgroup or admin policy, hints could be abused by untrusted workloads.
- Portability risk: `schedutil` and cpuidle hook locations vary by kernel release and platform.
- Measurement risk: control-loop improvements may be confounded by IRQ affinity, cache locality, NIC behavior, or storage completion paths.

## Why This Is Not Upstream-Ready

- No cgroup budget model
- No thermal or energy policy integration
- No strong story for abuse prevention
- No multi-socket or heterogeneous-CPU policy
- No version-specific proof that the chosen hook points are correct across current kernels
- No real benchmark corpus in this repository

## Upstreaming Posture

Do not send this patch to LKML in its current form.

If the project matures, a more credible path would be:

1. Rebase onto a specific kernel baseline.
2. Reduce the design to one narrowly justified kernel hook at a time.
3. Produce measurements across at least one ARM platform with a clear reproducible harness.
4. Compare against `performance`, `uclamp.min`, IRQ affinity tuning, and existing scheduler knobs.
5. Narrow the semantics so the interface is understandable without agent-specific marketing language.

## Naming Risk

The "agent latency" label is useful for research framing, but an upstream discussion would likely need more neutral language around bursty completion-driven workloads or bounded post-wakeup latency hints.

## Future Work

- IRQ-aware placement
- cgroup-scoped rate limiting or budget accounting
- heterogeneous CPU policy for big.LITTLE systems
- thermal-pressure integration
- deeper analysis of PELT and util-clamp interaction
- trace-driven adaptive window sizing rather than a static default
