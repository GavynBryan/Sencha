# Texture Projection

The substrate for decals, projected textures, and projected-shadow illusions:
the pure arithmetic lives in `render/TextureProjectionPolicy.{h,cpp}` with
headless tests, and this page records the pass recipe that consumes it. The
one shipped consumer -- projected object shadows -- was removed by owner
ruling on 2026-08-23 (see `shadows.md`); commit `e18ebe9a` holds that
complete implementation and is the reference for every step below.

## What the policy provides

All of it plain values in, plain values out, tested without a device
(`test/runtime/TextureProjectionPolicyTests.cpp`):

| Function | Job |
|---|---|
| `FitProjection(volume, direction, reach)` | ortho view-projection fitted around the volume looking along the direction, far plane extended by the reach, 5% pad so the projected texture never touches a tile border. `DepthRange` converts a world-unit bias into normalized projector depth |
| `SweptProjectionBounds(volume, direction, reach)` | world bounds of everything the projection can touch |
| `GatherProjectionReceivers(items, sweptBounds, cap, out)` | queue indices of static items inside the swept bounds; skinned items skip (they deform under a static projector), overflow past the cap is counted |
| `ComputeProjectionScreenRect(sweptBounds, cameraVP, w, h)` | the re-draw's scissor; empty when off screen, conservatively full-target when straddling the near plane |
| `UnionProjectionScreenRects(rects)` | one scissor for a pass that applies every projector in a single draw |
| `MakeProjectionTileGrid` / `ProjectionTileRectFor` / `ProjectionTileUvScaleBias` | uniform atlas tiling for projectors that render their own source texture per frame; the index is the whole placement |

## The pass recipe

A projection pass is the `extending.md` "Record a pass" template plus
projective sampling. The shape that shipped:

1. **Source texture.** A static decal samples an ordinary bindless texture.
   A dynamic projector (the shadow silhouette case) renders its source into
   an atlas tile per frame: one small color target owned by the pass through
   `RenderTargetStore`, tile placement from the tile-grid helpers, bindless
   read, linear-clamp sampler.
2. **Fit.** `FitProjection` per projector. The silhouette render and the
   receiver re-draw share the one matrix, so a matrix error cannot put the
   projection on the wrong side.
3. **Receivers.** `GatherProjectionReceivers` against the swept bounds, then
   re-draw those items with depth `LESS_OR_EQUAL`, depth write off, and the
   projector's matrix in a per-draw uniform. Identical position math to the
   main pass, or the re-draw sparkles against its own depth.
4. **Blend.** The choice is the effect: multiply (`ZERO`/`SRC_COLOR`) darkens
   (shadows), alpha-over composites (decals). The shipped shadow version
   wrote coverage into a shared R8 screen mask with blend `MAX` first and
   composited once, so N overlapping projectors darken like the strongest
   one instead of multiplying -- worth copying for any effect where overlap
   must not stack. Check format blend support with
   `vkGetPhysicalDeviceFormatProperties` before relying on R8/R16F blends.
5. **Scissor.** `ComputeProjectionScreenRect` per projector per view;
   `UnionProjectionScreenRects` for the single composite.
6. **Ordering.** After opaque, before transparent -- a projection multiplied
   onto glass is paint. The shipped version interleaved mid-phase against
   the opaque depth; if the effect needs that, the depth attachment must be
   stored across the boundary (commit `e18ebe9a` carried a
   `RenderScopeInterruption` helper owning the suspend/resume barriers --
   deleted with its consumer, resurrect it from there).

What the removed implementation added on top, all recoverable from
`e18ebe9a`: per-caster light-derived directions smoothed against popping and
clamped to a grounding cone, an atlas-space separable blur for penumbra,
depth-only occluder tiles (blend `MIN` -- the blend is the depth test) so
projection stops at the first surface along the ray, and a receiver-facing
test so back sides stay clean.

## Why the shadow consumer was removed

Not because the machinery failed -- overlap, occlusion, and facing were all
proven pixel-exact -- but because a post-multiply darkening layered on final
colour cannot agree with shadow maps or baked lighting where they overlap.
Any resurrection for shadows should either accept that as a stylistic effect
in controlled spaces, or fold the mask into the direct-light term instead of
multiplying the composed image. For decals and texture projections the
limitation does not apply.
