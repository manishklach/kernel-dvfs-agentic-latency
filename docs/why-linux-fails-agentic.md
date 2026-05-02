# Why Linux Fails Agentic Workloads

## The Reactive Nature of Linux Power Management
Modern Linux power management is fundamentally reactive. 

Subsystems like `schedutil` (DVFS) and the `cpuidle` governors are designed to observe past behavior and make statistical predictions about the future:
* **schedutil**: Uses PELT (Per-Entity Load Tracking) to build a geometric series of past utilization. It takes time to ramp up, and decays slowly when idle.
* **cpuidle**: Looks at recent sleep durations to guess how long the CPU will remain idle, selecting C-states based on that prediction.

This works exceptionally well for bulk compute, web servers, and mobile UI rendering.

## The Repetitive & Predictive Agent
Agentic AI workloads break this model. Agents operate in tight, repetitive, and highly predictive control loops:
1. Fire request (Wait)
2. Process response (Wake -> Compute)
3. Immediately fire next request

The latency bottleneck occurs because the kernel interprets the "Wait" phase as the end of a workload. It decays the PELT signal and drops the CPU into a deep C-state.

When the completion arrives milliseconds later, the kernel reacts to the wakeup as a "new" burst of work. The CPU must physically wake from the C-state, migrate the task, and wait for PELT to slowly ramp the frequency back up.

By the time the CPU is running at full capacity, the fast "Compute" phase is already over, and the agent goes back to sleep.

## The Cost: Latency Amplification
Because the agent and the kernel are completely out of sync, the agent *never* runs at peak CPU performance. 

Instead, the agent continuously pays maximum penalty costs at every iteration. Across thousands of steps in an agentic sequence, this mismatch causes profound **latency amplification**.

## The Solution: A Time-Based Control Signal
To fix this, the kernel needs to stop reacting to the past and start protecting the immediate future.

Agentic workloads require a **time-based control signal**—a moving "active window." When the loop is active, the kernel must hold the core warm (high frequency, shallow C-state) for a short, predefined duration. If the next completion arrives within the window, the agent executes instantly with zero latency penalty. If the window expires, the kernel safely resumes aggressive power management.
