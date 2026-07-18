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
