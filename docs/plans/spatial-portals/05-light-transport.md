# Light Transport

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stages 12 and
13. Independent of the rigid-body stages.

Light from one side of a static link reaches the other side through the aperture and
nowhere else. This extends the existing bake kernels, light extraction and shadow
residency. It adds no cooker, no light kind and no second renderer.

## 1. What lighting is today

Read at `fbe262a3`.

| Area | On `main` |
|---|---|
| Light kinds | Point and spot. No directional light. |
| Bake contribution | Per light: `None` (realtime only), `Indirect` (realtime direct, baked bounce into probes), `Direct` (baked into the lightmap, also kept in the forward set flagged baked for movable receivers, never shadowed at runtime). There is no separate mobility field. |
| Baked direct | Per-zone lightmap atlas, RGB9E5. Kernel `engine/src/assets/cook/DirectLightBake.cpp`. |
| Ambient occlusion | R8, sharing the atlas layout. |
| Probes | L1 volumes, at most 8 active. Kernel `engine/src/assets/cook/ProbeBake.cpp`. |
| Occlusion | `BakeBvh` over render geometry, built per zone by `BuildDocumentOcclusionBvh`, including "halo" triangles from neighbouring zones within probe-ray reach. |
| Cook scope | `CollectWorldCookInput` loads every zone and hands each zone the others' halos; then `ExecuteDocumentCook` runs per zone. Only triangles cross zones. A zone's bake lights come from that zone's document alone. |
| Fingerprints | `ComputeDocumentCookFingerprints`: per step, with the occlusion step including the content hashes of reachable halo zones. |
| Forward lights | 64 per view, 64 bytes each, in the view uniform block (5712 bytes asserted, 8 KiB budget). Selected per camera. |
| Shadows | One atlas with 8 spot slots, one cube array with 4 point slots, at most 12 views a frame. Slots are owned by `RenderEntityKey`. Requests come only from lights in a view's light set. |

## 2. The optical model

A rigid unit-scale mapping makes a virtual image of a source-side light in destination
space. For a destination sample:

1. carry the light's pose through the mapping;
2. take the segment from the sample to the virtual light;
3. require it to pass through the destination aperture's rectangle;
4. carry the crossing point to the source aperture;
5. test the source aperture point to the real light against source geometry;
6. test the sample to the destination aperture point against destination geometry;
7. evaluate the existing point or spot model with the virtual light;
8. stop. One link per path (D7).

Step 3 is not optional. A carried light without it lights the room around the wall.

Unit scale keeps distance and attenuation. Upright or not makes no difference to light.

## 3. Stage 12: baked transport

### 3.1 What the world cook passes

The halo exchange already gives each zone its neighbours' occluder triangles. For each
spatial link whose destination aperture is in zone Z, the world cook additionally gives
Z a *linked halo* for the source zone:

- the link id, both aperture frames, and the mapping;
- the source zone's occluder triangles within reach of the source aperture, in source
  coordinates, as their own BVH;
- the source zone's bake lights whose range or cone reaches the source aperture.

Coarse culling by aperture and light bounds happens here, before any per-sample work.

The source triangles are not merged into Z's occlusion BVH. Carried into destination
space they would sit behind the wall, overlapping whatever is really there, and a ray
that did not pass through the aperture could hit them. They stay a separate domain,
entered only by a path that passes through the rectangle.

### 3.2 Direct bake

`EvaluateBakedDirectRadiance` combines light evaluation with single-segment visibility.
Refactor only what is needed into concrete functions: the unoccluded contribution,
single-segment visibility, and two-segment visibility through an aperture. No strategy
interface.

Local and transmitted contributions go into the same atlas. A transmitted sample is lit
only when both segments are clear, the crossing point is strictly inside the rectangle
under a named tolerance, and the real light's range and cone include the carried path.

Only lights whose contribution is `Direct` are baked this way, as today.

### 3.3 Probe rays

The probe bake traces a fixed ray table. A ray's nearest event becomes one of:

- geometry hit: unchanged behaviour;
- environment miss: unchanged;
- an aperture before any geometry, entered from the front: carry the point and
  direction, subtract the distance travelled, continue in the linked domain's BVH;
- a second aperture after one hop: the ray ends with the pinned non-transmitting result.

Ties: authored geometry wins at the rectangle's edge; an aperture wins only strictly
inside; the lower `LinkId` wins between apertures.

The ray table and accumulation order do not change. Serial and job paths stay
bit-identical.

Lights with `Indirect` contribution reach probes through the link by the same path.

### 3.4 Fingerprints

