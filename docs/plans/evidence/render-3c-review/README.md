# Phase 3C review: capture evidence

The renderer plan's evidence-based review (Section 11, Phase 3C): three
benchmark scenes captured across resolutions, reduced against the Section 14
budget table. Findings and the budget verdict live in the plan document
(Section 16); this directory holds the method and the reduced numbers.

## Method

- Build: `build-lights` preset shape (Release, `SENCHA_ENABLE_RENDER_PROFILING=ON`,
  `DEBUG_UI=OFF`, `COOK=OFF`) on an RTX 4060 laptop. Not the GTX 1060
  reference tier: scale GPU milliseconds by roughly 2.5-3x when reading
  against the budgets.
- Scenes (authored + cooked by `RenderBenchGen` with
  `SENCHA_RENDER_BENCH_ROOT`, plus the existing probe spike):
  - `levels/probe_spike` - small room: two lit rooms, baked lightmap + AO,
    one probe volume, 1 dynamic light.
  - `levels/bench_hub` - representative content: enclosed 30x30 hub with
    pillars, 4 baked Direct lights, probe volume, AO, 12 dynamic lights of
    which 6 cast shadows (4 spot + 2 point).
  - `levels/bench_stress` - worst case: exactly 64 authored dynamic lights
    (52 plain + 8 shadow spots + 4 shadow points) with overlapping pools,
    pillars as casters, probes resident.
- Run: `SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=<size>`, scripted
  orbit, 1500 frames, `render.profile.mode capture` +
  `render.capture.output`, 400 warmup frames dropped in reduction.
- Sizes: 1280x720 and 1920x1080. The 2560x1440 runs were executed but the
  window manager clamped the swapchain to the display, producing numbers
  identical to 1080p; they are recorded in summary.json as duplicates and
  excluded from conclusions. 1440p needs a display that can host it.

Reproduce:

    SENCHA_RENDER_BENCH_ROOT=template/assets \
      build/test/level_cook_tests --gtest_filter=RenderBench.Generate
    cd template && for map in probe_spike bench_hub bench_stress; do
      for size in 1280x720 1920x1080; do
        SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=$size \
          build-lights/example/SceneViewer/app +set r.target_fps 0 \
          +set sceneviewer.camera.scripted 1 +set app.exit_after_frames 1500 \
          +set render.profile.mode capture \
          +set render.capture.output out/${map}_${size}.json +map levels/$map
      done; done
    # reduce with the script inlined in the plan's Section 16 notes (medians,
    # p95, counter maxima; per-frame JSONs ~1.5 MB each, not committed).

## Reduced results

`summary.json` (one entry per run). Headline rows at 1920x1080, medians over
~1010 post-warmup frames:

| Scene | MainColor med | MainColor p95 | lights visible med/max | shadow steady | worst shadow frame |
|---|---|---|---|---|---|
| probe_spike | 0.147 ms | 0.182 ms | 1 / 1 | 0.0007 ms | none rendered |
| bench_hub | 0.450 ms | 0.707 ms | 7 / 12 | 0.0007 ms | 0.0007 ms (7 views) |
| bench_stress | 1.214 ms | 3.065 ms | 25 / 64 | 0.0007 ms | 0.013 ms (11 views) |

Caveats recorded with the findings: brush-only content keeps geometry
trivially small (4 draws, ~100 triangles), so the CPU-extraction and
per-shadow-view budget rows are not meaningfully exercised; the scripted
orbit culls lights behind the camera, so the stress median sees ~25 lights
with peaks at the full 64.

Also observed: the known shutdown segfault (TextureCache teardown during
game.so static destruction) reproduced on 3 of 9 runs, always after the
capture file was fully written. Previously recorded as non-reproducible;
these odds make it worth a dedicated chase.
