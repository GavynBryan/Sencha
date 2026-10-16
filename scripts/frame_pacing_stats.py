#!/usr/bin/env python3
"""Reduce render captures (render.capture.output) to frame-pacing statistics.

Per capture: the interval between presented frames (the wall time between the
starts of successive frames that presented, which is what a player sees as
frame rate), the throughput it implies, how the interval sits against the
display period, the long-then-short interval pairs that read as microstutter,
the frames that did not present, the most presents outstanding (an upper
bound), late present-wait completions, the worst frame latency estimate, and
the median of every CPU-side wait and work span, so a slow run says where its
time went. GPU spans are reported when the capture was taken at mode gpu or
above. --series prints one row per window of a long session instead.

Fields a capture does not carry (an older build) print as "-".

Usage:
  frame_pacing_stats.py [--warmup 120] [--refresh-hz 144] [--pool] [--json out.json] cap.json ...
  frame_pacing_stats.py --series 30 cap.json
"""

import argparse
import json
import math
import re
import statistics as st

# (label, capture key) for the per-frame spans worth a median.
SPANS = [
    ("busy", "busy_ms"),
    ("slot", "frame_slot_wait_ms"),
    ("acquire", "acquire_wait_ms"),
    ("img_fence", "image_fence_wait_ms"),
    ("record", "render_record_ms"),
    ("submit", "submit_ms"),
    ("present", "present_ms"),
    ("limiter", "limiter_wait_ms"),
    ("gpu", "Phase_MainColor_gpu_ms"),
    ("pres_lat", "present_latency_estimate_ms"),
    ("frame_lat", "frame_latency_estimate_ms"),
]


def percentile(sorted_vals, q):
    if not sorted_vals:
        return float("nan")
    idx = q / 100.0 * (len(sorted_vals) - 1)
    lo, hi = math.floor(idx), math.ceil(idx)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] * (hi - idx) + sorted_vals[hi] * (idx - lo)


WAITS = ["frame_slot_wait_ms", "acquire_wait_ms", "image_fence_wait_ms"]


def attach_busy(frames):
    # raw_dt of frame N+1 spans frame N's work and waits plus the limiter hold
    # reported with N+1; what no wait explains is the frame's own CPU time.
    for cur, nxt in zip(frames, frames[1:]):
        if all(k in cur for k in WAITS) and "limiter_wait_ms" in nxt:
            cur["busy_ms"] = nxt["raw_dt_ms"] - nxt["limiter_wait_ms"] - sum(cur[k] for k in WAITS)


def median_of(frames, key):
    vals = [f[key] for f in frames if key in f and f[key] >= 0]
    return st.median(vals) if vals else None


def load_frames(path, warmup):
    """Every frame after the warm-up. A presented frame's presented_dt_ms spans
    back to the previous presented frame's start, through any frame between
    them that did not present (a skipped acquire, a minimized window)."""
    with open(path) as f:
        data = json.load(f)
    frames = data.get("frames", [])[warmup:]
    attach_busy(frames)
    since_presented = 0.0
    for f in frames:
        since_presented += f.get("raw_dt_ms", 0)
        if f.get("presented_bool", 1):
            f["presented_dt_ms"] = since_presented
            since_presented = 0.0
    return frames, data.get("environment", {})


