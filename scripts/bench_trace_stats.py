#!/usr/bin/env python3
"""Reduce chrome://tracing frame traces to per-frame CPU-duration statistics.

Each input is a JSON array of trace events (the format frame.trace.output emits).
Per-frame duration is the span between a "Frame N" begin (ph=B) and its matching
end (ph=E), in microseconds. A run's warmup frames (asset load, swapchain
settle) are dropped before statistics.

Usage:
  bench_trace_stats.py --label on  out/on/run_*.json
  bench_trace_stats.py --label off out/off/run_*.json
  bench_trace_stats.py --compare on=out/on/*.json off=out/off/*.json \
      --warmup 400 --csv frames.csv --json summary.json
"""

import argparse
import glob
import json
import math
import statistics
import sys


def frame_durations_us(path):
    with open(path) as f:
        events = json.load(f)
    starts = {}
    durations = []
    for e in events:
        name = e.get("name", "")
        if not name.startswith("Frame "):
            continue
        ph = e.get("ph")
        if ph == "B":
            starts[name] = e["ts"]
        elif ph == "E" and name in starts:
            durations.append(e["ts"] - starts.pop(name))
    return durations


def percentile(sorted_vals, q):
    if not sorted_vals:
        return float("nan")
    idx = q / 100.0 * (len(sorted_vals) - 1)
    lo = math.floor(idx)
    hi = math.ceil(idx)
    if lo == hi:
        return float(sorted_vals[int(idx)])
    frac = idx - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def summarize(values_us):
    v = sorted(values_us)
    ms = [x / 1000.0 for x in v]
    return {
        "frames": len(v),
        "mean_ms": statistics.fmean(ms) if ms else float("nan"),
        "median_ms": percentile(ms, 50),
        "stdev_ms": statistics.pstdev(ms) if len(ms) > 1 else 0.0,
        "p95_ms": percentile(ms, 95),
        "p99_ms": percentile(ms, 99),
        "min_ms": ms[0] if ms else float("nan"),
        "max_ms": ms[-1] if ms else float("nan"),
    }


def collect(paths, warmup):
    per_run = []
    pooled = []
    for p in sorted(paths):
        durs = frame_durations_us(p)[warmup:]
        per_run.append((p, durs))
        pooled.extend(durs)
    return per_run, pooled


def print_arm(label, per_run, pooled):
    print(f"\n== arm: {label} ==")
    print(f"  runs: {len(per_run)}  pooled frames (post-warmup): {len(pooled)}")
    for p, durs in per_run:
        s = summarize(durs)
        print(f"  {p.split('/')[-1]:<16} "
              f"n={s['frames']:<5} mean={s['mean_ms']:.4f}ms "
              f"median={s['median_ms']:.4f}ms p95={s['p95_ms']:.4f}ms "
              f"p99={s['p99_ms']:.4f}ms")
    s = summarize(pooled)
    print(f"  POOLED           n={s['frames']:<5} mean={s['mean_ms']:.4f}ms "
          f"median={s['median_ms']:.4f}ms p95={s['p95_ms']:.4f}ms "
          f"p99={s['p99_ms']:.4f}ms stdev={s['stdev_ms']:.4f}ms")
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="arm")
    ap.add_argument("--warmup", type=int, default=400,
                    help="frames dropped from the start of each run")
    ap.add_argument("--compare", nargs="*", default=None,
                    help="label=glob pairs for an A/B comparison")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("inputs", nargs="*")
    args = ap.parse_args()

    arms = {}
    if args.compare:
        for pair in args.compare:
            label, _, pattern = pair.partition("=")
            arms[label] = glob.glob(pattern)
    else:
        arms[args.label] = args.inputs

    summary = {"warmup": args.warmup, "arms": {}}
    pooled_by_arm = {}
    for label, patterns in arms.items():
        paths = []
        for p in patterns:
            paths.extend(glob.glob(p) if any(c in p for c in "*?[") else [p])
        per_run, pooled = collect(paths, args.warmup)
        s = print_arm(label, per_run, pooled)
        summary["arms"][label] = {
            "pooled": s,
            "runs": [{"path": p, **summarize(d)} for p, d in per_run],
        }
        pooled_by_arm[label] = pooled

        if args.csv:
            mode = "w" if label == list(arms)[0] else "a"
            with open(args.csv, mode) as f:
                if mode == "w":
                    f.write("arm,run,frame,duration_ms\n")
                for p, durs in per_run:
                    run = p.split("/")[-1]
                    for i, d in enumerate(durs):
                        f.write(f"{label},{run},{i},{d / 1000.0:.6f}\n")

    if len(pooled_by_arm) == 2:
        (la, a), (lb, b) = list(pooled_by_arm.items())
        sa, sb = summarize(a), summarize(b)
        d_mean = sb["mean_ms"] - sa["mean_ms"]
        d_median = sb["median_ms"] - sa["median_ms"]
        rel = (d_mean / sa["mean_ms"] * 100.0) if sa["mean_ms"] else float("nan")
        print(f"\n== compare: {lb} - {la} ==")
        print(f"  delta mean:   {d_mean:+.4f} ms ({rel:+.2f}% of {la})")
        print(f"  delta median: {d_median:+.4f} ms")
        summary["compare"] = {
            "baseline": la, "other": lb,
            "delta_mean_ms": d_mean, "delta_median_ms": d_median,
            "delta_mean_pct": rel,
        }

    if args.json:
        with open(args.json, "w") as f:
            json.dump(summary, f, indent=2)
        print(f"\nwrote summary: {args.json}")
    if args.csv:
        print(f"wrote per-frame csv: {args.csv}")


if __name__ == "__main__":
    sys.exit(main())
