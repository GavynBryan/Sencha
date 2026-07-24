#!/usr/bin/env python3
"""Reduces render captures to per-CPU-scope percentiles, one row per capture.

Reads the capture JSON written by render.capture.output. Warmup frames are
dropped because the first frames of a run include map streaming and pipeline
work that no steady-state frame repeats. Percentiles only: a mean hides the
frames that decide whether a budget holds.

Usage: scope_scale_stats.py [--warmup N] [--csv out.csv] capture.json...
"""
import argparse
import json
import os
import statistics
import sys


def percentile(values, q):
    """Nearest-rank percentile, so the reported number is a frame that happened."""
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(round(q / 100.0 * len(ordered) + 0.5)) - 1))
    return ordered[index]


def scope_keys(frames):
    keys = set()
    for frame in frames:
        keys.update(k for k in frame if k.endswith("_cpu_ms"))
    return sorted(keys)


def load(path, warmup):
    with open(path) as handle:
        data = json.load(handle)
    frames = data.get("frames", [])[warmup:]
    env = data.get("environment", {})
    return frames, env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("captures", nargs="+")
    parser.add_argument("--warmup", type=int, default=60)
    parser.add_argument("--csv")
    args = parser.parse_args()

    rows = []
    for path in args.captures:
        frames, env = load(path, args.warmup)
        if not frames:
            print(f"{path}: no frames after {args.warmup} warmup", file=sys.stderr)
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        row = {
            "capture": name,
            "frames": len(frames),
            "gpu": env.get("gpu_name", "?"),
            "build": env.get("build_type", "?"),
        }
        for key in scope_keys(frames):
            # A scope reads negative on frames where it did not run; those are
            # "not measured", which is not the same fact as "measured as free".
            values = [f[key] for f in frames if f.get(key, -1.0) >= 0.0]
            if not values:
                continue
            label = key[: -len("_cpu_ms")]
            row[f"{label}_p50"] = round(statistics.median(values), 4)
            row[f"{label}_p99"] = round(percentile(values, 99), 4)
        for count_key in ("draw_calls_count", "shadow_caster_draws_count",
                          "shadow_casters_tested_count", "shadow_casters_visible_count",
                          "shadow_instance_runs_count", "triangles_count"):
            values = [f[count_key] for f in frames if count_key in f]
            if values:
                row[count_key.replace("_count", "")] = int(statistics.median(values))
        rows.append(row)

    if not rows:
        return 1

    columns = []
    for row in rows:
        for key in row:
            if key not in columns:
                columns.append(key)

    widths = {c: max(len(c), max(len(str(r.get(c, ""))) for r in rows)) for c in columns}
    print("  ".join(c.ljust(widths[c]) for c in columns))
    for row in rows:
        print("  ".join(str(row.get(c, "")).ljust(widths[c]) for c in columns))

    if args.csv:
        with open(args.csv, "w") as handle:
            handle.write(",".join(columns) + "\n")
            for row in rows:
                handle.write(",".join(str(row.get(c, "")) for c in columns) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
