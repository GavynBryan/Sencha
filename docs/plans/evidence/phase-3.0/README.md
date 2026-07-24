# Phase 3.0 renderer instrumentation: disabled-path evidence

This directory holds the reproducible evidence that the renderer instrumentation
ladder (`render.profile.mode` off/counters/gpu/capture) costs nothing when it is
either compiled out (`SENCHA_ENABLE_RENDER_PROFILING=OFF`) or compiled in and
left in mode `off`. Two claims are backed here:

1. The OFF build carries no instrumentation at all: the timestamp, query,
   debug-label, capture, stats-panel, and debug-view translation units are not
   compiled, so no command stream can contain their commands.
2. The compiled-in-Off build's per-frame CPU time is statistically
   indistinguishable from the compiled-out build's.

## 1. Off-path is compiled out

`scripts/check_render_profiling_off.sh` is the durable, CI-runnable form of the
proof. It has two parts:

- Part A (source invariant): the Vulkan timestamp/query commands live only in
  `GpuTimestampPool.cpp` and the debug-label commands only in
  `VulkanDebugLabels.cpp`, and `engine/CMakeLists.txt` lists those plus
  `RenderCapture`, `RenderStatsPanel`, and `RenderDebugView` for OFF-build
  exclusion. If the commands leak into another TU, the exclusion no longer covers
  them and Part A fails.
- Part B (compiled artifact): against an OFF-configured build directory, the five
  excluded translation units produced no object file, while the always-compiled
  tier policy (`RenderInstrumentation.cpp`, holding the mode enum and
  `ResolveInstrumentationBundle`) is present.

Object files, not binary symbols, are the discriminating artifact: the release
binaries drop these symbols anyway (hidden visibility, `--gc-sections`) and load
Vulkan entry points dynamically, so a stripped-of-meaning symbol table shows the
same thing in both builds. A translation unit that is never compiled cannot
contribute a single command to any frame. The captured run is in
[off_path_check.txt](off_path_check.txt); the same script returns non-zero when
pointed at an ON build (the excluded objects are present), so the check
discriminates.

This replaces a live RenderDoc command-stream capture, which was not available in
the measurement environment. Absence-from-the-binary is a stronger claim than
absence-from-one-captured-frame: it holds for every frame, every scene, every
code path. The manual RenderDoc procedure is recorded below for anyone who wants
the live confirmation on top of the static proof.

### Ring-version / bundle-nulling unit proof

The complementary runtime guarantee (the Off path cannot *write* even when
profiling is compiled in) is a unit test, not a manual capture:

- `test/core/RenderInstrumentationBundleTests.cpp` drives `ResolveInstrumentationBundle`
  and asserts that mode `Off` yields an all-null bundle (`Stats`, `StatsHistory`,
  `GpuTimestamps`, `Capture` all null), so `PushRenderStatsFrame` and
  `RenderCapture::Append` have nothing to write through. It also pins the tier
  ladder for Counters/Gpu/Capture. These run in every build (the function is
  compiled in both), so the Off-path guarantee is checked in the shipping config
  too.
- `test/core/RenderProfilingTests.cpp` already covers the ring/version contracts:
  `RenderStatsHistory` version counts writes, `RenderCapture` does not advance its
  version before `Start`, and the JSON envelope carries `schema_version`,
  unit-suffixed keys, and the `-1` uncollected-GPU-span sentinel; the CSV form has
  one header and one row per frame.

## 2. Compiled-in-Off vs compiled-out A/B

### Method

Two Release builds identical except for the one flag:

    cmake -S . -B build-ab-on  -DCMAKE_BUILD_TYPE=Release -DSENCHA_ENABLE_VULKAN=ON \
        -DSENCHA_ENABLE_RENDER_PROFILING=ON  -DSENCHA_ENABLE_DEBUG_UI=OFF -DSENCHA_ENABLE_COOK=OFF
    cmake -S . -B build-ab-off -DCMAKE_BUILD_TYPE=Release -DSENCHA_ENABLE_VULKAN=ON \
        -DSENCHA_ENABLE_RENDER_PROFILING=OFF -DSENCHA_ENABLE_DEBUG_UI=OFF -DSENCHA_ENABLE_COOK=OFF
    cmake --build build-ab-on  --target SceneViewer -j
    cmake --build build-ab-off --target SceneViewer -j

