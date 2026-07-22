# Link Data and Authoring

Status: proposed execution specification, revised for the unified runtime world.

This document defines the canonical authored relationship, endpoint-local data, durable
cross-zone references, runtime resolution, validation, cook inputs, and participation
ownership for a spatial portal.

A spatial portal is not a world-partition transition record and is not a first-class
dimension. It is one persistent binary relationship between two endpoint entities.

## 1. Dependency gate

Implementation depends on the unified runtime providing:

- one runtime ECS `World`;
- a persistent storage partition;
- durable `PersistentEntityId` values for authored entities;
- `StableEntityRef { ZoneId, PersistentEntityId }` or its merged equivalent;
- package encoding and fixups for durable references;
- live resolution from a stable reference to `EntityId` when the zone is resident;
- stale-resolution detection across unload, reload, and partition-slot reuse;
- world-scene content imported into the persistent partition;
- caller-held participation leases.

Do not add a portal-specific substitute for any missing general contract. If the merged
stable-reference shape differs, revise this plan before implementation.

## 2. Canonical authored data

### 2.1 Endpoint component

An endpoint entity stores only endpoint-local data:

```cpp
struct SpatialPortalEndpointComponent
{
    Vec2d HalfExtents = Vec2d(1.0f, 2.0f);
    bool Enabled = true;
};
```

The endpoint frame comes from the existing transform components.

The endpoint component does not contain:

- a partner reference;
- a shared grouping id;
- a link entity reference;
- a cached matrix;
- a zone id;
- a render target;
- a physics body or proxy;
- traversal state;
- lighting state;
- participation lease handles;
- editor-only state.

### 2.2 Relationship component

A world-lifetime relationship entity stores exactly two durable endpoint references:

```cpp
struct SpatialPortalLinkComponent
{
    StableEntityRef EndpointA;
    StableEntityRef EndpointB;
    bool Enabled = true;
};
```

The relationship entity is normally authored in the world scene and imported into the
persistent storage partition.

Its durable entity identity is the portal's canonical stable identity. Runtime `EntityId`
serves as the live identity while loaded. Cooked and saved systems use the relationship
entity's persistent identity, not a random portal id and not either endpoint's identity.

The link component does not store derived endpoint transforms, live entity ids, partitions,
leases, render state, or physics state.

### 2.3 Why the relationship is canonical

This structure guarantees that one relationship has exactly two endpoint slots. It avoids:

- three or more endpoints accidentally sharing a scalar id;
- duplicated A-to-B and B-to-A partner fields;
- asymmetric partner references;
- scan-and-group discovery as the canonical relationship mechanism;
- exposing a numeric link id in the inspector;
- reconstructing link identity from runtime registry order.

World validation still enforces that one enabled endpoint is not owned by two enabled
relationships. That uniqueness rule is global and cannot be represented by one component's
field layout alone.

## 3. Stable identity and live resolution

### 3.1 Identity categories

Portal code uses each identity only for its intended lifetime:

- `PersistentEntityId`: stable authored identity inside one zone or world scene;
- `StableEntityRef`: durable cross-load reference to an endpoint;
- `EntityId`: live runtime identity inside one simulation world;
- `StoragePartitionId`: dense runtime storage ownership, never serialized.

A runtime `EntityId` or storage partition id must never appear in authored JSON, cooked
packages, save data, cook hashes, or stable request keys.

### 3.2 Resolved link record

A concrete world-scoped link state owns deterministic resolved records equivalent to:

```cpp
struct ResolvedSpatialPortalLink
{
    EntityId LinkEntity;
    EntityId EndpointA;
    EntityId EndpointB;

    StoragePartitionId PartitionA;
    StoragePartitionId PartitionB;

    SpatialPortalFrame FrameA;
    SpatialPortalFrame FrameB;
    SpatialPortalAperture ApertureA;
    SpatialPortalAperture ApertureB;

    Mat4 AToB;
    Mat4 BToA;
};
```

The actual record also carries the stable relationship identity and the reference-resolution
validity token required by the unified runtime.

The record is immutable for one fixed-tick view and one render-extraction frame.

### 3.3 Resolution states

Each enabled relationship resolves to one of:

- `Active`: both references resolve to valid enabled endpoints;
- `UnresolvedA`: endpoint A's zone or entity is unavailable;
- `UnresolvedB`: endpoint B's zone or entity is unavailable;
- `UnresolvedBoth`: neither endpoint is resident;
- `Disabled`: the link or both endpoints are disabled;
- `InvalidReference`: malformed, zero, self, or permanently missing stable reference;
- `InvalidEndpoint`: a target exists but lacks valid endpoint data or frame;
- `DuplicateEndpointOwnership`: another enabled relationship owns A or B.

Unresolved streaming state is normal and is not reported as malformed content. Only `Active`
relationships participate in traversal, views, physics, and lighting.

### 3.4 Resolution update ownership

The world-scoped link state updates from:

- persistent relationship component changes;
- zone attach and detach batches;
- stable-reference resolution epochs;
- endpoint structural changes;
- endpoint transform and component changes;
- endpoint migration journals when applicable.

It does not scan every entity every frame.

Preferred mechanism:

1. cache a query over persistent relationship entities;
2. use the stable-reference resolver to subscribe or poll at deterministic lifecycle drains;
3. cache endpoint query and transform change epochs;
4. rebuild only affected resolved records;
5. sort output by stable relationship identity;
6. publish one immutable ordered span.

No worker mutates the live resolution table. A future parallel collection path merges into
the same stable order as the serial reference.

## 4. Frame convention and validity

### 4.1 Endpoint frame

The endpoint convention is pinned before subsystem integration:

- center is the endpoint world position;
- front normal is the endpoint world forward vector;
- up is the endpoint world up vector;
- right follows the engine transform handedness;
- local aperture coordinates are right and up;
- front half-space has positive signed distance;
- traversal enters from front and exits from the linked endpoint's front;
- the link transform includes the required local half-turn.

Tests pin the exact matrix, handedness, and inverse relation. No subsystem independently
rebuilds the mapping.

### 4.2 Valid endpoint transform

An endpoint is valid only when:

- translation is finite;
- rotation is finite and normalizable;
- scale is unit within a named validation tolerance;
- the basis is nondegenerate;
- both half extents are finite and positive.

v1.0 endpoints are static. A transform change invalidates derived view, physics, light, and
cook state, but authored runtime movement of endpoints is rejected.

## 5. Serialization and registration

Stage 1 traces and updates:

- endpoint component declaration and registration;
- relationship component declaration and registration;
- stable component type ids;
- metadata schemas;
- endpoint and relationship serializers;
- `StableEntityRef` field codec and package fixups;
- world-scene and zone-scene cooked passthrough;
- persistent partition import;
- default runtime schema registration;
- editor inspector exposure;
- tests for unknown, missing, zero, malformed, stale, and round-trip fields.

No second scene-loading or reference-resolution path is added.

If the component additions change a public game-module ABI, stop at the ABI gate and update
the module fingerprint and compatibility tests as one explicit stage.

## 6. Editor ownership

### 6.1 Link creation command

Normal authoring uses a pair-level command:

1. create or select two endpoint entities;
2. validate that they are distinct and currently unowned;
3. ensure both have `SpatialPortalEndpointComponent`;
4. create one relationship entity in the world scene;
5. assign durable endpoint references;
6. validate immediately;
7. select or frame the resulting relationship.

Undo removes the relationship entity and restores any endpoint components created by the
command. Redo recreates the same durable relationship identity when the editor's entity
identity rules permit it, otherwise it recreates an equivalent relationship through the
ordinary undo snapshot contract.

### 6.2 Link field editing

Endpoint references are not ordinary free-form numeric fields.

The relationship inspector exposes:

- Endpoint A picker;
- Endpoint B picker;
- swap endpoints;
- select endpoint;
- open endpoint zone;
- unlink or delete relationship;
- enabled state;
- resolution and validation status.

The picker only offers endpoint entities and checks ownership conflicts before commit.

Low-level raw component editing may remain available for debugging or test fixtures, but it
is not a supported normal authoring path and cannot bypass validation.

### 6.3 Duplication

Pinned behavior:

- duplicating an endpoint duplicates only endpoint-local data and creates no relationship;
- duplicating a relationship entity without endpoints clears its references or is rejected;
- a dedicated duplicate-pair command duplicates both endpoint entities and creates one new
  relationship referencing the duplicates;
- cross-zone pair duplication uses the existing multi-document command discipline;
- undo and redo operate as one transaction;
- no duplicate operation copies a stable relationship identity onto a second live relation.

Do not invent a generic relationship framework solely for portals. Extend existing document
commands with the smallest mechanism justified by current durable-reference consumers.

### 6.4 Deletion and moves

Deleting an endpoint does not silently delete world-level content unless the editor command
owns both operations.

Supported behavior:

- delete-pair command deletes relationship and both endpoints;
- deleting one endpoint through generic deletion leaves the relationship invalid and visibly
  repairable;
- moving an endpoint between authored zones updates its stable reference atomically;
- moving the relationship entity out of the world scene is rejected;
- unloading an editor document leaves the durable reference visible but unresolved.

### 6.5 Visualization

Editor visualization includes:

- endpoint aperture rectangle and normal;
- stable relationship label;
- line or arc between endpoints when both documents are open;
- active, disabled, unresolved, invalid, and duplicate-owner states;
- relationship selection from either endpoint;
- transformed camera preview after v0.1 runtime rendering is stable.

Visualization is derived and is never serialized.

## 7. Validation

### 7.1 Relationship rules

World validation reports:

- relationship not owned by world-scene content;
- zero or malformed endpoint reference;
- Endpoint A equals Endpoint B;
- endpoint does not exist in authored world content;
- target lacks `SpatialPortalEndpointComponent`;
- endpoint disabled while relationship enabled;
- endpoint owned by another enabled relationship;
- invalid endpoint transform or aperture;
- runtime movement policy applied to a static endpoint;
- endpoint not aligned with an authored render and collision opening;
- overlapping apertures that violate the one-active-link-per-body content rule;
- relationship requiring a zone not present in the world manifest.

Validation output is deterministic and sorted by stable relationship identity and rule id.

### 7.2 Runtime defense

Runtime resolution repeats safety-critical checks because cooked content, save overlays, and
hand-authored fixtures may be stale or malformed.

An invalid relationship remains inert. Diagnostics are emitted once per state transition,
not once per frame.

## 8. Cook data

### 8.1 World-level collection

World cook collects before individual zone cooks:

- persistent relationship entities from the world scene;
- endpoint records from every authored zone scene;
- stable endpoint identities;
- endpoint world frames and apertures;
- relationship enabled state;
- endpoint ownership and zone identity;
- source content identity needed for hashing.

The cook resolves and validates every durable reference before creating per-zone lighting
inputs.

### 8.2 Deterministic pairing

There is no scan-and-group pairing by shared id.

Sort relationship inputs by:

1. relationship `PersistentEntityId`;
2. endpoint A stable reference;
3. endpoint B stable reference.

Endpoint order is authored and meaningful for deterministic debug labels. The transforms are
still generated in both directions.

### 8.3 Cook hash participation

A zone's lighting hash includes a relationship only when it can affect that zone's direct or
probe bake.

Relevant fields include:

- stable relationship identity;
- both stable endpoint references;
- endpoint frames and extents;
- enabled states;
- source light inputs;
- relevant source and destination occluder hashes;
- one-hop transport policy version;
- applicable lighting settings.

Moving or resizing an endpoint restales affected zones. Editing a distant unrelated relation
does not.

### 8.4 Package and runtime import

The world-scene package carries the relationship entity and durable references. Zone packages
carry endpoint entities and persistent identity metadata.

At runtime:

1. import the world scene into the persistent partition;
2. import endpoint zones independently;
3. publish stable-reference fixups only after each zone import is complete;
4. update resolved relationship state at the lifecycle drain;
5. never expose a partially imported endpoint as active.

## 9. Participation leases

The world-scoped link state owns concrete lease handles for present portal needs.

Lease reasons are explicit and counted separately:

- visible destination view;
- near traversable character;
- near traversable rigid body;
- active character overlap;
- active rigid-body coupling;
- pending partition migration;
- portal audio;
- debugging pin.

Rules:

- leases are one hop;
- source and destination requirements are computed independently;
- an unresolved relationship holds no destination lease unless an authored prefetch policy
  explicitly identifies the zone from the stable reference;
- a visible portal may request destination Visible before endpoint resolution completes;
- crossing bodies pin both endpoint Physics participation until clear;
- a lease releases at a deterministic lifecycle boundary;
- forced teardown produces an explicit portal outcome before endpoint destruction;
- portal code does not place demand policy inside `RuntimeWorld` or `ZoneRuntime`.

## 10. Migration and endpoint identity

Endpoint entities are normally static and zone-owned. Portal traversal does not migrate the
endpoints or relationship entity.

Crossing gameplay entities may migrate partitions. That migration does not change endpoint
references, link identity, or the resolved relation.

If an endpoint itself is moved between authored zones in the editor, the relationship's
`StableEntityRef` is updated as part of the authoring transaction.

## 11. Stage 1 tests

Add focused coverage equivalent to:

- `SpatialPortalRelationshipSerializationTests`
- `SpatialPortalRelationshipValidationTests`
- `SpatialPortalReferenceResolutionTests`
- `SpatialPortalEditorCommandTests`
- `SpatialPortalCookInputTests`
- `SpatialPortalLeaseTests`

Required cases:

1. one relationship stores exactly two durable references;
2. A equal to B is rejected;
3. a target lacking endpoint data is rejected;
4. one endpoint owned by two relationships invalidates the conflict deterministically;
5. endpoint unload produces `UnresolvedA` or `UnresolvedB`, not malformed state;
6. endpoint reload resolves to a new live `EntityId` through the same stable reference;
7. partition-slot reuse cannot satisfy a stale cache;
8. world-scene relationship identity remains stable across endpoint unload;
9. persistent and zone package import order does not change resolved ordering;
10. editor creation, swap, repair, unlink, delete, duplicate endpoint, and duplicate pair undo
    and redo correctly;
11. moving an endpoint between authored zones updates the durable reference atomically;
12. identical world input produces byte-identical cook relationship inputs;
13. relevant link edits restale affected zones only;
14. leases acquire and release with deterministic reasons and no recursive demand;
15. no authored output contains runtime `EntityId` or `StoragePartitionId` values.
