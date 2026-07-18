#!/usr/bin/env bash
# Runs a fixed, deterministic SceneViewer flythrough N times and writes one
# chrome://tracing frame trace per run (via frame.trace.output). The camera
# follows a scripted orbit (sceneviewer.camera.scripted) so every run renders an
# identical view sequence, and the run self-terminates after a fixed frame count
# (app.exit_after_frames). Present mode is forced to IMMEDIATE so frame times
# reflect CPU+GPU work rather than the vsync interval, and pacing is disabled.
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
for i in $(seq 1 "$runs"); do
    run=$(printf 'run_%02d.json' "$i")
    echo "  run $i/$runs -> $out/$run"
    (
        cd "$content"
        SENCHA_PRESENT_MODE=IMMEDIATE "$app" \
            +set r.target_fps 0 \
            +set sceneviewer.camera.scripted 1 \
            +set app.exit_after_frames "$frames" \
            +set frame.trace.output "$out/$run" \
            +map "$map" >/dev/null 2>&1
    )
done
echo "done: $runs runs -> $out"
