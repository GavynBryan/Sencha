# Validation, Stress, and Profiling

Status: proposed execution specification.

This document defines how the implementation is challenged. Happy-path screenshots are insufficient.

The goal is to falsify the design early, especially the assumptions that:

- one transform is shared correctly by every subsystem;
- clipping prevents cross-domain leakage;
- a coupled rigid body behaves as one mass and inertia;
- work remains bounded;
- cache identities and invalidation are stable;
- the no-feature path remains unchanged.

## 1. Verification discipline

### 1.1 Per-stage workflow

For normal code stages:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
git diff --check
```

Run focused tests first while iterating.

Do not run CTest in parallel by default.

Additional required workflows by stage:

- rendering: Release profiling build with scripted capture;
- physics: physics isolation plus ASAN;
- concurrency changes: serial reference, worker path, and TSAN where supported;
- public boundary changes: ABI, layout, and isolation tests;
- cook changes: deterministic repeated output and old/new fixture behavior;
- editor changes: undo, redo, cancel, focus loss, document close, and shutdown;
- teardown changes: repeat clean window close and process exit under ASAN.

A command not run is reported as unverified. It is not described as passing.

### 1.2 Regression standard

A bug regression test must fail before the fix for the intended reason.

Do not:

- loosen a tolerance merely to pass;
- add a timing sleep;
- hide a failure behind a snapshot update;
- skip an adversarial configuration;
- average away a catastrophic outlier;
- call a visual result correct without numeric state where numeric invariants exist.

## 2. Evidence layout

Use:

```text
docs/plans/evidence/spatial-portals/
    baseline/
    view-rendering/
    character-traversal/
    rigid-body-coupling/
    static-light-transport/
    dynamic-light-transport/
    final/
