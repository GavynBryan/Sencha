# Baked lights light the movers

Evidence for the light-mode change: `LightBakeContribution::Direct` lights
re-enter the runtime forward set flagged baked (`kGpuLightBakedBit`), packed
strictly after every live light, never requesting shadow slots; receivers
that own a lightmap chart skip them in the fragment loop, everything else is
lit by them live.

## The gap, in pixels

A skinned rig standing on a lightmapped floor lit by three
`bake_contribution: direct` point lights (a copy of the `golden_skinned`
scene with the lights flipped to `direct` and shadows off; floor geometry
made unique so content-hash dedup cannot cross-link it; retired after
capture per the recorded scrub procedure). Same cooked content, Release
SceneViewer, frame 150 of 300, `SENCHA_PRESENT_MODE=IMMEDIATE`.

- `mover_before.png` — baseline (`c785fb39`): the room's floor shows the
  baked pools, the rig receives nothing but ambient. The intuition gap the
  owner named: "can objects actually be lit by the static lights?" — no.
- `mover_after.png` — with the change: the rig is lit by the room's lights.

Pixel diff between the two captures: 19,934 differing pixels, all inside
x[687,810] y[327,530] — the rig's screen rect. **Zero pixels differ on the
charted floor**, which is the no-double-count proof: the floor's copy of
those lights comes from the lightmap in both builds, and the in-shader skip
keeps the live copies off it exactly.

## The cost, measured

`levels/baked_stress_96` (96 Direct point lights over one charted floor
brush — the worst case for the skip: a full 64-light cap of baked lights
over a receiver that shades none of them). `scripts/bench_render_ab.sh`,
5 runs x 900 frames, warmup 400, RTX 4060 Laptop, Release, pinned,
IMMEDIATE present.

Counters confirm the regime: baseline packs `lights_visible = 0`; the
change packs `lights_visible = 64` with 14 dropped at the cap (78 candidates
in the scripted view).

| | mean | median | p95 | stdev |
|---|---|---|---|---|
| baseline (excluded) | 6.9285 ms | 6.9395 ms | 7.1480 ms | 0.373 ms |
| baked resident + skip | 6.9261 ms | 6.9420 ms | 7.1710 ms | 0.400 ms |
| delta (old − new) | +0.0024 ms | −0.0025 ms | | |

Unmeasurable against the run-to-run spread. Baseline run 1 of 5 was
excluded: its map streamed in late, leaving sub-millisecond empty frames
after the warmup cutoff (mean 2.33 ms, median 0.90 ms); runs 2–5 agree with
each other to 0.01 ms.

Selection guarantees (baked never evicts live; baked fills only leftover
slots; no shadow requests) are table-tested in
`test/runtime/LightSelectionTests.cpp` and
`test/runtime/LightExtractionTests.cpp`.
