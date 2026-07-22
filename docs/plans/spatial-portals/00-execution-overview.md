# Spatial Portal Execution Suite

Status: proposed execution plan, revised for the unified runtime world.

Planning branch origin: `lightmap-spike` at `061e610baf338a5b47cd270a88496f42a04b850a`.

Implementation dependency: the unified runtime cutover represented by
`feature/unified-runtime-container`, or its merged successor, plus the current lighting
architecture integrated onto that runtime.

This suite defines linked planar apertures that render and traverse discontinuous spaces.
It does not introduce dimensions, spaces, alternate worlds, or any other first-class
simulation category. A spatial portal connects two authored endpoint entities inside one
runtime simulation world. The endpoints may belong to the same zone, adjacent zones, or
spatially remote zone groups.

`CLAUDE.md` and the unified runtime plan are binding. In particular:

- one gameplay simulation owns one ECS `World`;
- streamed zones are storage partitions, not entity identity universes;
- live cross-zone relationships use ordinary `EntityId` values;
- durable authored and saved relationships use `StableEntityRef`;
- runtime `EntityId` values are never persisted;
- components contain data only;
- render backends consume extracted render-domain data and never query ECS;
- retained physics state belongs to one simulation-scoped backend scene;
- portal operations use the existing partition migration and participation mechanisms;
- no compatibility architecture is added for per-zone runtime registries;
- serial behavior is the deterministic reference;
- tests, adversarial cases, and measurements ship with each mechanism;
- no stage is complete while required verification remains unrun;
- no em dash is used in code, comments, docs, or commit messages.

## 1. Product boundary

### 1.1 v0.1

v0.1 reproduces the browser prototype inside Sencha:

1. Two linked, static, rectangular planar apertures.
2. A nonrecursive destination view through each aperture.
3. Perspective-correct rendering at arbitrary viewing angle and distance.
4. Destination-side clipping at the exit plane.
5. First-person character traversal.
6. Position, facing, and velocity transformed through the link.
7. Continuous crossing detection from previous and current capsule-center positions.
8. Predictable straddling. A capsule may overlap the aperture, stop, and back out.
9. Same-zone and cross-zone endpoints.
10. Explicit destination participation requests and safe refusal when unavailable.
11. Debug visualization, counters, deterministic tests, and reproducible profiling scenes.

v0.1 does not include:

- recursive views;
- runtime cutting of arbitrary wall collision;
- moving endpoints;
- rigid bodies spanning both sides;
- portal-transmitted lights;
- portal-aware shadows;
- scale-changing links;
- one body crossing two links simultaneously.

The wall opening is authored in render and collision geometry. Runtime geometry carving is
a separate capability and is not hidden inside this project.

### 1.2 v1.0

v1.0 adds:

1. Static, kinematic, skinned, and dynamic meshes crossing an aperture with clipped source
   and destination render representations.
2. Dynamic rigid bodies occupying both coordinate spaces while remaining one logical body.
3. Full-shape aperture overlap, rim collision, and contact-domain filtering.
4. Long and wide colliders that can enter by one end, jam, rotate, fall through, or back out.
5. Entity partition migration after committed traversal without changing `EntityId`.
6. Direct baked-light transport through static links.
7. One-hop probe-ray transport through static links.
8. Dynamic point and spot light images transmitted through visible links.
9. Shadowed dynamic-light transport with source and destination occluders.
10. Stable render, light, shadow, and physics identities with bounded work.

v1.0 remains nonrecursive. One operation crosses at most one link unless a later plan
expands that contract.

## 2. Owning invariant

The central invariant is:

> A spatial portal relationship owns exactly two durable endpoint references. When both
> endpoints are resident, every portal subsystem consumes one resolved rigid mapping,
> aperture test, plane-side convention, stable relationship identity, and one-hop limit.

The canonical authored relationship is not duplicated on the endpoint components.

The relationship entity is world-lifetime content, normally authored in the world scene and
imported into the persistent storage partition. Its component is mechanically equivalent to:

```cpp
struct SpatialPortalLinkComponent
{
    StableEntityRef EndpointA;
    StableEntityRef EndpointB;
    bool Enabled = true;
};
```

Each endpoint entity carries only endpoint-local data:

```cpp
struct SpatialPortalEndpointComponent
{
    Vec2d HalfExtents = Vec2d(1.0f, 2.0f);
    bool Enabled = true;
};
```

A concrete world-scoped `SpatialPortalLinkState` resolves durable references to live
`EntityId` values and publishes immutable resolved records. It owns derived relationship
state and participation leases. It does not own ECS entities, Vulkan objects, Jolt bodies,
editor documents, or cooked assets.

The exact type name may change during implementation. The ownership boundary may not.

## 3. Unified runtime consequences

### 3.1 One live identity universe

Resident endpoint entities share one runtime `World`. A resolved relationship contains:

