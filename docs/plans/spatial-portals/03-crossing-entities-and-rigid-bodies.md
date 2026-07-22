# Crossing Entities and Rigid Bodies

Status: proposed execution specification.

This document owns the v1.0 entity and physics work. It intentionally separates visual duplication from physical representation.

## 1. Crossing state

A crossing record represents one logical entity interacting with one resolved link.

It contains values equivalent to:

- canonical entity identity;
- source and destination endpoint identities;
- source and destination transforms;
- collider support interval along the source plane;
- exact aperture-prism overlap;
- source-side and destination-side clip planes;
- current authority side;
- optional physics proxy handle;
- previous crossing classification;
- activation and clear hysteresis.

The record is owned by the spatial-portal physics integration, not by a duplicate ECS entity.

A render extraction step reads immutable crossing records and emits render representations.

## 2. Moving render representations

### 2.1 Static and kinematic meshes

A transform-driven mesh that intersects the aperture emits:

1. a source representation at its ordinary world transform, clipped to the source side;
2. a destination representation at the linked transform, clipped to the destination side.

The ordinary mesh extractor must not also emit an unclipped full representation.

The two records share:

- mesh handle;
- material set;
- section mask;
- animation state;
- skinning palette;
- authored visibility and shadow flags.

They differ in:

- world transform;
- world bounds;
- camera depth;
- zone lightmap and AO bindings;
- active probes and dynamic lights;
- clip plane;
- stable synthetic render identity.

### 2.2 Stable render identity

Synthetic identity derives from:

- canonical `RenderEntityKey`;
- link id;
- endpoint side;
- representation kind.

It does not use a raw pointer, body id, registry attachment order, or transient vector index.

Shadow cache invalidation and sort ties use the same derived identity.

### 2.3 Dedicated clipped pass

Crossing representations use a dedicated clipped mesh pipeline or tightly scoped shader variant.

The ordinary opaque pipeline remains branch-free.

The clip plane is supplied per representation or per run. Run merging includes the clip-plane identity where required for correctness.

Clipping is applied consistently in:

- forward color;
- shadow depth;
- selection and editor overlays where applicable;
- motion or depth-only passes added later.

### 2.4 Skinned meshes

Skinned representations share one evaluated pose.

Preferred implementation:

- evaluate bones once in canonical model space;
- reuse the palette for both representations;
- apply source or destination root transform during vertex transformation;
- clip both outputs by their world plane.

Do not duplicate animation graphs or advance animation twice.

If existing skinning bakes world transforms into bone matrices, extract a narrow model-to-world separation before adding a second evaluation path.

## 3. Physics prerequisite stage

The current rigid-body bridge is linear-only. Complete it before portal coupling.

### 3.1 Engine-facing state

Add backend-free support for:

- angular velocity get and set;
- linear and angular impulse at center;
- impulse at world contact point;
- torque impulse;
- center of mass;
- mass;
- inverse mass;
- inertia or inverse inertia in a backend-free representation;
- sleep and wake state;
- motion-quality or CCD policy if the current backend exposes it;
- body activation;
- contact event records sufficient for portal coupling.

Only add operations consumed by tests, ordinary rigid-body behavior, or the coupling spike. Do not mirror the entire Jolt API.

### 3.2 ECS bridge

`RigidBody` gains angular state only when `PhysicsScene` consumes and publishes it.

The bridge must preserve:

- dynamic pull of rotation, linear velocity, and angular velocity;
- kinematic push of pose;
- body recreation behavior;
- structural-version gate;
- generational entity identity;
- no owning backend handles inside relocatable component data beyond the existing link component.

Mass and gravity-scale behavior must match existing semantics or be corrected as a separate regression with tests.

### 3.3 Contact boundary

Add a physics-internal contact collector or modifier behind the firewall.

The engine-facing record contains mechanical values such as:

- participating body ids;
- user data;
- world contact points;
- normal;
- penetration depth;
- relative velocity;
- accumulated or proposed impulse where available;
- subshape identity only if required for compound filtering.

No Jolt type appears in public engine headers.

## 4. Coupling investigation

Do not choose a coupled-body architecture from intuition alone.

Build both candidates in an isolated physics test harness.

### 4.1 Candidate A: canonical body plus collision proxy

- one body owns canonical mass, inertia, sleep, and ECS state;
- a transformed proxy exists at the linked pose;
- the proxy collides only with valid destination-side contacts;
- solved destination impulses transform back to the canonical body;
- source pose then resynchronizes the proxy;
- authority swaps when the center of mass crosses.

