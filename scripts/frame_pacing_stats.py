#!/usr/bin/env python3
"""Reduce render captures (render.capture.output) to frame-pacing statistics.

Per capture: the frame interval distribution (raw_dt: the wall time between
successive frame starts, which is what a player sees as frame rate), the
throughput it implies, how the interval sits against the display period, and
the median of every CPU-side wait and work span, so a slow run says where its
time went. GPU spans are reported when the capture was taken at mode gpu or
above.

Fields a capture does not carry (an older build) print as "-".

Usage:
  frame_pacing_stats.py [--warmup 120] [--refresh-hz 144] [--json out.json] cap.json ...
"""

import argparse
import json
import math
import statistics as st

# (label, capture key) for the per-frame spans worth a median.
SPANS = [
    ("busy", "busy_ms"),
    ("fence", "frame_fence_wait_ms"),
    ("acquire", "acquire_ms"),
    ("img_fence", "image_fence_wait_ms"),
    ("record", "render_record_ms"),
    ("submit", "submit_ms"),
    ("present", "present_ms"),
    ("limiter", "limiter_wait_ms"),
    ("gpu", "Phase_MainColor_gpu_ms"),
    ("pres_lat", "present_latency_ms"),
    ("frame_lat", "frame_latency_ms"),
]


def percentile(sorted_vals, q):
    if not sorted_vals:
        return float("nan")
    idx = q / 100.0 * (len(sorted_vals) - 1)
    lo, hi = math.floor(idx), math.ceil(idx)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] * (hi - idx) + sorted_vals[hi] * (idx - lo)


WAITS = ["frame_fence_wait_ms", "acquire_ms", "image_fence_wait_ms"]


def attach_busy(frames):
    # raw_dt of frame N+1 spans frame N's work and waits plus the limiter hold
    # reported with N+1; what no wait explains is the frame's own CPU time.
    for cur, nxt in zip(frames, frames[1:]):
        if all(k in cur for k in WAITS) and "limiter_wait_ms" in nxt:
            cur["busy_ms"] = nxt["raw_dt_ms"] - nxt["limiter_wait_ms"] - sum(cur[k] for k in WAITS)


def median_of(frames, key):
    vals = [f[key] for f in frames if key in f and f[key] >= 0]
    return st.median(vals) if vals else None


def reduce_file(path, warmup, refresh_hz):
    with open(path) as f:
        data = json.load(f)
    frames = [f for f in data.get("frames", [])[warmup:] if f.get("presented_bool", 1)]
    attach_busy(frames)
    env = data.get("environment", {})
    dts = sorted(f["raw_dt_ms"] for f in frames if f.get("raw_dt_ms", 0) > 0)
    row = {
        "path": path,
        "frames": len(dts),
        "gpu": env.get("gpu_name", "?"),
        "build": env.get("build_type", "?"),
        "refresh_hz": refresh_hz or float(env.get("display_refresh_hz", "0") or 0),
        "present_mode": env.get("present_mode", None),
        "extent": None,
        "images": None,
        "median_ms": percentile(dts, 50),
        "p95_ms": percentile(dts, 95),
        "p99_ms": percentile(dts, 99),
        "max_ms": dts[-1] if dts else float("nan"),
        "fps": 1000.0 * len(dts) / sum(dts) if dts else float("nan"),
        "spans": {label: median_of(frames, key) for label, key in SPANS},
    }
    if frames:
        last = frames[-1]
        row["extent"] = f"{int(last.get('swapchain_width_px', 0))}x{int(last.get('swapchain_height_px', 0))}"
        if "swapchain_image_count" in last:
            row["images"] = int(last["swapchain_image_count"])
        if row["present_mode"] is None and "present_mode_enum" in last:
            row["present_mode"] = {0: "IMMEDIATE", 1: "MAILBOX", 2: "FIFO", 3: "FIFO_RELAXED"}.get(
                int(last["present_mode_enum"]), "?")
    # Share of intervals that sit on one display period, within a quarter of
    # it: the "smooth at refresh" number. Meaningless without a refresh rate.
    if row["refresh_hz"] and dts:
        period = 1000.0 / row["refresh_hz"]
        row["on_one_period_pct"] = 100.0 * sum(
            1 for d in dts if abs(d - period) <= period * 0.25) / len(dts)
        row["periods_median"] = row["median_ms"] / period
    return row


def fmt(v, width=7, prec=2):
    return f"{'-':>{width}}" if v is None or (isinstance(v, float) and math.isnan(v)) \
        else f"{v:>{width}.{prec}f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warmup", type=int, default=120,
                    help="captured frames dropped from the start of each file")
    ap.add_argument("--refresh-hz", type=float, default=0.0,
                    help="display refresh; defaults to the capture's environment")
    ap.add_argument("--json", default=None)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()

    rows = [reduce_file(p, args.warmup, args.refresh_hz) for p in args.files]
    head = (f"{'capture':<28}{'mode':>10}{'img':>4}{'extent':>11}{'n':>6}{'fps':>8}"
            f"{'med':>7}{'p95':>7}{'p99':>7}{'max':>7}{'@1T%':>6}"
            + "".join(f"{label:>10}" for label, _ in SPANS))
    print(head)
    print("-" * len(head))
    for r in rows:
        name = r["path"].split("/")[-1][:27]
        print(f"{name:<28}{str(r['present_mode']):>10}{str(r['images'] or '-'):>4}"
              f"{str(r['extent']):>11}{r['frames']:>6}{fmt(r['fps'], 8, 1)}"
              f"{fmt(r['median_ms'])}{fmt(r['p95_ms'])}{fmt(r['p99_ms'])}{fmt(r['max_ms'])}"
              f"{fmt(r.get('on_one_period_pct'), 6, 0)}"
              + "".join(fmt(r["spans"][label], 10) for label, _ in SPANS))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"warmup": args.warmup, "runs": rows}, f, indent=2)


if __name__ == "__main__":
    main()
