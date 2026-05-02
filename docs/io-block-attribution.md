# I/O and Block Attribution

## Why Storage Matters for Agentic Workloads
Agent control loops frequently block on asynchronous operations, specifically small random I/O (e.g., SQLite, vector database lookups, RocksDB). Unlike bulk data transfers, these reads are small but occur synchronously within the agent's critical path.

The kernel path involves:
`Agent Wait → io_uring SQE → blk-mq request → NVMe IRQ → bio completion → io_uring CQE → Scheduler Wakeup → DVFS/cpuidle exit → Agent Resume`

## Phase 3: io_uring and blk-mq Observability
We expand our experimental tracepoints into the storage and asynchronous subsystem paths. 

### 1. io_uring Attribution
Because modifying ABI or adding metadata to SQEs is invasive, this RFC introduces a non-invasive convention: **Userspace agents encode their `agent_step_id` in the upper 32 bits of the io_uring `user_data` field.**

* `agent_io_uring_submit`: Captures the SQE submission along with the decoded `step_id` and `local_request_id`.
* `agent_io_uring_complete`: Captures the CQE completion. Traces regardless of current task context if a `step_id` is present in `user_data`.

### 2. blk-mq Attribution
For the block layer, we trace the hardware boundary with safer argument passing.
* `agent_blk_rq_issue`: Captures the request right before it is issued to the hardware.
* `agent_blk_rq_complete`: Captures completion. Note that partial completions may be visible via `blk_update_request`.

## Current v1/v2 Limitations
The current RFC implementation focuses on observability without invasive structural changes:
1. **user_data Convention**: Attribution relies on the userspace 64-bit `user_data` encoding (`step_id = user_data >> 32`).
2. **Indirect Correlation**: `blk-mq` tracepoints do not yet carry the `agent_step_id`. Correlation between `io_uring` and `blk` is currently indirect (using timestamps, PIDs, and CPUs).
3. **No ABI Changes**: We do not modify `struct request` or io_uring SQE layouts.
4. **Partial Completions**: Block layer tracing may observe multiple partial completion events for a single request.

**TODO for Future Work (v3):** 
Propagate the `agent_step_id` from the io_uring SQE `user_data` into the `blk` request metadata natively, allowing robust end-to-end correlation and evaluating `blk_mq_end_request` as a final completion hook.

## Measuring I/O Latency
Use the included `bpftrace` tools:
* `tools/bpftrace/io_uring_submit_complete.bt`: Measures time between SQE submission and CQE completion.
* `tools/bpftrace/blk_request_latency.bt`: Measures time between blk-mq request issue and completion.
* `tools/bpftrace/agent_io_block_path.bt`: Consolidates events to visualize the entire path.