- the persistent link entity `EntityId`;
- endpoint A and B `EntityId` values;
- endpoint storage partitions;
- endpoint transforms and apertures;
- A-to-B and B-to-A rigid transforms;
- reference-resolution epochs;
- link validity and participation state.

No portal runtime type contains `RegistryId`, a span of registries, registry attachment
order, or cross-registry lookup state.

### 3.2 Zone partitions remain meaningful

A zone remains the authored, cooked, streamed, resident, and participation atom. Portal
logic must therefore coordinate:

- endpoint reference resolution;
- destination residency;
- Visible, Physics, Logic, and Audio participation;
- entity storage migration after committed traversal;
- endpoint detach and reload;
- partition-slot reuse without stale aliasing.

The destination may be spatially remote. No portal subsystem may infer destination
relevance from ordinary world-space proximity.

### 3.3 Portal transfer and partition migration are distinct facts

A portal crossing changes spatial state immediately at a deterministic simulation boundary.
A nonpersistent entity may also need to move from its source zone partition to the
destination zone partition.

The portal transfer mechanism therefore records:

- transformed pose;
- transformed linear and angular velocity;
- source and destination endpoint sides;
- source and destination storage partitions;
- whether storage migration is required;
- the legal structural drain at which migration commits.

The entity keeps the same `EntityId`, component signature, component values, gameplay
relationships, and retained backend record.

Partition ownership does not decide which portal representation is physically valid while a
body is straddling. The crossing record and contact-domain rules do.

### 3.4 Participation leases are part of correctness

One-hop portal demand uses the unified runtime's caller-held participation leases:

- a visible portal requests destination Visible participation;
- a nearby traversable portal requests destination Physics and, when needed, Logic;
- a straddling body pins source and destination Physics participation;
- a transfer keeps required participation until migration and clear hysteresis complete;
- portal audio requests destination Audio participation only when implemented;
- portal demand never recursively activates a connected portal graph.

If a required destination cannot become resident or participate, traversal refuses safely
and rendering uses a defined fallback.

## 4. Architecture facts that shape the plan

### 4.1 Rendering

The correct boundary remains simulation extraction into render-domain queues. Portal views
extend that boundary:

- one `World` is queried through explicit partition sets;
- destination view extraction receives the destination-visible partitions;
- an offscreen feature renders transient portal-view records;
- a main-color feature composites targets onto aperture surfaces;
- crossing entities emit transient clipped source and destination representations;
- the Vulkan backend never resolves links or queries ECS.

Do not widen a public module boundary unless current source proves it necessary.

### 4.2 Physics

The unified runtime owns one simulation-scoped physics scene with separate retained record
families. Portal rigid-body state belongs in a dedicated physics record family, not in ECS
components or a per-zone service.

The physics prerequisite is no longer creation of a shared scene. It is completion and use
of the unified scene contracts for:

- angular state and impulses;
- constraints and contact modification;
- zone secondary indices;
- entity partition migration journals;
- participation eviction and restoration;
- body lifetime across partition migration.

### 4.3 Lighting

The current lighting architecture remains the foundation:

- dynamic point and spot lights;
- RGB9E5 per-zone direct-light atlases;
- R8 baked ambient occlusion;
- L1 irradiance probes;
- deterministic bake BVHs;
- cached point and spot shadow residency;
- profiling captures and evidence.

Portal lighting extends these mechanisms. It does not create a second renderer, cooker,
light representation, or world concept.

### 4.4 Editor and cook

Editor documents remain isolated ownership domains. Runtime unification does not unify
editor registries.

World-scene link entities therefore use durable endpoint references. World cook resolves
those references across zone documents before individual zone lighting cooks. The runtime
imports the link entity into the persistent partition and resolves the same references as
zones attach.

## 5. Document map

| Document | Owns |
| --- | --- |
| `00-execution-overview.md` | Scope, dependency order, invariants, stage order, decisions, and stop conditions. |
| `01-link-data-and-authoring.md` | Persistent relationship entities, endpoint data, stable references, validation, serialization, cook resolution, and leases. |
| `02-view-rendering-and-character-traversal.md` | Geometry kernel, v0.1 traversal, transfer staging, partition-aware portal views, offscreen rendering, and compositing. |
| `03-crossing-entities-and-rigid-bodies.md` | Crossing render records, unified physics record family, coupled-body investigation, contact filtering, and migration. |
| `04-light-transport.md` | World-cook link resolution, static direct bake, probes, dynamic light images, shadow transport, and bounded work. |
| `05-validation-stress-and-profiling.md` | Unified-world test matrix, adversarial scenarios, migration and unload tests, benchmarks, counters, sanitizers, and evidence. |

## 6. Pinned decisions

### D1. Unified runtime is a prerequisite

