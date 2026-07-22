# View Rendering and Character Traversal

Status: proposed execution specification.

This document owns v0.1 after the link-data stage is complete.

## 1. Pure geometry kernel

The same kernel supports character crossing, portal cameras, light images, bake rays, traces, and rigid-body classification.

### 1.1 Required operations

Implement focused pure operations for:

- signed distance from a point to an endpoint plane;
- projection of a world point into aperture coordinates;
- point-in-rectangle with explicit edge tolerance;
- support interval of a sphere or capsule along the portal normal;
- segment-plane crossing time;
- swept center crossing with previous and current positions;
- rigid point transform;
- rigid direction and linear-velocity transform;
- rigid orientation and angular-velocity transform;
- ray intersection with aperture;
- destination clip-plane conversion;
- portal-camera transform;
- conservative screen-space bounds for visible-aperture ranking.

Each operation receives explicit values. No function reaches into ECS, a renderer, a physics world, or global cvars.

### 1.2 Tolerance policy

Tolerances are named by purpose:

- plane classification epsilon;
- aperture edge epsilon;
- exit separation epsilon;
- crossing hysteresis;
- parallel-ray epsilon.

Do not use one global magic epsilon.

Tests cover values just below, exactly at, and just above every boundary. Tolerance changes require a regression reason, not visual preference.

## 2. Character candidate collection

The first-person character uses a `CharacterVirtual` capsule. Portal traversal remains owned by engine systems outside Jolt internals.

Candidate work is bounded:

1. Broadphase portal slabs or cached endpoint bounds find nearby links.
2. Exact capsule support and aperture tests classify overlap.
3. Only active links with loaded destinations participate.
4. The character may own at most one active overlap record.
5. Stable link id breaks equal-time ties.

The overlap record stores:

- link id and entry side;
- previous capsule center;
- previous signed distance;
- whether the capsule intersects the aperture prism;
- whether the center has crossed;
- an exit hysteresis state.

It does not become a component unless persistence across systems truly requires ECS ownership. A character-controller-owned value is preferred for one character mover.

## 3. Straddling semantics

The v0.1 rule is pinned:

- overlap begins when any part of the capsule intersects the aperture prism;
- the character remains in source coordinates while the center is on the source side;
- the character may stop while overlapping;
- backing out clears overlap without teleportation;
- crossing commits when the swept center crosses from source front to source back and the crossing point lies inside the aperture reduced by the capsule radius;
- after commit, position, facing, and velocity are transformed;
- the destination position receives a small separation along its front normal;
- overlap remains latched until the capsule clears the destination plane by the hysteresis distance;
- an immediate reverse crossing is allowed only after that clear state.

Reducing the aperture by capsule radius prevents the capsule center from crossing at an edge where the capsule would intersect the rim. The authored wall collision remains authoritative for slide and rim behavior.

### 3.1 High-speed crossing

Crossing uses the full previous-to-current segment from the fixed tick. It does not rely on a trigger callback firing at the plane.

If the segment crosses more than one active link in one tick:

1. compute every valid crossing time;
2. choose the smallest time;
3. transform the remaining displacement through that link;
4. do not traverse another link in the same tick;
5. record a diagnostic counter for the deferred second crossing.

The one-hop tick limit prevents unbounded loops and preserves deterministic cost.

### 3.2 Facing and velocity

Transform:

- capsule center;
- character linear velocity;
- camera yaw and pitch basis;
- any controller-owned vertical velocity;
- grounded state only after destination support is re-evaluated.

Do not preserve a stale source ground contact after transfer. The next destination update decides support.

The camera may interpolate presentation state, but simulation-authoritative position and velocity change on the fixed tick.

## 4. Portal view extraction

### 4.1 Render-domain record

A portal view record contains values equivalent to:

```cpp
struct SpatialPortalView
{
    SpatialPortalLinkId Link;
    RenderEntityKey Surface;
    CameraRenderData Camera;
    RenderQueue Queue;
    RenderLightSet Lights;
    Rect2i TargetRect;
    Plane4 DestinationClip;
    float Priority;
};
```

The exact ownership may split queue and light storage into retained arrays to avoid per-frame allocation. The invariant is that the record is render-domain data produced before Vulkan recording.

The Vulkan backend never queries portal ECS components.

### 4.2 Camera transform

For source endpoint `S`, linked endpoint `D`, and main camera transform `C`:

```text
portalCamera = D * HalfTurn * inverse(S) * C
```

The implementation uses the shared link transform, not a second renderer-specific matrix builder.

The destination near plane is converted into camera clip space and used to reject geometry on the wrong side. The projection must remain valid at oblique angles and while the camera is close to or partially through the source plane.

### 4.3 Visibility and ranking

A portal requests a view only when:

- its endpoint is active and front-facing under the sidedness rule;
- its conservative aperture bounds intersect the main camera frustum;
- its projected screen rectangle has nonzero area;
- the destination registry is visible;
- it survives the configured view cap.

Rank by:

1. contains or intersects the camera near volume;
2. projected pixel area;
3. distance;
4. previous-frame residency hysteresis;
5. stable link and endpoint identity.

The rank is deterministic. Reversing zone attachment order does not change selected views.

### 4.4 Resolution policy

Initial cvars:

