# View Rendering and Character Traversal

Status: proposed execution specification, revised for the unified runtime world.

This document owns v0.1 after the relationship and pure-geometry stage is complete.

## 1. Unified-world assumptions

v0.1 runs against:

- one runtime ECS `World`;
- persistent relationship entities;
- endpoint entities in zone storage partitions;
- one stable `FrameZoneView` with Visible, Physics, Logic, Audio, and Resident sets;
- durable endpoint references resolved to live `EntityId` values;
- participation leases;
- entity partition migration preserving `EntityId`;
- one simulation-scoped character and physics backend.

No code in this document accepts a span of runtime registries or routes by `RegistryId`.

## 2. Pure geometry kernel

The shared geometry kernel supports character crossing, portal cameras, light images, bake
rays, traces, and rigid-body classification.

### 2.1 Required operations

Implement pure operations for:

- signed point-to-plane distance;
- projection into aperture right and up coordinates;
- point-in-rectangle with explicit edge tolerance;
- sphere and capsule support intervals along the endpoint normal;
- segment-plane crossing time;
- swept center crossing from previous and current positions;
- rigid point, direction, orientation, linear-velocity, and angular-velocity transforms;
- one-hop ray intersection with an aperture;
- destination clip-plane conversion;
- portal-camera transformation;
- conservative screen-space aperture bounds;
- source and destination endpoint-side classification.

Each operation receives explicit values and reaches into no ECS, renderer, physics backend,
world resource, or cvar registry.

### 2.2 Tolerance policy

Use named tolerances for:

- plane classification;
- aperture edge inclusion;
- capsule-fit reduction;
- exit separation;
- crossing hysteresis;
- parallel rays;
- oblique projection stability.

No global portal epsilon is allowed. Tests cover values below, at, and above each boundary.

## 3. Character portal state

A character may interact with one relationship at a time.

The controller-owned crossing state contains:

- persistent relationship identity;
- live link and endpoint `EntityId` values for the current resolution epoch;
- entry endpoint side;
- source and destination storage partitions;
- previous capsule center and signed distance;
- aperture-prism overlap;
- center-crossed state;
- pending partition migration state;
- acquired participation lease handles;
- exit hysteresis state.

This state is backend or controller state, not a serialized gameplay component.

Stale endpoint resolution invalidates the state before use. It never silently aliases a
reloaded endpoint or reused partition slot.

## 4. Candidate collection and destination availability

Candidate work is bounded:

1. Query active resolved links whose source endpoint partition is in Physics participation.
2. Broadphase endpoint slabs or a compact endpoint acceleration structure find nearby links.
3. Exact capsule support and aperture tests classify overlap.
4. Stable relationship identity breaks equal-time ties.
5. One active relationship wins until the character clears it.

Before overlap becomes traversable:

- destination zone is resident or requested through a one-hop lease;
- destination Physics participation is active;
- destination Logic participation is active when the character's gameplay owner requires it;
- the destination endpoint remains resolved for the current lifecycle epoch;
- the authored opening and collision are available.

If those requirements fail, the aperture behaves as a safe nontraversable boundary and emits
a diagnosed reason. The renderer may still display a fallback surface.

## 5. Straddling semantics

Pinned v0.1 behavior:

- overlap begins when any part of the capsule intersects the aperture prism;
- source and destination Physics participation are pinned while overlapping;
- the character remains spatially authoritative on the source side while its center remains
  on the source side;
- the character may stop while overlapping;
- backing out clears overlap without teleportation or partition migration;
- crossing commits when the swept capsule center crosses from source front to source back and
  the crossing point lies inside the aperture reduced by capsule radius;
- pose, facing, and velocity transform at commit;
- destination support is re-evaluated instead of preserving stale grounded state;
- an exit separation moves the capsule slightly toward the destination front half-space;
- overlap remains latched until the capsule clears the destination plane by hysteresis;
- reverse traversal is allowed only after clear.

Reducing the aperture by capsule radius prevents the center from crossing where the capsule
would intersect the rim. Authored wall collision remains authoritative for slide behavior.

## 6. High-speed and multiple-crossing policy

Crossing uses the full previous-to-current fixed-tick segment. It does not depend on trigger
callback timing.

If one movement segment intersects more than one valid aperture:

1. compute valid crossing times;
2. choose the smallest time;
3. transform the remaining displacement through that relationship;
4. stop portal traversal for the rest of that fixed tick;
5. record a deferred-second-crossing counter.

This keeps cost bounded and prevents loops.

## 7. Spatial transfer and storage migration

### 7.1 Distinct but coordinated operations

A committed crossing always changes spatial state. It changes storage partition only when the
entity is zone-owned and the destination ownership policy requires migration.

Common cases:

