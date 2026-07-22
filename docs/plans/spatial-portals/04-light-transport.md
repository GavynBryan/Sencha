# Light Transport

Status: proposed execution specification, revised for the unified runtime world.

This document extends the existing direct-light atlas, probe bake, dynamic light extraction,
and shadow-residency mechanisms through one static spatial portal relationship.

It does not introduce another world, space, dimension, renderer, cooker, or light system.

## 1. Unified-world assumptions

Light transport depends on:

- persistent world-scene relationship entities;
- endpoint entities in zone documents and runtime storage partitions;
- durable endpoint references resolved at world-cook and runtime lifecycle boundaries;
- one runtime ECS `World` queried through visible partition sets;
- one-hop participation leases for remote destination zones;
- the current RGB9E5 direct atlas, R8 AO, L1 probe, dynamic forward-light, and shadow-cache
  architecture;
- stable persistent relationship identity independent of runtime `EntityId` and partition slot.

Portal lighting never infers destination relevance from ordinary world-space proximity between
endpoint zones.

## 2. Shared optical model

A rigid unit-scale relationship creates a virtual image of a source light in destination
space.

For a destination sample:

1. transform the real light pose through the source-to-destination portal transform;
2. trace from the sample toward the virtual light;
3. require that segment to intersect the destination aperture;
4. require the intersection to lie inside the rectangular aperture;
5. map the intersection to the source endpoint;
6. test source-endpoint-to-real-light visibility;
7. test destination-sample-to-aperture visibility;
8. evaluate the existing point or spot attenuation and diffuse model using the virtual light;
9. stop after one relationship traversal.

The aperture constraint is mandatory. A transformed light without it illuminates around the
wall and is incorrect.

Unit scale preserves distance, attenuation, and angular velocity relationships. Scale-changing
portals remain unsupported.

## 3. Stable identity

All portal light and shadow identity derives from stable authored data:

- persistent relationship identity;
- endpoint side;
- stable source light identity;
- light kind;
- shadow face where applicable.

Runtime `EntityId`, storage partition id, import order, visible-list index, and raw pointers are
never stable request identity.

When a source light has no durable authored identity, its live render identity may be used for
the current runtime session, combined with persistent relationship identity. That path is not
serialized and is invalidated on entity destruction.

## 4. Static direct-light bake

### 4.1 World-level relationship resolution

World cook resolves portal relationships before cooking individual zones:

1. load persistent relationship records from the world scene;
2. collect endpoint records and stable identities from all authored zone scenes;
3. validate both durable references and endpoint uniqueness;
4. build deterministic resolved relationship inputs;
5. collect source lights and source and destination occluder identities;
6. pass only relevant relationships into each zone cook.

There is no shared-id endpoint grouping and no dependence on editor registry order.

### 4.2 Evaluator split

The current direct bake combines light evaluation with ordinary single-segment visibility.
Refactor only as much as required into concrete cook operations:

- unoccluded point or spot contribution;
- ordinary single-segment visibility;
- portal two-segment visibility.

Do not introduce a strategy interface or second bake pipeline.

### 4.3 Relevant light-image assembly

For one destination zone:

1. collect local direct lights as today;
2. find active relationships whose destination endpoint belongs to the zone;
3. find source lights whose range or spot cone can reach the source aperture;
4. coarse-cull by aperture and light bounds before per-sample work;
5. create deterministic one-hop light-image inputs;
6. include relevant source and destination BVH domains;
7. bake local and transmitted direct contributions into the same RGB9E5 atlas.

Remote source zones participate because of the relationship, not because their Euclidean
bounds are near the destination zone.

### 4.4 Two-segment visibility

A transmitted sample is visible only when:

- sample-to-destination-aperture segment is clear in destination geometry;
- aperture intersection lies strictly inside the opening under the bake tolerance;
- source-aperture-to-real-light segment is clear in source geometry;
- source light range and spot cone include the transformed path;
- neither segment begins inside invalid solid geometry;
- one-hop transport has not already been consumed.

The bake uses render geometry BVHs, not physics collision.

### 4.5 Hashing and determinism

The destination lighting hash includes only relevant inputs:

- persistent relationship identity;
- both stable endpoint references;
- endpoint frames and extents;
- relationship and endpoint enabled state;
- stable source light state;
- relevant source and destination geometry hashes;
- direct-light shading settings;
- aperture tolerances;
- portal light-transport policy version.

Stable ordering:

1. persistent relationship identity;
2. destination endpoint side;
3. stable source light identity;
4. light kind.

