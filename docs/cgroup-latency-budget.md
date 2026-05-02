# Cgroup Latency Budgeting

## The Need for Budgeting
The agent latency control plane grants workloads significant power: the ability to force high CPU frequencies, block deep idle states, and bypass standard scheduler fairness for fixed windows. Without budgeting, a malicious or poorly-behaved workload could permanently override system power management, leading to thermal throttling or excessive energy consumption.

## Phase 6: Enforcement via Token Buckets
This RFC introduces a simple, cgroup-aware (simplified for research) token-bucket mechanism to bound agentic behavior.

### 1. The Token Bucket Model
Each cgroup (or the global system in this RFC) maintains a `struct agent_latency_budget`:
* `tokens`: Current available latency "credits".
* `refill_rate`: How quickly tokens are replenished (e.g., 100 tokens per ms).
* `cost_per_step`: The "price" subtracted from the bucket every time an agent window is refreshed.

### 2. Degradation Behavior
When the token bucket is exhausted (`tokens <= 0`):
* **No Window Refresh**: The `rq_agent_refresh()` hook will refuse to extend the runqueue's active agent window.
* **Fallthrough**: The CPU will fall back to standard `schedutil` and `cpuidle` governors, potentially downclocking or entering deep sleep if the workload stalls.
* **Recovery**: Behavior is restored only after the bucket refills above a safety threshold (e.g., 500 tokens).

## Tracepoints and Observability
* `agent_cgrp_tokens`: Periodically emits the current token levels and degradation state.
* `agent_cgrp_degrade`: Explicitly marks the start and end of budget-enforced degradation periods.
* `agent_cgrp_refill`: Tracks the background replenishment of the budget.

## Future Work
* **Controller Integration**: Fully integrating this into the `cpu` cgroup controller (e.g., `cpu.agent_latency_budget`).
* **Energy-Aware Scheduling (EAS)**: Adjusting the cost-per-step based on the current energy/thermal headroom of the SoC.
* **Dynamic Refilling**: Scaling the refill rate based on system load or battery status.

## Measuring Budgets
Use the included `bpftrace` tool:
* `tools/bpftrace/agent_cgroup_budget.bt`: Monitors real-time token exhaustion and recovery cycles.
