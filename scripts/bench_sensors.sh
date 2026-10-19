#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="${SENCHA_SENSOR_BUILD_DIR:-$repo/build-profile}"
out=$(realpath -m "${1:-$build_dir/bench/sensors.json}")
mkdir -p "$(dirname "$out")"
if [ -z "${SENCHA_SKIP_BUILD:-}" ]; then
    cmake --preset profile -S "$repo" -B "$build_dir"
    cmake --build "$build_dir" --target core_tests --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
fi

pin=()
if [ -n "${SENCHA_BENCH_CPUS:-}" ]; then
    pin=(taskset -c "$SENCHA_BENCH_CPUS")
fi
SENCHA_SENSOR_BENCH_OUT="$out" \
    "${pin[@]}" "$build_dir/test/core_tests" --gtest_filter=SensorObservationBench.Generate