Runtime portal code does not land on the per-zone-registry architecture. Pure geometry,
shader experiments, cook experiments, and fixture design may proceed earlier.

No adapter, fallback, feature flag, or dual path preserves old runtime registry ownership.

### D2. Explicit binary relationship

A persistent relationship entity owns exactly two `StableEntityRef` values. Endpoint
components do not store a partner or shared grouping id.

The relationship entity's durable identity is the stable portal identity used by cook,
render, physics, light, shadow, diagnostics, and deterministic ordering.

### D3. Endpoint uniqueness is validated globally

The binary record structurally guarantees two endpoint slots. World validation additionally
requires that one endpoint is not owned by two enabled portal relationships.

Normal editor commands preserve this rule. Cook and runtime validation still reject hand-edited,
stale, or conflicting data.

### D4. Rectangular rigid apertures first

v0.1 and v1.0 support rectangular endpoints under translation and rotation with unit scale.
Nonuniform, negative, animated, or nonfinite scale is rejected.

### D5. Static endpoints through v1.0

Entities, characters, rigid bodies, and lights may move. Portal endpoints do not.

### D6. Authored openings

The surrounding wall and opening are authored geometry. Runtime wall cutting is not required.

### D7. One-hop transport

Views, rays, lights, probes, traces, leases, and traversal cross at most one link per
operation. A portal visible through a portal uses fallback presentation and does not schedule
another destination.

### D8. One active link per body

A character or rigid body may straddle one link at a time. A second overlapping link behaves
as solid or nontraversable according to the tested safe rule.

### D9. No duplicate gameplay entities

Crossing render representations are transient render-domain data. Physics proxies are
backend-owned retained records. Neither is another gameplay entity or serialized ECS entity.

### D10. Exact aperture constraints

Broadphase bounds find candidates. Final rendering, traversal, light, ray, and contact
behavior uses the exact plane and rectangular aperture.

### D11. Source and destination lighting remain distinct

Source and destination render representations use lighting, probes, baked data, and shadows
from their own spatial domain. Lighting is not copied as object state.

### D12. Storage migration preserves identity

A completed traversal may move a nonpersistent entity to the destination storage partition.
The move uses the unified runtime migration mechanism and preserves `EntityId`, component
state, and retained backend identity.

### D13. Measurements define budgets

No arbitrary millisecond or memory budget is invented. Each stage records a control and
feature baseline on the same hardware, build, resolution, scene, and scripted path.

### D14. Stage-sized commits

Each stage is independently buildable, tested, reviewable, and revertible. The complete suite
is green at every merge gate.

## 7. Stage order

### Stage U. Unified runtime dependency gate

Required before runtime portal code:

1. One shipping runtime `World`.
2. Zone storage partitions and domain partition sets.
3. Persistent partition.
4. Durable `StableEntityRef` encoding and resolution.
5. Detached package fixups for stable references.
6. Entity partition migration preserving `EntityId`.
7. Participation leases.
8. One simulation-scoped physics scene with zone-indexed record families.
9. Current lighting architecture integrated onto the unified runtime.
10. Legacy runtime registry paths deleted.

If any contract differs when merged, revise this suite before implementation.

### Stage 0. Baseline and fixtures

Record compiler, build, GPU, driver, resolution, present mode, worker count, unified-runtime
commit, and lighting commit. Build deterministic portal, character, physics, lighting, and
remote-zone fixtures. Capture zero-portal baselines.

### Stage 1. Relationship data and pure geometry

Implement endpoint and relationship components, durable reference resolution, editor commands,
validation, rigid transforms, aperture math, continuous crossing, and debug visualization.

No renderer or physics integration beyond pure and resolution tests.

### Stage 2. Destination demand and v0.1 character traversal

Implement one-hop participation leases, destination availability, overlap state, center-plane
crossing, pose and velocity transfer, transfer staging, optional partition migration, and
backing out.

Headless tests pass before portal rendering begins.

### Stage 3. v0.1 view rendering

Implement partition-aware destination extraction, visible-aperture ranking, portal cameras,
offscreen targets, destination clipping, surface compositing, resize and teardown, counters,
GPU scopes, and captures.

### Stage 4. Crossing render representations

Implement clipped source and destination records for static, kinematic, skinned, and scripted
dynamic meshes. Integrate stable synthetic identity and shadow-caster extraction.

### Stage 5. Unified physics portal record foundation

Add the dedicated portal body record family and the contact, constraint, angular-state,
partition-index, lifecycle, and diagnostic mechanisms required by the coupling spike.

### Stage 6. Coupled-body investigation

Implement and measure:

1. canonical body plus transformed collision proxy with impulse relay;
2. transform-constrained dynamic pair with corrected effective mass.

Choose by evidence. Delete the rejected production candidate.

### Stage 7. Coupled rigid bodies and migration