- player and camera in the persistent partition: transform only, no partition move;
- zone-owned NPC or controller: transform and queue destination partition migration;
- world-lifetime actor: transform only;
- test fixture with explicit fixed ownership: transform and retain current partition.

The ownership policy is explicit. It is not inferred from portal geometry.

### 7.2 Legal phase contract

Portal code uses the unified runtime's one documented structural mutation window. It does not
invent an ad hoc migration drain.

For controller-driven movement before physics:

1. fixed movement computes the committed portal transfer;
2. controller state and ECS transform receive the destination pose;
3. a partition-move request is queued when required;
4. the ordinary structural drain commits `MoveEntityToZone`;
5. the migration journal updates backend zone indices before the next relevant backend step;
6. fixed logic continues only after the engine's documented ordering permits it.

If current merged scheduling cannot preserve this ordering, stop and amend the engine phase
contract before portal traversal lands.

### 7.3 Migration guarantees

A portal-driven move must preserve:

- live `EntityId` and generation;
- component signature and values;
- change-tracking semantics;
- character backend record and mover handle where supported;
- gameplay relationships;
- stable persistent identity;
- portal crossing state until clear.

The move happens at most once for one committed crossing.

### 7.4 Forced teardown

While overlapping or awaiting migration, source and destination leases remain held.

If forced teardown overrides those leases:

- the portal system receives the explicit detaching lifecycle visit;
- traversal stops;
- the character is placed at a tested safe pose on the authoritative side or the operation is
  rejected before teardown;
- backend state is coherent before endpoint destruction;
- no unresolved destination transform is applied.

The exact forced-teardown policy is pinned before implementation tests are written.

## 8. Facing, velocity, and camera state

Transform:

- capsule center;
- character linear velocity;
- controller-owned vertical velocity;
- camera root orientation;
- yaw and pitch basis;
- any movement-plane basis used by locomotion.

Do not preserve source support normals, floor entity, step state, or cached ground contacts.

Simulation-authoritative state changes on the fixed tick. Camera presentation may interpolate,
but it must not blend through an invalid Euclidean path between the two endpoint locations.

## 9. Portal view extraction

### 9.1 Render-domain record

A portal view record is render-only data equivalent to:

```cpp
struct SpatialPortalView
{
    RenderEntityKey LinkKey;
    RenderEntityKey SurfaceKey;
    StoragePartitionId DestinationPartition;
    CameraRenderData Camera;
    RenderQueue Queue;
    RenderLightSet Lights;
    Rect2i TargetRect;
    Plane4 DestinationClip;
    float Priority;
};
```

Stable keys derive from persistent relationship identity and endpoint side. Runtime partition
ids are transient extraction data and never stable cache identity.

The exact ownership may retain queue and light storage to avoid frame allocations. The Vulkan
backend receives only completed render records.

### 9.2 Destination partition set

Extraction consumes one `World` and explicit visible partitions.

For one portal view:

1. verify the destination endpoint is resolved and its zone is Visible;
2. construct a destination partition set containing the destination zone and any explicitly
   approved persistent or context partitions;
3. extract meshes and lights through existing partition-aware queries;
4. apply portal-specific clip and recursion rules;
5. never iterate every resident partition by default.

The destination may be spatially remote. View extraction uses relationship ownership and
participation, not camera-to-zone Euclidean distance.

### 9.3 Camera transform

For source endpoint `S`, destination endpoint `D`, and main camera transform `C`:

```text
portalCamera = D * HalfTurn * inverse(S) * C
```

The shared resolved transform provides this mapping. Render code does not rebuild it.

The destination plane is converted into the portal camera's clipping convention. Projection
must remain finite at oblique angles and near-plane overlap.

### 9.4 Visibility and ranking

A portal requests a view only when:

- relationship and both endpoints are active;
- source endpoint is front-facing under the sidedness rule;
- aperture bounds intersect the main camera frustum;
- projected area is nonzero;
- destination Visible participation is active;
- the relationship survives the configured cap.

Rank by:

1. camera-near-volume intersection;
2. projected pixel area;
3. distance to source aperture;
4. previous-frame target residency hysteresis;
5. stable relationship identity and endpoint side.

Zone import order, partition slot, and entity allocation order do not change ranking.

### 9.5 One-hop recursion rule

Portal surfaces encountered during destination extraction remain renderable only through the
defined fallback material. They do not schedule another view or acquire another destination
lease.

The view record carries a one-hop context flag so extraction cannot recurse accidentally.

## 10. Resolution and target policy

Initial cvars control:

- maximum portal views;
- target scale;
- minimum and maximum target dimensions;
- target-pool memory cap;
- projected-area threshold;
- target retention hysteresis;
- debug freeze and selected view.

Defaults come from Stage 0 measurements.

Targets use retained size classes. Zero selected views allocate no per-frame target and issue
no offscreen portal pass.