def reduce_frames(path, all_frames, env, refresh_hz):
    frames = [f for f in all_frames if "presented_dt_ms" in f]
    dts = sorted(f["presented_dt_ms"] for f in frames if f["presented_dt_ms"] > 0)
    row = {
        "path": path,
        "frames": len(dts),
        "not_presented": len(all_frames) - len(frames),
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
        # Fixed ticks per second of wall time: the simulation rate, which must
        # not move with the frame rate.
        "ticks_per_s": 1000.0 * sum(f.get("fixed_ticks_count", 0) for f in all_frames)
                       / sum(f.get("raw_dt_ms", 0) for f in all_frames) if all_frames else float("nan"),
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
        # A frame shown late, then one rushed in behind it: the 2-then-0
        # refresh pair a display queue that starves and refills produces.
        ordered = [f["presented_dt_ms"] for f in frames if f["presented_dt_ms"] > 0]
        row["long_short_pct"] = 100.0 * sum(
            1 for a, b in zip(ordered, ordered[1:]) if a >= 1.5 * period and b <= 0.5 * period
        ) / max(len(ordered) - 1, 1)
    outstanding = [f["presents_outstanding_max"] for f in frames if f.get("presents_outstanding_max", -1) >= 0]
    row["outstanding_max"] = max(outstanding) if outstanding else None
    row["late_waits"] = sum(f.get("present_wait_late_count", 0) for f in frames) \
        if any("present_wait_late_count" in f for f in frames) else None
    lat = sorted(f["frame_latency_estimate_ms"] for f in frames if f.get("frame_latency_estimate_ms", -1) >= 0)
    row["frame_lat_p99"] = percentile(lat, 99) if lat else None
    return row


def fmt(v, width=7, prec=2):
    return f"{'-':>{width}}" if v is None or (isinstance(v, float) and math.isnan(v)) \
        else f"{v:>{width}.{prec}f}"


def print_series(path, warmup, window_s, refresh_hz):
    frames, env = load_frames(path, warmup)
    print(f"{'t_s':>6}{'n':>7}{'skip':>6}{'fps':>8}{'p99':>7}{'max':>7}{'@1T%':>6}{'out':>5}{'late':>6}"
          f"{'acq':>7}{'pres_lat':>9}{'frame_lat':>10}{'lat_p99':>8}")
    start, elapsed, bucket = 0.0, 0.0, []
    for f in frames:
        bucket.append(f)
        elapsed += f.get("raw_dt_ms", 0) / 1000.0
        if elapsed - start >= window_s or f is frames[-1]:
            r = reduce_frames(path, bucket, env, refresh_hz)
            print(f"{start:>6.0f}{r['frames']:>7}{r['not_presented']:>6}{fmt(r['fps'], 8, 1)}{fmt(r['p99_ms'])}"
                  f"{fmt(r['max_ms'])}{fmt(r.get('on_one_period_pct'), 6, 0)}{fmt(r['outstanding_max'], 5, 0)}"
                  f"{fmt(r['late_waits'], 6, 0)}"
                  f"{fmt(r['spans']['acquire'])}{fmt(r['spans']['pres_lat'], 9)}"
                  f"{fmt(r['spans']['frame_lat'], 10)}{fmt(r['frame_lat_p99'], 8)}")
            start, bucket = elapsed, []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warmup", type=int, default=120,
                    help="captured frames dropped from the start of each file")
    ap.add_argument("--refresh-hz", type=float, default=0.0,
                    help="display refresh; defaults to the capture's environment")
    ap.add_argument("--json", default=None)
    ap.add_argument("--pool", action="store_true",
                    help="pool repeated runs (names ending _rN) into one row per cell")
    ap.add_argument("--series", type=float, default=0.0,
                    help="seconds per row: one capture as a time series")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    if args.series:
        for p in args.files:
            print_series(p, args.warmup, args.series, args.refresh_hz)
        return

    cells = {}
    for p in sorted(args.files):
        frames, env = load_frames(p, args.warmup)
        key = re.sub(r"_r\d+(\.json)?$", "", p) if args.pool else p
        cell = cells.setdefault(key, ([], env))
        cell[0].extend(frames)
    rows = [reduce_frames(key, frames, env, args.refresh_hz) for key, (frames, env) in cells.items()]
    head = (f"{'capture':<28}{'mode':>10}{'img':>4}{'extent':>11}{'n':>6}{'skip':>6}{'fps':>8}"
            f"{'med':>7}{'p95':>7}{'p99':>7}{'max':>7}{'@1T%':>6}{'LS%':>6}{'tick/s':>7}{'out':>5}{'late':>6}"
            f"{'lat_p99':>8}"
            + "".join(f"{label:>10}" for label, _ in SPANS))
    print(head)
    print("-" * len(head))
    for r in rows:
        name = r["path"].split("/")[-1][:27]
        print(f"{name:<28}{str(r['present_mode']):>10}{str(r['images'] or '-'):>4}"
              f"{str(r['extent']):>11}{r['frames']:>6}{r['not_presented']:>6}{fmt(r['fps'], 8, 1)}"
              f"{fmt(r['median_ms'])}{fmt(r['p95_ms'])}{fmt(r['p99_ms'])}{fmt(r['max_ms'])}"
              f"{fmt(r.get('on_one_period_pct'), 6, 0)}{fmt(r.get('long_short_pct'), 6, 1)}"
              f"{fmt(r['ticks_per_s'], 7, 1)}{fmt(r['outstanding_max'], 5, 0)}{fmt(r['late_waits'], 6, 0)}"
              f"{fmt(r['frame_lat_p99'], 8)}"
              + "".join(fmt(r["spans"][label], 10) for label, _ in SPANS))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"warmup": args.warmup, "runs": rows}, f, indent=2)


if __name__ == "__main__":
    main()