A zone's occlusion, direct and probe fingerprints include, for each link that delivers
it a linked halo: the link id, both frames and extents, `Directions`, the linked halo's
light state, and the content hash of the linked halo's triangles.

Moving or resizing an aperture restales the zones it lights. Editing an unrelated link
restales nothing. Identical input gives identical bytes whatever the zone order.

### 3.5 Tests

- a bright room lights its neighbour through the aperture with the right falloff;
- removing the link restores the sealed-wall result;
- wall beside the aperture stays dark;
- a source-side blocker blocks; a destination-side blocker blocks; both;
- light behind the source aperture contributes nothing; sample behind the destination
  aperture receives nothing;
- range ending at the aperture; cone tangent to it;
- probe directional response through the aperture; second aperture after one hop;
- repeated cook is byte-identical; reversed zone order is byte-identical; serial equals
  worker;
- an unrelated link edit leaves this zone's fingerprints unchanged;
- cook work scales with rays and samples that reach an aperture, shown by counters.

## 4. Stage 13: dynamic transport

### 4.1 Light images

During light extraction for a view, a transmitted image is made when:

- the source light is in a zone in the Visible set;
- its sphere or cone reaches the source aperture;
- the destination aperture's zone is in that view's partition set;
- the image survives the cap.

An image is render-domain data. It is not a light entity.

A light with `Direct` contribution already baked its transmitted light in Stage 12, so
it makes an image only for uncharted and movable receivers, flagged baked like its
ordinary record.

### 4.2 Where images live

Images carry more than a light record: the virtual light plus the destination aperture
frame for the per-fragment test. They do not go into the 64-light array, and ordinary
lights do not grow to carry aperture data they never use.

The view block has about 2.4 KiB of headroom under its budget. A small fixed image
array fits; a large one does not. The cap is therefore small and stated:

| Limit | Default | Past it |
|---|---|---|
| Transmitted light images per view | 8 | lowest ranked dropped, counted |
| Shadowed transmitted images per view | 2 | the rest draw unshadowed, counted |

Images never consume ordinary forward-light slots. Record both limits in
`docs/renderer/constraints.md`, with the view block's new asserted size.

Rank: destination aperture on screen; projected affected area; estimated contribution;
held a slot last frame; `LinkId` then the light's key.

### 4.3 Fragment test

For each destination fragment and image: intersect the segment from the fragment to the
virtual light with the destination plane; reject if parallel, outside the segment or
outside the rectangle; otherwise evaluate the ordinary model.

The loop exists in the forward shader only when the view has images. Measure the
zero-image cost before and after.

### 4.4 Shadows

Unshadowed images land first. v1.0 needs shadowed ones.

A transmitted shadow view is drawn from the virtual light in destination coordinates.
Its casters are destination casters between receiver and aperture, plus source casters
carried through the mapping, plus straddling entities' clipped halves from `04`.

- A spot image requests one view. A point image follows the cube model and may request
  all six faces at first; face culling is a measured follow-up.
- The request goes through `ShadowResidency` with the existing atlas, budgets and
  denial counters. No private atlas.
- The slot key is the light's `RenderEntityKey` with `Scope` set from the `LinkId` and
  side, the same scheme `04` uses for carried render halves.
- Invalidation adds: the link becoming invalid, either zone leaving the Visible set,
  and caster changes on both sides.

The one-view-per-frame minimum for invalidated shadows is shared with ordinary lights.
Transmitted requests must not starve them: ordinary requests rank first under pressure.

### 4.5 Tests

- carried point and spot poses match the shared mapping;
- a fragment whose path misses the rectangle receives nothing;
- source and destination occluders block independently, shadowed;
- same-zone and cross-zone links give the same result;
- cap pressure drops deterministically and counts;
- an invalid or awaiting link emits no image;
- a zone outside the Visible set adds no extraction work;
- an image seen in a linked view does not request a further image;
- golden images for a moving shadowed light on the far side.

## 5. Counters

Linked halos built; transmitted paths evaluated; probe rays reaching and crossing an
aperture, and stopped by the one-hop rule; light-image candidates, packed, dropped;
transmitted shadow requests, granted, denied; transmitted caster draws.

## 6. Performance shape

- Direct bake: destination samples times relevant lights after coarse culling. Not
  zones times links times lights.
- Probe bake: unchanged, plus work for rays that reach an aperture.
- Extraction: active visible links times source lights that reach their aperture.
- Shading: ordinary lights plus packed images.
- Shadows: inside existing budgets.

## 7. Stop conditions

Stop when coarse culling of source lights would need a scan of all lights per link per
frame; when a transmitted shadow would need a recursive view; when a transmitted
request would have to bypass shadow fairness; or when the zero-link lighting path gains
measurable work.
