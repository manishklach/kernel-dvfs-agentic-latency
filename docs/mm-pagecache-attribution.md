# MM and Page Cache Attribution

## Why Memory Matters for Agentic Workloads
Agentic AI workloads often interact with large, persistent data structures mapped into their address space (e.g., local vector indexes, embedding caches, tokenizer data). Unlike traditional batch processing, these accesses occur in the critical path of the agent's reasoning loop.

When an agent accesses a page that is not currently in memory, the resulting "hidden" latency from the page fault slowpath can significantly amplify end-to-end delay.

The slowpath involves:
`Memory Access → Page Fault → handle_mm_fault → filemap_fault → Page Cache Miss → I/O (blk-mq) → Wakeup → Agent Resume`

## Phase 4: MM and Page Cache Observability
We expand our experimental tracepoints into the memory management subsystem to surface these hidden gates.

### 1. Page Fault Attribution
We track the entry and exit of the core fault handler:
* `agent_mm_fault_begin`: Captures the faulting address and flags (e.g., read vs write).
* `agent_mm_fault_end`: Captures the return code, identifying whether the fault was resolved or resulted in an error (e.g., SIGBUS).

### 2. Filemap and Page Cache Tracing
For file-backed mappings, we hook into the filemap resolution path:
* `agent_filemap_fault`: Tracks the file offset (`pgoff`) being requested.
* `agent_pagecache_event`: Explicitly records whether the request resulted in a page cache **hit** (fast path) or **miss** (slowpath/IO).

## Current Limitations
This RFC focuses strictly on observability to build a baseline for future policy:
1. **No New Policy Yet**: We do not implement proactive warming or eviction protection flags like `MADV_AGENT_HOTSET`. These are noted as future work.
2. **Indirect Correlation**: While we propagate `agent_step_id` to the MM tracepoints, correlation to downstream `blk-mq` I/O (if a miss occurs) remains indirect (via timestamp/CPU/PID).
3. **Overhead**: MM hot paths are extremely sensitive. These tracepoints are minimal and gated behind `CONFIG_AGENT_LATENCY` and `task_agent_latency()` checks.

## Measuring MM Latency
Use the included `bpftrace` tools:
* `tools/bpftrace/mm_fault_latency.bt`: Measures the end-to-end time spent in `handle_mm_fault`.
* `tools/bpftrace/pagecache_faults.bt`: Records hit/miss ratios for the agent loop.
* `tools/bpftrace/agent_mm_io_path.bt`: Visualizes the transition from MM fault to block I/O.
