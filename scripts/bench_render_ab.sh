#!/usr/bin/env bash
# Runs a fixed, deterministic SceneViewer flythrough N times and writes one
# chrome://tracing frame trace per run (via frame.trace.output). The camera
# follows a scripted orbit (sceneviewer.camera.scripted) so every run renders an
# identical view sequence, and the run self-terminates after a fixed frame count
# (app.exit_after_frames). Present mode is forced to IMMEDIATE so frame times
# reflect CPU+GPU work rather than the vsync interval, and pacing is disabled.
#
# Every run is checked before it counts: a run whose assets fell back to
# defaults, or whose frames dropped work the frame scratch could not carry, is
# measuring something other than the scene and fails the whole invocation.
# Validation is off by default because it costs CPU time in every build;
# SENCHA_VALIDATION=1 keeps it on for a correctness pass.
#
# Usage:
#   bench_render_ab.sh <app-binary> <content-dir> <out-dir> <runs> <frames> <map>
set -euo pipefail

# Resolve to absolute paths: each run cd's into the content dir, so a relative
# app or output path would no longer resolve from there.
app=$(realpath "$1")
content=$(realpath "$2")
out=$(realpath -m "$3")
runs=$4
frames=$5
map=$6

mkdir -p "$out"

# On a hybrid CPU the same code runs at different clocks and with a different
# cache hierarchy depending on which core class it lands on, so an unpinned run
# reports the scheduler's choices as if they were the renderer's cost. Pin to
# the performance cores, which sysfs names directly on hybrid parts. Override
# with SENCHA_BENCH_CPUS; empty disables pinning.
default_cpus=""
if [ -r /sys/devices/cpu_core/cpus ]; then
    default_cpus=$(cat /sys/devices/cpu_core/cpus)
fi
bench_cpus="${SENCHA_BENCH_CPUS-$default_cpus}"
pin=()
if [ -n "$bench_cpus" ] && command -v taskset >/dev/null 2>&1; then
    pin=(taskset -c "$bench_cpus")
    echo "  pinned to CPUs $bench_cpus"
fi

for i in $(seq 1 "$runs"); do
    run=$(printf 'run_%02d.json' "$i")
    capture=$(printf 'capture_%02d.json' "$i")
    log=$(printf 'run_%02d.log' "$i")
    echo "  run $i/$runs -> $out/$run"
    (
        cd "$content"
        SENCHA_PRESENT_MODE=IMMEDIATE \
        SENCHA_VALIDATION="${SENCHA_VALIDATION:-0}" \
        "${pin[@]}" "$app" \
            +set r.target_fps 0 \
            +set sceneviewer.camera.scripted 1 \
            +set app.exit_after_frames "$frames" \
            +set frame.trace.output "$out/$run" \
            +set render.profile.mode capture \
            +set render.capture.output "$out/$capture" \
            +map "$map" >"$out/$log" 2>&1
    )

    # An unresolved asset silently swaps in a default material or drops a mesh,
    # so the run would report timings for content that is not the scene.
    if grep -qE "failed to resolve texture|neutral default|unsupported version|could not open scene file" "$out/$log"; then
        echo "FAIL: run $i resolved assets to fallbacks; see $out/$log" >&2
        grep -E "failed to resolve texture|neutral default|unsupported version|could not open scene file" "$out/$log" >&2
        exit 1
    fi

    # A frame that dropped its scene is fast for the wrong reason.
    python3 - "$out/$capture" <<'PY' || exit 1
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
frames = data.get("frames", [])
# The map streams in asynchronously and nothing renders until it lands, so the
# early part of a run records nothing at all. A short run can finish with a
# couple of samples, which is not enough to read a percentile off; say so
# instead of reporting one frame as a result.
MIN_FRAMES = 60
if len(frames) < MIN_FRAMES:
    print(f"FAIL: only {len(frames)} frames recorded (need {MIN_FRAMES}); "
          f"raise the frame count so the run outlives map load",
          file=sys.stderr)
    sys.exit(1)
bad = [
    (f["frame_index_count"], f["scratch_alloc_failures_count"],
     f["passes_skipped_count"], f["instances_dropped_count"])
    for f in frames
    if f["scratch_alloc_failures_count"] or f["passes_skipped_count"]
    or f["instances_dropped_count"]
]
if bad:
    print(f"FAIL: {len(bad)} of {len(frames)} frames dropped render work "
          f"(frame, alloc_failures, passes_skipped, instances_dropped): "
          f"{bad[:5]}", file=sys.stderr)
    sys.exit(1)
env = data.get("environment", {})
print(f"    {env.get('gpu_name', '?')} | validation={env.get('validation_enabled', '?')} "
      f"| build={env.get('build_type', '?')} {env.get('build_sha', '?')} "
      f"| map={env.get('map', '?')} | {len(frames)} frames clean")
PY
done
echo "done: $runs runs -> $out"