`DEBUG_UI` and `COOK` are off in both arms so the only difference is the profiling
flag. The ON arm runs in mode `off` (its default), i.e. the latch leaves every
bundle pointer null, exactly the state the null-checks guard.

Each arm runs a fixed, deterministic flythrough ten times, 2000 frames per run,
recording one chrome://tracing frame trace per run:

    scripts/bench_render_ab.sh build-ab-on/example/SceneViewer/app  template <out>/on  10 2000 levels/test
    scripts/bench_render_ab.sh build-ab-off/example/SceneViewer/app template <out>/off 10 2000 levels/test

The harness makes the run reproducible and vsync-free:

- `sceneviewer.camera.scripted 1` drives the camera along a fixed orbit that is a
  pure function of an internal frame counter, so every run renders the identical
  view sequence.
- `app.exit_after_frames 2000` self-terminates the run.
- `frame.trace.output <path>` writes the per-frame phase timings at shutdown; this
  path is independent of `SENCHA_ENABLE_RENDER_PROFILING`, so it works in the OFF
  build too (the render-capture writer does not).
- `SENCHA_PRESENT_MODE=IMMEDIATE` and `r.target_fps 0` remove the FIFO/vsync floor
  so frame time reflects CPU+GPU work rather than the refresh interval.

Per-frame duration is the "Frame N" begin→end span from the trace. The first 400
frames of each run (asset load, swapchain settle) are dropped. Statistics reduce
via `scripts/bench_trace_stats.py`.

### Result

Pooled over 10 runs x 1600 post-warmup frames per arm (16000 frames each):

| arm | mean | median | p95 | p99 |
| --- | ---- | ------ | --- | --- |
| ON  (profiling compiled in, mode off) | 2.205 ms | 0.888 ms | 10.71 ms | 12.33 ms |
| OFF (profiling compiled out)          | 2.103 ms | 0.849 ms | 10.38 ms | 12.15 ms |

The proper independent unit is the run, so the significance test is a Welch's
t-test on the ten per-run means per arm:

- ON  per-run mean: 2.205 +/- 0.226 ms (sd over 10 runs)
- OFF per-run mean: 2.103 +/- 0.283 ms
- difference (ON - OFF): +0.102 ms (+4.9%)
- Welch t = 0.89, df = 17.2, two-sided p ~= 0.37

The between-run standard deviation (~0.23 ms) is more than twice the +0.10 ms
between-arm difference, and p ~= 0.37 is far from significant. Compiling the
instrumentation in and leaving it in mode `off` has no statistically detectable
frame-time cost versus compiling it out. This is the expected result: the latch
leaves the bundle pointers null and the per-frame publish sites are guarded by
null checks that never fire.

Raw per-frame durations are in [ab_frames.csv](ab_frames.csv) (arm, run, frame,
duration_ms); the per-run and pooled summary is in
[ab_summary.json](ab_summary.json). The full chrome traces are large and
regenerable from the two commands above, so they are not committed.

## Manual RenderDoc procedure (optional live confirmation)

The static proof above already shows the OFF binary contains no timestamp/query/
label commands. To confirm on a live command stream with RenderDoc:

1. Launch the profiling build in mode off, under RenderDoc:

       renderdoccmd capture -d . -- build-ab-on/example/SceneViewer/app \
           +set render.profile.mode off +map levels/test

2. Capture a frame (RenderDoc default trigger) and open it.
3. In the capture's command list, confirm there are no `vkCmdWriteTimestamp`,
   `vkCmdResetQueryPool`, `vkCmdBeginDebugUtilsLabelEXT`, or `vkCmdEndDebugUtilsLabelEXT`
   commands, and no query-pool resources. Switch `render.profile.mode gpu` at
   runtime and re-capture to see the same commands appear, confirming the mode
   gate rather than a missing feature.

The OFF build cannot be captured for these commands because the code that would
issue them is not compiled into it.
