#!/usr/bin/env bash
# Records the animation runtime's measurements by running AnimBench.Generate:
# rig binding per tier, the headless server tick over cosmetic props with and
# without the participation skip, and the pose pass serially and across workers;
# then AnimationPreviewBench.Generate: replaying the editor preview and running
# the fixture project's scenario batch, written beside it as <out>_preview.json.
#
# The bench is built through the profile preset, not dev: the dev preset keeps
# asserts and a debug allocator, which makes zone import roughly an order of
# magnitude slower and describes a build nobody ships. The emitted JSON records
# which configuration produced it.
#
# On a hybrid CPU the same code runs at different clocks depending on which core
# class it lands on, so an unpinned run reports the scheduler's choices as if
# they were the engine's cost. Pinned to the performance cores by default;
# override with SENCHA_BENCH_CPUS, empty disables pinning.
#
# Usage:
#   bench_animation.sh [out-json]
#     out-json  where to write the run (default build-profile/bench/animation.json;
#               a .csv is written beside it)
#
# Environment:
#   SENCHA_ANIM_BENCH_REPS       repetition count (default: bench's own)
#   SENCHA_BENCH_CPUS            taskset CPU list, empty to disable pinning
#   SENCHA_SKIP_BUILD            set to reuse an existing build-profile binary
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${1:-$repo/build-profile/bench/animation.json}")
mkdir -p "$(dirname "$out")"

binary="$repo/build-profile/test/runtime_tests"
editor_binary="$repo/build-profile/test/editor_tests"
preview_out="${out%.json}_preview.json"

if [ -z "${SENCHA_SKIP_BUILD:-}" ]; then
    echo "building profile preset (release codegen + symbols)"
    cmake --preset profile >/dev/null
    cmake --build --preset profile --target runtime_tests editor_tests --parallel
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
SENCHA_ANIM_BENCH_OUT="$out" \
    "${pin[@]}" "$binary" --gtest_filter='AnimBench.Generate'
echo "recording -> $preview_out"
SENCHA_ANIMATION_PREVIEW_BENCH_OUT="$preview_out" \
    "${pin[@]}" "$editor_binary" --gtest_filter='AnimationPreviewBench.Generate'

echo
echo "recorded metrics:"
python3 - "$out" "$preview_out" <<'PY'
import json, sys
metrics = []
for path in sys.argv[1:]:
    with open(path) as handle:
        payload = json.load(handle)
    print(f"  {path}: build {payload['build']}")
    metrics += payload["metrics"]
width = max(len(m["name"]) for m in metrics)
for metric in metrics:
    value = metric["value"]
    shown = f"{value:.4f}" if metric["unit"] == "ms" else f"{value:.0f}"
    print(f"  {metric['name']:<{width}}  {shown} {metric['unit']}")
PY
