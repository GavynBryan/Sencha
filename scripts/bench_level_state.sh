#!/usr/bin/env bash
# Records what keeping level state costs by running LevelStateBench.Generate:
# a zone's capture per saved entity, untouched and changed, and its baseline
# text, for zones of 500 and 5,000 entities with three saved components each,
# and a zone of 5,000 dynamic props whose transforms placement keeps, captured
# untouched, with 100 changed, with every prop changed, and at a detach
# straight after its import, and attached with 500 patched, beside its
# baseline text and capture images; a capture with 2,000 gained members of
# which 20 changed;
# what a patched entity adds to a zone's attach, for 50 and 500 of 500; and what
# an entity the zone gained adds to its capture and its attach, for 50 and 500
# beside 500 authored.
#
# Built through the profile preset, as bench_replication.sh is and for the same
# reason. Pinned to the performance cores by default; override with
# SENCHA_BENCH_CPUS, empty disables pinning.
#
# Usage:
#   bench_level_state.sh [out-json]
#     out-json  where to write the run (default build-profile/bench/level_state.json;
#               a .csv is written beside it)
#
# Environment:
#   SENCHA_LEVEL_STATE_BENCH_REPS  repetition count (default: bench's own)
#   SENCHA_BENCH_CPUS              taskset CPU list, empty to disable pinning
#   SENCHA_SKIP_BUILD              set to reuse an existing build-profile binary
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-$repo/build-profile/bench/level_state.json}")
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
SENCHA_LEVEL_STATE_BENCH_OUT="$out" \
    "${pin[@]}" "$binary" --gtest_filter='LevelStateBench.Generate'
echo
cat "${out%.json}.csv"
