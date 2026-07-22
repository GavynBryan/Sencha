# Spatial Portal Execution Suite

Status: proposed execution plan.

Base: `lightmap-spike` at `061e610baf338a5b47cd270a88496f42a04b850a`.

This suite defines the implementation path for linked planar apertures that render and traverse discontinuous spaces. It is written against the current lighting branch because that branch owns the renderer, lightmap, probe, shadow-residency, capture, and evidence systems this work must extend.

`CLAUDE.md` was read in full before this plan was written. Its repository constraints apply to every stage. In particular:

- names describe mechanisms, not a game or genre;
- components contain data only;
- renderer backends consume extracted render-domain data, never live ECS state;
- Jolt remains behind the physics firewall;
- no speculative interfaces, service locator, raw threads, locks, or third concurrency lane;
- serial behavior is the deterministic reference;
- tests and measurements ship with each mechanism;
- no stage is called complete while required verification is unrun;
- no em dash is used in code, comments, documentation, or commit messages.

## 1. Product boundary

### 1.1 v0.1

v0.1 reproduces the current browser prototype in Sencha:

1. Two linked, static, rectangular planar apertures.
2. A nonrecursive view through each aperture.
3. Viewpoint-correct perspective at arbitrary viewing angle and distance.
4. Destination-side clipping at the exit plane.
5. First-person character traversal.
6. Position, facing, and velocity transformed through the link.
7. Continuous crossing detection using previous and current capsule-center positions.
8. Predictable straddling. A capsule may overlap the aperture, stop, and back out. Authority changes only when the capsule center crosses the plane.
9. Debug visualization, counters, deterministic tests, and reproducible profiling scenes.

v0.1 does not include:

- recursive views;
- runtime cutting of arbitrary wall collision;
- moving apertures;
- rigid bodies spanning both sides;
- portal-transmitted lights;
- portal-aware shadows;
- scale-changing links;
- one body crossing two links simultaneously.

A portal must be aligned with an authored opening in render and collision geometry. Runtime geometry carving is a separate capability and is not smuggled into this project.

### 1.2 v1.0

v1.0 adds:

1. Static and kinematic mesh entities crossing an aperture with clipped source and destination render representations.
2. Dynamic rigid bodies occupying both coordinate spaces while remaining one logical body.
3. Full-shape aperture overlap, rim collision, and contact filtering.
4. Long and wide colliders that can enter by one end, jam on the rim, rotate, fall through, or back out.
5. Direct baked-light transport through static links.
6. One-hop probe-ray transport so indirect lighting does not treat an open link as a sealed wall.
7. Dynamic point and spot light images transmitted through visible links.
8. Shadowed dynamic-light transport with source and destination occluders.
9. Stable light and shadow identities, bounded work, diagnostics, captures, and evidence.

v1.0 remains nonrecursive. A light, view, trace, or body crosses at most one link per operation unless a later plan explicitly expands the contract.

## 2. Owning invariant

The central invariant is:

> A linked aperture is one rigid mapping between two planar frames. Every participating mechanism uses the same mapping, aperture test, plane-side convention, stable link identity, and one-hop limit.

The invariant is not owned by the renderer, player controller, physics backend, editor, or lighting cooker individually.

The proposed owner is a concrete `SpatialPortalRuntime` object created by the spatial-portal registration path and passed explicitly to the systems that need it. It owns resolved endpoint pairs and transient crossing records. It does not own ECS entities, GPU resources, Jolt bodies, editor documents, or cooked assets.

The core values and pure operations live below that runtime:

- `SpatialPortalLinkId`
- `SpatialPortalFrame`
- `SpatialPortalTransform`
- `SpatialPortalAperture`
- plane classification
- aperture projection
- pose and vector transformation
- continuous crossing
- one-hop ray mapping

The exact filenames are chosen during Stage 1 after the existing math and strong-id conventions are inspected again. The names above describe responsibilities, not a requirement to create one type per bullet.

## 3. Current architecture facts that shape the plan

### 3.1 Rendering

The current render path already has the correct major boundary:

- simulation state is extracted into render-domain queues;
- `MeshForwardPass` draws a supplied camera, light set, and queue;
- offscreen features own their own render targets and passes;
- main-color features are ordered;
- profiling has Off, Counters, Gpu, and Capture tiers;
- the production mesh shader has no generic clipping path.

The current pipeline is centered on one active camera, one render queue, and one light set. Portal views must reuse the extraction and pass mechanisms without making the Vulkan backend query ECS.

The preferred shape is:

- main-view extraction remains the existing path;
- portal-view extraction produces transient render-domain view records;
- an offscreen feature renders those records;
- a main-color feature composites the resulting images onto aperture surfaces;
- a small shared render state object owns the target pool and per-frame view records;
- crossing entities use a dedicated clipped draw path so the ordinary opaque shader does not pay a portal branch.

Do not widen `RenderPacket` or a public module boundary unless inspection proves it is necessary. The current pipeline-owned render state may be sufficient and avoids an ABI change.

### 3.2 Physics

The current physics module has one shared `PhysicsWorld` across active registries, which is favorable for linked-space interaction. The public engine facade currently exposes:

- body creation and removal;
- body transform;
- linear velocity.

The current `RigidBody` component explicitly describes the physics phase as linear-only. v1.0 cannot be built honestly on that contract. Angular velocity, impulses, torque, mass properties, contact information, and portal-specific contact filtering must become first-class backend-free mechanisms before coupled rigid bodies are attempted.

The first physics work is therefore not portal code. It is a narrow completion of the existing rigid-body contract with focused tests.

### 3.3 Lighting

The lighting branch already provides:

- point and spot extraction;
- dynamic forward lighting;
- RGB9E5 per-zone direct-light atlases;
- R8 baked ambient occlusion;
- L1 irradiance probes;
- deterministic bake BVHs;
- cached point and spot shadow residency;
- GPU timestamps, captures, and evidence conventions.

Portal lighting must extend those mechanisms. It must not create a second renderer, second cooker, or side-channel asset path.

### 3.4 World partition and authoring

Earlier partition plans used the word portal for transition markers and later retired those markers. This project is a runtime spatial mapping and must not revive the retired transition-marker architecture.

A spatial portal may connect endpoints in one zone or two zones. The link does not automatically become world-partition topology. Cross-zone residency is an explicit integration stage with its own tests and stop conditions.

The editor has stable partition identities but does not provide a stable persisted entity reference suitable for cross-zone pairing. The plan therefore uses a new editor-minted strong link identity stored on both endpoints. It does not put an entity reference in the world manifest.

## 4. Document map

| Document | Owns |
| --- | --- |
| `00-execution-overview.md` | Scope, invariants, stage order, global decisions, and stop conditions. |
| `01-link-data-and-authoring.md` | Link identity, component data, validation, serialization, cook inputs, editor behavior, and zone participation. |
| `02-view-rendering-and-character-traversal.md` | Pure geometry, v0.1 character traversal, portal-view extraction, offscreen rendering, compositing, and renderer lifecycle. |
| `03-crossing-entities-and-rigid-bodies.md` | Clipped entity rendering, angular-physics prerequisites, coupled-body investigation, contact filtering, long-collider behavior, and transfer authority. |
| `04-light-transport.md` | Static direct bake, probe transport, dynamic light images, shadow transport, cache invalidation, and bounded work. |
| `05-validation-stress-and-profiling.md` | Test matrix, adversarial scenarios, benchmarks, counters, capture evidence, sanitizer runs, and completion gates. |

## 5. Pinned decisions

These decisions are binding unless owner review changes them on the record.

### D1. Rectangular rigid apertures first

v0.1 and v1.0 support a rectangular aperture in a rigid transform with unit scale. Translation and rotation are allowed. Nonuniform, negative, or animated scale is rejected by validation and runtime diagnostics.

This keeps position, direction, velocity, angular velocity, distance attenuation, inertia, and shadow transforms physically coherent.

### D2. Static endpoints through v1.0

Both endpoints are statically placed. Entities and lights may move. Moving endpoints are deferred.

Static placement makes bake hashes, world-collision assumptions, render-target relevance, shadow invalidation, and coupled-body transfer tractable.

