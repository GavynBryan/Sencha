# Validation, Stress, and Profiling

Status: proposed execution specification, revised for the unified runtime world.

This document defines how the implementation is challenged. Happy-path screenshots are not
sufficient.

The goal is to falsify the assumptions that:

- durable relationships resolve correctly through unload and reload;
- one rigid transform is shared by every subsystem;
- clipping prevents spatial-domain leakage;
- portal traversal preserves live entity identity;
- storage partition migration is coherent and occurs exactly once;
- a coupled rigid body behaves as one mass and inertia;
- remote destination zones add work only when explicitly demanded;
- caches survive lifecycle changes without stale identity;
- the zero-portal path remains unchanged.

## 1. Verification discipline

### 1.1 Per-stage workflow

For normal code stages:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Run focused tests first while iterating. Do not run CTest in parallel by default.

Additional workflows:

- unified-runtime dependencies: lifecycle, migration, partition, package, reference, and
  shutdown suites;
- rendering: Release profiling build with scripted capture;
- physics: physics isolation plus ASAN;
- concurrency changes: serial reference, worker path, and TSAN where supported;
- public boundary changes: ABI, layout, component schema, and isolation tests;
- cook changes: repeated deterministic output, reference fixups, old and new fixtures;
- editor changes: undo, redo, cancel, focus change, document close, zone unload, and shutdown;
- teardown changes: repeated clean close and process exit under ASAN.

A command not run is reported as unverified. It is not described as passing.

### 1.2 Regression standard

A regression test fails before the fix for the intended reason.

Do not:

- loosen tolerance merely to pass;
- add synchronization sleeps;
- hide failure behind snapshot replacement;
- skip adversarial lifecycle states;
- average away catastrophic outliers;
- call a visual result correct when numeric invariants exist;
- retain an obsolete compatibility path because the new test is difficult.

## 2. Evidence layout

Use:

```text
docs/plans/evidence/spatial-portals/
    unified-runtime-baseline/
    relationship-resolution/
    view-rendering/
    character-traversal/
    entity-migration/
    rigid-body-coupling/
    static-light-transport/
    dynamic-light-transport/
    remote-zone-scale/
    final/
```

Each directory contains:

- `README.md` with question, dependency commits, hardware, build, scene, method, commands,
  warmup, sample count, result, and verdict;
- reduced JSON or CSV summaries;
- selected screenshots, state traces, or diagrams;
- no large regenerable frame captures unless needed to preserve a failure.

The acceptance rubric is written before the result.

## 3. Deterministic fixtures

### 3.1 Relationship world

Contains:

- persistent world-scene portal relationship entities;
- endpoint entities across several zone documents;
- same-zone and cross-zone pairs;
- invalid, disabled, duplicate-owner, and unresolved fixtures;
- zone unload and reload controls;
- deterministic package import orders;
- partition-slot reuse pressure.

### 3.2 View world

Contains:

- two disconnected rooms with asymmetric landmarks;
- different source and destination lighting and probes;
- horizontal and rotated relationships;
- narrow and wide apertures;
- grid geometry touching exit planes;
- scripted camera path;
- moving kinematic marker;
- no random input or animation.

### 3.3 Character world

Contains:

- persistent player and zone-owned test character;
- floor, wall, and ceiling portals;
- rim thickness variations;
- ledges immediately after exits;
- high-speed launch paths;
- edge and corner approach rails;
- destination participation controls;
- fixed input replay;
- migration-journal trace export.

### 3.4 Physics world

Contains:

- sphere, box, capsule, convex, compound, long pole, and wide plate;
- source and destination floors and walls;
- dynamic collision partners;
- stacks and sleep platforms;
- linear and angular launchers;
- relationship disable and endpoint detach controls;
- fixed tick count;
- state, contact, impulse, coupling, body-handle, and partition trace export.

### 3.5 Lighting world

Contains:

- source-only point and spot lights;
- independent source and destination occluders;
- narrow aperture;
- crossing caster;
- baked static surfaces;
- irradiance volumes on both sides;
- moving dynamic light;
- cached shadow policies;
- scripted camera and light paths.

### 3.6 Remote-zone world

