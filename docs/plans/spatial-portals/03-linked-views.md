# Linked Views

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stage 4.

A linked view is the scene in front of the far aperture, drawn from the main camera
carried through the mapping, and shown on the near aperture's surface.

## 1. What exists and is reused

| Mechanism | Where | Used for |
|---|---|---|
| `FrameComposition`, `FrameView`, work nodes with dependency points | `engine/include/render/FrameComposition.h` | Describing more than one view in a frame. The editor already uses it per viewport. |
| `RenderTargetStore` | `engine/include/graphics/vulkan/RenderTargetStore.h` | Offscreen colour and depth targets with resize, lifetimes and quiet-interval release. Its header names render-to-texture as a purpose. |
| `MeshForwardPass::DrawContext` | `engine/include/render/pass/MeshForwardPass.h` | Replaying the forward pass under another camera into another target. |
| `RenderExtractionSystem::Extract(World, StoragePartitionSet, ..., CameraRenderData, RenderQueue&, ...)` | `engine/include/render/extract/RenderExtractionSystem.h` | Extraction for any partition set and camera. |
| `RenderScope` | `engine/include/graphics/vulkan/RenderScope.h` | One dynamic-rendering scope per target. |
| `StageFeature` with declared dependencies | `engine/include/graphics/vulkan/Renderer.h` | Ordering the new features. |
| `ShadowResidency` | `engine/include/render/ShadowResidency.h` | One shadow arbiter for all views. |
| `GpuFrameScratch` pools and tags | `engine/include/graphics/GpuFrameScratch.h` | Per-view uniforms. |
| Render stats, GPU scopes, capture | `engine/include/profiling/` | Counters and timing. |
| Golden image host | `test/render_golden/`, `test/fixtures/render_host/` | Acceptance images. |

There is no render graph and this stage does not add one.

## 2. The runtime frame gains views

Today `DefaultRenderPipeline` owns exactly one `Queue`, `Lights`, `Camera`,
`ShadowRequests`, `SkinnedPoses`, `Outlines` and particle set, and `MeshRenderFeature`
draws that one queue into the primary presentation. The sky, outlines and particles each
bind the single camera.

Stage 4 first makes the runtime describe its frame through `FrameComposition`, with one
view. This is a behaviour-preserving refactor and lands as its own change:

- the per-view state (camera, queue, light set, particle draw set, sky binding) moves
  into a view record that `DefaultRenderPipeline` holds a small fixed array of;
- shadow residency becomes one work node the views depend on, as the editor already
  arranges it;
- every golden image is byte-identical before and after (stop condition 6).

The variation axis exists in the tree today: the editor draws several views of the same
passes. The header of `RenderTargetStore` names split screen and scopes as further
consumers. That is what justifies the seam; it is not built for portals alone.

## 3. Choosing views

Each frame, from `ResolvedLinks` and the main camera:

A link direction requests a view when:

- the near aperture's zone and the far aperture's zone are both in the Visible set;
- `Directions` allows looking this way;
- the camera is in front of the near aperture;
- the aperture's conservative screen bounds intersect the frustum with nonzero area.

Requests are ranked by:

1. the camera's near volume intersecting the aperture;
2. projected area;
3. whether it held a target last frame;
4. `LinkId`, then side.

The top `render.linked_views.max` requests get a view. The rest draw the fallback
surface and are counted. Rank never depends on entity allocation or zone load order.

Destination zones that are resident but not Visible are not drawn, even if a link names
them: presentation transforms are propagated over the Visible set only, so anything
drawn from outside it would be stale.

## 4. Extraction for a linked view

For each chosen view:

1. The camera is the main camera carried through the mapping, built directly as
   `CameraRenderData`, with the far aperture's plane as an oblique near plane.
2. The partition set is the far aperture's zone and the persistent partition. Never
   every resident zone.
3. `RenderExtractionSystem::Extract` runs with that camera and set into the view's own
   queue. Extraction culls and depth-keys against the camera it is given, so the view
   needs its own queue.
4. Light extraction runs for that camera into the view's own light set.
5. The view's shadow requests are merged with the main view's and passed to
   `ShadowResidency::Update` once. Its frame counter advances per call, so two calls in
   one frame are wrong. Existing budgets apply; a linked view's light that gets no slot
   is unshadowed and counted like any denied request.
6. Aperture surfaces met during a linked view's extraction draw the fallback surface.
   The view carries a flag that stops it requesting another view (D7).

### 4.1 Skinned meshes

Posed vertex buffers are keyed by `RenderEntityKey`, and extracting the same entity a
second time into the frame's `SkinnedPoseFrameData` would create two instances with one
key. Pose selection therefore runs once per frame over the union of the views'
partition sets, and each view's queue refers to the slot. Two queue items may share a
`PoseSlot` with different world matrices.

Pose selection ranks by main-camera depth today. A skinned mesh visible only through a
link ranks by its depth along the carried camera. Stated as a limit: under pose budget
pressure the main view wins.

### 4.2 Particles and sky

The particle extractor and the sky bind a camera. Each runs per view with that view's
camera. Particle sorting is per camera, so a sorted emitter is sorted once per view that
draws it. See `06` for particles that cross.

## 5. Targets

Each view draws into a colour and depth target from a `RenderTargetStore` owned by the
linked view feature.

- Formats are the scene formats (`kSceneColorFormat` and the presentation's depth
  format). `PipelineVariantSet` holds one key; a different format set would rebuild the
  forward pipelines on every switch.
- Extent is the main target's extent times `render.linked_views.scale`, clamped to a
  minimum and maximum dimension. Because the linked camera has the main camera's
  projection apart from its near plane, the linked image lines up with the main image
  in screen space, and the view draws only inside a scissor rectangle around the
  aperture's screen bounds.
