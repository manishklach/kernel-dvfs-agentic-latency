#!/usr/bin/env python3
"""Correlate simple line-oriented bpftrace logs for the agent latency study."""

from __future__ import annotations

import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


KEYVAL_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")


@dataclass
class WakeEvent:
    ts_ns: int
    pid: Optional[int]
    cpu: Optional[int]
    wakeup_to_run_us: Optional[float]


@dataclass
class FreqEvent:
    ts_ns: int
    cpu: Optional[int]
    freq: Optional[int]


@dataclass
class IdleEvent:
    ts_ns: int
    cpu: Optional[int]
    state: Optional[int]


def parse_keyvals(line: str) -> Dict[str, str]:
    return {key: value for key, value in KEYVAL_RE.findall(line)}


def maybe_int(value: Optional[str]) -> Optional[int]:
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def percentile(values: List[float], pct: float) -> Optional[float]:
    if not values:
        return None
    if len(values) == 1:
        return values[0]
    rank = (len(values) - 1) * pct
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return values[low]
    weight = rank - low
    return values[low] * (1.0 - weight) + values[high] * weight


def load_lines(path: Optional[Path]) -> Iterable[str]:
    if path is None:
        return []
    with path.open("r", encoding="utf-8") as handle:
        return list(handle)


def load_wake_events(path: Optional[Path]) -> List[WakeEvent]:
    events: List[WakeEvent] = []
    for raw in load_lines(path):
        line = raw.strip()
        if not line.startswith("WAKE "):
            continue
        kv = parse_keyvals(line)
        ts_ns = maybe_int(kv.get("ts_ns"))
        if ts_ns is None:
            continue
        wake_us = maybe_int(kv.get("wakeup_to_run_us"))
        events.append(
            WakeEvent(
                ts_ns=ts_ns,
                pid=maybe_int(kv.get("pid")),
                cpu=maybe_int(kv.get("cpu")),
                wakeup_to_run_us=float(wake_us) if wake_us is not None else None,
            )
        )
    return events


def load_freq_events(path: Optional[Path]) -> List[FreqEvent]:
    events: List[FreqEvent] = []
    for raw in load_lines(path):
        line = raw.strip()
        if not line.startswith("FREQ "):
            continue
        kv = parse_keyvals(line)
        ts_ns = maybe_int(kv.get("ts_ns"))
        if ts_ns is None:
            continue
        events.append(
            FreqEvent(
                ts_ns=ts_ns,
                cpu=maybe_int(kv.get("cpu")),
                freq=maybe_int(kv.get("freq")),
            )
        )
    return events


def load_idle_events(path: Optional[Path]) -> List[IdleEvent]:
    events: List[IdleEvent] = []
    for raw in load_lines(path):
        line = raw.strip()
        if not line.startswith("IDLE "):
            continue
        kv = parse_keyvals(line)
        ts_ns = maybe_int(kv.get("ts_ns"))
        if ts_ns is None:
            continue
        events.append(
            IdleEvent(
                ts_ns=ts_ns,
                cpu=maybe_int(kv.get("cpu")),
                state=maybe_int(kv.get("state")),
            )
        )
    return events


def next_freq_after(events: List[FreqEvent], ts_ns: int, cpu: Optional[int], window_ns: int) -> Optional[FreqEvent]:
    for event in events:
        if event.ts_ns < ts_ns:
            continue
        if event.ts_ns - ts_ns > window_ns:
            break
        if cpu is None or event.cpu is None or event.cpu == cpu:
            return event
    return None


def idle_near_wakeup(events: List[IdleEvent], ts_ns: int, cpu: Optional[int], window_ns: int) -> bool:
    lower = ts_ns - window_ns
    upper = ts_ns + window_ns
    for event in events:
        if event.ts_ns < lower:
            continue
        if event.ts_ns > upper:
            break
        if cpu is None or event.cpu is None or event.cpu == cpu:
            if event.state is not None:
                return True
    return False


def summarize(label: str, values: List[float]) -> None:
    if not values:
        print(f"{label}: no data")
        return
    values = sorted(values)
    print(
        f"{label}: count={len(values)} "
        f"p50={percentile(values, 0.50):.2f} "
        f"p95={percentile(values, 0.95):.2f} "
        f"p99={percentile(values, 0.99):.2f}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Correlate wakeup, CPU frequency, and idle logs from bpftrace."
    )
    parser.add_argument("--wakeup", type=Path, help="Path to wakeup_to_run.bt output")
    parser.add_argument("--frequency", type=Path, help="Path to cpu_frequency.bt output")
    parser.add_argument("--idle", type=Path, help="Path to cpu_idle.bt output")
    parser.add_argument(
        "--ramp-window-us",
        type=int,
        default=1000,
        help="Look for a frequency event within this many microseconds after wakeup",
    )
    parser.add_argument(
        "--idle-window-us",
        type=int,
        default=250,
        help="Look this many microseconds around wakeup for a nearby idle transition",
    )
    args = parser.parse_args()

    wake_events = load_wake_events(args.wakeup)
    freq_events = load_freq_events(args.frequency)
    idle_events = load_idle_events(args.idle)

    freq_events.sort(key=lambda event: event.ts_ns)
    idle_events.sort(key=lambda event: event.ts_ns)

    ramp_window_ns = args.ramp_window_us * 1000
    idle_window_ns = args.idle_window_us * 1000

    wakeup_to_run_samples: List[float] = []
    ramp_samples: List[float] = []
    idle_hits = 0

    for wake in wake_events:
        if wake.wakeup_to_run_us is not None:
            wakeup_to_run_samples.append(wake.wakeup_to_run_us)

        freq_event = next_freq_after(freq_events, wake.ts_ns, wake.cpu, ramp_window_ns)
        ramp_us: Optional[float] = None
        if freq_event is not None:
            ramp_us = (freq_event.ts_ns - wake.ts_ns) / 1000.0
            ramp_samples.append(ramp_us)

        idle_hit = idle_near_wakeup(idle_events, wake.ts_ns, wake.cpu, idle_window_ns)
        if idle_hit:
            idle_hits += 1

        fields = [
            f"wake_ts_ns={wake.ts_ns}",
            f"cpu={wake.cpu if wake.cpu is not None else 'na'}",
            f"pid={wake.pid if wake.pid is not None else 'na'}",
            f"wakeup_to_run_us={wake.wakeup_to_run_us if wake.wakeup_to_run_us is not None else 'na'}",
            f"frequency_ramp_after_wakeup_us={ramp_us if ramp_us is not None else 'na'}",
            f"idle_exit_near_wakeup={'yes' if idle_hit else 'no'}",
        ]
        print("CORRELATED " + " ".join(fields))

    print()
    summarize("wakeup_to_run_us", wakeup_to_run_samples)
    summarize("frequency_ramp_after_wakeup_us", ramp_samples)
    print(f"idle_exit_near_wakeup: hits={idle_hits} total_wake_events={len(wake_events)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