Contains:

- ordinary world zone group;
- spatially remote alternate-environment zone group;
- enclosing environment mesh or mesh-based sky treatment hiding unrelated world geometry;
- portal relationship as the only relevance path between groups;
- unrelated dormant zones and relationships;
- no first-class dimension or space metadata.

This fixture proves that remote destination behavior is ordinary portal and zone behavior.

### 3.7 Scale world

Contains many loaded and unloaded relationships, endpoints, lights, and entities that are not
relevant to the active view or nearby bodies. It proves work scales with active relevance,
not total world content.

## 4. Relationship and lifecycle matrix

Test:

- world scene loads before both endpoint zones;
- endpoint zones load before world scene;
- A loads before B and B before A;
- endpoint unload and reload;
- both endpoints unload while relationship remains persistent;
- relationship delete and recreate;
- endpoint persistent identity preserved across save and reload;
- live `EntityId` changes after reload but durable resolution remains correct;
- partition slot reused by an unrelated zone;
- duplicate endpoint ownership introduced by hand-edited fixture;
- endpoint moved between authored zones;
- endpoint target missing permanently;
- package import canceled before publication;
- forced detach after participation leases are denied or overridden;
- engine shutdown with resolved and unresolved relationships.

Assertions:

- no runtime id is persisted;
- no stale live id or partition slot resolves;
- relationship ordering is stable;
- unresolved state is distinct from invalid content;
- diagnostics emit once per state transition;
- invalid relationships remain inert;
- endpoint uniqueness conflicts resolve deterministically;
- no partial imported zone becomes active.

## 5. Adversarial geometry matrix

### 5.1 Orientation

- identity-facing pair;
- 90-degree yaw;
- 180-degree yaw;
- wall to floor;
- floor to wall;
- wall to ceiling;
- upside-down destination;
- nearly parallel endpoint planes;
- physically overlapping endpoint regions;
- same-zone, cross-zone, and spatially remote pairs.

### 5.2 Viewpoint

- far away;
- grazing angle;
- one-pixel projection;
- fullscreen projection;
- eye on plane;
- eye partly through;
- eye at edge and corner;
- camera behind endpoint;
- near plane intersecting rim;
- rapid alternation between two visible links.

### 5.3 Aperture

- minimum valid extent;
- very wide and very tall;
- square;
- thin and thick rim;
- exact edge and corner;
- invalid zero and negative extent;
- scale just inside and outside validation tolerance.

### 5.4 Motion

- stationary overlap;
- slow crossing;
- high-speed crossing;
- reverse before center crossing;
- reverse immediately after clear;
- repeated oscillation;
- rotating long body with stationary center;
- simultaneous linear and angular velocity;
- contact on spatial-authority transfer tick;
- partition migration pending while shape remains straddled.

## 6. Character steelman cases

Required:

1. Persistent player crosses without storage migration.
2. Zone-owned character crosses with one partition migration and the same `EntityId`.
3. Stop with capsule center on source side and head through aperture.
4. Stop with center exactly on plane.
5. Back out after several fixed ticks.
6. Slide along rim without crossing.
7. Jump diagonally through a corner.
8. Fall through a floor portal and land immediately after exit.
9. Exit over empty space and re-evaluate grounded state.
10. Destination transitions from loading to active while character waits.
11. Destination loses required participation before commit.
12. Relationship disables while overlapping.
13. Endpoint detaches while not overlapping.
14. Forced detach attempts while overlapping.
15. Move faster than aperture slab thickness per fixed tick.
16. Replay at different render frame rates with identical fixed ticks.
17. Cross on the same lifecycle epoch that destination publishes.
18. Attempt two portal crossings in one tick.
19. Remote destination has no Euclidean proximity demand.
20. Partition slot is reused after the previous destination unloads.

Assertions:

- finite state;
- deterministic relationship choice;
- no rim tunneling;
- no duplicate crossing;
- transformed velocity and facing;
- stale grounded state cleared;
- bounded exit separation;
- migration exactly once when required;
- live entity identity preserved;
- backend mover handle preserved when the contract supports it;
- required leases held until clear and migration completion;
- identical fixed-simulation result across render rates.

