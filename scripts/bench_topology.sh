#!/usr/bin/env bash
# Records how the partition's manifest-level mechanisms scale with world size.
#
# Builds the profile preset, pins the run to a performance core, and writes a
# schema-1 metrics document plus a CSV beside it. Compare two runs with
# scripts/bench_streaming_compare.py, which reads the same schema.
#
#   scripts/bench_topology.sh [out-json]
#
# Environment:
#   SENCHA_SKIP_BUILD=1            use the existing build-profile binary
#   SENCHA_TOPOLOGY_BENCH_REPS=N   override repetition counts
#   SENCHA_TOPOLOGY_BENCH_ONLY=P   restrict to metrics whose name starts with P
#   SENCHA_BENCH_CPUS=list         taskset list; empty disables pinning
#
# The machine must be otherwise idle. Core pinning does not protect against a
# neighbouring process evicting shared cache.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

out="${1:-build-profile/bench/topology.json}"

if [[ "${SENCHA_SKIP_BUILD:-0}" != "1" ]]; then
    cmake --preset profile >/dev/null
    cmake --build --preset profile --target runtime_tests --parallel
fi

binary="build-profile/test/runtime_tests"
if [[ ! -x "$binary" ]]; then
    echo "missing $binary; build the profile preset first" >&2
    exit 1
fi

mkdir -p "$(dirname "$out")"

cpus="${SENCHA_BENCH_CPUS-}"
if [[ -z "${SENCHA_BENCH_CPUS+x}" ]] && [[ -r /sys/devices/cpu_core/cpus ]]; then
    cpus="$(cat /sys/devices/cpu_core/cpus)"
fi

pin=()
if [[ -n "$cpus" ]]; then
    pin=(taskset -c "$cpus")
    echo "pinned to cpus $cpus"
fi

SENCHA_TOPOLOGY_BENCH_OUT="$out" \
    "${pin[@]}" "$binary" --gtest_filter='TopologyBench.Generate'

echo
echo "wrote $out"
python3 - "$out" <<'PY'
import json, sys
with open(sys.argv[1]) as handle:
    document = json.load(handle)
print(f"build: {document['build']}  metrics: {len(document['metrics'])}")
width = max(len(m["name"]) for m in document["metrics"])
for metric in document["metrics"]:
    print(f"  {metric['name']:<{width}}  {metric['value']:>12.6f} {metric['unit']}")
PY