Primary risk:

- post-solve impulse access may be insufficient or one frame late;
- proxy correction may inject energy;
- contact friction and resting stacks may be difficult to relay accurately.

### 4.2 Candidate B: transform-constrained dynamic pair

- source and destination bodies are both dynamic;
- a custom bilateral constraint enforces the portal transform;
- contacts on either body propagate through the solver;
- effective mass is corrected so the pair behaves as one body;
- one representation remains authoritative for ECS publication.

Primary risk:

- doubled mass or inertia;
- overconstraint;
- solver oscillation;
- sleeping and island behavior;
- constraint error at high angular velocity.

### 4.3 Decision rubric

Measure both candidates under identical conditions.

Choose the candidate that best satisfies:

1. bounded pose constraint error;
2. bounded linear and angular momentum error relative to a no-portal control;
3. no doubled gravitational acceleration;
4. no doubled resistance to impulse;
5. stable static friction;
6. stable resting contact on one side and both sides;
7. predictable rim jams;
8. predictable torque from a destination-side contact;
9. stable sleep and wake;
10. stable CCD or documented high-speed policy;
11. lower CPU cost and fewer allocations;
12. simpler contact filtering and authority transfer;
13. smaller backend-specific surface.

The result is recorded in evidence with raw scenario parameters and reduced summaries. The rejected production candidate is deleted.

If neither candidate clears the rubric, stop. Do not ship transform snapping and call it coupled physics.

## 5. Full-shape activation

A body enters crossing state when its actual collider intersects the portal aperture prism.

Do not use only:

- entity origin;
- center of mass;
- axis-aligned bounds;
- a trigger callback.

Broadphase may use bounds. The final test uses the body shape and endpoint frame.

For primitive and convex shapes, use a backend-supported shape query or conservative support mapping. For compound shapes, inspect subshapes and preserve enough identity to filter contacts correctly.

Activation does not require the whole body to fit inside the aperture. The rim and contact filter decide what can move through.

## 6. Contact-domain filtering

While crossing, the full source and destination shapes exist, but each representation may contribute contacts only in its valid coordinate domain.

### 6.1 Source representation

Accept:

- contacts on the source side of the plane;
- contacts with the authored rim and wall outside the aperture;
- contacts within the aperture slab that prevent edge penetration.

Reject:

- contacts behind the source plane whose projected point lies inside the open aperture and therefore belongs to destination space.

### 6.2 Destination representation

Apply the mirrored rule at the linked endpoint.

Accept destination-side geometry and rim contacts. Reject contacts that belong to the source representation.

### 6.3 Pair exclusions

Always reject:

- canonical body against its own proxy;
- two representations of the same logical body;
- proxy against a body whose transformed counterpart already owns the same logical interaction;
- contacts from a second link while the body is coupled to the first, except the safe solid behavior pinned for that case.

Filtering is based on body identity, active link identity, plane side, aperture projection, and contact point. It is not a global class-name or gameplay-type branch.

## 7. Long-collider acceptance behavior

The canonical stress object is a long rectangular pole.

Required behavior:

1. Lying across the wall outside the opening, it collides with the wall and does not penetrate.
2. A tip entering the aperture activates a destination representation before the center of mass crosses.
3. Destination gravity or support can torque the complete pole.
4. The source portion continues to hit the source rim.
5. The destination portion hits destination geometry.
6. The pole can pivot and jam diagonally.
7. The pole can slide through under gravity.
8. The pole can be pulled back out without teleporting.
9. Center-of-mass crossing changes authority without a visible or physical snap.
10. Linear and angular velocity remain continuous under the link rotation.
11. No contact is solved twice.
12. No part collides with geometry hidden behind the open aperture in the wrong space.

Acceptance uses numeric pose, velocity, angular velocity, contact count, impulse, and constraint-error traces in addition to video or screenshots.

## 8. Authority transfer

### 8.1 Transfer point

Authority changes when the swept center of mass crosses the portal plane inside the aperture.

The proxy remains alive through a clear band so contacts do not disappear on the transfer frame.

### 8.2 Transfer transaction

A transfer:

1. captures canonical pose, linear velocity, angular velocity, sleep state, and relevant controller state;
2. maps them through the link;
3. promotes the destination representation;
4. demotes or rebuilds the source proxy;
5. updates the active endpoint side;
6. refreshes contact filters;
7. preserves the logical entity and `PhysicsBodyLink`;
8. publishes one coherent ECS state after the physics step.

No structural ECS mutation occurs inside an active query or contact callback.

### 8.3 Constraint and held-object state

Before v1.0 completion, explicitly classify:

- unconstrained rigid body;
- compound body;
- body connected by a joint to a noncrossing body;
- multiple bodies in a jointed assembly;
- kinematic grab or held-object controller;
- character pushing the body;
- body sleeping while straddling.

Pinned minimum:

- unconstrained primitive, convex, mesh-backed dynamic, and compound bodies must work;
- a jointed assembly may be rejected safely with diagnostics if the entire assembly cannot transfer coherently;
- held objects must either remain held through a tested controller transform or be released by a documented rule;
- rejection never leaves a body inside solid geometry.

The final held and joint policy is an owner-review gate after the coupling spike.

## 9. CCD and tunneling

High-speed bodies use swept portal activation and the backend's motion-quality mechanism where available.

Tests include:

- small sphere faster than portal slab thickness per tick;
- long pole rotating so a tip sweeps through between poses;
- body crossing near an aperture corner;
- body contacting destination geometry on the transfer tick.

If portal-specific contact filtering invalidates backend CCD assumptions, define a conservative substep or sweep policy and profile it. Do not globally raise physics substeps without evidence.

## 10. Sleeping and lifecycle

A crossing body stays awake while:

- overlap classification changes;
- either representation has active contacts;
- coupling error exceeds tolerance;
- authority transfer is pending.

It may sleep only when both representations are stable and the coupling mechanism agrees.

On clear:

- destroy the proxy;
- remove crossing state;
- restore ordinary collision filtering;
- preserve canonical sleep state where safe.

On entity destroy, collider removal, zone unload, link disable, or endpoint invalidation:

- remove proxy bodies first;
- clear contact-filter state;
- release retained shapes;
- never leave a stale physics body keyed by a recycled entity id.

## 11. Physics diagnostics

Add counters with live producers:

- near-portal body candidates;
- exact shape overlaps;
- active coupled bodies;
- proxy bodies;
- source contacts accepted and rejected;
- destination contacts accepted and rejected;
- relayed impulses or constraint rows;
- authority transfers;
- rejected second-link overlaps;
- maximum coupling position error;
- maximum coupling angular error;
- proxy create and destroy count;
- CCD portal sweeps.

Debug draw:

- source and destination collider representations;
- center-of-mass path;
- aperture prism;
- accepted and rejected contact points;
- force and impulse vectors;
- authority side;
- coupling error;
- sleep state.

## 12. Tests

### 12.1 Rigid-body foundation

Extend focused physics tests for:

- angular velocity integration;
- impulse at center;
- impulse off center producing torque;
- inertia and mass reporting;
- sleep and wake;
- dynamic ECS round trip;
- body recreation preserving authored state;
- physics isolation and public-header checks.

### 12.2 Coupling harness

Run both candidates against:

- sphere, box, capsule, convex hull, and compound;
- horizontal, vertical, 90-degree, and 180-degree links;
- no-contact free flight;
- one-side resting contact;
- contacts on both sides;
- static and dynamic collision partner;
- frictional slide;
- rim jam;
- long pole;
- stacked bodies;
- sleeping body;
- high linear speed;
- high angular speed;
- repeated back-and-forth traversal;
- 10,000 fixed ticks;
- link disable and entity destroy.

### 12.3 Determinism and sanitizers

- identical fixed-step input produces identical ordered state traces on the serial reference path;
- any parallel extraction path matches stable ordering;
- ASAN run covers create, transfer, clear, destroy, unload, and shutdown;
- TSAN runs if coupling introduces shared worker access;
- no test uses sleeps for state synchronization.

## 13. Stop conditions

Stop before production coupling when:

- the Jolt version lacks the required stable contact or constraint hook;
- the only solution leaks Jolt types through public engine headers;
- both candidates inject unbounded energy or fail resting contact;
- compound shape filtering cannot identify valid contact domains;
- dynamic portal collision requires runtime wall carving;
- a jointed assembly policy cannot be made safe;
- a proxy must become a gameplay entity to receive events;
- per-body work becomes proportional to all portals rather than nearby portals;
- tolerances need to grow with elapsed time to hide drift.
