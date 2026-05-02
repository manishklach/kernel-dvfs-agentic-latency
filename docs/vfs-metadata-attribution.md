# VFS Metadata Attribution

## Why Metadata Matters for Agentic Workloads
Agentic coding systems (e.g., agents scanning a repository for context) and retrieval systems (e.g., scanning thousands of small documentation files) are extremely sensitive to VFS metadata performance. Repeatedly issuing `open`, `stat`, and `readdir` calls can trigger numerous "hidden" latency slowpaths.

The VFS path involves:
`Agent Step → path_lookup → dentry_cache → inode_cache → possible metadata I/O → Userspace Resume`

## Phase 5: VFS Metadata Observability
We expand our experimental tracepoints into the filesystem layer to identify metadata bottlenecks.

### 1. Operation Boundaries
We track the entry and exit of the core metadata syscall handlers:
* `agent_vfs_open_begin` / `agent_vfs_open_end`: Bounds `openat` and `openat2`.
* `agent_vfs_stat_begin` / `agent_vfs_stat_end`: Bounds `stat`, `lstat`, and `statx`.
* `agent_vfs_readdir_begin` / `agent_vfs_readdir_end`: Bounds directory iteration.

### 2. Path Lookup and Dentry Tracking
To understand why an operation is slow, we hook into the internal name resolution path:
* `agent_vfs_path_lookup`: Captures when the kernel begins resolving a string path.
* `agent_dentry_cache_event`: Records dentry cache hits and misses.

## Current Limitations
This RFC focuses on observability without modifying VFS internals:
1. **No Path String Copying**: For performance and safety in hot paths, we avoid copying full path strings in the kernel. Attribution relies on pointers and inode/dev IDs which can be symbolized by userspace.
2. **No New VFS Policy**: We do not yet implement dentry pinning or metadata hotset protection. These are noted as future work (e.g., `LOOKUP_AGENT_LATENCY` flag).
3. **Indirect Correlation**: Correlation between metadata lookups and downstream block I/O (if a cache miss occurs) is indirect (via timestamp/CPU/PID).

## Measuring VFS Latency
Use the included `bpftrace` tools:
* `tools/bpftrace/vfs_open_latency.bt`: Measures time spent in `do_sys_openat2`.
* `tools/bpftrace/vfs_stat_latency.bt`: Measures time spent in `vfs_statx`.
* `tools/bpftrace/vfs_readdir_latency.bt`: Measures time spent in directory iteration.
* `tools/bpftrace/path_lookup_latency.bt`: Measures internal name resolution time.
* `tools/bpftrace/agent_vfs_metadata_path.bt`: Visualizes the complete metadata resolution path.
