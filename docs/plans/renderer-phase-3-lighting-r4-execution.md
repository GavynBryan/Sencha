# Renderer Phase 3 Revision 4: Execution Corrections

Status: ACCEPTED execution amendment, 2026-07-15.

This document is normative over `renderer-phase-3-lighting.md`. Where the two documents disagree, this document wins. It exists as a compact implementation patch so work can begin without rewriting the 2,694-line design record.

The original document remains the rationale and reference design. This amendment corrects the implementation blockers found in review and defines the shortest safe path to a visible renderer.

## 1. Immediate delivery boundary

The first delivery target is a playable dynamic-lighting vertical slice:

1. StandardLit and Unlit materials.
2. Normal maps, stylized diffuse, stylized specular, emission, exposure, and the interim shoulder.
3. Deterministic point and spot light extraction with range and frustum culling.
4. Always-updated spot shadows in fixed 512 physical atlas tiles with guard bands.
5. The existing CPU draw counters plus only the minimum new counters needed to tune the slice.

The first delivery does not wait for:

- GPU timestamp pools.
- JSON or CSV capture export.
- the runtime profiling panel.
- quadtree shadow allocation.
- shadow caching or invalidation.
- point-light shadows.
- irradiance probes.
- baked ambient occlusion.
- the full debug-view suite.

Those remain planned, but they are no longer prerequisites for getting the renderer on screen.

The production shader must still contain no debug branch. A minimal separate development shader may land with the slice if it is useful for shadow bias tuning, but the complete debug-view matrix belongs with the later instrumentation stage.

## 2. Revised stage order

### 3A.0: Renderer correctness preparation

Small prerequisite patch:

- Fix the stale reversed-Z comments while retaining standard `[0,1]` Z.
- Move `render.ambient.*` cvar registration into the engine.
- Split shared frame-uniform GLSL declarations into one include so vertex and fragment declarations cannot drift.
- Add the cofactor normal transform for non-uniform scale.
- Add the tangent vertex attribute and TBN construction.

No profiling dependency.

### 3A.1: StandardLit, Unlit, and deterministic lights

Land together or as two tightly ordered patches:

- `.smat` v2 and the StandardLit evaluation.
- Unlit evaluation.
- spot-light component, extraction, packing, culling, and stable prioritization.
- the four production opaque pipelines from shading family and cull mode.
- pipeline bits in the render sort key.

No shadow resources yet.

### 3A.2: First spot shadows

Get shadows on screen with the smallest complete substrate:

- one 2048x2048 depth atlas.
- fixed 512 physical tiles for this stage.
- 8-texel guard bands.
- always-update resident spot shadows.
- fixed deterministic slot assignment.
- comparison sampler and the 3x3 tent filter.
- full bias stack.
- camera-independent caster extraction.
- set 2 with dummy-backed point and probe bindings.

No quadtree, caching, hysteresis, or point-light cube array in this stage.

### 3.0: Full renderer instrumentation

The full Off, Counters, Gpu, Capture ladder remains independently mergeable. It may land before, during, or after 3A.0 through 3A.2. It is not a functional dependency of the renderer vertical slice.

The disabled-path contract remains unchanged. Nothing in this amendment permits profiling work in the Off path.

### 3A.3: Managed shadow residency

After first shadows are correct:

- quadtree resolution tiers.
- scoring and hysteresis.
- bounded-fair scheduling.
- OnChange and Static policies.
- previous/current caster diff.
- lighting budget UI.

### 3A.4: Point-light shadows

Point cube shadows land after the corrected residency system.

### 3B: Baked lighting

Probe irradiance and vertex AO remain independently mergeable after the shared bake core, subject to the corrected bake authority and topology rules below.

## 3. Stable renderer identity

`RegistryId` is a runtime allocation identity. It is not stable when zones attach in a different order and must not be used as the deterministic identity of a zone-owned light, caster, or probe volume.

Use:

```cpp
struct RenderEntityKey
{
    RegistryKind Kind;
    ZoneId Zone;       // Invalid for the global registry.
    EntityId Entity;
};
```

