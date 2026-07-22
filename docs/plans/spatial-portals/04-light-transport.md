# Light Transport

Status: proposed execution specification.

This document extends the current direct-light atlas, probe bake, dynamic light extraction, and shadow-residency mechanisms through one static spatial link.

## 1. Shared optical model

A rigid unit-scale link creates a virtual image of a source light in destination space.

For a destination sample:

1. transform the real light position and direction through the source-to-destination link;
2. trace from the sample toward the virtual light;
3. require the ray to intersect the destination aperture;
4. transform the aperture intersection to the source endpoint;
5. require the source segment to reach the real light;
6. test occlusion on the destination segment and source segment;
7. evaluate the existing point or spot attenuation and diffuse model using the virtual light;
8. stop after one link.

The aperture test is mandatory. A transformed light without an aperture constraint leaks through the wall and is incorrect.

Rigid unit scale preserves distance and energy under the virtual-light transform. Scale-changing portals remain unsupported.

## 2. Static direct-light bake

### 2.1 Existing evaluator reuse

The current direct bake combines light evaluation and a single-BVH visibility test.

Before portal transport, split the reusable math at a concrete function boundary:

- unoccluded point or spot contribution;
- ordinary single-segment visibility;
- portal two-segment visibility.

Do not introduce a runtime strategy interface. These are cook-only concrete operations with one present algorithm each.

### 2.2 World-cook assembly

World cook already gathers every zone's bake halo before individual cooks. Extend that collection with resolved static link and light records.

For each destination zone:

1. collect ordinary local direct lights;
2. find active links whose destination endpoint belongs to the zone;
3. find source lights whose range or cone can reach the source aperture;
4. create deterministic one-hop light-image records;
5. include only source and destination occluder sets within the relevant path bounds;
6. bake local and transmitted direct contributions into the same RGB9E5 atlas.

No second atlas or side-channel texture is introduced.

### 2.3 Two-segment visibility

A transmitted direct-light sample is visible only when:

- sample-to-destination-aperture segment is clear in destination geometry;
- intersection lies inside the rectangle with the bake edge tolerance;
- source-aperture-to-real-light segment is clear in source geometry;
- the source light range and spot cone include the path;
- neither segment begins inside invalid geometry.

The existing bake BVH remains the triangle authority. Physics collision is not used for lighting.

### 2.4 Hashing and determinism

The destination bake hash includes only links and source inputs that can affect it:

- both endpoint frames and extents;
- source light state;
- source and destination relevant geometry content hashes;
- direct-light shading parameters;
- aperture tolerance and one-hop policy version.

Stable sort order:

1. destination link id;
2. source endpoint ownership;
3. source light stable cook identity;
4. light kind.

Identical input produces byte-identical atlas output.

Tests reverse zone manifest order and halo collection order.

## 3. Probe transport

The current probe bake traces a fixed deterministic ray table against one assembled BVH. A portal is an opening into another spatial domain, so a ray that reaches the aperture must continue from the linked endpoint.

### 3.1 One-hop ray state

A probe ray carries:

- current point and direction;
- remaining distance;
- current zone or geometry domain;
- whether one link has already been crossed.

When the nearest event is:

- geometry: evaluate the existing front-face, back-face, bounce, or invalid-probe behavior;
- environment miss: use sky or ground;
- portal aperture before geometry: transform point and direction, subtract traveled distance, continue in the linked domain;
- a second portal after one crossing: treat it as nontransmitting under the one-hop rule.

### 3.2 Deterministic event ordering

Exact hit ties use a pinned order and epsilon:

1. geometry before portal when the geometry is the authored rim or surface boundary;
2. portal before geometry only for a point strictly inside the open rectangle;
3. stable link identity breaks multiple-aperture ties.

The fixed ray table and per-probe accumulation order remain unchanged. Serial and job paths remain bit-identical.

### 3.3 Probe bake input

World cook provides domain-specific BVHs and resolved link records to the probe kernel.

Do not flatten all transformed source geometry permanently into every destination BVH. That can grow without bound and obscures domain ownership. One-hop traversal selects the linked BVH only when a ray reaches an aperture.

### 3.4 Probe acceptance

Tests prove:

- bright source room contributes directional SH through the aperture;
- closing or disabling the link restores the sealed-wall result;
- the wall around the aperture remains dark;
- a source occluder blocks transport;
- a destination occluder blocks transport;
- identical inputs are byte-identical;
- serial and worker paths match;
- ray count scales with actual aperture hits, not all links per ray.

## 4. Dynamic light images

### 4.1 Extraction

Dynamic light extraction creates a transmitted image only when:

- the source light is active;
- its sphere or spot cone can reach the source aperture;
- the destination endpoint is active and visible for the current camera;
- the one-hop light-image cap has room after deterministic ranking.

A transmitted record contains:

- virtual point or spot light data;
- destination aperture frame and extents;
- real source light stable identity;
- source and destination endpoint identity;
- shadow request identity when applicable.

The real light remains in its ordinary source view. The image is a render-domain light, not an ECS light entity.

### 4.2 Fragment aperture constraint

For a destination fragment and virtual light:

1. intersect fragment-to-light ray with the destination portal plane;
2. reject parallel rays and intersections outside the segment;
3. reject intersection outside the rectangle;
4. evaluate ordinary point or spot contribution only when valid.

Pack aperture data in a dedicated transmitted-light structure. Do not add unused portal fields to every ordinary `GpuLight` if a separate bounded array is clearer.

