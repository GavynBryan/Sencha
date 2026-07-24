# Baked direct lighting: Phase 0 quality spike

The Phase 0 gate from the plan (`~/.claude/plans/1-point-light-scalable-truffle.md`):
does vertex-baked *direct* light look acceptable, and is adaptive tessellation
mandatory before it is usable? Answered by a throwaway spike, not the real feature.

## Method

The spike evaluates the direct diffuse term (wrap-Lambert + windowed inverse-square
attenuation, the exact model from `lighting.glsli`) **in the vertex shader** and
interpolates it across the triangle, which is precisely what a per-vertex bake would
store and the hardware would interpolate. No bake, no cook, no `.smesh` change: a
`render.baked_direct.spike` cvar switches the fragment shader between today's
per-fragment light loop and the per-vertex-interpolated term. Diffuse only (no
specular, no occlusion), matching what a bake produces.

Scene (`scripts/gen_phase0_spike_scene.py`, planes from `gen_flat_plane_smesh.py`):
two identical 12x12 floor planes lit by the identical set of three small accent point
lights (range 3.5) sitting mid-face. Left plane is **coarse** (subdiv 1: 4 vertices,
the raw-brush-density worst case). Right plane is **fine** (subdiv 32: 1089 vertices).
Only tessellation differs. RTX 4060, Release, low ambient for accent legibility.

Reproduce:

    python3 scripts/gen_flat_plane_smesh.py 12 1  template/assets/.cooked/levels/phase0_spike/floor_coarse.smesh
    python3 scripts/gen_flat_plane_smesh.py 12 32 template/assets/.cooked/levels/phase0_spike/floor_fine.smesh
    python3 scripts/gen_phase0_spike_scene.py    template/assets/.cooked/levels/phase0_spike.cooked.json
    cd template && build-lights/example/SceneViewer/app +set sceneviewer.camera.scripted 0 \
      +set render.baked_direct.spike <true|false> +map levels/phase0_spike \
      +set render.ambient.sky_r 0.02 +set render.ambient.sky_g 0.02 +set render.ambient.sky_b 0.03

## Result

- `phase0_perfragment.png` (spike off): ground truth. Both planes render identical,
  crisp light pools. Per-fragment cost is geometry-independent.
- `phase0_pervertex.png` (spike on): the **fine** plane approximates the ground-truth
  pools smoothly and looks era-appropriate (GoldSrc/Source). The **coarse** plane
  loses the lights almost entirely: the accent lights sit mid-face, beyond range of
  all four corners, so every vertex samples near zero and interpolation yields a
  near-black plane. The light vanishes.

## Phase 1 and 2 results (after the spike)

- `phase1_direct_excluded.png`: lights authored `direct` vanish on GPU (excluded from the
  runtime set), with the v4 baked channel still neutral (no bake yet). The correct Phase 1 state.
- `phase1_parity_dynamic.png`: dynamic lights + neutral channel render identically to the
  per-fragment reference (the v4 format and shader read change nothing when unbaked).
- `phase2_baked_render.png`: three smooth colored pools that are the plane's *entire* lighting,
  produced by the real `BakeDirectLighting` and stored in the v4 vertex channel, while all three
  lights are authored `direct` and excluded from the runtime forward set. End-to-end proof: real
  bake -> `.smesh` -> shader -> screen, zero runtime lights. (Dense 32x32 plane, so no smear;
  a raw brush face would still smear until Phase 3 tessellation.)
- `phase3_brush_tessellated.png`: the Phase 3 payoff. A real cooked box-brush floor (a top face
  with four corners, the Phase 0 smear case) shows smooth colored pools because the cook
  subdivides cell triangles near `direct` lights before baking. Same exclusion (all lights
  `direct`, none in the runtime set).

  Tessellation scheme note: the first implementation was error-driven adaptive refinement
  (renderer plan 7A.5 re-gated on lights). It produced irregular skinny triangles whose
  interpolation streaked the smooth falloff; every good-looking capture in this ladder is a
  regular lattice. It was replaced with distance-graded uniform subdivision: a pure per-edge
  predicate (split while longer than distance-to-light x grading factor, gated on the closest
  point ON the edge) that converges to a regular lattice near lights, needs no rays during
  refinement, and conforms without T-junctions (verified watertight on the cooked mesh). The
  stair-stepping visible on the far rim silhouette is rasterization aliasing (no MSAA in the
  forward pass today), present on every hard edge at glancing angles; it is unrelated to the
  bake.

## Phase 4: the closed loop (96-light validation)

The baked counterpart of the original measurement (`docs/plans/evidence/point-light-cost/`):
96 small accent point lights authored `bake_contribution: direct` over a floor brush, cooked
through the real level cook (tessellate + bake), rendered with the same rig (RTX 4060, Release,
IMMEDIATE present, capture export, 300-frame warmup dropped).

| | dynamic 96 (original measurement) | baked 96 |
| --- | --- | --- |
| LightsVisible (median) | 64 (capped) | **0** |
| LightsDroppedAtCap | **32 (lights vanish)** | **0** |
| MainColor median | 5.08 ms | **0.041 ms** |
| Triangles | 2116 | 49682 (tessellated) |

All 96 pools render (`phase4_baked_stress_96.png`; the blown-white core is real energy: 96
overlapping pools sum far past 1.0 into the tonemap shoulder, and summed radiance clips at the
RGBM ceiling `kBakedDirectRange`). The cap wall is gone and the per-frame cost is noise, even
at 23x the triangle count.

Reproduce: build `level_cook_tests`, then
`SENCHA_BAKED_STRESS_ROOT=$(pwd)/template/assets ./build/test/level_cook_tests
--gtest_filter='BakedLightingStress.Generate'` and run SceneViewer on
`levels/baked_stress_96` with the capture cvars above.

`phase4_debug_view.png`: `render.debug.view baked_direct` isolates the baked vertex term (raw
irradiance); dynamic-lit and unbaked surfaces read black. Bake tuning is exposed as archived
editor cvars (`editor.cook.bake_grading`, `bake_min_edge`, `bake_max_depth`, `bake_margin`,
`bake_growth_cap`), folded into the cook hash so retuning restales the level; the bake's
diffuse wrap follows `render.style.diffuse_wrap`.

## Verdict

**Proceed with vertex-baked direct lighting; adaptive tessellation (plan Phase 3) is
mandatory, not optional.** The fine plane proves the look is achievable and good, so
this is not a bail to per-object light lists. But the coarse plane proves raw brush
density is unusable: a small light's falloff is far higher-frequency than the sparse
"never split brushes" cook provides, exactly the tessellation trap the plan's design
constraints call out. Consequences folded back into the plan:

- Phase 3 tessellation must be **light-proximity gated** (subdivide near baked lights),
  not only occluder-gated as the AO bake (renderer plan 7A.5) is, and likely finer.
- A raw-density MVP (Phase 1/2 without tessellation) will look broken on real Kyusu
  geometry; it is only useful as a pipeline smoke test, never a shippable state.
- The smooth, specular-free pools on the fine plane confirm the "bake shading not
  shadows, diffuse only" model reads correctly for dim accent lighting.
