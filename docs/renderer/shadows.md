# Shadows

Two pools, one arbiter, one depth pass. Everything CPU-side is deterministic:
identical request and event sequences produce identical slot assignment, atlas
placement, and view schedules.

## Storage

| | Spot | Point |
|---|---|---|
| Image | one 2048x2048 `D16_UNORM` 2D | one `D16_UNORM` cube array, 4 cubes = 24 layers |
| Per view | a quadtree tile, 256 / 512 / 1024 | one 512x512 face |
| Slots | 8 (`kMaxSpotShadows`) | 4 (`kMaxPointShadows`) |
| Views per slot | 1 | 6 |
| Fixed memory | 8 MiB | 12 MiB |
| Sampler | comparison, linear, clamp to **border**, opaque white | comparison, linear, clamp to **edge** |
| Descriptor | set 2 binding 0, `sampler2DShadow` | set 2 binding 1, `samplerCubeArrayShadow` |

Both images are owned by `LightBindings` and created in
`ShadowRenderFeature::Setup`. Creation failure is survivable: the tiny dummy
depth images stay bound and every comparison sample reads fully lit.

Border clamp with opaque white on the atlas is what makes a spot's out-of-tile
sample read lit. The cube pool uses edge clamp because a cube face has no
meaningful border.

### Guard bands

`kSpotShadowTileExtent = 512`, `kSpotShadowGuardTexels = 8`, so the logical
interior of a 512 tile is 496 texels. The depth pass sets the viewport to the
inset rect and the scissor to the whole tile, so the clear covers the guard band
and geometry cannot write into it. `kSpotShadowFilterReachTexels = 7` is
`static_assert`ed to be less than the guard, which is what proves the 3x3 tent
filter at maximum softness cannot reach a neighbouring tile.

`ShadowAtlasAllocator::InsetScaleBias` produces the `AtlasScaleBias` the shader
uses: it maps light-space UV `[0,1]` onto the inset interior, not the physical
rect.

## Atlas allocation

`ShadowAtlasAllocator` is a quadtree over the fixed atlas. Levels run from the
whole atlas down to `kSpotShadowMinTileExtent = 256`; each node is `Empty`,
`Split` (something is allocated below it), or `Allocated`.

Allocation scans the target level in **Morton order**, taking the first `Empty`
node whose ancestors are all non-`Allocated`, then marks the ancestor chain
`Split`. Morton order fills an already-split parent's remaining children before
opening a fresh parent, so small tiles pack together instead of fragmenting
every large-tier node. Freeing clears the node and collapses `Split` ancestors
whose subtrees emptied.

The allocator only satisfies or rejects an exact power-of-two size. Tier
downgrade policy lives one level up, in the arbiter:
`ShadowResidency::AllocateWithDowngrade` tries the requested size, then halves
until `kSpotShadowMinTileExtent`, and returns the null allocation if nothing
fits.

## The residency arbiter

`ShadowResidency` (`engine/src/render/ShadowResidency.cpp`) decides which light
holds which slot, when cached content re-renders, and which views run this
frame. It is CPU only and holds no Vulkan objects.

`Update()` runs this fixed sequence:

```
++FrameNumber
IntakeEvents          caster-diff events invalidate intersecting OnChange slots
MatchRequests         bind each request to the slot its RenderEntityKey owns
MatchPointRequests
EnforceSlotBudget     release lowest-scored live slots down to the budget
EnforcePointSlotBudget
GrantFreeSlots        give free slots to ungranted requests, strongest first
GrantFreePointSlots
ApplyHysteresisAndSteals
ApplyPointHysteresisAndSteals
ScheduleViews         pick this frame's views under the shared budget
BuildGrants           publish (light index -> slot) for slots with usable content
```

Then `ApplyGrants(lights)` writes `ShadowIndex` onto the granted lights and
copies each live slot's **last rendered** record into `RenderLightSet`.

### Slot records are what was rendered, not what was requested

A slot's `Rendered` field is written only when a view is actually scheduled. The
forward pass therefore always samples cached content against the state it was
drawn with. A light that moved this frame but whose slot did not re-render
samples last frame's matrix, which is correct for the pixels in that tile, and
the slot is already marked invalid so it re-renders as budget allows.

### Ownership, scoring, hysteresis

- A slot's effective score is its request's score times
  `kHolderScoreMultiplier = 1.25`.
- Contenders (ungranted requests, already score-descending) pair with holders
  (live slots, weakest first). A holder's `OutscoredFrames` counter grows only
  while its paired contender strictly beats it and resets otherwise.
- A steal happens only after `kStealOutscoredFrames = 30` consecutive
  outscored frames.

Together these stop equal-importance lights from trading a slot back and forth
every frame.

A tier change on an existing slot (the request now asks for a different tile
size) frees the old allocation and re-acquires through the downgrade chain, then
marks the slot never-rendered: the old tile's contents are meaningless in a
differently placed rect. Freeing first is what guarantees the downgrade chain
terminates in a fit.

### Update policies

`ShadowUpdatePolicy` on the light component:

| Policy | Re-renders when |
|---|---|
| `EveryFrame` | it is scheduled, every frame budget allows |
| `OnChange` | its state hash changes, or a caster event intersects its volume |
| `Static` | only through `InvalidateAll` (the `render.shadow.invalidate` command, or an editor edit that bypasses extracted state) |

The state hash is FNV-1a over the view-projection, sampling params, and tile
size for spots (`HashSpotShadowState`), and over position/far and params for
points (`HashPointShadowState`).

Point slots track `PendingFaces`, a six-bit mask. A point light stays ungranted
until every face has rendered at least once, which is why a newly acquired point
slot bursts its faces. `MarkPointInvalid` re-dirties all six, including faces a
previous invalidation already re-rendered, because a state change moves the
whole cube.