## 11. Render feature shape

Use two concrete features sharing one concrete portal render state.

### 11.1 Offscreen feature

The offscreen feature:

- consumes extracted portal views;
- acquires retained color and depth targets;
- renders the destination camera, queue, and light set;
- reuses the existing forward pass where its sequential ownership allows;
- owns dynamic-rendering begin and end for its targets;
- writes fixed GPU scopes only while profiling is active;
- transitions targets for sampling by the composite feature.

Do not introduce a general render graph solely for portals.

### 11.2 Composite feature

The main-color composite feature:

- runs after ordinary opaque world geometry;
- draws the authored aperture surface with depth testing;
- samples the assigned target;
- maps projection correctly from the main camera;
- uses fallback presentation when no target exists;
- never recursively schedules a destination;
- does not write depth outside the aperture surface.

A dedicated pipeline keeps portal sampling out of ordinary opaque fragments.

### 11.3 Destination clipping

Investigate in this order:

1. oblique near-plane projection for whole destination views;
2. dedicated clip data for crossing representations;
3. no unconditional clip branch in the normal production shader.

Captures prove that geometry behind the destination plane cannot bleed into the view.

## 12. Main-camera straddling

When the main camera approaches or crosses the source plane:

- projected portal bounds may cover the full frame;
- near-plane math remains finite;
- endpoint side follows the character's authoritative crossing state;
- target selection does not flicker between sides;
- the transfer frame cannot sample stale pre-transfer content;
- a destination partition move, when required, is visible to extraction at the documented
  frame boundary;
- a persistent player remains in the persistent partition without special registry routing.

## 13. Lifecycle

Required paths:

- no active camera;
- zero extent and minimized window;
- swapchain recreation;
- target policy changes;
- relationship disable or delete;
- endpoint unload and reload;
- endpoint partition-slot reuse;
- destination participation loss;
- material and mesh hot reload;
- renderer teardown before GPU services disappear;
- profiling-mode changes at the existing latch;
- engine shutdown while a portal is visible or a character overlaps it.

GPU resource creation and destruction remain owner-thread operations.

## 14. Diagnostics

Add counters with their producers:

- resolved portal relationships;
- unresolved visible relationships;
- portal endpoints considered;
- portal views requested, rendered, and dropped;
- destination lease acquisitions and releases;
- portal target pixels;
- portal queue items and draw calls;
- portal composite draws;
- character overlap starts;
- committed crossings;
- backed-out overlaps;
- refused crossings by reason;
- queued and committed partition migrations;
- deferred second crossings;
- forced-teardown resolutions.

Debug visualization includes:

- relationship identity and resolution state;
- endpoint frames and apertures;
- source and destination partition ownership;
- lease reasons;
- capsule support interval and crossing time;
- portal camera frustum;
- destination clip plane;
- projected target rectangle;
- view rank and drop reason;
- pending transfer and migration state.

Profiling Off writes no histories, timestamps, captures, strings, or labels.

## 15. Tests

### 15.1 Pure geometry

Cover head-on, oblique, vertical, upside-down, near-parallel, edge, corner, high-speed, inverse
transform, velocity, orientation, and clipping cases.

### 15.2 Character integration

Headless tests include:

- persistent player crosses without partition migration;
- zone-owned character crosses and preserves `EntityId` through migration;
- walk through and back;
- stop halfway and back out;
- jump through floor-to-wall and wall-to-floor pairs;
- preserve velocity under arbitrary rotation;
- destination support re-evaluation;
- rim collision and failed crossing;
- destination unresolved, loading, dormant, and forced-detaching states;
- endpoint unload and reload while not overlapping;
- relationship disable while overlapping;
- same-tick multiple crossing candidates;
- fixed-step replay at different render rates;
- migration journal updates the character backend without recreation;
- partition-slot reuse does not satisfy stale crossing state.

### 15.3 Render extraction

Tests include:

- zero relationships;
- active, disabled, invalid, and unresolved relationships;
- same-zone and cross-zone destination extraction;
- spatially remote destination zone;
- front and back facing;
- frustum reject;
- deterministic cap ordering independent of partition slot and import order;
- target resolution clamping;
- no recursive request;
- destination extraction visits only approved partitions;
- target records clear on detach, reload, resize, and relationship deletion.

### 15.4 Captured acceptance scene

The acceptance world contains:

- two visibly distinct rooms in separate zones;
- one spatially remote zone group hidden by its own enclosing environment mesh;
- one horizontal and one rotated relationship;
- grid geometry touching the exit plane;
- moving kinematic markers;
- narrow rims;
- scripted camera poses at far, near, oblique, half-overlap, and post-transfer positions;
- a persistent player and one zone-owned character migration case.

Every pose records screenshots, capture summaries, partition visits, lease state, and view
counters.