Implement full-shape overlap, source and destination contact filtering, proxy lifetime,
authority swap, partition migration, sleep, wake, CCD policy, rim collision, leases, unload
handling, and diagnostics.

### Stage 8. Static light transport

Resolve links at world-cook scope. Implement one-hop direct-light atlas transport and
portal-aware probe rays with deterministic hashes.

### Stage 9. Dynamic light transport

Implement unshadowed point and spot images, then portal-aware shadow maps, stable residency,
and invalidation across both endpoint partitions.

### Stage 10. Integration hardening

Complete remote-zone demand, editor tooling, forced teardown policies, caps, sanitizers,
profiling sweeps, evidence, and documentation.

## 8. Merge gates

### Relationship gate

- a relationship stores exactly two durable references;
- A and B are distinct valid endpoints;
- an endpoint is owned by at most one enabled relationship;
- unresolved streaming state is distinct from malformed content;
- stale resolution caches are rejected by epoch or validity checks;
- partition-slot reuse cannot alias a former endpoint;
- no runtime id is persisted.

### Rendering gate

- extraction consumes one `World` and explicit partition sets;
- the backend never queries ECS;
- zero active portals schedule no portal pass or target work;
- view and target counts are bounded;
- resize, minimize, unload, hot reload, and teardown release resources;
- nonrecursive behavior is explicit and tested.

### Transfer gate

- spatial transfer and storage migration have one documented phase contract;
- `EntityId` and component values survive migration;
- migration happens at most once per committed crossing;
- required partitions remain participating until transfer and clear complete;
- a forced destination teardown produces an explicit safe outcome;
- render and physics never reconstruct the gameplay entity.

### Physics gate

- one simulation-scoped backend owns canonical and proxy bodies;
- Jolt types remain behind the physics boundary;
- mass and inertia are not counted twice;
- contacts on either side affect one logical motion state;
- no persistent penetration or unbounded correction impulse;
- migration updates backend zone indices without body recreation;
- sleep, wake, CCD, detach, and shutdown pass.

### Lighting gate

- illumination cannot leak around the aperture;
- source and destination occlusion independently block transport;
- stable request identity derives from the persistent relationship and source object;
- work is bounded and deterministically prioritized;
- bake output is byte-identical for identical inputs;
- link, endpoint, light, geometry, and setting changes restale exactly affected outputs.

### Performance gate

- the zero-portal path remains inside measured noise;
- cost scales with selected target pixels, visible destination content, active crossing bodies,
  and transmitted lights rather than total resident world content;
- remote dormant zones add no per-entity hot-path work;
- target, proxy, lease, and cache memory obey documented caps;
- median, p95, and p99 deltas are recorded.

## 9. Stop conditions

Stop and request owner review when:

1. Durable stable-reference support is not complete enough for world-scene relationship data.
2. The unified runtime or lighting integration changes a relied-on ownership contract.
3. A public SDK or game-module ABI change becomes necessary.
4. A persisted or cooked format needs nonadditive migration.
5. Runtime collision carving becomes required.
6. Moving endpoints become required.
7. One body must occupy more than two portal-related coordinate spaces.
8. Jolt cannot provide the required contact or constraint mechanism without leaking types.
9. A portal relationship would need to become topology policy inside `ZoneRuntime`.
10. Destination participation cannot be expressed through existing lease mechanisms.
11. A new interface has one implementation and no real module or backend boundary.
12. A stage requires a lock, raw thread, `std::async`, or third worker lane.
13. The zero-portal path gains measurable allocations, GPU work, or broad world scans.
14. A test is being hidden by tolerance inflation, sleeps, snapshots, or disabled coverage.
15. A plan assumption disagrees with merged source or tests.

## 10. Definition of done

v0.1 is done when:

- two persistent relationships resolve same-zone and cross-zone endpoint pairs;
- a spatially remote destination is requested explicitly and never by proximity inference;
- portal views render correctly from arbitrary angles;
- the player traverses both directions with transformed view and velocity;
- a zone-owned test entity migrates partitions without changing `EntityId`;
- the player may stop halfway, back out, graze an edge, and cross at high speed;
- unavailable destination behavior is safe and diagnosed;
- no recursive destination is scheduled;
- focused tests, complete tests, sanitizers, captures, and profiling evidence pass.

v1.0 is done when:

- renderable entities split cleanly across the plane;
- a long dynamic collider behaves as one body across both spaces;
- source and destination contacts produce coherent translation and torque;
- storage migration and backend zone indices remain correct;
- unload and reload restore durable endpoint resolution;
- direct atlases and probes transport light through static links;
- dynamic point and spot lights cross links without aperture leakage;
- shadowed lights respect occluders on both sides;
- remote zones, caps, sleep, wake, CCD, resize, teardown, and forced detach pass;
- the adversarial matrix and profiling gates pass;
- every deferred limitation is explicit.
