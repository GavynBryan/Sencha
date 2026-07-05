# Phase V: Context-Zone and Portal Viewport Rendering

Status: IMPLEMENTED 2026-07-05 (owner-directed same-day execution, commit
"partition 08"). Fixes two owner-reported defects from hands-on testing and lands the intended
look: context zones VISIBLE with their real materials under a grey overlay (not the
procedural checker), portals as flat translucent cyan volumes (not checker boxes).

## Why it looks wrong today

- Context zones draw through the procedural-checker fallback
  (`BrushSolidRenderer`), not the real-material queue: the WYSIWYG path
  (`SceneRenderQueueBuilder` + `MeshForwardPass`) is built for the focus document
  only.
- The editor solid shader (`editor_solid.frag.glsl`) GENERATES the checker and
  multiplies the vertex tint into it, so the portal fill tint from T1 still shows
  checker: no flat-color path exists.
- The verified cheap lever: `mesh_forward.frag.glsl` computes
  `baseColor = push.BaseColor * texture(...)`, so a draw-level tint multiplied into
  `MeshPushConstants::BaseColor` on the CPU dims real materials with ZERO shader
  changes.

## Stages

### V1. Draw-level tint on the mesh forward pass (engine, shape-neutral)

`MeshForwardPass::Draw` gains `Vec4 tint = {1,1,1,1}` (multiplied into
`push.BaseColor` per run). Default white: every existing caller renders
byte-identically. Test: the pass is GPU-side; the contract lands in the header
comment plus the editor consumption below (manual gate).

### V2. Context zones through the real-material path

- `EditorRenderFeature` owns one `SceneRenderQueueBuilder` per OPEN zone
  (`std::unordered_map<ZoneId, std::unique_ptr<...>>`, entries dropped when the
  zone closes; each builder's brush-hash cache keeps idle zones free).
- Solid viewports draw context zones via `Forward.Draw(..., tint = grey)` (theme
  constant `ContextZoneDim`) over their builders' brush + placed-mesh queues,
  replacing the checker context path when the real-material path is active (the
  checker fallback remains for the no-assets boot). Wireframe viewports keep the
  current dimmed-wire context look.
- Context zones use a NEUTRAL full-bright ambient light set, not the focus zone's
  lights: the grey must not depend on whether a focus light happens to reach the
  neighboring zone (unlit-and-dimmed reads as black, not grey).

### V3. Flat portal fill

`editor_solid.frag.glsl` gains a flat branch: vertex tint alpha below 0.5 selects
the tint color WITHOUT the checker (the portal fill constant's 0.30 alpha already
qualifies; regular tints stay at alpha 1). That covers the checker-fallback path.
On the real-material path, `PortalVolumeRenderer` (its own gatherer over the
shared `EditorFillPipeline`, the face-highlight pipeline) draws portal markers as
ALPHA-BLENDED flat fills after the body pass: translucent from both sides,
depth-tested so walls occlude them, never depth-writing. Focus tint white,
context tint the overlay grey. (In the real-material body pass portals are
otherwise invisible: the cook-input collector rightly skips them.)

### V4. Manual gate

Three-zone world, Solid shading: context zones show their true materials dimmed
grey; the focus zone full-bright; portals flat translucent cyan from both sides;
streaming preview toggled on draws its tinted bounds ON TOP of geometry (already
landed) and the demand list matches what the viewport shows.

## Non-goals

Per-item queue tints (the draw-level tint is one value per Draw call; per-entity
tinting waits for a real need), selection/hover changes, transparency sorting
(portal fills blend in gather order, which is fine for sparse markers).
