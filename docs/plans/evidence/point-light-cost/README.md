# Point-light cost: measurement evidence

Measures the forward renderer's per-frame cost of many small static point lights, to
decide whether to build a static-light optimization. Question and rubric are from the
plan `~/.claude/plans/1-point-light-scalable-truffle.md`; the trigger is
`docs/plans/renderer-phase-3-lighting.md:2555` (build per-object light lists only when
MainColor > 8 ms at 1080p AND avg per-fragment light iterations > 16), plus the separate
64-light cap correctness wall.

## Method

- Build: Release, `SENCHA_ENABLE_RENDER_PROFILING=ON`, `DEBUG_UI=OFF`, `COOK=OFF`
  (`build-lights`), on an RTX 4060 laptop (around/just above the ~$500 GPU tier, so read
  the ms as a mild upper bound for weaker hardware).
- Scenes: `scripts/gen_light_stress_scene.py` generates `levels/light_stress_<N>` for
  N = 16 / 32 / 64 / 96 — a 5x5 tiled static floor (constant across N) plus N small
  non-shadow-casting point lights (range 3.0) clustered in the near frustum so all N stay
  visible and their pools overlap (the many-small-lights worst case). Geometry is identical
  across the sweep; only the light count changes.
- Camera: the SceneViewer default static camera (`sceneviewer.camera.scripted 0`, no input),
  so framing is fixed and the run is deterministic. All N lights sit in the frustum
  (`LightsVisible == N` until the cap).
- Run: `SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=1920x1080` (vsync off, ~1080p;
  the WM trims to a 1908x1038 swapchain, ~4% under true 1080p), 1500 frames, capture
  exported headlessly via the new `render.capture.output` cvar. Reduced with
  `scripts/capture_stats.py` (drops 400 warmup frames).

Reproduce:

    for N in 16 32 64 96; do
      python3 scripts/gen_light_stress_scene.py $N template/assets/.cooked/levels/light_stress_$N.cooked.json
    done
    # per N (foreground; windowed Vulkan needs the display):
    cd template && SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=1920x1080 \
      build-lights/example/SceneViewer/app +set r.target_fps 0 +set sceneviewer.camera.scripted 0 \
      +set app.exit_after_frames 1500 +set render.profile.mode capture \
      +set render.capture.output out/lights_$N.json +map levels/light_stress_$N
    python3 scripts/capture_stats.py --warmup 400 out/lights_*.json

The full per-frame capture JSONs (~1.5 MB each) are regenerable from the above and are not
committed; `summary.json` holds the reduced numbers.

## Result (RTX 4060, ~1080p, Release)

| N authored | lights visible | dropped | MainColor median | MainColor p95 |
| ---------- | -------------- | ------- | ---------------- | ------------- |
| 16 | 16 | 0 | 1.25 ms | 1.25 ms |
| 32 | 32 | 0 | 2.40 ms | 2.40 ms |
| 64 | 64 | 0 | 5.02 ms | 7.07 ms |
| 96 | 64 | **32** | 5.08 ms | 7.28 ms |

Geometry constant across the sweep (23 visible objects, 2116 triangles). Per-light cost is
linear at roughly **80 us/light** at this resolution. `light_complexity_64.png` shows the
scene fills the screen and the light pools overlap heavily (warm = 8+ lights reaching a
fragment); note every fragment still iterates all 64 lights regardless, so the per-fragment
iteration count equals `LightsVisible`.

## Rubric evaluation

**B. 64-light cap correctness wall — FIRES.** At N = 96, `LightsVisible` caps at 64 and
**32 lights are silently dropped** (`LightsDroppedAtCap = 32`); the extra lights simply do
not render. This is a correctness failure, not a perf one, and it is independent of frame
time. The reference art (dark corridors/shafts with dozens of small accent lights per view)
will exceed 64 frustum-visible lights in real levels, so this wall will be hit.

**A. Performance trigger — borderline, does not clear 8 ms on this GPU.** Per-fragment
iterations = `LightsVisible` > 16 at N >= 17 (met). MainColor at 64 lights is 5.02 ms median
/ 7.07 ms p95 — under the 8 ms line on the RTX 4060, but with thin headroom (p95 ~7 ms). On a
weaker $500-tier GPU (1.5-2x slower) 64 lights would cross 8 ms, and a denser scene raises it
further. So A is not met on the test hardware but is close and likely met on the target floor.

## Verdict

**Yes, a static-light optimization is warranted — driven primarily by the 64-cap correctness
wall (B), reinforced by real, linear per-light cost (A borderline).** Because the accent
lights are static, the recommended remedy is **per-vertex baked static shading** (see the
plan's design constraints): a baked static light leaves the forward loop entirely, so its
~80 us/light cost disappears AND it no longer counts against the 64 cap — hundreds of static
accent lights become effectively free and never drop. Per-object light lists remain the
complementary path for whatever direct lighting stays dynamic. Both are separate follow-up
plans; this measurement establishes that the problem is real and quantifies it.

## Content lever (N_real vs N_total)

This measurement's lights are all real illuminators (N_real = N). In the reference art, many
glowing elements (screens, signage, the data-trees) illuminate nothing and should be
**emissive materials** (a per-fragment emission term, never a `GpuLight`, never counted in
`LightsVisible` or against the cap). Reclassifying glow-only sources as emissive lowers
N_real directly and is the first, zero-code lever before any baking or culling work.
