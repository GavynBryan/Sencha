#!/usr/bin/env bash
# Records the unified World's streaming and iteration measurements by running
# StreamingBench.Generate, and prints the numbers that gate the hardening work.
#
# The bench is built through the profile preset, not dev: the dev preset keeps
# asserts and a debug allocator, which makes zone import roughly an order of
# magnitude slower and describes a build nobody ships. The emitted JSON records
# which configuration produced it, and bench_streaming_compare.py refuses to
# compare across configurations.
#
# On a hybrid CPU the same code runs at different clocks depending on which core
# class it lands on, so an unpinned run reports the scheduler's choices as if
# they were the engine's cost. Pinned to the performance cores by default;
# override with SENCHA_BENCH_CPUS, empty disables pinning.
#
# Usage:
#   bench_streaming.sh [out-json]
#     out-json  where to write the run (default build-profile/bench/streaming.json;
#               a .csv is written beside it)
#
# Environment:
#   SENCHA_STREAMING_BENCH_REPS  repetition count (default: bench's own)
#   SENCHA_BENCH_CPUS            taskset CPU list, empty to disable pinning
#   SENCHA_SKIP_BUILD            set to reuse an existing build-profile binary
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-$repo/build-profile/bench/streaming.json}")
mkdir -p "$(dirname "$out")"

binary="$repo/build-profile/test/runtime_tests"

if [ -z "${SENCHA_SKIP_BUILD:-}" ]; then
    echo "building profile preset (release codegen + symbols)"
    cmake --preset profile >/dev/null
    cmake --build --preset profile --target runtime_tests --parallel
fi

if [ ! -x "$binary" ]; then
    echo "error: $binary not found; run without SENCHA_SKIP_BUILD" >&2
    exit 1
fi

default_cpus=""
if [ -r /sys/devices/cpu_core/cpus ]; then
    default_cpus=$(cat /sys/devices/cpu_core/cpus)
fi
bench_cpus="${SENCHA_BENCH_CPUS-$default_cpus}"
pin=()
if [ -n "$bench_cpus" ] && command -v taskset >/dev/null 2>&1; then
    pin=(taskset -c "$bench_cpus")
    echo "pinned to CPUs $bench_cpus"
fi

echo "recording -> $out"
SENCHA_STREAMING_BENCH_OUT="$out" \
    "${pin[@]}" "$binary" --gtest_filter='StreamingBench.Generate'

echo
echo "recorded metrics:"
python3 - "$out" <<'PY'
import json, sys
with open(sys.argv[1]) as handle:
    payload = json.load(handle)
print(f"  build: {payload['build']}")
width = max(len(m["name"]) for m in payload["metrics"])
for metric in payload["metrics"]:
    value = metric["value"]
    shown = f"{value:.4f}" if metric["unit"] == "ms" else f"{value:.0f}"
    print(f"  {metric['name']:<{width}}  {shown} {metric['unit']}")
PY
