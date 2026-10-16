#!/usr/bin/env bash
# Runs a frame-pacing matrix against one host build and writes one render
# capture per cell, then reduces them with frame_pacing_stats.py.
#
# Each cell is a present mode x window size x frame-rate limit x adapter, run
# `runs` times. A cell launches the host from the project directory exactly as
# the editor's Play does (--game <module> +map <map>), with saved settings
# ignored so the cell, not a previous session, decides the window and limits.
# Pacing depends on everything else the machine is doing, so every run records
# the load average and the number of running compilers beside its capture, and
# the script waits for a quiet machine before each run (SENCHA_BENCH_MAX_LOAD,
# default 3; empty disables the wait).
#
# Windowed runs need the display: run this in the foreground of a desktop
# session that is not locked.
#
# Usage:
#   bench_frame_pacing.sh <bin-dir> <project-dir> <map> <out-dir> [runs] [frames]
#
#   bin-dir      holds app, game.so and libsencha_engine.so (a build tree's
#                copies, so a measured build cannot change under the run)
#   project-dir  the game project; its cooked content must exist
#
# Matrix axes (space-separated environment lists):
#   SENCHA_BENCH_MODES   present modes            default "FIFO IMMEDIATE"
#   SENCHA_BENCH_SIZES   1280x720, 1920x1080, fullscreen  default "1280x720"
#   SENCHA_BENCH_LIMITS  r.target_fps values      default "0"
#   SENCHA_BENCH_DEVICES adapter indices, "auto" for scoring  default "auto"
#
# SENCHA_BENCH_ARGS adds host arguments to every run, for example
# "+set app.frame_delay_ms 40 +set app.frame_delay_period 144" to inject a
# hitch once a second.
set -euo pipefail

bin=$(realpath "$1")
project=$(realpath "$2")
map=$3
out=$(realpath -m "$4")
runs=${5:-3}
frames=${6:-900}

modes=${SENCHA_BENCH_MODES:-"FIFO IMMEDIATE"}
sizes=${SENCHA_BENCH_SIZES:-"1280x720"}
limits=${SENCHA_BENCH_LIMITS:-"0"}
devices=${SENCHA_BENCH_DEVICES:-"auto"}
read -r -a extra_args <<<"${SENCHA_BENCH_ARGS:-}"
# Long sessions get time for their frames at 30 fps or slower.
run_timeout=$(( frames / 30 > 120 ? frames / 30 + 60 : 180 ))
max_load=${SENCHA_BENCH_MAX_LOAD-3}

mkdir -p "$out"
manifest="$out/manifest.tsv"
[ -f "$manifest" ] || printf 'capture\tmode\tsize\tlimit\tdevice\trun\tload_before\tcompilers_before\tload_after\tcompilers_after\texit\n' >"$manifest"

# By process name: a full-command-line match would count any shell whose
# command mentions a compiler, this script's caller included.
compilers() { pgrep -c -x 'cc1plus|cc1|clang|clang\+\+|rustc' || true; }
load1() { cut -d' ' -f1 /proc/loadavg; }

wait_for_quiet() {
    [ -z "$max_load" ] && return
    while [ "$(compilers)" != "0" ] || awk -v l="$(load1)" -v m="$max_load" 'BEGIN{exit !(l >= m)}'; do
        sleep 5
    done
}

for mode in $modes; do
for size in $sizes; do
for limit in $limits; do
for device in $devices; do
for run in $(seq 1 "$runs"); do
    name="${mode}_${size}_fps${limit}_dev${device}_r${run}"
    window=()
    size_env=()
    if [ "$size" = fullscreen ]; then
        window=(+set window.mode fullscreen)
    else
        size_env=(SENCHA_WINDOW_SIZE="$size")
    fi
    device_env=()
    [ "$device" != auto ] && device_env=(SENCHA_DEVICE_INDEX="$device")

    wait_for_quiet
    before_load=$(load1); before_cc=$(compilers)
    status=0
    (
        cd "$project"
        env SENCHA_PRESENT_MODE="$mode" SENCHA_VALIDATION="${SENCHA_VALIDATION:-0}" \
            "${size_env[@]}" "${device_env[@]}" LD_LIBRARY_PATH="$bin" \
            timeout "$run_timeout" "$bin/app" --game "$bin/game.so" --settings none \
            +set app.exit_after_frames "$frames" \
            +set r.target_fps "$limit" \
            "${window[@]}" "${extra_args[@]}" \
            +set render.profile.mode capture \
            +set render.capture.output "$out/$name.json" \
            +map "$map" >"$out/$name.log" 2>&1
    ) || status=$?
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name.json" "$mode" "$size" "$limit" "$device" "$run" \
        "$before_load" "$before_cc" "$(load1)" "$(compilers)" "$status" >>"$manifest"
    echo "  $name exit=$status load=$before_load->$(load1)"
done; done; done; done; done

# Captures from builds that predate the refresh rate in their environment need
# it named: SENCHA_BENCH_REFRESH_HZ.
python3 "$(dirname "$0")/frame_pacing_stats.py" ${SENCHA_BENCH_REFRESH_HZ:+--refresh-hz "$SENCHA_BENCH_REFRESH_HZ"} \
    "$out"/*.json | tee "$out/summary.txt"