### D3. Authored openings, not runtime collision carving

The surrounding wall and opening are authored geometry. The portal surface fills the opening visually, but the collision cook already contains the physical hole and rim.

Runtime wall cutting is not required for v0.1 or v1.0.

### D4. One-hop transport

Views, rays, lights, probes, traces, and crossing state traverse one link. Portal surfaces encountered inside a portal view render a defined fallback and do not schedule another view.

### D5. One active link per body

A character or rigid body may straddle one link at a time. If a body overlaps a second link while coupled, the first active link wins by stable identity and crossing time. The second link remains nontraversable for that body until it clears the first.

The behavior must be safe, deterministic, visible in diagnostics, and tested. Supporting one body across multiple links is a separate solver problem.

### D6. No persistent duplicate entities

Crossing render representations are transient render-domain records. Physics proxies are physics-owned bodies recorded in a physics-owned table. Neither becomes a second gameplay entity or a serialized ECS entity.

### D7. Exact aperture constraints

A portal view, light image, ray, or body representation is valid only through the rectangular aperture. A broadphase volume may find candidates, but final behavior uses the exact plane and aperture test.

### D8. Source and destination lighting remain distinct

A crossing mesh uses source lighting on the source representation and destination lighting on the destination representation. Mesh, material, animation, and skinning data are shared. Lighting is not copied as an object property.

### D9. Measurements define budgets

No unmeasured millisecond budget is invented in this plan. Each performance stage first records a control and a feature baseline on the same hardware, build, resolution, scene, and scripted camera. Acceptance limits are then expressed as a documented delta and scaling law.

### D10. Stage-sized commits

Each implementation stage is independently buildable, tested, reviewable, and revertible. The complete suite is green at every merge gate. A stage that cannot remain green is too large or has an unresolved contract.

## 6. Stage order

### Stage 0. Baseline and reproduction harness

Before product code:

1. Record branch commit, compiler, configuration, GPU, driver, resolution, and present mode.
2. Build the deterministic two-room scene with authored openings.
3. Add a scripted camera and repeatable character path.
4. Capture no-portal render and physics baselines.
5. Confirm the existing profiling Off path remains inert.
6. Confirm the canonical build and test workflow is green.

Output:

- baseline evidence README;
- reduced capture summaries;
- no portal runtime code.

### Stage 1. Link data and pure geometry

Implement:

- strong link identity;
- component registration and serialization;
- endpoint resolver;
- rigid portal transform;
- signed distance and aperture projection;
- continuous segment crossing;
- one-hop point, direction, orientation, and velocity mapping;
- validation and debug drawing.

No renderer or physics integration beyond pure tests.

### Stage 2. v0.1 character traversal

Implement character candidate collection, overlap state, center-plane crossing, velocity and facing transfer, hysteresis, and backing out.

The stage must pass headless character tests before rendering begins.

### Stage 3. v0.1 view rendering

Implement visible-aperture ranking, portal cameras, offscreen targets, destination clipping, surface compositing, resize and teardown, counters, GPU scopes, and captures.

v0.1 is complete only after the combined character and render acceptance scene passes.

### Stage 4. General rigid-body foundation

Complete angular state and force/contact mechanisms in the physics facade, component bridge, and tests. No portal body is introduced until this stage is complete and independently useful.

### Stage 5. Coupled-body investigation spike

Implement two isolated candidates behind test-only or physics-internal code:

1. a canonical body plus transformed collision proxy with contact impulse relay;
2. two dynamic bodies coupled by a portal transform constraint with corrected effective mass.

Run the adversarial matrix in `03-` and `05-`. Choose one based on evidence. Delete the rejected candidate from production code.

### Stage 6. Crossing render representations

Implement clipped source and destination representations for static, kinematic, dynamic, and skinned meshes. Integrate shadow-caster extraction and stable synthetic render identity.

This stage may land before final coupled-body physics if it consumes scripted crossing state.

### Stage 7. Coupled rigid bodies

Implement full-shape overlap, source and destination contact filtering, proxy lifetime, impulse transfer, authority swap, sleeping, CCD policy, rim collision, and diagnostics.