Rules:

- Zone-owned entities use `(RegistryKind::Zone, registry.Zone, entity)`.
- Global entities use `(RegistryKind::Global, invalid ZoneId, entity)`.
- Transient and boundary registries may use `RegistryId` only as an explicitly nonpersistent fallback until they gain a stable owner identity.
- Shadow tie-breaking, cache keys, caster-table ordering, and light-cap ordering use `RenderEntityKey`.
- Tests that reverse zone attachment order must produce identical packed-light ordering and shadow grants.

This guarantees independence from zone attachment order. It does not claim persistence across a scene recook that remints entity IDs. Authored persistent entity IDs may replace the entity portion later if content proves that requirement.

## 4. Bounded-fair shadow scheduling

The old categorical order could starve invalidated OnChange shadows whenever EveryFrame demand filled the view clamp. Replace it with this scheduler:

1. Serve never-rendered slots first, oldest acquisition first.
2. If at least one invalidated cached slot exists, reserve one view from the frame budget for the oldest invalidation.
3. Fill remaining views with EveryFrame work in stable slot order.
4. Fill any remaining views with invalidated work, oldest invalidation first.
5. Point-light faces are individual views for the clamp, but a never-rendered point light may request a contiguous face burst only when budget remains. It must not consume the reserved invalidation view.

If `max_views_per_frame` is nonzero, every invalidated slot is guaranteed service within a bounded number of frames. The exact bound is:

```text
ceil(invalidated_view_count / reserved_invalidation_views_per_frame)
```

with one reserved invalidation view per frame by default.

The runtime may later expose `render.shadow.min_invalidated_views_per_frame`, default 1, clamped so never-rendered work can still make progress.

Required regression test:

- Saturate the scheduler forever with more EveryFrame work than the frame clamp.
- Invalidate an OnChange shadow.
- Assert that it renders within the documented bound.

## 5. Caster state and material-sensitive invalidation

A caster snapshot must include every extracted state value capable of changing the shadow silhouette or shadow-pass pipeline.

Use a state equivalent to:

```cpp
struct ShadowCasterState
{
    Aabb WorldBounds;
    StaticMeshHandle Mesh;
    MaterialSetHandle Materials;
    uint32_t EffectiveShadowSectionMask;
    uint64_t ShadowMaterialStateHash;
};
```

`ShadowMaterialStateHash` includes only shadow-relevant state:

- per-section `cast_shadows`.
- per-section `double_sided`.
- future alpha-mask mode, cutoff, and shadow texture identity when alpha-masked shadows land.

It excludes base color, roughness, metallic, emission, and other state that cannot change the shadow silhouette.

Changing a material set, changing a section's effective shadow flag, or changing double-sided state must produce a caster changed event even when mesh, transform, and section mask remain unchanged.

## 6. Separate runtime-shadow intent from bake participation

Runtime shadow cost policy and baked-lighting geometry are separate concerns.

### Static meshes

Add:

```cpp
bool CastShadows = true;
bool AffectsBakedLighting = true;
```

Semantics:

- `CastShadows` controls participation in runtime direct shadow maps.
- `AffectsBakedLighting` controls whether static render geometry occludes and reflects probe and AO bake rays.
- Disabling runtime shadows must not make a wall disappear from enclosure, indirect bounce, or AO.
- `AffectsBakedLighting` defaults true for opaque static geometry and is an advanced opt-out.

Material `cast_shadows` remains a runtime shadow-pass property. It does not remove opaque geometry from the bake. A later material-level `affects_baked_lighting` override may be added only if real content needs per-section bake exclusion.

### Lights

Add a bake-specific field to point and spot lights in 3A.1 so 3B does not depend on shadow stages:

```cpp
enum class LightBakeContribution : uint8_t
{
    None,
    Indirect
};

LightBakeContribution BakeContribution = LightBakeContribution::None;
```

Semantics:

- `CastShadows` requests a runtime shadow map.
- `ShadowUpdate` controls runtime cache behavior.
- `BakeContribution` controls whether the static light contributes direct-at-hit-point energy to the one-bounce probe bake.
- `ShadowUpdate::Static` is not a bake declaration.

The bake's occlusion shadow rays are internal bake calculations and do not depend on whether the light owns a runtime shadow slot.

## 7. Derived cook authority and stable bake hashes

The lighting bake must never hash or consume its own AO-enriched output as authoritative source geometry.

The source authority is:

1. authored brush geometry and its existing pre-output `geometryHash`.
2. authored static prop mesh content identity and world transform.
3. bake-relevant material state.
4. neighboring zones' equivalent source identities.
5. bake-contributing light state.
6. irradiance-volume placement and bake settings.

The output is derived:

- base cooked cell mesh.
- AO refinement topology.
- packed vertex AO.
- `.sprobe` payload.

Rules:

- Every AO bake begins from freshly generated pre-AO `MeshGeometry` or an equivalent immutable base-cook representation.
- It never reloads the prior v4 `.smesh` and refines it again.
- The staleness key does not include the AO output bytes.
- A light-only rebake cannot increase vertex count.
- Repeating a bake with identical authored input produces byte-identical `.smesh` and `.sprobe` outputs.
- The cooked cache records both source hash and output content hashes, but only source hashes participate in lighting staleness.

For static prop geometry, bake participation uses mesh asset content hashes, not transient runtime handles.

## 8. Conforming adaptive AO tessellation

Deterministic midpoint coordinates alone do not prevent T-junctions. Refinement must be edge-conforming.

Use a two-pass refinement per recursion level:

1. Evaluate candidate AO error for each triangle.
2. Emit requested edge splits into a canonical edge map keyed by quantized endpoint pair and subdivision level.
3. Union all requests for an edge.
4. Propagate every accepted split to every incident triangle in the local cook set.
5. Retriangulate triangles only after the accepted edge set is finalized.
6. Repeat until tolerance, minimum edge, maximum depth, or growth cap terminates refinement.

A triangle may gain a conformity split even when its own AO error is below tolerance. Those splits are required to preserve topology and count against the same growth cap.

### Cross-mesh and cross-zone boundaries

The cook must build a canonical boundary-edge table from base geometry before per-zone refinement. Boundary edge identity uses globally quantized world endpoints plus compatible surface-normal class.

Each zone evaluates boundary-edge split requests using the same:

- base edge.
- halo geometry.
- fixed ray table.
- bake settings.
- quantized candidate positions.

The accepted boundary split pattern is therefore derivable identically by either zone. Tests must compare the entire ordered boundary split sequence, not only final AO values at existing vertices.

If independent derivation proves brittle in implementation, the approved fallback is a tiny shared boundary-refinement sidecar generated by the world cook. Do not ship nonconforming independent triangle refinement.

## 9. Probe residency and the eight-volume cap

Separate loaded GPU residency from the per-camera active list.

### Resident volumes

A loaded zone may own its uploaded probe textures. Zone streaming controls their lifetime.

### Active volumes

Each camera builds at most `kMaxActiveProbeVolumes = 8` frame headers and descriptor indices.

Rank candidate volumes by:

1. contains camera position.
2. authored priority.
3. intersects camera frustum.
4. projected or distance relevance.
5. current-list hysteresis.
6. smallest volume.
7. stable `(ZoneId, volume file index)` identity.

Fragments select among the active list using the existing overlap rule of priority, smallest volume, then stable ID.

Volumes excluded from the active list fall back to hemispheric ambient for that camera. Exclusion is visible through counters and the lighting panel.

The descriptor array must be fully dummy-filled. Dynamically selected volume texture indices in GLSL must use the engine's non-uniform descriptor indexing path where required.

Required tests:

- More than eight resident volumes produce a deterministic active list.
- Reversing zone attachment order does not change that list.
- Hysteresis prevents flicker near an equal-relevance boundary.
- Excluded volumes never cause an invalid descriptor access.

## 10. Unlit and ambient occlusion