### View scheduling order

One budget, `MaxViewsPerFrame` (default 12), shared by both pools. A point face
costs one view, the same as a whole spot tile.

```
reserve = min(MinInvalidatedViewsPerFrame, total pending invalidated views)

1. never-rendered slots, oldest acquisition first, spot before point on ties
     spot tiles may consume the full budget
     point face bursts stop short of the reserve
2. the reserve, oldest invalidation first
     a point slot re-renders one face per reserved view and keeps the rest pending
3. EveryFrame slots in stable slot order: spot pool, then point pool
     a point slot refills its face rotation only once the previous one finished,
     so a budget clamp still cycles coverage over all six faces
4. remaining budget drains the rest of the invalidated backlog
```

The reserve exists so `EveryFrame` demand and never-rendered point bursts cannot
starve the oldest invalidations indefinitely.

Cross-pool ordering is total: by frame stamp, then spot before point, then slot
index. There is no unordered container anywhere in the schedule.

### Failure handling

`ShadowDepthPass` reports back into the arbiter:

- `MarkViewFailed(slot)`: clears `EverRendered`, marks invalid, and erases the
  slot's grant from this frame's list.
- `MarkPointFaceFailed(slot, face)`: re-sets that one pending bit and erases the
  grant. The slot's other faces already match its rendered record, so only the
  failed face re-queues.

Both are paired with `RevokeGrant` on the light set, so nothing samples a slot
whose content does not match its published record.

### Introspection

`SlotInfo` / `PointSlotInfo` return per-slot snapshots (owner, allocation,
policy, ever-rendered, invalid, pending faces, frames since acquired, frames
since rendered). The editor lighting panel reads these through
`ShadowResidencyReadout`; the runtime publishes aggregates through
`ShadowFrameStats`.

## Caster extraction and invalidation

`ShadowCasterExtractionSystem::Extract` walks every active registry and appends
one `ShadowCasterItem` per masked, shadow-casting section of every visible
mesh whose component has `CastShadows`. A section contributes only if its
resolved material has `CastShadows`; the item records whether that material is
double-sided, which selects the depth pipeline.

The per-entity record table is built **only when `emitRecords` is true**, which
`DefaultRenderPipeline` sets from `ShadowResidency::HasOnChangeSlots()`. With no
on-change slot resident, nothing consumes the table and it is not built.

`ShadowCasterState` is what a silhouette change means:

| Field | Why |
|---|---|
| `WorldBounds` | quantized to 1/16 world unit, so float noise in transform propagation cannot read as movement |
| `Mesh`, `Materials` | swapping either changes the silhouette |
| `EffectiveShadowSectionMask` | which sections actually cast |
| `ShadowMaterialStateHash` | FNV over (section index, cast flag, double-sided flag) for every resolvable masked section, so a base-color edit does not read as a change |

`ShadowCasterDiff::Apply` sorts this frame's records by key, sorted-merges them
against the retained previous table, and emits `Added` / `Removed` / `Changed`
events. `Removed` carries the previous bounds and `Changed` carries the union of
previous and current, so a caster leaving a shadow volume still invalidates it
at the departure site. The table is swapped whether or not events were emitted,
which keeps it current while no consumer wants events.

## Sampling

`engine/shaders/shadow_sampling.glsli`.

### Spot

```glsl
receiver = worldPos + geometricNormal * SamplingParams.x * SamplingParams.z
clip     = shadow.ViewProjection * vec4(receiver, 1)
bail lit if clip.w <= 0, or z outside [0,1], or uv outside [0,1]
atlasUv  = logicalUv * AtlasScaleBias.xy + AtlasScaleBias.zw
softness = clamp(SamplingParams.y, 0.5, 4.0)
3x3 tent (offsets -1.5, 0, 1.5, weights 1/2/1) scaled by softness * texelSize
visibility = weighted sum / 16
```

`SamplingParams.x` is the world size of one texel at the far plane, computed at
extraction as `2 * range * tan(outerAngle) / kSpotShadowInnerExtent`. The
arbiter rescales it when a request was downgraded to a smaller tile, so a
downgraded tile offsets by its own coarser texels. `SamplingParams.z` is the
per-light `ShadowBiasScale`.

Offsetting the receiver along the geometric normal (not the shading normal) is
the primary acne control; the pipeline depth bias is the secondary one.

### Point

Same structure adapted to directions. The depth reference is computed from the
major-axis distance so it matches what the cube face's projection wrote:

```glsl
reference = far * (d - near) / ((far - near) * d),  d = max(majorAxis, near)
```

Five taps: the direction itself plus four offset along a tangent basis, each
re-normalized, averaged. The filter offset is
`clamp(softness, 0.5, 4.0) * 2 / cubeExtent`.

The `w` component of the cube coordinate is the array index, which is the slot
index directly (one cube per slot).

### Visibility composition

`ResolveFilteredShadowVisibility` returns 1.0 immediately when the material has
`ReceiveShadows = false`, and otherwise lerps the filtered result toward 1.0 by
`render.shadow.darkness`. `SampleSpotShadow` / `SamplePointShadow` return 1.0
when `ShadowIndex >= Count`, which is how a revoked or never-granted light reads
as unshadowed.

## Known artifact: the spot wedge

Spot shadows can show a wedge-shaped umbra near the cone rim. This was
investigated and the volume math verified correct: the wedge is physically what
a cone-projected shadow of a finite occluder looks like at that angle. It is not
a bug. The knobs that change its severity are authoring and quality settings:
cone falloff angles, PCF softness clamp, and the per-light receiver bias scale.