Identical authored input produces byte-identical atlas output regardless of zone manifest,
document load, or package import order.

## 5. Probe transport

The existing probe bake traces a fixed deterministic ray table. A ray that reaches a portal
aperture may continue in the linked zone's geometry domain.

### 5.1 One-hop ray state

A probe ray carries:

- current world point and direction;
- remaining distance;
- current authored zone or geometry domain;
- whether one portal traversal has occurred;
- persistent relationship identity used for deterministic tie breaking.

The nearest event is one of:

- geometry hit: preserve existing front-face, back-face, bounce, and invalid-probe behavior;
- environment miss: use sky or ground;
- portal aperture before geometry: transform point and direction, subtract distance, continue
  in the linked domain;
- second portal after one hop: use the pinned nontransmitting fallback.

### 5.2 Event ordering

Exact hit ties use a documented rule:

1. authored rim or surface geometry wins at the aperture boundary;
2. portal wins only when the projected point is strictly inside the opening;
3. persistent relationship identity breaks multiple-aperture ties.

The fixed ray table and accumulation order remain unchanged. Serial and job paths stay
bit-identical.

### 5.3 Domain BVHs

World cook provides domain-specific BVHs and resolved relationships to the probe kernel.

Do not permanently flatten every transformed remote zone into every destination BVH. A ray
selects the linked geometry domain only when it actually reaches an aperture.

### 5.4 Probe acceptance

Tests prove:

- bright remote room contributes directional SH through the aperture;
- disabling or deleting the relationship restores sealed-wall output;
- wall regions outside the aperture remain dark;
- source and destination occluders independently block transport;
- identical input is byte-identical;
- serial and worker paths match;
- work scales with rays reaching apertures rather than all relationships per ray;
- unrelated remote relationship edits do not restale this zone.

## 6. Dynamic light images

### 6.1 Extraction ownership

Dynamic light extraction consumes one `World`, explicit visible partition sets, and immutable
resolved portal relationships.

A transmitted image is created only when:

- source light is active in a participating source partition;
- source light sphere or cone can reach the source aperture;
- destination endpoint is resolved and its partition is Visible for the current view;
- destination relationship survives deterministic cap ranking;
- one-hop context has not already been consumed.

The image is render-domain data, not another ECS light entity.

### 6.2 Transmitted record

A transmitted light record contains:

- virtual point or spot light parameters;
- destination aperture frame and extents;
- persistent relationship identity;
- endpoint side;
- source light stable render identity;
- source and destination partition information for this extraction frame;
- shadow request identity when applicable.

The source light remains ordinary in source views.

### 6.3 Fragment aperture constraint

For each destination fragment and transmitted image:

1. intersect fragment-to-virtual-light segment with the destination plane;
2. reject parallel or out-of-segment intersections;
3. reject points outside the aperture;
4. evaluate ordinary point or spot contribution only when valid.

Use a dedicated bounded transmitted-light representation unless measurements prove a unified
record is better. Do not add unused portal data to every ordinary light for symmetry.

### 6.4 Culling and caps

Rank images by:

1. destination aperture visible;
2. source light reaches source aperture;
3. projected affected area;
4. estimated contribution;
5. prior-frame shadow or image residency;
6. persistent relationship and source light identity.

Expose packed, dropped, and candidate counts. Portal images must not silently consume the
ordinary forward-light cap.

A remote destination may acquire a Visible lease because a portal is visible. A light image
does not recursively acquire another portal destination.

## 7. Dynamic shadow transport

Unshadowed transport lands first. v1.0 is incomplete until shadow-casting transmitted lights
preserve source and destination occlusion.

### 7.1 Virtual shadow view

A transmitted shadow view uses the virtual light in destination coordinates.

Caster input includes:

- destination-domain casters between receivers and destination aperture;
- source-domain casters transformed through the relationship;
- crossing entities represented and clipped in their valid domain;
- exact aperture constraint;
- one-hop context preventing recursive portal shadow views.

The result uses the existing shadow atlas and scheduler.

### 7.2 Spot and point lights

A spot light requests one virtual shadow view.

A point light follows the existing cube-face model. The first correct implementation may
request all six faces. Face culling is a measured follow-up only when correctness is preserved.

### 7.3 Residency and invalidation

A transmitted shadow key derives from:

- source light stable render identity;
- persistent relationship identity;
- destination endpoint side;
- light kind and point face.

Invalidation includes:

- source light transform or shadow-relevant state;
- endpoint frame, aperture, enabled state, or resolution change;
- source caster changes;
- destination caster changes;
- crossing representation changes;
- source or destination partition detach;
- relationship deletion;
- stale stable-reference resolution;
- shadow settings.

