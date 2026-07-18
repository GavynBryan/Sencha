#!/usr/bin/env python3
"""Reduce render-capture JSON envelopes (from render.capture.output) to a per-run
table of forward-shading cost and light counters, for the light-cost measurement.

Per file: MainColor GPU ms distribution (median/p95/p99, warmup dropped) plus the
median of the light/draw counters. Prints a table and, with --json, writes a
machine-readable summary.

Usage:
  capture_stats.py --warmup 400 [--json summary.json] lights_16.json lights_32.json ...
"""

import argparse
import json
import statistics as st


def percentile(vals, q):
    s = sorted(vals)
    if not s:
        return float("nan")
    i = int(round(q / 100.0 * (len(s) - 1)))
    return s[i]


def median_int(frames, key):
    vals = [f[key] for f in frames if key in f]
    return int(st.median(vals)) if vals else 0


def reduce_file(path, warmup):
    d = json.load(open(path))
    frames = d.get("frames", [])[warmup:]
    mc = [f["Phase_MainColor_gpu_ms"] for f in frames
          if f.get("Phase_MainColor_gpu_ms", -1) >= 0]
    fo = [f["Forward_Opaque_gpu_ms"] for f in frames
          if f.get("Forward_Opaque_gpu_ms", -1) >= 0]
    return {
        "path": path,
        "frames": len(mc),
        "lights_visible": median_int(frames, "lights_visible_count"),
        "lights_dropped": median_int(frames, "lights_dropped_at_cap_count"),
        "visible_objects": median_int(frames, "visible_objects_count"),
        "draw_calls": median_int(frames, "draw_calls_count"),
        "triangles": median_int(frames, "submitted_triangles_count"),
        "main_color_median_ms": st.median(mc) if mc else float("nan"),
        "main_color_p95_ms": percentile(mc, 95),
        "main_color_p99_ms": percentile(mc, 99),
        "forward_opaque_median_ms": st.median(fo) if fo else float("nan"),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--warmup", type=int, default=400)
    ap.add_argument("--json", default=None)
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()

    rows = [reduce_file(p, args.warmup) for p in args.files]
    rows.sort(key=lambda r: r["lights_visible"])

    hdr = f"{'file':<22}{'lights':>7}{'dropped':>8}{'objs':>6}{'tris':>7}" \
          f"{'MainColor med':>15}{'p95':>9}{'p99':>9}"
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        name = r["path"].split("/")[-1]
        print(f"{name:<22}{r['lights_visible']:>7}{r['lights_dropped']:>8}"
              f"{r['visible_objects']:>6}{r['triangles']:>7}"
              f"{r['main_color_median_ms']:>13.3f}ms{r['main_color_p95_ms']:>8.3f}"
              f"{r['main_color_p99_ms']:>9.3f}")

    # Per-light slope from the two extreme light counts (rough linear estimate).
    if len(rows) >= 2 and rows[-1]["lights_visible"] != rows[0]["lights_visible"]:
        dn = rows[-1]["lights_visible"] - rows[0]["lights_visible"]
        dms = rows[-1]["main_color_median_ms"] - rows[0]["main_color_median_ms"]
        print(f"\nper-light slope (median): {dms / dn * 1000:.1f} us/light "
              f"over {rows[0]['lights_visible']}..{rows[-1]['lights_visible']} lights")

    if args.json:
        json.dump({"warmup": args.warmup, "runs": rows}, open(args.json, "w"), indent=2)
        print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