- maximum rendered portal views;
- target scale relative to projected bounds or main extent;
- minimum target dimension;
- maximum target dimension;
- target-pool memory cap;
- debug freeze and view selection.

Defaults are established from Stage 0 measurements.

Targets are rounded to retained size classes to avoid allocation churn. A target is reused in place and released on teardown. Zero visible portals own no live per-frame target allocation beyond an intentionally retained empty pool, if profiling proves retention useful.

## 5. Render feature shape

The current renderer requires one phase per feature. Use two concrete features sharing one concrete render state.

### 5.1 Offscreen feature

The offscreen feature:

1. receives extracted portal-view records;
2. acquires or reuses color and depth targets;
3. renders each selected destination view;
4. reuses the existing mesh forward pass and light bindings where their ownership permits;
5. opens and closes its own dynamic-rendering pass;
6. writes one GPU scope around all portal views and optional per-view debug labels only while profiling is active;
7. leaves images in the layout expected by the composite feature.

If reusing `MeshForwardPass` would require sharing mutable state unsafely between main and offscreen draws, extract the smallest retained pass state that supports sequential draws. Do not add a general render-graph abstraction for this feature.

### 5.2 Composite feature

The main-color feature:

- runs after ordinary opaque world geometry;
- draws the authored aperture surface with depth testing;
- samples the assigned offscreen target;
- maps screen projection correctly from the main camera;
- uses a fallback material when no view was rendered;
- does not recursively schedule portal surfaces visible in the texture;
- does not write depth beyond the aperture plane.

Aperture surfaces use a dedicated pipeline and material contract. Do not add a branch to every standard opaque fragment.

### 5.3 Destination clipping

Destination world geometry is clipped by the transformed exit plane.

Preferred order of investigation:

1. oblique near-plane projection for whole-view geometry;
2. a dedicated clip plane in a portal-view vertex or fragment variant only if projection clipping is insufficient for crossing render representations;
3. never add an unconditional clip branch to the ordinary production shader.

Tests and captures must prove no geometry behind the exit plane bleeds into the portal view.

## 6. Main-camera straddling

When the main camera is close to the source plane:

- screen bounds may cover the whole frame;
- near-plane math must remain finite;
- the view must not mirror or invert unexpectedly;
- the selected endpoint side follows the character's authoritative side;
- after center crossing, the main camera and visible source endpoint swap coherently on the same presentation frame;
- temporal target reuse must not show a stale pre-crossing view.

A fixed-tick transfer and a render-frame extraction can occur at different cadence. Extraction always consumes the coherent post-fixed transform state defined by the frame pipeline.

## 7. Renderer lifecycle

Required paths:

- no camera;
- zero target extent;
- minimized window;
- swapchain recreation;
- target scale change;
- portal cap change;
- zone unload;
- material or mesh hot reload;
- renderer teardown before GPU services disappear;
- profiling mode changes at the extract-phase latch.

GPU resource creation and destruction occur on the owner thread. No async task publishes Vulkan state.

## 8. v0.1 diagnostics

Add counters only with their producers:

- portal endpoints considered;
- portal views requested;
- portal views rendered;
- portal views dropped by cap;
- portal target pixels;
- portal queue items;
- portal draw calls;
- portal composite draws;
- character overlap starts;
- character committed crossings;
- character backed-out overlaps;
- deferred second crossings.

Add debug views for:

- endpoint frame and aperture;
- active pair id;
- signed character distance;
- capsule support interval;
- selected crossing time;
- portal camera frustum;
- destination clip plane;
- projected target rectangle;
- view rank and drop reason.

The profiling Off path must not update histories, write timestamps, emit capture rows, allocate strings, or record labels.

## 9. v0.1 tests

### 9.1 Pure geometry

Table-driven tests include:

- head-on, oblique, vertical, and upside-down links;
- both directions through every pair;
- point exactly on plane;
- segment parallel to plane;
- segment beginning behind plane;
- center crossing at center, edge, and corner;
- capsule radius just fitting and just failing;
- high-speed segment;
- inverse transform round trip;
- orientation and velocity basis preservation;
- finite oblique clip projection near the plane.

### 9.2 Character integration

Headless tests include:

- walk through and back;
- stop halfway and back out;
- jump through floor-to-wall and wall-to-floor links;
- preserve horizontal and vertical velocity under rotation;
- land on destination geometry;
- hit the rim and fail to cross;
- start overlapping due to load;
- destination unavailable;
- disable or unlink before crossing;
- same-tick multiple crossing candidates;
- fixed-step replay produces identical final state.

### 9.3 Render extraction

Tests include:

- zero links;
- inactive and invalid links;
- front and back facing;
- frustum reject;
- deterministic cap ordering;
- target resolution clamping;
- portal queue uses destination camera;
- no recursive request from a portal view;
- source and destination registry order reversal;
- target records clear on unload and resize.

### 9.4 Captured acceptance scene

The scene contains:

- two rooms with clearly different geometry, lighting, and materials;
- one horizontal pair;
- one vertical or rotated pair;
- grid lines crossing the exit plane;
- a moving marker visible through the link;
- a narrow rim for edge tests;
- scripted camera positions at far, near, oblique, half-overlap, and post-crossing poses.

Every pose records a screenshot and capture summary. Visual acceptance is paired with numeric plane, view, queue, and target counters.
