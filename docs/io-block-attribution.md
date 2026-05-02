# I/O and Block Attribution

## Why Storage Matters for Agentic Workloads
Agent control loops frequently block on asynchronous operations, specifically small random I/O (e.g., SQLite, vector database lookups, RocksDB). Unlike bulk data transfers, these reads are small but occur synchronously within the agent's critical path.

The kernel path involves:
`Agent Wait → io_uring SQE → blk-mq request → NVMe IRQ → bio completion → io_uring CQE → Scheduler Wakeup → DVFS/cpuidle exit → Agent Resume`

## Phase 3: io_uring and blk-mq Observability
We expand our experimental tracepoints into the storage and asynchronous subsystem paths. 

### 1. io_uring Attribution
Because modifying ABI or adding metadata to SQEs is invasive, this RFC introduces a non-invasive convention: **Userspace agents encode their `agent_step_id` in the upper 32 bits of the io_uring `user_data` field.**

* `agent_io_uring_submit`: Captures the SQE submission along with the encoded `user_data` and operation opcode.
* `agent_io_uring_complete`: Captures the CQE completion.

### 2. blk-mq Attribution
For the block layer, we trace the hardware boundary.
* `agent_blk_rq_issue`: Captures the request right before it is issued to the hardware.
* `agent_blk_rq_complete`: Captures the completion of the block request.

## Limitations of v1 (The "Correlation Gap")
In the current RFC implementation, we intentionally avoid heavily modifying `struct request` to carry the `agent_step_id` down to the hardware layer.

As a result, correlation between `io_uring` and `blk-mq` is indirect:
1. We can trace the `io_uring` submit/complete boundaries.
2. We can trace the `blk-mq` issue/complete boundaries.
3. We correlate them post-hoc using `bpftrace` scripts by matching timestamps, PIDs, and CPUs.

**TODO for Future Work:** 
Propagate the `agent_step_id` from the io_uring SQE `user_data` into the `blk` request metadata natively, allowing robust and direct attribution mappings without relying on heuristic time correlations.

## Measuring I/O Latency
Use the included `bpftrace` tools:
* `tools/bpftrace/io_uring_submit_complete.bt`: Measures time between SQE submission and CQE completion.
* `tools/bpftrace/blk_request_latency.bt`: Measures time between blk-mq request issue and completion.
* `tools/bpftrace/agent_io_block_path.bt`: Consolidates events to visualize the entire path.