Portal shadow requests participate in existing fairness and budgets. They do not own a private
atlas or bypass denial counters.

## 8. Crossing entities and lighting

A crossing entity has two clipped render representations while remaining one ECS entity.

Rules:

- source representation uses source dynamic lights, probes, atlas, AO, and shadow domain;
- destination representation uses destination-domain inputs;
- destination representation does not wait for storage partition migration to commit;
- crossing records explicitly provide its spatial domain;
- movable objects normally have no direct-light atlas allocation and sample destination probes;
- shadow-caster identity derives from the canonical entity and persistent relationship;
- clip planes prevent duplicate silhouettes;
- lighting is never copied from source object state.

## 9. Partition participation and detach

Portal lighting uses domain participation explicitly:

- ordinary main-view extraction sees Visible partitions;
- a visible portal acquires one-hop destination Visible participation;
- shadow extraction sees only participating source and destination domains required by the
  selected transmitted request;
- dormant remote zones perform no normal lighting extraction;
- source or destination detaching invalidates images and shadow requests before entity rows or
  backend resources disappear;
- unresolved relationships emit no light image.

Static bake is independent of runtime participation and resolves authored world content.

## 10. Diagnostics

Counters include:

- resolved lighting relationships;
- portal light-image candidates, packed, and dropped;
- portal aperture fragment tests;
- static transmitted paths evaluated;
- source and destination BVH tests;
- probe rays reaching and crossing apertures;
- probe rays stopped by one-hop limit;
- transmitted shadow requests and rendered views;
- transmitted caster draws;
- portal-driven shadow invalidations;
- destination Visible leases held for lighting;
- unresolved or detaching light paths rejected.

Debug views include:

- virtual point and spot lights;
- source and destination aperture frusta;
- stable relationship and light keys;
- source and destination geometry domain;
- aperture rejection;
- two-segment visibility;
- shadow slot and point face;
- probe ray domain and one-hop state;
- partition and lease state.

## 11. Performance shape

### 11.1 Static direct bake

Work is proportional to destination samples and relevant local or transmitted lights after
coarse culling. It is not all zones times all links times all lights.

### 11.2 Probe bake

Normal work remains probes times fixed rays times BVH traversal. Added work is proportional to
rays that actually hit an aperture.

### 11.3 Dynamic extraction

Candidate generation is proportional to active visible relationships and source lights that
can reach their source aperture. Avoid all-lights times all-relationships in steady state.

### 11.4 Dynamic shading

Cost is proportional to ordinary visible lights plus packed transmitted images. Record actual
fragment iterations. Do not hide a correctness cap by increasing it without a measured
lighting-list plan.

### 11.5 Shadows

Work is bounded by existing per-frame shadow-view and atlas budgets. Portal requests obey the
same scheduler and denial diagnostics.

## 12. Tests

Add focused coverage equivalent to:

- `SpatialPortalDirectLightTests`
- `SpatialPortalProbeBakeTests`
- `SpatialPortalLightExtractionTests`
- `SpatialPortalShadowRequestTests`
- `SpatialPortalLightingCookTests`

Required cases:

1. virtual point and spot transforms match the shared rigid mapping;
2. fragment path outside aperture receives zero transmitted light;
3. source and destination occlusion independently block;
4. aperture edge tolerance is symmetric and pinned;
5. source range and spot cone cull correctly;
6. same-zone, cross-zone, and spatially remote endpoints produce equivalent optical results;
7. manifest, document, package import, and partition order do not affect bake bytes or runtime
   ranking;
8. relationship and endpoint edits restale affected zones only;
9. probe serial and worker paths are bit-identical;
10. shadow request keys survive endpoint unload and reload through stable relationship identity;
11. cache invalidation covers both partitions and crossing casters;
12. cap pressure drops deterministically and reports counts;
13. disabled, invalid, unresolved, and detaching relationships emit no image;
14. dormant remote zones add no dynamic extraction work;
15. one portal visible through another cannot schedule recursive light transport.

## 13. Stop conditions

Stop when:

- durable relationship resolution is unavailable to world cook;
- the current lighting architecture has not been reconciled onto the unified runtime;
- relevant source lights cannot be coarse-culled without global scans;
- shadow transport requires recursive portal views;
- a transmitted request must bypass existing shadow fairness or atlas ownership;
- portal light data must become another ECS light entity;
- remote-zone participation cannot be expressed through existing leases;
- deterministic bake hashes would require runtime partition ids;
- the zero-portal lighting path gains measurable work.
