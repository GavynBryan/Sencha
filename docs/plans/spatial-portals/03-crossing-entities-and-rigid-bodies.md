# Crossing Entities and Rigid Bodies

Status: proposed execution specification, revised for the unified runtime world.

This document owns v1.0 crossing render representations and rigid-body traversal. It
separates four concerns that share one logical entity but have different owners:

1. authored gameplay entity and components;
2. storage partition ownership;
3. backend physics representations;
4. transient render representations.

No concern is allowed to masquerade as another.

## 1. Unified physics assumptions

Implementation depends on:

- one runtime ECS `World`;
- one simulation-scoped physics scene;
- separate retained record-family modules;
- rigid bodies and character movers indexed by live `EntityId`;
- backend secondary indices by storage partition;
- entity partition migration journals;
- participation eviction and restoration;
- backend-free angular state, impulses, constraints, and contact hooks;
- body lifetime preserved across partition migration;
- deterministic owner-thread lifecycle reconciliation.

Portal physics does not land on per-zone physics services or per-registry body bindings.

## 2. Crossing state

A crossing record represents one logical entity interacting with one resolved relationship.

It contains values equivalent to:

- canonical entity `EntityId` and stable persistent identity where available;
- persistent relationship identity;
- live endpoint `EntityId` values and resolution epoch;
- source and destination endpoint sides;
- source and destination storage partitions;
- canonical pose and linked pose;
- full-shape support and aperture-prism overlap;
- source and destination clip planes;
- spatial authority side;
- current storage ownership;
- optional destination physics proxy handle;
- participation lease handles;
- pending storage migration;
- activation and clear hysteresis;
- diagnostics and coupling error.

The dedicated physics portal record family owns this data. It is not serialized and is not a
second ECS entity.

Render extraction consumes an immutable view of active crossing records.

## 3. Crossing render representations

### 3.1 General rule

A renderable intersecting an active aperture emits:

1. source representation at the canonical source-side transform, clipped to source space;
2. destination representation at the linked transform, clipped to destination space.

The ordinary extractor must not also emit an unclipped complete representation.

The representations share:

- canonical entity identity;
- mesh and material handles;
- section and layer masks;
- skinning palette and animation state;
- authored visibility, receive-shadow, and cast-shadow flags.

They differ in:

- world transform and bounds;
- endpoint side;
- storage-domain lighting inputs;
- baked atlas and AO bindings;
- active probes and dynamic lights;
- clip plane;
- stable synthetic render key.

### 3.2 Stable synthetic identity

Synthetic render identity derives from:

- canonical persistent or live render identity;
- persistent relationship identity;
- endpoint side;
- representation kind.

It does not use body handles, raw pointers, partition slots, import order, or transient vector
indices.

Shadow caches and deterministic sort ties use the same identity derivation.

### 3.3 Partition-aware extraction

A crossing entity remains one ECS row in one storage partition at any instant. Render
representations may appear in two spatial domains while the row remains in one partition.

Therefore crossing extraction must not rely only on ordinary per-partition spatial ownership.
It receives explicit active crossing records and emits both representations into the
appropriate main or portal-view queues.

The source representation uses source-domain lighting and the destination representation uses
destination-domain lighting even before storage migration commits.

### 3.4 Dedicated clipped path

Crossing representations use a dedicated clipped pipeline or tightly scoped shader variant.
The ordinary opaque path remains branch-free.

Clip correctness applies consistently to:

- forward color;
- shadow depth;
- editor selection and debug overlays where applicable;
- future motion or depth-only passes.

Run merge identity includes clip state where required.

### 3.5 Skinned meshes

Evaluate animation once for the canonical entity.

Preferred shape:

- one model-space pose;
- one shared skinning palette;
- source or destination root transform applied during vertex transformation;
- independent world clip plane per representation.

Do not advance animation twice or duplicate animation graphs.

## 4. Dedicated physics record family

Portal retained state belongs in a separate module mechanically equivalent to:

```text
PhysicsScene
|-- RigidBodyRecords
|-- CharacterMoverRecords
|-- ConstraintRecords
`-- SpatialPortalBodyRecords
```

`SpatialPortalBodyRecords` owns:

- nearby-body candidate collection;
- exact full-shape overlap;
- source-to-proxy association;
- proxy body lifetime;
- contact-domain filtering;
- coupling constraint or impulse relay;
- spatial authority side;
- partition ownership observation;
- migration request publication;
- endpoint detach behavior;
- diagnostics and evidence traces.

The physics composition root owns ordering only. Portal body storage, lifecycle, tests, and
diagnostics remain in the dedicated module.

## 5. Physics prerequisite verification

The unified branch is expected to supply much of the former prerequisite work. Before the
coupling spike, verify the merged implementation exposes backend-free mechanisms for:

- angular velocity get and set;
- linear and angular impulse at center;
- impulse at world point;
- torque impulse;
- center of mass;
- mass and inverse mass;
- inertia or inverse inertia;
- sleep and wake;
- motion quality or CCD policy;
- retained constraints;
- contact modification or post-solve impulse information;
- body and constraint lifecycle across partition migration;
- partition-index updates without body recreation.

Add only mechanisms consumed by ordinary rigid-body behavior or the portal spike. Do not
mirror the entire Jolt API.

No Jolt type appears in public engine headers.

## 6. Coupled-body investigation

Do not choose a final coupling architecture from intuition.

Build both candidates inside an isolated physics harness using the same unified backend scene.

### 6.1 Candidate A: canonical body plus transformed collision proxy

- one body owns canonical mass, inertia, sleep, and ECS publication;
- one transformed proxy exists at the linked pose;
- proxy contacts are restricted to the valid destination domain;
- solved destination impulses transform back to the canonical body;
- canonical pose resynchronizes the proxy;
- spatial authority swaps when center of mass crosses;
- storage partition migration is independent and staged through the runtime journal.

Risks:

- solved impulse information may arrive too late;
- proxy correction may inject energy;
- friction and resting stacks may relay poorly;
- source and destination contact ordering may become backend-sensitive.

### 6.2 Candidate B: transform-constrained dynamic pair

- source and destination bodies are dynamic;
- a custom bilateral constraint enforces the portal transform;
- contacts on either representation propagate through the solver;
- effective mass and inertia are corrected so the pair behaves as one body;
- one representation publishes canonical ECS state;
- storage partition migration does not recreate either body.

Risks:

- doubled effective mass or inertia;
- overconstraint and solver oscillation;
- sleep-island instability;
- high-angular-velocity drift;
- awkward authority transfer.

### 6.3 Decision rubric

Measure both candidates under identical conditions and against equivalent no-portal controls.

Choose the candidate that best satisfies:

1. bounded position and angular coupling error;
2. bounded linear and angular momentum error;
3. no doubled gravitational acceleration;
4. no doubled resistance to impulse;
5. stable static and dynamic friction;
6. stable resting contact on either side and both sides;
7. predictable rim jams;
8. destination-side contact produces correct whole-body torque;
9. stable sleep and wake;
10. stable CCD or a documented high-speed policy;
11. no body recreation during partition migration;
12. lower CPU cost and fewer allocations;
13. simpler contact filtering and authority transfer;
14. smaller backend-specific surface.

Record raw scenario parameters and reduced summaries. Delete the rejected production
candidate.

If neither candidate clears the rubric, stop. Transform snapping is not accepted as coupled
rigid-body physics.

## 7. Full-shape activation

A body enters crossing state when its actual shape intersects the aperture prism.

Do not use only:

- entity origin;
- center of mass;
- axis-aligned bounds;
- trigger callbacks.

Broadphase may use bounds. Final activation uses backend shape queries or conservative support
mapping.

For compound shapes, inspect subshapes and preserve enough identity for correct contact-domain
filtering.

Activation does not require the complete body to fit. The authored rim and valid contacts
determine whether the body can progress.

## 8. Contact-domain filtering

While crossing, complete source and destination shape representations exist, but each may
contribute contacts only within its valid spatial domain.

### 8.1 Source representation

Accept:

- contacts on the source side;
- contacts with source wall and rim outside the aperture;
- aperture-edge contacts that prevent solid penetration.

Reject:

- contacts behind the source plane whose projected point lies strictly inside the open
  aperture and belongs to destination space.

### 8.2 Destination representation

Apply the mirrored rule at the linked endpoint.

Accept destination geometry and rim contacts in the valid destination domain. Reject contacts
that belong to the source representation.

### 8.3 Pair exclusions

Always reject:

- canonical body against its own proxy;
- two representations of the same logical body;
- duplicate logical interactions represented on both sides;
- contacts from a second portal relationship while already coupled, except the pinned safe
  nontraversable behavior.

Filtering uses canonical body identity, persistent relationship identity, endpoint side,
contact point, plane side, aperture projection, and subshape identity where required.

It does not branch on gameplay class names.

## 9. Long-collider acceptance behavior

The canonical stress object is a long rectangular pole.

Required behavior:

1. Lying across solid wall outside the aperture, it remains blocked.
2. A tip entering the aperture activates the destination representation before center of mass
   crosses.
3. Destination gravity or support can torque the entire pole.
4. The source section continues colliding with the source rim.
5. The destination section collides with destination geometry.
6. The pole can pivot and jam diagonally.
7. It can slide through under gravity.
8. It can be pulled back out without teleportation.
9. Center-of-mass crossing changes spatial authority without a visible or physical snap.
10. Linear and angular velocity remain continuous under link rotation.
11. Storage partition migration occurs exactly once when required.
12. Migration does not recreate the body or clear coupling state.
13. No contact is solved twice.
14. No part collides with wrong-domain geometry behind the open aperture.

Acceptance uses numeric pose, velocity, angular velocity, contact, impulse, coupling-error,
partition, and backend-handle traces.

## 10. Spatial authority and storage ownership

These are separate state machines.

### 10.1 Spatial authority

Spatial authority selects which representation publishes the canonical pose and velocity.
It changes when the swept center of mass crosses the portal plane inside the aperture.

The proxy remains alive through a clear band so contacts do not vanish on the transfer frame.

### 10.2 Storage ownership

Storage ownership selects the entity's zone partition for streaming and normal partition
queries.

Rules:

- persistent entities do not migrate;
- zone-owned entities normally migrate to the destination endpoint's zone after committed
  center crossing;
- migration uses `MoveEntityToZone` at the unified runtime's legal structural drain;
- the live `EntityId`, component signature, and retained physics record survive;
- the crossing record remains active until migration commits and the complete shape clears;
- both endpoint Physics leases remain held through that period;
- partition ownership never determines contact-domain validity while straddling.

### 10.3 Physics-step crossing and migration timing

A dynamic body may cross during the physics step, after the ordinary pre-physics structural
drain.

Pinned contract:

1. the physics portal record detects center crossing and changes spatial authority inside the
   backend;
2. it emits a deterministic pending portal-transfer fact;
3. physics pull publishes one coherent transformed pose and velocity;
4. the next existing legal structural drain applies partition migration before the next
   domain-specific logic or physics work that requires destination ownership;
5. portal crossing records provide explicit spatial-domain information during the bounded
   interim;
6. both source and destination participation remain pinned;
7. render extraction uses crossing records and therefore does not depend on migration having
   committed in the same frame.

Do not add a second arbitrary structural mutation window solely to make the partition label
change earlier.

If gameplay systems require destination-zone logic between physics pull and the next legal
drain, stop and revise the engine schedule explicitly rather than adding a hidden mutation
path.

### 10.4 Transfer transaction

A committed transfer captures:

- canonical pose;
- linear and angular velocity;
- sleep state;
- source and destination endpoint side;
- source and destination partitions;
- backend representation handles;
- pending migration requirement;
- lease ownership.

It then:

- maps motion through the relationship;
- changes spatial authority;
- preserves proxy continuity;
- queues migration when required;
- updates backend partition indices from the migration journal;
- publishes one ECS pose;
- clears only after complete shape separation and migration completion.

No ECS structural mutation occurs inside a query or contact callback.

## 11. Constraints, held objects, and assemblies

Before v1.0 completion, classify:

- unconstrained rigid body;
- compound body;
- body joined to a noncrossing body;
- jointed assembly with several crossing members;
- kinematic grab or held-object controller;
- character pushing a crossing body;
- sleeping body while straddling.

Pinned minimum:

- unconstrained primitive, convex, mesh-backed dynamic, and compound bodies work;
- one held-object controller either transforms coherently or releases by an explicit tested
  rule;
- unsupported jointed assemblies reject traversal safely with diagnostics;
- rejection never leaves a body inside solid geometry;
- participation leases cover every retained constraint endpoint while supported.

Final joint and held-object policy is an owner-review gate after the coupling spike.

## 12. CCD and tunneling

High-speed bodies use swept portal activation plus backend motion quality where available.

Tests include:

- sphere faster than aperture slab thickness per tick;
- long pole whose tip sweeps through while center remains outside;
- corner crossing;
- destination collision on authority-transfer tick;
- partition migration pending during high-speed clear.

If portal contact filtering invalidates backend CCD assumptions, define and profile a
conservative sweep or substep policy. Do not globally increase physics substeps without
evidence.

## 13. Sleeping, detach, and lifecycle

A crossing body remains awake while:

- overlap classification changes;
- either representation has active contacts;
- coupling error exceeds tolerance;
- authority or migration transfer is pending;
- a required endpoint lifecycle transition is pending.

It may sleep only when both representations and coupling agree.

On clear:

- destroy proxy state;
- release crossing leases;
- restore ordinary collision filtering;
- retain the canonical body and sleep state;
- confirm partition migration and backend indices agree.

On entity destroy, collider removal, relationship disable, endpoint detach, or engine
shutdown:

- receive the explicit lifecycle visit;
- remove proxies and constraints first;
- clear contact-filter state;
- release retained shapes and leases;
- avoid stale records keyed by recycled `EntityId` or partition slot;
- record the terminal outcome.

Forced endpoint teardown while a body straddles is a stop condition until a safe evacuation,
refusal, or hard-destroy policy is approved and tested.

## 14. Diagnostics

Physics portal counters include:

- nearby endpoint candidates;
- exact shape overlaps;
- active crossing bodies;
- proxy bodies;
- accepted and rejected source contacts;
- accepted and rejected destination contacts;
- impulse relays or constraint rows;
- spatial authority transfers;
- queued and committed partition migrations;
- backend zone-index updates;
- rejected second-link overlaps;
- maximum position and angular coupling error;
- proxy creates and destroys;
- CCD portal sweeps;
- active lease counts;
- forced-detach outcomes.

Debug draw includes:

- source and destination shapes;
- center-of-mass path;
- aperture prism;
- accepted and rejected contacts;
- impulses and constraint error;
- spatial authority side;
- current and pending storage partition;
- backend handles in diagnostic form;
- sleep state and lease reasons.

## 15. Tests

### 15.1 Unified physics conformance

Before portal coupling, prove:

- angular velocity and off-center impulses;
- mass and inertia reporting;
- sleep and wake;
- retained body handle survives `MoveEntityToZone`;
- migration updates partition secondary indices;
- active-to-active, active-to-dormant, dormant-to-active, and dormant-to-dormant migration
  behavior;
- endpoint detach leaves no physics residue;
- public headers contain no Jolt types.

### 15.2 Coupling harness

Run both candidates against:

- sphere, box, capsule, convex, compound, long pole, and wide plate;
- horizontal, vertical, 90-degree, and 180-degree links;
- same-zone and cross-zone endpoints;
- spatially remote destination zone;
- free flight;
- source contact only;
- destination contact only;
- contacts on both sides;
- static and dynamic friction;
- rim jam;
- stacks;
- sleep and wake;
- high linear and angular speed;
- repeated back-and-forth traversal;
- partition migration on and off;
- endpoint unload after clear;
- relationship disable and entity destroy;
- 10,000 fixed ticks.

### 15.3 Determinism and sanitizers

- identical fixed input produces identical ordered state traces on the serial path;
- partition import order and partition slot do not change coupling choice or results;
- parallel extraction matches stable ordering where used;
- ASAN covers create, overlap, proxy, authority transfer, migration, clear, detach, destroy,
  and shutdown;
- TSAN runs if new shared worker access is introduced;
- no test uses sleeps for synchronization.

## 16. Stop conditions

Stop before production coupling when:

- the unified physics scene or migration contracts are incomplete;
- body migration recreates retained bodies;
- required Jolt hooks cannot remain behind backend-free engine types;
- neither coupling candidate preserves mass, momentum, friction, and resting contact;
- compound filtering cannot identify valid contact domains;
- dynamic traversal requires runtime wall carving;
- forced endpoint teardown has no safe explicit outcome;
- joint or held-object behavior cannot reject safely;
- per-body work becomes proportional to every portal relationship;
- partition ownership is being used as a substitute for spatial-domain state;
- tolerances must grow over time to hide drift.