### Stage 8. Static light transport

Implement one-hop direct-light atlas baking and portal-aware probe rays. Extend cook hashes and deterministic world-cook assembly.

### Stage 9. Dynamic light transport

Implement unshadowed point and spot images with exact aperture constraints, then portal-aware shadow maps and residency invalidation.

### Stage 10. Integration hardening

Complete cross-zone residency behavior, editor visualization, content validation, many-portal caps, unload and teardown, sanitizers, profiling sweeps, evidence, and documentation.

## 7. Merge gates

Every stage must satisfy all applicable gates.

### Geometry gate

- pure table-driven tests cover all frame and crossing math;
- no NaN, infinity, or undefined orientation for valid inputs;
- invalid scale and degenerate aperture are rejected;
- serial output is byte-stable where serialized.

### Rendering gate

- Vulkan backend does not traverse ECS;
- zero active portals allocates no portal targets and records no portal pass;
- target count and pixel count are bounded;
- resize, minimize, device teardown, and zone unload release resources;
- nonrecursive behavior is explicit and tested;
- profiling Off adds no capture, timestamp, history, or label work.

### Physics gate

- Jolt types remain inside the physics module;
- mass and inertia are not counted twice;
- source and proxy never collide with one another;
- contact forces on either side affect one canonical motion state;
- no persistent penetration or unbounded correction impulse;
- angular and linear state survive authority transfer;
- sanitizer and long-run tests pass.

### Lighting gate

- illumination cannot leak around the aperture;
- occlusion on either side blocks the path;
- portal images have stable identities;
- light and shadow work is capped and deterministically prioritized;
- static bake output is byte-identical for identical inputs;
- link, light, geometry, and bake-setting changes restale exactly the affected outputs.

### Performance gate

- control scenario remains within measured run-to-run noise;
- cost scales with visible portal pixels, extracted visible content, active crossing bodies, and transmitted lights, not total world content;
- peak target and proxy memory stays within documented formulas and caps;
- median, p95, and p99 deltas are recorded;
- no performance claim is made without a reproducible method.

## 8. Stop conditions

Stop and request owner review before implementation proceeds when:

1. The clean design requires a public SDK or game-module ABI change.
2. A persisted or cooked format needs migration rather than an additive field.
3. A portal must cut arbitrary runtime collision to satisfy the intended content workflow.
4. A moving endpoint becomes required.
5. One body must occupy more than two spaces simultaneously.
6. Jolt cannot expose the contact or constraint information needed without leaking backend types.
7. The coupled-body spike cannot conserve motion within the control-scene error envelope.
8. Portal light shadows require an unbounded caster expansion or recursive render path.
9. Cross-zone traversal would require policy inside `ZoneRuntime` rather than an explicit demand source above it.
10. A proposed interface has only one implementation and no real module, backend, or test boundary.
11. A stage requires a lock, raw thread, `std::async`, or third worker lane.
12. The no-feature path gains measurable allocation, GPU work, or broad registry scans.
13. A test failure is being hidden by tolerance inflation, sleeps, snapshots, or disabled coverage.
14. A plan assumption disagrees with current source or tests.

## 9. Definition of done

v0.1 is done when:

- the authored acceptance scene renders both links correctly from arbitrary angles;
- the player traverses in both directions with transformed view and velocity;
- the player may stop halfway, back out, graze an edge, and cross at high speed;
- no recursive view is scheduled;
- zero, one, and many visible-portal cases are bounded and diagnosed;
- focused tests, complete tests, validation, and profiling evidence pass.

v1.0 is done when:

- renderable entities split cleanly across the plane;
- a long dynamic collider behaves as one body across both spaces;
- source and destination contacts produce coherent translation and torque;
- direct atlas lighting and probe transport cross static links;
- dynamic point and spot lights cross links without aperture leakage;
- shadowed lights respect occluders on both sides;
- cross-zone unload, sleep, wake, CCD, resize, and teardown paths are proven;
- the adversarial matrix and profiling gates pass;
- all deferred limitations are explicit and none are described as implemented.