```

Each evidence directory contains:

- `README.md` with question, hardware, build, scene, method, commands, warmup, sample count, result, and verdict;
- reduced JSON or CSV summaries;
- selected screenshots or diagrams;
- no large regenerable raw frame captures unless they are needed to preserve a failure.

Every verdict names the acceptance rubric before presenting the result.

## 3. Deterministic acceptance scenes

### 3.1 View scene

Contains:

- two disconnected rooms;
- strongly asymmetric landmarks;
- grid lines crossing the exit plane;
- different source and destination colors and probe environments;
- one horizontal and one rotated link;
- narrow and wide apertures;
- scripted camera track;
- moving kinematic marker;
- no random animation or input.

### 3.2 Character scene

Contains:

- level floor, wall, and ceiling links;
- rim thickness variations;
- ledges immediately after exits;
- high-speed launch path;
- edge and corner approach rails;
- destination unavailable toggle;
- fixed input replay.

### 3.3 Physics scene

Contains:

- sphere, box, capsule, convex, compound, and long pole;
- source and destination floors;
- walls immediately beside both apertures;
- dynamic collision partners;
- a stack;
- sleep platform;
- linear and angular launchers;
- link disable and destroy triggers;
- fixed tick count and state trace export.

### 3.4 Lighting scene

Contains:

- source-only point and spot lights;
- independent source and destination occluders;
- narrow aperture;
- crossing caster;
- baked static surfaces;
- irradiance volume on both sides;
- moving dynamic light;
- cached shadow policies;
- scripted camera and light paths.

### 3.5 Scale scene

Contains many portals and content that is not relevant to the visible apertures. It proves work depends on active relevance rather than total world size.

## 4. Adversarial geometry matrix

Test every applicable subsystem against:

### 4.1 Orientation

- identity-facing pair;
- 90-degree yaw;
- 180-degree yaw;
- wall to floor;
- floor to wall;
- wall to ceiling;
- upside-down destination;
- nearly parallel endpoint planes;
- physically overlapping endpoint regions;
- same-zone and cross-zone pairs.

### 4.2 Viewpoint

- far away;
- grazing angle;
- aperture fills one pixel;
- aperture fills the whole screen;
- eye on plane;
- eye partly through;
- eye at aperture edge;
- camera behind endpoint;
- near plane intersecting rim;
- rapid alternating view between two links.

### 4.3 Aperture

- minimum valid extent;
- very wide;
- very tall;
- near-square;
- thin rim;
- thick rim;
- exact edge and corner;
- invalid zero or negative extent;
- scale just within and outside validation tolerance.

### 4.4 Motion

- stationary overlap;
- slow crossing;
- high-speed crossing;
- reverse before center crossing;
- reverse immediately after crossing;
- repeated oscillation;
- rotating long body with stationary center;
- linear and angular velocity together;
- collision on the transfer tick.

## 5. Character steelman cases

Required:

1. Stand with capsule center on source side and nose through the aperture.
2. Stop with center exactly on the plane.
3. Back out after several fixed ticks.
4. Slide along the rim without crossing.
5. Jump diagonally through a corner.
6. Fall through a floor link and land immediately after the exit.
7. Enter a wall link while grounded and exit over empty space.
8. Cross while destination moves from visible to unavailable before commit.
9. Disable the link while overlapping.
10. Run at a speed greater than slab thickness per tick.
11. Replay at different render frame rates with the same fixed ticks.
12. Minimize and restore the window while overlapping.
13. Cross on the same fixed tick that a zone attaches.
14. Attempt two links in one tick.

Assertions:

- finite state;
- deterministic selected link;
- no tunneling through the rim;
- no duplicate crossing;
- no stale grounded state;
- transformed velocity and facing;
- bounded exit separation;
- identical fixed-simulation result across render rates.

## 6. Render steelman cases

Required:

1. Portal view at every orientation and distance class.
2. Destination geometry touching the exit plane.
3. Geometry behind the exit plane.
4. Portal surface visible inside the portal view.
5. More visible portals than the cap.
6. Equal projected priorities with reversed zone order.
7. Resize every frame across retained size classes.
8. Swapchain recreation with live targets.
9. Zone unload after extraction but before a later frame.
10. Material and mesh hot reload while visible.
11. Crossing opaque, double-sided, unlit, normal-mapped, and skinned meshes.
12. Crossing mesh casts a shadow on each side.
13. Fullscreen portal plus maximum ordinary lights and shadows.
14. Profiling Off, Counters, Gpu, and Capture transitions.

Assertions:

- no recursion;
- no wrong-side geometry;
- no stale target;
- no invalid descriptor;
- deterministic cap result;
- no target leak;
- no ordinary opaque shader cost when no crossing representations exist;
- main and portal views use correct zone light and probe data.

## 7. Rigid-body steelman cases

### 7.1 Shape coverage

- sphere;
- box;
- capsule;
- convex hull;
- compound with separated children;
- long thin pole;
- wide plate;
- center of mass outside shape center where supported.

### 7.2 Contact coverage

- no contacts;
- source contact only;
- destination contact only;
- simultaneous source and destination contacts;
- static friction on both sides;
- dynamic body collision on destination side;
- source and destination contacts producing opposing torque;
- rim jam;
- stacked bodies through aperture;
- character push;
- held controller;
- link disable during contact.

### 7.3 Lifecycle coverage

- wake near aperture;
- fall asleep while straddling;
- wake from destination contact;
- destroy entity while coupled;
- remove collider;
- unload source zone;
- unload destination zone;
- unlink endpoints;
- recook and reload;
- engine shutdown with active proxy.

### 7.4 Long-run coverage

For at least 10,000 fixed ticks:

- repeated free-flight traversal;
- repeated gravity fall;
- repeated rim contact;
- resting coupled body;
- moving collision partner.

Record:

- position and orientation error;
- linear and angular velocity;
- kinetic and potential energy where meaningful;
- contact and impulse counts;
- proxy count;
- allocations;
- sleep transitions.

The acceptable envelope is established against a geometrically equivalent no-portal control. It is not selected after seeing the portal result.

## 8. Lighting steelman cases

### 8.1 Static direct

- point and spot;
- aperture centered and edge-clipped;
- source blocker;
- destination blocker;
- both blockers;
- light behind source endpoint;
- sample behind destination endpoint;
- range ending at aperture;
- spot cone tangent to aperture;
- repeated cook;
- reversed zone order;
- unrelated distant link edit.

### 8.2 Probe transport

- ray misses environment;
- ray hits source geometry after crossing;
- ray hits destination geometry before crossing;
- ray reaches a second portal after one hop;
- probe inside geometry;
- aperture edge tie;
- serial and parallel jobs;
- low and high ray counts.

### 8.3 Dynamic light

- light enters and leaves source-aperture influence;
- destination aperture offscreen;
- many source lights near one aperture;
- one light near many visible apertures;
- cap pressure;
- moving light with cached shadow;
- point cube and spot shadow;
- crossing caster;
- link disable and zone unload.

Assertions:

- zero leakage outside aperture;
- both-side occlusion;
- stable keys and cache behavior;
- deterministic drop order;
- no invalid descriptor;
- no stale shadow;
- bounded light image count;
- exact cook staleness.

## 9. Profiling counters

Add a field only with the stage that writes it.

### 9.1 Rendering

- `PortalViewsRequested`
- `PortalViewsRendered`
- `PortalViewsDropped`
- `PortalTargetPixels`
- `PortalQueueItems`
- `PortalDrawCalls`
- `PortalCompositeDraws`
- `PortalCrossingRenderItems`

### 9.2 Physics

Use a physics-specific stats record rather than forcing solver counters into `RenderStats`:

- candidates;
- exact overlaps;
- active coupled bodies;
- proxies;
- accepted and rejected contacts;
- impulse relays or constraint rows;
- transfers;
- maximum position and angular error;
- CCD sweeps;
- proxy allocations and releases.

### 9.3 Lighting

- light image candidates;
- light images packed and dropped;
- transmitted shadow requests and rendered views;
- transmitted caster draws;
- portal bake paths;
- source and destination BVH tests;
- probe rays reaching and crossing apertures.

### 9.4 GPU scopes

Add fixed scopes with their producer:

- portal offscreen views;
- portal composite;
- transmitted shadow views only if the existing aggregate shadow scope cannot answer the measured question.

Do not add dynamic per-portal timestamp registration.

## 10. Benchmark sweeps

### 10.1 Render sweep

Control variables:

- Release build;
- fixed scripted camera;
- immediate present mode;
- fixed output resolution;
- fixed geometry and local lights;
- warmup discarded;
- enough frames for median, p95, and p99;
- same process startup pattern.

Sweep:

- visible portals: 0, 1, 2, 4, cap plus overflow;
- target scale: 0.25, 0.5, 1.0;
- projected size: tiny, quarter-screen, fullscreen;
- destination objects: low, medium, stress;
- crossing renderables: 0, 1, 8, 32;
- transmitted lights: 0, 1, 8, 32;
- shadowed images: 0, 1, cap pressure.

Record:

- CPU extraction and recording;
- GPU portal-view and main-color times;
- target memory;
- scratch high water;
- queue items, draw calls, triangles;
- light iterations and drops;
- shadow views and cache hits.

### 10.2 Physics sweep

Sweep:

- near portals: 0, 1, 8, 32;
- total far portals: constant high count to prove irrelevance;
- candidate bodies: 0, 1, 16, 64;
- active coupled bodies: 0, 1, 8, 32;
- contact manifolds: free flight, one side, both sides, stack;
- fixed substeps and any portal-specific sweep policy.

Record fixed-step CPU distribution, broadphase candidates, contact work, proxy count, allocations, and coupling error.

### 10.3 Cook sweep

Sweep:

- portal links: 0, 1, 8, 32;
- lights reaching apertures: 0, 8, 32, 128;
- atlas samples and probe counts;
- aperture hit ratio;
- serial and worker count.

Record total cook time, BVH tests, portal path tests, peak memory, output hashes, and unchanged-zone cache hits.

## 11. Performance acceptance laws

The final numeric budgets are written after Stage 0 baselines. The following scaling laws are already binding.

### 11.1 No-feature path

With no active spatial portals:

- no portal offscreen pass;
- no portal target acquisition;
- no crossing proxy table work beyond an empty-state check;
- no portal light loop;
- no portal shadow request;
- no cook work for absent components;
- CPU, GPU, and allocation deltas remain inside measured run-to-run noise.

### 11.2 Render path

Cost is proportional to:

- selected portal target pixels;
- visible destination queue items;
- packed destination lights;
- crossing representations;
- transmitted shadow views.

It is not proportional to every portal or every entity in loaded registries.

### 11.3 Physics path

Cost is proportional to:

- bodies in portal broadphase regions;
- active coupled bodies;
- valid contacts.

Far bodies and far portals do not appear in per-body inner loops.

### 11.4 Lighting path

Dynamic image generation is proportional to relevant source lights near active source apertures. Static bake work is proportional to relevant paths after coarse culling. Probe continuation occurs only for rays that reach an aperture.

### 11.5 Memory

Portal target memory is bounded by:

```text
sum(selected target width * height * color bytes)
+ sum(selected target width * height * depth bytes)
+ fixed retained metadata
```

Physics proxy memory is bounded by active crossing bodies, not historical crossings.

Every retained resource has a teardown test.

## 12. Final review checklist

Before v0.1 or v1.0 is declared complete:

- [ ] `CLAUDE.md` reread against the final diff.
- [ ] Invariant owner is identifiable.
- [ ] No other-engine or gameplay vocabulary entered engine identifiers.
- [ ] Components remain data-only.
- [ ] No duplicate canonical state.
- [ ] No Vulkan or Jolt type leaked across its firewall.
- [ ] No backend traversal of ECS.
- [ ] No public ABI change is unacknowledged.
- [ ] No persisted or cooked format change is unversioned.
- [ ] Serial and worker paths match where applicable.
- [ ] Zero, one, and many resource cases pass.
- [ ] Resize, unload, destroy, and shutdown pass.
- [ ] ASAN passes the full lifecycle.
- [ ] TSAN passes when concurrency changed.
- [ ] Focused tests pass.
- [ ] Complete suite passes serially.
- [ ] `git diff --check` passes.
- [ ] Reproduction scenes pass.
- [ ] Profiling control and feature captures are recorded.
- [ ] Median, p95, and p99 are reported.
- [ ] Counters prove bounded work.
- [ ] Evidence commands are reproducible.
- [ ] Rejected design candidates are removed.
- [ ] Deferred limitations are explicit.
- [ ] Nothing partial is called finished.
