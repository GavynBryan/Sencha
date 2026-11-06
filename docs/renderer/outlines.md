# Outlines

An outline marks an entity with a colored edge drawn in the output pass, around the
visible part of the entity only.

## Using one

`RegisterOutlineStyle(world, name, {Color, WidthPixels, Priority})` returns a slot from 1
to 15. A duplicate priority or a sixteenth style is refused, and styles are per World, so a
game owns its own and the editor owns its own. Colors are authored in sRGB and converted to
linear once, at registration.

`SetOutline(world, entity, {Owner, Style, Order})` holds an outline in the shared
[store](presentation-contributions.md). An outline adds no component, so an entity's
archetype never changes. It ends with `ClearOutline`, `ClearOutlinesOf(writer)`, its
holder's death, or the entity's.

## How it draws

- **One style per entity.** The highest `Order` wins, then the owner
  `PresentationOwnerPrecedes` puts first. Only the entities named are outlined; descendants
  and `PresentedWith` are not walked.
- **The mask.** `OutlineFrame` holds a `RenderQueue` per style, filled by extraction beside
  the opaque queue, so nothing joins `RenderQueueItem` or run identity. `MeshForwardPass`
  draws them into an `R8_UNORM` mask through a mask pipeline family, lowest priority first,
  testing `LESS_OR_EQUAL` against the scene's depth with no depth write. Attachment writes to
  one sample land in primitive order, so the higher priority wins an exact depth tie. Skinned
  items bind the same posed buffer as the forward pass. `outline_mask.vert` outputs only the
  UV a cutout samples, and takes its position from `mesh_position.glsli`, the same expression
  under the same `invariant gl_Position` as `mesh_forward.vert`, so the two passes cannot
  disagree about depth.
- **The composite.** `OutlineCompositePass` draws `outline_composite.frag` after the present
  draw, from inside `OutputTransformPass::Draw` so every host that owes that draw gets it,
  scissored to the highlighted objects' screen bounds grown by the widest style, so normal
  presentation carries no outline work and the cost follows what is highlighted rather than
  the window. It draws only on pixels whose own mask is 0, from a fixed ring of taps at three
  radii up to the widest style. A style counts only within its own width, and the higher
  priority takes a pixel two styles compete for, so a highlighted object's material is never
  painted over.
- **The bounds.** They must hold what the mask drew or the outline is cut off. A skinned item
  uses `PosedWorldBounds`, since a posed limb reaches outside its rest bounds, and an item
  reaching behind the camera takes the whole view, since its projection means nothing.
- **The style table** is a small bindless image uploaded when styles change, so a frame with
  nothing highlighted carries no extra descriptor or push data.

With nothing highlighted, extraction does no per-entity lookup, the mask scope records
nothing, the mask image is released after a quiet interval, and the composite does not draw.

## In the editor

Kyusu outlines the selection's placed meshes through the same mask family and composite;
brushes keep their wireframe. Viewports replay one queue under several cameras, so each
recomputes the screen bounds with `OutlineFrame::UpdateScreenBounds`, and each holds its own
mask, released while nothing is outlined. The editor owns its style.

## Cost

`Phase/Output` on `levels/golden_outline_single`, one small object highlighted:

| GPU | Nothing highlighted | One object highlighted |
|---|---|---|
| RTX 4060 Laptop | 0.010 ms | 0.012 ms |
| Intel RPL-P | 0.095 ms | 0.141 ms |
