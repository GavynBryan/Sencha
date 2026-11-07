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
  draws them into an `R32_SFLOAT` mask through a mask pipeline family, lowest priority first,
  testing `LESS_OR_EQUAL` against the scene's depth with no depth write. Attachment writes to
  one sample land in primitive order, so the higher priority wins an exact depth tie. Skinned
  items bind the same posed buffer as the forward pass. `outline_mask.vert` outputs only the
  UV a cutout samples, and takes its position from `mesh_position.glsli`, the same expression
  under the same `invariant gl_Position` as `mesh_forward.vert`, so the two passes cannot
  disagree about depth. A mask texel is the style's slot plus the surface's depth as the
  fraction.
- **The composite.** `OutlineCompositePass` draws `outline_composite.frag` after the present
  draw, from inside `OutputTransformPass::Draw` so every host that owes that draw gets it,
  scissored to the highlighted objects' screen bounds grown by the widest style, so normal
  presentation carries no outline work and the cost follows what is highlighted rather than
  the window. It draws only on pixels whose own mask is 0, from a fixed ring of taps at three
  radii up to the widest style. A scissor can still be far larger than the outline -- an item
  straddling the eye plane takes the whole view -- so `OutlineTilePass` first marks the 8x8
  tiles holding any mask texel, with gathers, then grows the marks by the widest style's reach,
  and the composite leaves any pixel in an unmarked tile after one fetch. On an Intel RPL-P at
  1920x1080 that took the whole-view case from 2.7 ms to about 0.5 ms. A style counts only
  within its own width, and the higher priority takes a pixel two styles compete for, so a
  highlighted object's material is never painted over.
- **What stands in front.** The mask holds only the visible part of an object, so the edge of
  anything in front of it is an edge of the mask too. The composite writes the outlined
  surface's depth, moved toward the camera by 5 cm or 2% of its distance, whichever is more,
  as its fragment depth, and tests it against the scene's: the ring is hidden wherever
  something nearer than that stands, and kept where the floor beside a box's base is only
  millimetres nearer. The view's projection rows (`OutlineDepthTermsOf`) let it move a depth
  by a distance in a perspective or an orthographic view.
- **Under display effects.** When the chain runs a display effect, it draws the outlines into
  its plane right after the transform, with the scene's depth attached read only, and the
  output pass then draws none, so a fade covers them like the rest of the frame
  (`outline_fade`). A scene-domain effect still runs before they are drawn, so an outline does
  not follow a displacement. The mask scope stores neither depth nor stencil
  (`VK_ATTACHMENT_STORE_OP_NONE`), so the two scopes reading them in one phase need no barrier
  between them.
- **The bounds.** They must hold what the mask drew or the outline is cut off. A skinned item
  uses `PosedWorldBounds`, since a posed limb reaches outside its rest bounds, and an item
  straddling the eye plane takes the whole view, since its projection means nothing, and one
  wholly behind it covers nothing. That rule is `ProjectToNdc` in `render/ScreenBounds.h`, which
  texture projection's scissor follows too.
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

All of an outline's work on an Intel RPL-P at 1920x1080, `Phase/Post` (mask and tiles) and
`Phase/Output` (present and composite):

| Case | Post | Output |
|---|---|---|
| Nothing highlighted | 0.001 ms | 0.177 ms |
| `levels/golden_outline`, every mesh highlighted | 0.060 ms | 0.253 ms |
| `levels/golden_outline_behind`, scissor is the whole view | 0.182 ms | 0.475 ms |

Earlier, at the host's default window size:

`Phase/Output` on `levels/golden_outline_single`, one small object highlighted:

| GPU | Nothing highlighted | One object highlighted |
|---|---|---|
| RTX 4060 Laptop | 0.010 ms | 0.012 ms |
| Intel RPL-P | 0.095 ms | 0.141 ms |