## 7. Render steelman cases

Required:

1. Portal view at every orientation and distance class.
2. Destination geometry touching and crossing the exit plane.
3. Geometry behind exit plane.
4. Portal surface visible inside destination view.
5. More visible portals than cap.
6. Equal priorities with reversed package import and partition-slot order.
7. Resize every frame across target size classes.
8. Swapchain recreation with live targets.
9. Destination unload after one frame's extraction.
10. Endpoint reload with new live `EntityId`.
11. Material and mesh hot reload while visible.
12. Crossing opaque, double-sided, unlit, normal-mapped, and skinned meshes.
13. Crossing mesh casts shadows on both sides.
14. Fullscreen portal with maximum ordinary lights and shadows.
15. Remote destination zone group with unrelated ordinary-world geometry nearby in numeric
    coordinates.
16. Profiling Off, Counters, Gpu, and Capture transitions.
17. Destination Visible lease denied or delayed.
18. One portal visible through another.

Assertions:

- no recursion;
- no wrong-side geometry;
- no stale target after endpoint resolution change;
- no invalid descriptor;
- deterministic cap result;
- no target or lease leak;
- no ordinary opaque shader cost without crossing representations;
- destination extraction visits only approved partition sets;
- source and destination representations use correct lighting domains;
- partition migration timing does not cause a one-frame missing or duplicate mesh.

## 8. Rigid-body steelman cases

### 8.1 Shape coverage

- sphere;
- box;
- capsule;
- convex hull;
- separated-child compound;
- long thin pole;
- wide plate;
- offset center of mass where supported.

### 8.2 Contact coverage

- free flight;
- source contact only;
- destination contact only;
- simultaneous contacts on both sides;
- static friction on both sides;
- dynamic collision partner on destination side;
- opposing source and destination torque;
- rim jam;
- stacked bodies across aperture;
- character push;
- held-object controller;
- relationship disable during contact;
- endpoint detach request during contact.

### 8.3 Migration coverage

- persistent body with no migration;
- zone-owned body source-to-destination migration;
- back-and-forth migration;
- migration journal updates backend secondary index;
- body handle remains stable;
- storage ownership changes while proxy remains active;
- migration commits before next destination-zone logic requirement;
- source active to destination active;
- source active to destination dormant refusal;
- dormant restoration after later participation;
- migration followed by entity destruction.

### 8.4 Lifecycle coverage

- wake near aperture;
- sleep while straddling;
- wake from destination contact;
- destroy entity while coupled;
- remove collider;
- relationship deletion;
- source and destination detach after clear;
- forced detach while coupled;
- package reload;
- engine shutdown with active proxy and leases.

### 8.5 Long-run coverage

For at least 10,000 fixed ticks:

- repeated free-flight traversal;
- repeated gravity fall;
- repeated rim contact;
- resting coupled body;
- moving collision partner;
- repeated partition migration;
- repeated endpoint zone unload and reload between completed traversals.

Record:

- pose and orientation error;
- linear and angular velocity;
- energy where meaningful;
- contacts and impulses;
- proxy and constraint counts;
- body handles;
- current and pending partitions;
- lease counts;
- allocations;
- sleep transitions.

The acceptable envelope is established against equivalent no-portal controls before portal
results are reviewed.

## 9. Lighting steelman cases

### 9.1 Static direct

- point and spot;
- centered and edge-clipped aperture;
- source blocker;
- destination blocker;
- both blockers;
- light behind source endpoint;
- sample behind destination endpoint;
- range ending at aperture;
- spot cone tangent to aperture;
- repeated cook;
- reversed zone and document order;
- unrelated remote relationship edit;
- endpoint persistent identity stable across authoring reload.

### 9.2 Probe transport

- environment miss;
- source geometry hit after crossing;
- destination geometry hit before crossing;
- second portal encountered after one hop;
- probe inside geometry;
- aperture edge tie;
- serial and parallel jobs;
- low and high ray count;
- spatially remote source environment.

### 9.3 Dynamic light