- Lifetime is within-frame. Targets unused for the store's quiet interval are freed.

### 5.1 Limits

Recorded in `docs/renderer/constraints.md` in the same change:

| Limit | Default | Set by | Past it |
|---|---|---|---|
| Linked views per frame | 2 | `render.linked_views.max`, live | lowest ranked requests draw the fallback surface; counted as dropped |
| Target scale | 1.0 | `render.linked_views.scale`, live | clamped |
| Target bytes | derived from the two above | reported | a view whose target cannot be acquired draws the fallback surface; counted |
| Projected area threshold | measured in Stage 0 | `render.linked_views.min_area`, live | below it, no request |

The Main view-uniform scratch pool floor is 16 KiB, about two views of the 5712-byte
view block. The floor rises with the view cap so that linked views cannot starve the
main view: the main view's uniforms are allocated first, and a linked view that cannot
get its block is dropped and counted. Boundary tests cover the cap and one past it.

Dropped views log once when dropping starts and once when it stops, and count between.

## 6. Clipping at the exit plane

Geometry between the carried camera and the far aperture's plane must not appear. The
view uses an oblique near plane: the projection's near plane is replaced by the
aperture's plane in view space.

- It needs no device feature and no shader change.
- Depth is `[0, 1]`, not reversed. The oblique plane changes depth distribution inside
  the linked view only; nothing outside the view reads that depth.
- The projection must stay finite when the camera is nearly on the plane. When the
  camera is closer than a named distance, the plane is pushed to that distance.

A per-draw clip plane is a different mechanism, needed for straddling meshes. It is in
`04` and is not used here.

Golden images prove that nothing behind the exit plane shows, with geometry touching and
crossing the plane.

## 7. Features

Two features share one linked-view state object.

### 7.1 Offscreen feature

Phase Offscreen. Depends on shadow and pose. For each chosen view it acquires targets,
opens a `RenderScope`, and draws sky, opaque meshes, blended meshes and particles
through the same passes the main view uses, with that view's `DrawContext`.

`MeshForwardPass::Draw` overwrites `LastStats` and `TransparentOrder` on each call.
Stats are accumulated per view by the caller before the next draw.

One fixed GPU scope, `LinkedViews`, brackets all linked views together. A GPU scope can
be written once per frame, so the main view's `ForwardOpaque` cannot be reused.

### 7.2 Composite feature

Phase MainColor, after opaque meshes and before blended meshes and particles.

- Draws each near aperture's rectangle with depth test and depth write.
- Samples the view's target at the fragment's screen position, through the bindless
  index the store publishes.
- A request that got no view draws the fallback surface: a flat material named by a
  cvar until content asks for more.
- Its own small pipeline. The ordinary opaque shader does not learn about linked views.

Blended meshes and particles in front of the aperture draw over it afterwards, in the
main view, as they would over any opaque surface.

### 7.3 Post-processing

Linked views are not post-processed. They are composited into scene colour before post,
so bloom and the rest apply once to the final image.

## 8. The camera at the aperture

For v0.1 characters cross only upright links, and the first-person eye sits on the
capsule's vertical axis, so the eye crosses the plane on the same tick the capsule
centre does. There is no period with the eye through and the body not.

Two things remain:

- **Near plane and the surface.** As the camera approaches, the main view's near plane
  can cut the aperture rectangle and show the recess behind it. The composite draws the
  aperture as a shallow box extending back into the recess, so the near plane always
  meets a face that samples the linked view.
- **The crossing frame.** Presentation interpolates between ticks. The side the camera
  is drawn on comes from `PresentationPoseOf` and the relocation's crossing fraction
  (`02`, section 2.5). View selection uses that presented pose, so on the frame where
  the presented eye is still on the entry side the linked view is still the far side,
  and on the next it is the main view. The two images are the same scene from the same
  place; there is no cut.

The orbit camera is in `06`.

## 9. Lifecycle

Required paths, each tested:

- no camera; zero extent; minimised window;
- swapchain recreation with live targets;
- changing the cap or scale while views are live;
- a link becoming invalid, or a zone leaving the Visible set, with a view live;
- material and mesh hot reload while visible through a link;
- shutdown with a view live.

Targets are created and destroyed on the owner thread. A target a frame still reads
stays alive until that frame retires, on the failure paths as well.

## 10. Counters

Added with their producers: views requested, drawn, dropped; target pixels and bytes;
queue items and draw calls per linked view; composite draws; fallback draws. Capture
schema version is bumped.

With profiling off, nothing is recorded.

## 11. Editor

Kyusu viewports are drawn by `EditorRenderFeature`, which has its own composition and
does not use `DefaultRenderPipeline`. Nothing in this stage changes what a viewport
shows. The aperture overlay from `01` is the viewport's representation; the linked view
is seen in play-in-editor, which runs the game binary.

## 12. Tests

- Composition refactor: every existing golden image unchanged; render A/B bench inside
  noise.
- View selection: none; behind; frustum reject; below area threshold; more requests
  than the cap, with reversed zone load order giving the same choice; one-way link.
- Extraction: only the far zone and the persistent partition are visited; a skinned
  mesh seen in both views has one pose slot; no second view requested from within one.
- Limits: at the cap and one past it; scratch block refused.
- Golden images: head on, oblique, grazing, one pixel, full screen, geometry through
  the exit plane, a portal seen through a portal, particles through a link, a shadowed
  light on the far side.
- Lifecycle list above.
- Zero links: no linked-view pass, target, scratch block or allocation.
