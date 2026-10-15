#!/usr/bin/env bash
# Records what the ability kit costs by running AbilityBench.Generate: the bytes
# each actor carries, the ability pass per tick over idle and firing actors, and
# presenting them.
#
# Built through the profile preset, as bench_animation.sh is and for the same
# reason: the dev preset keeps asserts and a debug allocator, which describes a
# build nobody ships. Pinned to the performance cores by default; override with
# SENCHA_BENCH_CPUS, empty disables pinning.
#
# Usage:
#   bench_abilities.sh [out-json]
#     out-json  where to write the run (default build-profile/bench/abilities.json;
#               a .csv is written beside it)
#
# Environment:
#   SENCHA_ABILITY_BENCH_REPS    repetition count (default: bench's own)
#   SENCHA_BENCH_CPUS            taskset CPU list, empty to disable pinning
#   SENCHA_SKIP_BUILD            set to reuse an existing build-profile binary
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-$repo/build-profile/bench/abilities.json}")
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
SENCHA_ABILITY_BENCH_OUT="$out" \
    "${pin[@]}" "$binary" --gtest_filter='AbilityBench.Generate'
echo
cat "${out%.json}.csv"
