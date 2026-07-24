#!/usr/bin/env python3
"""Compare a streaming bench run against a recorded baseline.

Reads two JSON files emitted by StreamingBench.Generate and reports, per metric,
the change from baseline to candidate. Exits non-zero when a metric regressed
past its tolerance, so a phase gate can run this without reading the numbers.

Tolerances differ by unit because the quantities differ in kind:
  ms      wall clock, noisy between runs; --tolerance (default 10%) applies.
  count   counted work (row migrations, chunk census, cache rebuilds); machine
          independent, so any increase is a regression and any decrease is an
          improvement worth reporting. No tolerance.

A comparison across build configurations is refused rather than reported: a dev
run carries asserts and a debug allocator and is roughly an order of magnitude
slower than a profile run, so the delta would describe the build, not the code.

Usage:
  bench_streaming_compare.py baseline.json candidate.json
  bench_streaming_compare.py baseline.json candidate.json --tolerance 0.15
  bench_streaming_compare.py baseline.json candidate.json --expect-faster import_20000_ms
"""

import argparse
import json
import sys


def load(path):
    with open(path) as handle:
        payload = json.load(handle)
    metrics = {m["name"]: m for m in payload["metrics"]}
    return payload.get("build", "unknown"), metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument(
        "--tolerance",
        type=float,
        default=0.10,
        help="fractional regression allowed on ms metrics (default 0.10)",
    )
    parser.add_argument(
        "--expect-faster",
        action="append",
        default=[],
        metavar="METRIC",
        help="fail unless this metric improved; repeatable",
    )
    parser.add_argument(
        "--allow-build-mismatch",
        action="store_true",
        help="compare runs from different build configurations anyway",
    )
    args = parser.parse_args()

    baseline_build, baseline = load(args.baseline)
    candidate_build, candidate = load(args.candidate)

    if baseline_build != candidate_build and not args.allow_build_mismatch:
        print(
            f"refusing to compare '{baseline_build}' against '{candidate_build}': "
            "the delta would describe the build configuration, not the code "
            "(pass --allow-build-mismatch to override)",
            file=sys.stderr,
        )
        return 2

    regressions = []
    unmet = []
    missing = sorted(set(baseline) - set(candidate))
    added = sorted(set(candidate) - set(baseline))

    name_width = max(len(name) for name in baseline | candidate) if baseline else 0
    print(f"{'metric':<{name_width}}  {'baseline':>12}  {'candidate':>12}  change")
    for name in sorted(baseline.keys() & candidate.keys()):
        before = baseline[name]["value"]
        after = candidate[name]["value"]
        unit = baseline[name]["unit"]

        if before == 0:
            change = "n/a" if after == 0 else "new cost"
            ratio = None
        else:
            ratio = (after - before) / before
            change = f"{ratio:+.1%}"

        fmt = "{:>12.4f}" if unit == "ms" else "{:>12.0f}"
        print(f"{name:<{name_width}}  " + fmt.format(before) + "  " + fmt.format(after) + f"  {change}")

        if unit == "count":
            if after > before:
                regressions.append(f"{name}: {before:.0f} -> {after:.0f} (counted work grew)")
        elif ratio is not None and ratio > args.tolerance:
            regressions.append(
                f"{name}: {before:.4f} -> {after:.4f} ms ({ratio:+.1%} > {args.tolerance:.0%})"
            )
        elif before == 0 and after > 0:
            regressions.append(f"{name}: 0 -> {after} (new cost where baseline had none)")

        if name in args.expect_faster and not (after < before):
            unmet.append(f"{name}: expected improvement, got {before} -> {after}")

    for name in args.expect_faster:
        if name not in baseline or name not in candidate:
            unmet.append(f"{name}: not present in both runs")

    if missing:
        print("\nmetrics in baseline but not candidate:", ", ".join(missing))
    if added:
        print("metrics new in candidate:", ", ".join(added))

    if regressions:
        print("\nREGRESSED:", file=sys.stderr)
        for entry in regressions:
            print(f"  {entry}", file=sys.stderr)
    if unmet:
        print("\nEXPECTATION NOT MET:", file=sys.stderr)
        for entry in unmet:
            print(f"  {entry}", file=sys.stderr)

    if regressions or unmet:
        return 1
    print("\nno regressions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
