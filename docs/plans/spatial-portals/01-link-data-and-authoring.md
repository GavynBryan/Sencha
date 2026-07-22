# Link Data and Authoring

Status: proposed execution specification.

This document defines the authored and runtime data for a linked planar aperture. It deliberately does not reuse world-partition transition markers. A spatial link maps coordinates. A partition transition describes topology and demand. They may be associated by content later, but neither owns the other.

## 1. Data contract

### 1.1 Strong identity

Add a strong 64-bit identity:

```cpp
using SpatialPortalLinkId =
    StrongId<struct SpatialPortalLinkIdTag, std::uint64_t>;
```

Rules:

- zero is invalid;
- the editor mints nonzero values;
- runtime code never mints random identities;
- JSON stores the value in the repository's established stable 64-bit form;
- two enabled endpoints with the same id form one link;
- endpoint ordering is derived deterministically from stable registry ownership and generational entity identity;
- identity does not depend on registry attachment order.

The exact strong-id declaration follows existing `ZoneId`, `TransitionId`, and serializer conventions.

### 1.2 Component

The initial data-only component is mechanically equivalent to:

```cpp
struct SpatialPortalComponent
{
    SpatialPortalLinkId Link;
    Vec2d HalfExtents = Vec2d(1.0f, 2.0f);
    bool Enabled = true;
};
```

Transform comes from the existing transform components.

The component does not contain:

- a pointer or entity handle to the other endpoint;
- a cached matrix;
- a render target;
- a Jolt body;
- a zone id;
- traversal state;
- lighting state;
- editor-only selection state.

Those are resolved or derived by the owning systems.

A zero-size marker may later opt rigid bodies into portal crossing, but it is not added until the rigid-body stage consumes it.

### 1.3 Frame convention

The frame convention is pinned before any subsystem integration:

- center is the endpoint world position;
- front normal is the endpoint world forward vector;
- up is the endpoint world up vector;
- right is the orthonormal cross product selected by the engine transform convention;
- local aperture coordinates are right and up;
- the front half-space is positive signed distance;
- traversal enters from the front and exits from the linked endpoint's front;
- the link transform includes the required half-turn in endpoint-local space so forward motion continues out of the destination rather than back into it.

Tests pin the exact matrix and handedness. No consumer rebuilds the transform independently.

### 1.4 Rigid-transform restriction

An endpoint is valid only when:

- translation is finite;
- rotation is finite and normalizable;
- scale is unit within a named validation tolerance;
- the basis is nondegenerate;
- both half extents are finite and positive.

Invalid endpoints do not form an active link. Runtime diagnostics name the link id and reason once, not once per frame.

## 2. Resolved runtime data

`SpatialPortalRuntime` owns a deterministic list of resolved pairs.

A resolved endpoint record contains values equivalent to:

```cpp
struct SpatialPortalEndpoint
{
    SpatialPortalLinkId Link;
    RenderEntityKey Entity;
    Transform3d WorldFromLocal;
    SpatialPortalFrame Frame;
    SpatialPortalAperture Aperture;
};
```

A resolved link contains:

```cpp
struct SpatialPortalLink
{
    SpatialPortalEndpoint A;
    SpatialPortalEndpoint B;
    Mat4 AToB;
    Mat4 BToA;
};
```

These names are illustrative. Implementation may consolidate them where one tight value is clearer.

### 2.1 Update ownership

The runtime index is updated from active registries through cached queries. Work is proportional to portal endpoints and to registries whose relevant structural or changed state advanced.

The runtime index:

- does not scan every entity;
- does not store raw entity indices;
- does not mutate ECS structure;
- does not depend on Vulkan or Jolt;
- publishes an immutable view for one fixed tick and one extraction frame;
- uses stable sorting before pairing and diagnostics.

The serial path is the reference. If registry collection is later parallelized, the final ordered result must be identical.

### 2.2 Link validity states

Every authored id resolves to one of:

- `Inactive`: all endpoints disabled;
- `Incomplete`: one enabled endpoint;
- `Active`: exactly two enabled valid endpoints;
- `Ambiguous`: more than two enabled endpoints;
- `InvalidEndpoint`: two endpoints exist but at least one frame or aperture is invalid.

Only `Active` links participate.

The editor and runtime share the same pure validation rules. The editor reports issues before cook. Runtime diagnostics still defend against hand-authored or stale cooked data.

## 3. Serialization and registration

Stage 1 must trace and update:

- component declaration;
- component type id;
- component registration before entity creation;
- metadata schema;
- scene serializer and field codec;
- editor inspector exposure;
- cooked scene passthrough;
- default zone builder registration;
- game template or example registration only where needed for the acceptance scene;
- tests for unknown, missing, zero, invalid, and round-trip fields.

No second scene-loading path is added.

An additive component does not require a world-manifest format bump. If adding the serializer changes a public module ABI, stop at the ABI gate before implementation.

## 4. Editor behavior

### 4.1 Creation

The first editor delivery may expose a simple component plus visualization rather than a dedicated placement tool.

A link-pair command is preferred once designers author links regularly:

1. create or select two endpoint entities;
2. mint one `SpatialPortalLinkId`;
3. assign it to both endpoints through one undoable command;
4. validate immediately;
5. frame both aperture rectangles in the viewport.

The command owns undo and redo. Direct raw edits remain possible through the inspector but do not bypass validation.

