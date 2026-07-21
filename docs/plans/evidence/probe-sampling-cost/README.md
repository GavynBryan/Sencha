# Probe sampling cost: measurement evidence

Confirms the Section 14 budget row "Probe sampling added cost (fragment,
volumes resident) <= 0.3 ms full-screen" for the shipped 3B.2 runtime
(`probe_sampling.glsli` volume selection + trilinear L1 evaluation over
three RGBA16F 3D textures, headers in the frame UBO).

## Method

- Build: the dev tree (Debug, `SENCHA_ENABLE_RENDER_PROFILING=ON`) on an
  RTX 4060 laptop. Build type does not affect the measured quantity: both
  variants run identical shaders and the comparison is GPU-scope only.
- Scene: `levels/probe_spike` (the 3B.2 validation scene: two lit rooms,
  one 24x4x12 m volume at 1 m cells, one dynamic light, baked lightmap).
  Cooked by `ProbeSpike.Generate` with `SENCHA_PROBE_SPIKE_ROOT`.
- A/B: the same cooked content with and without
  `.cooked/levels/probe_spike/probes.sprobe` present. The runtime resolves
  probes by path convention, so removing the file is a pure residency
  toggle: geometry, lightmap, and lights are byte-identical, and the off
  run samples the hemispheric fallback. `probe_volumes_resident_count`
  in the captures confirms 1 vs 0.
- Run: `SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=1920x1080`,
  scripted orbit (`sceneviewer.camera.scripted 1`), 1500 frames,
  `render.profile.mode capture` + `render.capture.output`, 400 warmup
  frames dropped. The orbit is deterministic, so frame i pairs with
  frame i across the two runs and the per-frame delta isolates the probe
  term from everything else in MainColor.

Reproduce:

    SENCHA_PROBE_SPIKE_ROOT=template/assets \
      build/test/level_cook_tests --gtest_filter=ProbeSpike.Generate
    cd template && SENCHA_PRESENT_MODE=IMMEDIATE SENCHA_WINDOW_SIZE=1920x1080 \
      build/example/SceneViewer/app +set r.target_fps 0 \
      +set sceneviewer.camera.scripted 1 +set app.exit_after_frames 1500 \
      +set render.profile.mode capture \
      +set render.capture.output out/probes_on.json +map levels/probe_spike
    # remove assets/.cooked/levels/probe_spike/probes.sprobe, rerun to
    # out/probes_off.json, restore, then take paired per-frame deltas of
    # Phase_MainColor_gpu_ms (median over ~1000 frames).

## Result (RTX 4060, ~1080p, 1012 paired frames)

| Statistic | probes on | probes off | paired delta |
| --------- | --------- | ---------- | ------------ |
| MainColor median | 0.315 ms | 0.249 ms | **0.039 ms** |
| MainColor mean delta | | | 0.058 ms |

The paired-delta tail (p95 0.196 ms) is timestamp jitter, not probe cost:
the two distributions' own p95s differ by only 0.016 ms. The orbit spends
part of its cycle outside the rooms where fragments fall outside the
volume, so the median slightly understates the interior-only cost; the
mean (0.058 ms) bounds it from above.

## Verdict

0.04-0.06 ms on an RTX 4060 scales to roughly 0.12-0.17 ms on the GTX
1060-class reference tier (~3x throughput ratio): **inside the 0.3 ms
budget with >= 1.7x headroom**. No escalation. The formal 3C review can
rerun this alongside the other captures; per-frame JSONs (~1.5 MB each)
are regenerable from the recipe and not committed.