- source light enters and leaves aperture influence;
- destination aperture offscreen;
- many source lights near one aperture;
- one light near many visible apertures;
- cap pressure;
- moving light with cached shadow;
- point cube and spot shadow;
- crossing caster;
- relationship disable;
- endpoint unload and reload;
- destination Visible lease delay;
- dormant remote zones.

Assertions:

- zero leakage outside aperture;
- independent source and destination occlusion;
- stable identity and cache behavior across endpoint reload;
- deterministic drop order;
- no stale shadow or descriptor;
- bounded image and view count;
- exact cook staleness;
- no recursive light demand.

## 10. Profiling counters

Add a field only with its live producer.

### 10.1 Relationship and participation

- resolved, unresolved, invalid, and disabled relationships;
- resolution updates;
- stale resolution rejects;
- endpoint ownership conflicts;
- portal lease acquisitions and releases by reason;
- destination residency requests;
- forced teardown outcomes.

### 10.2 Rendering

- views requested, rendered, and dropped;
- target pixels and bytes;
- queue items, draw calls, and triangles;
- composite draws;
- crossing render items;
- destination partition count visited;
- unresolved visible fallbacks.

### 10.3 Traversal and migration

- character and body overlap starts;
- committed spatial transfers;
- backed-out overlaps;
- refused crossings by reason;
- queued and committed partition moves;
- migration latency in fixed-tick boundaries;
- duplicate migration rejects;
- backend zone-index repairs.

### 10.4 Physics

Use a physics-specific stats record:

- candidates and exact overlaps;
- active coupled bodies and proxies;
- accepted and rejected contacts;
- impulse relays or constraint rows;
- authority transfers;
- maximum coupling position and angular error;
- CCD sweeps;
- proxy and constraint allocation and release;
- body recreations, which must remain zero for migration.

### 10.5 Lighting

- light-image candidates, packed, and dropped;
- transmitted shadow requests and rendered views;
- transmitted caster draws;
- static portal paths;
- source and destination BVH tests;
- probe rays reaching and crossing apertures;
- lighting lease counts.

### 10.6 GPU scopes

Add fixed scopes with their producer:

- portal offscreen views;
- portal composite;
- transmitted shadow views only when the aggregate shadow scope cannot answer the measured
  question.

Do not add dynamic timestamp registration per relationship.

## 11. Benchmark sweeps

### 11.1 Unified-runtime control

Before portal product code, measure:

- one active zone;
- several active zones;
- many resident but dormant zones;
- persistent entities plus active zones;
- zone import and detach;
- stable-reference resolution;
- entity partition migration;
- physics backend migration reconciliation;
- render extraction across partition sets;
- memory by world, partition, archetype, and backend record family.

This baseline establishes normal unified-runtime cost.

### 11.2 Render sweep

Control:

- Release build;
- scripted camera;
- immediate present mode;
- fixed resolution;
- fixed geometry and local lighting;
- warmup discarded;
- enough frames for median, p95, and p99;
- same startup pattern.

Sweep:

- visible portals: 0, 1, 2, 4, cap plus overflow;
- target scale: 0.25, 0.5, 1.0;
- projected size: tiny, quarter-screen, fullscreen;
- destination objects: low, medium, stress;
- crossing renderables: 0, 1, 8, 32;
- transmitted lights: 0, 1, 8, 32;
- shadowed images: 0, 1, cap pressure;
- destination type: same zone, nearby zone, spatially remote zone;
- unrelated resident dormant zones: 0, 8, 32.

Record CPU extraction and recording, GPU scopes, target memory, scratch high water, partition
visits, queue items, draw calls, light iterations, shadows, and leases.

### 11.3 Physics sweep

Sweep:

- nearby relationships: 0, 1, 8, 32;
- unrelated far relationships: constant high count;
- candidate bodies: 0, 1, 16, 64;
- active coupled bodies: 0, 1, 8, 32;
- contact mode: free, one side, both sides, stack;
- migrations: none, one-way, repeated;
- unrelated dormant partitions: 0, 8, 32.

Record fixed-step CPU distribution, broadphase candidates, contact work, proxies, constraints,
allocations, coupling error, migration journal cost, backend index repairs, and body handle
stability.

### 11.4 Cook sweep