### 4.2 Duplication

Generic entity duplication copies component data today. Blindly preserving a link id can create three or four endpoints.

Pinned behavior:

- duplicating one endpoint clears the duplicated endpoint's link id;
- duplicating both endpoints through a dedicated pair command mints one new id and preserves their relationship;
- undo restores exact prior values;
- raw JSON or unsupported copy paths that create ambiguity are rejected by validation.

This behavior requires a narrow duplication policy at the editor command boundary. Do not add a generic component cloning interface solely for this component. If the current duplicate command has no focused extension point and a local special case would grow into a branch pile, stop and improve the command's existing ownership contract with the smallest general mechanism justified by present components.

### 4.3 Visualization

Editor visualization includes:

- aperture rectangle;
- front normal;
- link id abbreviation;
- line or arc to the linked endpoint when both documents are open;
- distinct incomplete, active, disabled, ambiguous, and invalid states;
- transformed preview camera optional after v0.1 runtime rendering is stable.

Visualization state is derived. It is not serialized.

### 4.4 Validation messages

Validation reports at least:

- invalid zero link id;
- one endpoint only;
- more than two endpoints;
- disabled pair;
- nonfinite transform;
- nonunit scale;
- degenerate basis;
- nonpositive aperture extent;
- coincident endpoints with an ill-defined visual result;
- endpoint not aligned to an authored opening;
- cross-zone endpoint not available for cook verification;
- portal surface intersects solid collision across the full aperture;
- two active apertures overlap in a way that violates the one-active-link-per-body content rule.

The collision-alignment check may begin as an editor warning if exact cook geometry is not available in the live document. The cook performs the authoritative check when possible.

## 5. Cook data

### 5.1 Endpoint collection

World cook collects endpoint records from every zone and the world scene before individual zone lighting cooks.

The collected record contains only stable authored and derived bake inputs:

- link id;
- owning zone identity or global ownership;
- world frame;
- aperture extents;
- enabled state;
- source document content identity needed for staleness.

The runtime cooked scene still carries the component. A separate portal sidecar is introduced only if profiling or deterministic cross-zone assembly proves that repeated scene traversal is the wrong shape.

### 5.2 Deterministic pairing

Pairing order is:

1. link id;
2. owner kind;
3. zone id;
4. stable serialized entity order within the scene.

Do not use `RegistryId` or runtime attachment order as cook identity.

Identical authored input produces byte-identical pairing output and lighting hashes.

### 5.3 Cook hash participation

A zone's lighting staleness includes a link only when that link can affect the zone's bake.

The hash includes:

- link id;
- both endpoint frames and extents;
- enabled state;
- relevant source light state;
- relevant source and destination occluder identities;
- probe settings when probe rays traverse the link.

Moving or resizing an endpoint restales affected zones. Editing an unrelated distant link does not.

## 6. Zone participation

### 6.1 v0.1 residency rule

A link is active only when both endpoint registries participate in the views needed by the operation.

For v0.1:

- render view requires both endpoints visible and loaded;
- character traversal requires both endpoints in physics and logic participation;
- an unavailable destination renders the fallback surface and refuses traversal;
- refusal is diagnosed once and is never a silent teleport into an unloaded registry.

### 6.2 Demand integration

Spatial links do not mutate `ZoneRuntime` policy internally.

If gameplay needs visible links to request destination residency, add a concrete demand producer above `ZoneRuntime` that contributes destination zone demand based on active cameras and nearby traversable entities.

The producer must be:

- data-driven from resolved links;
- bounded by visible or near links;
- deterministic;
- independently tested;
- explicit about Visible, Physics, Logic, and Audio participation;
- unable to keep the entire connected world resident through recursive demand.

One hop is the hard cap.

### 6.3 Unload behavior

When either endpoint unloads:

- the resolved link becomes incomplete;
- portal views stop before GPU target use;
- crossing state must be empty before unload completes, or unload is delayed/refused by the owner above `ZoneRuntime`;
- physics proxies are removed before their source registry or shape cache entries disappear;
- render proxies vanish at the next extraction boundary;
- light and shadow images are invalidated;
- retained GPU resources return to their owning caches.

Tests exercise unload while idle, visible, near a character, and after a body has fully cleared the aperture. Unload during an active rigid-body crossing is a stop condition until an explicit pin or evacuation rule is approved.

## 7. Stage 1 tests

Add focused tests equivalent to:

- `SpatialPortalTransformTests`
- `SpatialPortalValidationTests`
- `SpatialPortalRuntimeTests`
- `SpatialPortalSerializationTests`
- `SpatialPortalCookInputTests`
- `SpatialPortalEditorCommandTests` when the pair command lands

Required cases:

1. two endpoints pair regardless of registry attachment order;
2. zero, one, two, three, and disabled endpoints classify correctly;
3. unit-scale transforms map points and vectors both directions;
4. `BToA(AToB(x))` returns `x` within the math tolerance;
5. aperture corners and plane signs match the frame convention;
6. nonunit and nonfinite transforms never activate;
7. JSON round trip preserves the 64-bit id exactly;
8. duplicate-one clears the copied id;
9. duplicate-pair mints one new shared id;
10. unloading one registry removes the active pair without stale entity aliasing;
11. serial and any parallel collection path produce identical ordered links.