Unlit has no ambient term.

Unlit materials:

- ignore point and spot lighting.
- ignore runtime shadows as receivers.
- ignore probes.
- ignore vertex AO.
- may still cast shadows when authored to do so.
- still pass through the shared exposure and output shoulder so bright emissive content clips consistently with StandardLit.

Only StandardLit applies:

```text
ambient = ambientC * aoFactor
```

Remove any statement that Unlit applies AO to an ambient term.

## 11. Corrected dependencies

- 3A.0 has no dependency on full renderer instrumentation.
- 3A.1 depends only on 3A.0.
- 3A.2 depends only on 3A.1 and the mechanical Vulkan image, sampler, and descriptor widening.
- Full instrumentation is independent.
- Managed shadow residency depends on first spot shadows.
- Point shadows depend on managed residency and cube-image support.
- 3B.1 depends on point and spot light shapes plus `BakeContribution`, not on runtime shadow fields or shadow residency.
- 3B.2 depends on the shared bake core and lighting bindings. It does not depend on shadow maps.
- 3B.3 depends on the shared bake core and the immutable base-cook geometry authority. It does not consume previous AO output.

## 12. Required test amendments

Add or change these tests before considering the corresponding stage complete:

### Identity

- Reverse zone attachment order and assert identical light-cap ordering.
- Reverse zone attachment order and assert identical shadow grants.
- Reverse zone attachment order and assert identical active probe-volume list.

### Scheduling

- Saturated EveryFrame workload cannot starve an invalidated OnChange slot.
- Never-rendered work and invalidated work both make bounded progress.

### Caster diff

- Material-set replacement invalidates when effective shadow state changes.
- Double-sided toggle invalidates.
- A base-color-only material edit does not invalidate.

### Bake authority

- Runtime `CastShadows = false` does not remove a wall from probe or AO occlusion when `AffectsBakedLighting = true`.
- `AffectsBakedLighting = false` excludes it.
- `BakeContribution = None` removes a light from the probe bake without changing runtime lighting.
- Repeating the bake does not grow topology.
- A light-only rebake preserves vertex count and base geometry identity.

### AO topology

- One triangle requesting a shared-edge split forces the adjacent triangle to conform.
- Cross-mesh shared edges produce the same split sequence.
- Cross-zone boundary edges produce the same split sequence.
- No T-junction is present in the final refined index buffers.

### Unlit

- Probe, AO, and direct-light changes do not alter Unlit output before exposure and shoulder.
- Unlit may still appear in the caster set.

## 13. Completion gates

### First dynamic renderer slice is complete when

- StandardLit and Unlit render together.
- point and spot lights are culled and packed deterministically.
- up to eight fixed-tier spot shadows render with isolated guard bands.
- non-uniformly scaled normal-mapped meshes light correctly.
- no debug branch exists in a production shader.
- the validation layer is clean.
- the existing suite plus new CPU tests is green.

### Managed dynamic renderer is complete when

- shadow residency uses stable identities.
- cached invalidations cannot starve.
- caster material-state changes invalidate correctly.
- point shadows share the corrected residency system.
- budgets and degradation are visible in the editor.

### Baked renderer is complete when

- runtime-shadow intent and bake participation are independent.
- staleness hashes derive only from authoritative source inputs.
- repeated AO bakes do not recursively refine output.
- adaptive tessellation is conforming across triangles, meshes, and zones.
- probe active-list overflow is deterministic and visible.
- Unlit remains independent of probes and AO.

## 14. Standing non-goals

This amendment does not reopen the accepted high-level renderer choices:

- forward rendering remains.
- StandardLit remains stylized rather than physically based.
- direct shadows remain conventional depth maps.
- spot shadows remain a guarded shared atlas.
- point shadows remain cube-array based.
- baked ambient remains zone-streamed L1 SH probes.
- baked contact AO remains a vertex channel on cooked level geometry.
- no lightmaps, Forward+, clustered lighting, generalized spatial-field framework, SSAO, or directional light enter this phase without the original metric or content triggers.