Sweep:

- relationships: 0, 1, 8, 32;
- endpoint zones: local, adjacent, remote;
- lights reaching apertures: 0, 8, 32, 128;
- atlas samples and probe counts;
- aperture-hit ratio;
- serial and worker count;
- unrelated world relationships and zones.

Record total cook time, stable-reference resolution, BVH tests, portal path tests, peak memory,
output hashes, and unchanged-zone cache hits.

### 11.5 Lifecycle sweep

Repeatedly:

- attach and detach endpoint zones;
- resolve and unresolve relationships;
- reuse partition slots;
- acquire and release portal leases;
- migrate entities through portals;
- recreate render targets;
- create and destroy physics proxies;
- shut down cleanly.

Record retained memory, stale-resolution rejects, resource counts, and teardown latency.

## 12. Performance acceptance laws

Final numeric budgets are written after baseline measurement. These scaling laws are binding.

### 12.1 Zero-portal path

With no portal relationship components:

- no link resolution work beyond an empty query or epoch check;
- no portal leases;
- no offscreen portal pass;
- no portal targets;
- no portal light loop or shadow request;
- no physics proxy table work beyond an empty-state check;
- no cook work for absent relationships;
- CPU, GPU, and allocation deltas remain inside measured noise.

### 12.2 Relationship resolution

Cost is proportional to relationship changes, endpoint lifecycle changes, and affected
reference fixups. It is not a complete world scan per frame.

### 12.3 Rendering

Cost is proportional to selected target pixels, visible destination content, packed lights,
crossing representations, and transmitted shadows. Unrelated resident or dormant partitions
do not enter inner loops.

### 12.4 Physics

Cost is proportional to bodies near active endpoint broadphase regions, active coupled bodies,
and valid contacts. Unrelated bodies, partitions, and relationships do not appear per body.

### 12.5 Lighting

Dynamic candidates scale with visible relationships and source lights reaching apertures.
Static bake scales with relevant paths after coarse culling. Probe continuation occurs only
for rays reaching an aperture.

### 12.6 Migration

Portal partition migration cost matches ordinary `MoveEntityToZone` plus bounded portal
bookkeeping. It does not reconstruct entities or backend bodies.

### 12.7 Memory

Portal target memory is bounded by selected target extents and formats. Physics proxy memory
is bounded by active coupled bodies. Relationship resolution memory is bounded by persistent
relationship count. Lease memory is bounded by active portal needs.

Every retained resource has detach and shutdown tests.

## 13. Completion checklist

Before v0.1 or v1.0 completion:

- [ ] `CLAUDE.md` reread against final diff.
- [ ] Unified runtime dependency commit recorded.
- [ ] Lighting integration dependency commit recorded.
- [ ] No per-zone runtime registry path added.
- [ ] No first-class dimension, space, or alternate-world engine concept added.
- [ ] Canonical relationship has exactly two durable endpoint references.
- [ ] No duplicated partner state on endpoints.
- [ ] No runtime id or partition slot persisted.
- [ ] Components remain data-only.
- [ ] Vulkan and Jolt types remain behind their boundaries.
- [ ] Render backend never queries ECS.
- [ ] Storage migration preserves `EntityId` and backend handles.
- [ ] Spatial authority is independent of storage ownership while straddling.
- [ ] Participation leases are one hop and release correctly.
- [ ] Remote zones become relevant only through explicit portal demand.
- [ ] Serial and worker paths match where applicable.
- [ ] Zero, one, and many relationship cases pass.
- [ ] Load, unload, reload, slot reuse, migrate, destroy, and shutdown pass.
- [ ] ASAN passes full lifecycle.
- [ ] TSAN passes when concurrency changed.
- [ ] Focused and complete suites pass serially.
- [ ] `git diff --check` passes.
- [ ] Reproduction worlds pass.
- [ ] Control and feature captures are recorded.
- [ ] Median, p95, and p99 are reported.
- [ ] Counters prove bounded relevance.
- [ ] Evidence commands are reproducible.
- [ ] Rejected coupling candidate is removed.
- [ ] Deferred limitations are explicit.
- [ ] Nothing partial is called finished.