The shader path is measured before deciding between:

- a separate transmitted-light loop;
- a unified light record with a local kind switch;
- per-object transmitted-light lists.

No option is chosen solely for symmetry.

### 4.3 Culling and caps

Rank images by:

1. destination aperture visible;
2. light reaches source aperture;
3. projected affected area;
4. estimated contribution;
5. previous-frame residency;
6. stable link and light identity.

Expose:

- image cap;
- dropped image count;
- aperture-test fragment iterations in profiling builds;
- per-view transmitted light count.

The current ordinary light cap and its known correctness wall are part of the baseline. Portal images must not silently consume the cap without counters.

## 5. Dynamic shadow transport

Unshadowed light transport lands first. v1.0 is not complete until shadow-casting transmitted lights preserve occlusion.

### 5.1 Virtual shadow view

A transmitted shadow view is built from the virtual light in destination space.

Caster input includes:

- destination casters between receiver region and destination aperture;
- source casters transformed through the link;
- source and destination representations clipped to their valid half-spaces;
- the aperture constraint.

The result is one shadow image in destination light space. It does not recursively render another portal.

### 5.2 Point and spot lights

Spot lights use one virtual shadow view.

Point lights use the existing cube-face model. Only faces whose frusta can contribute through the aperture are requested when that optimization is proven correct. The initial correct path may request all six faces, then profile.

### 5.3 Stable residency identity

A transmitted shadow request key derives from:

- real light `RenderEntityKey`;
- link id;
- destination endpoint side;
- light kind and face.

Invalidation includes:

- real light transform or shadow-relevant state;
- either endpoint transform or extent;
- source caster changes;
- destination caster changes;
- crossing representation changes;
- link activation and unload.

The bounded-fair scheduler remains authoritative. Portal requests do not bypass fairness or allocate a private atlas.

### 5.4 Shadow leakage tests

Required captures:

- source blocker only;
- destination blocker only;
- blocker crossing the source aperture;
- blocker crossing the destination aperture;
- narrow aperture with bright point light;
- spot cone partly intersecting aperture;
- light moving across aperture edge;
- caster moving while shadow slot is cached;
- link disable and re-enable;
- source or destination zone unload.

No lit pixels may appear outside the projected aperture path beyond the documented PCF edge tolerance.

## 6. Crossing entities and lighting

A crossing entity has two clipped render representations.

For direct and probe lighting:

- source representation uses source view light and probe data;
- destination representation uses destination view light and probe data;
- baked lightmap and AO bindings follow the representation's spatial domain;
- a movable object normally has no static lightmap and samples destination probes naturally;
- no light value is copied from the source representation.

For shadows:

- each clipped representation enters the caster set for its valid domain;
- stable synthetic caster keys derive from the canonical entity;
- a transformed source caster may also participate in a transmitted virtual-light shadow view;
- duplicate silhouettes are prevented by clip planes and caster identity.

## 7. Lighting diagnostics

Counters:

- portal light-image candidates;
- portal light images packed;
- portal light images dropped;
- portal aperture tests;
- static portal light paths evaluated;
- destination segment occlusion tests;
- source segment occlusion tests;
- probe rays reaching an aperture;
- probe rays crossing;
- probe rays stopped by one-hop limit;
- transmitted shadow requests;
- transmitted shadow views rendered;
- transmitted caster draws;
- portal-driven shadow invalidations.

Debug views:

- virtual light positions and cones;
- source and destination aperture frusta;
- transmitted-light affected bounds;
- aperture rejection;
- source and destination occlusion segment;
- transmitted shadow slot and face;
- probe ray domain and one-hop state.

## 8. Performance shape

Expected complexity:

### Static direct bake

```text
O(destination samples * relevant local lights)
+ O(destination samples * relevant transmitted light paths)
```

Relevance filtering must happen before per-sample evaluation.

### Probe bake

```text
O(probes * fixed rays * BVH traversal)
```

Additional work is proportional to rays that hit an aperture. It is not proportional to every link for every bounce step.

### Dynamic extraction

```text
O(visible or relevant links * source lights near their aperture)
```

Avoid all-lights times all-links in normal frames. Use source aperture bounds and existing light spatial tests.

### Dynamic shading

```text
O(ordinary visible lights + packed transmitted light images)
```

Record fragment iteration evidence. If portal images push the forward loop beyond the measured gate, a per-object or tiled light-list plan may become a prerequisite. Do not hide the cost by raising caps.

### Shadows

Work is bounded by the existing per-frame shadow-view budget and atlas residency. Portal requests participate in the same fairness rules.

## 9. Tests

Add focused coverage equivalent to:

- `SpatialPortalDirectLightTests`
- `SpatialPortalProbeBakeTests`
- `SpatialPortalLightExtractionTests`
- `SpatialPortalShadowRequestTests`
- `SpatialPortalLightingCookTests`

Required cases:

1. virtual point and spot transforms match ordinary rigid transforms;
2. fragment ray outside aperture receives zero transmitted light;
3. both occlusion segments independently block;
4. edge tolerance is symmetric and pinned;
5. source light range and cone cull correctly;
6. manifest and zone order do not affect bake bytes or runtime ranking;
7. link edits restale affected zones only;
8. probe serial and job paths are bit-identical;
9. shadow request keys remain stable across registry attachment order;
10. cache invalidation covers both domains;
11. cap pressure drops deterministically and reports counts;
12. disabled, incomplete, invalid, and unloaded links emit no image.
