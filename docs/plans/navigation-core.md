# Navigation Core: implementation plan

Status: **tickets B, C, and D implemented** on branch `navigation-core`
(2026-09-24); tickets E and F and the cross-zone planner are not started. The
current architecture is described in `docs/navigation/runtime.md`; this document
remains the execution spec for Track A items 5 and 6 of `engine-roadmap.md` and
records the decisions behind it. The roadmap owns versions and gates; this
document owns mechanism detail.

Navigation answers where an agent can travel, what the trip costs, and which
special traversals a route requires. It does not decide why an agent wants to
go somewhere. That belongs to future AI and environmental-query systems, which
compose navigation with `PhysicsQueries` rather than reaching into either
backend.

## 1. Settled shape

- **Backend.** Recast builds navmesh tiles. It runs in the cook and, for
  localized rebuilds, in the runtime. Detour stores tiles and answers
  projection, nav-raycast, and corner extraction. Both are pinned,
  statically linked, PRIVATE to `sencha_engine`, and fenced by an isolation
  check. No Recast or Detour type appears in an installed header.
  DetourCrowd and DetourTileCache are not used.
- **Search.** Sencha owns the A* and Dijkstra over Detour's polygon graph.
  Navigation links are Sencha edges held outside Detour tile data. Upstream
  Detour off-mesh connections only reach the same or an adjacent tile, so
  they cannot express long discontinuous links, and they would have to be
  re-baked into every rebuilt tile.
- **Ownership.** Each resident zone owns one `ZoneNavigation` zone resource,
  which contains one tiled Detour mesh per build profile. It dies with the
  zone. Queries name a `ZoneId`; there is no simulation-wide navmesh.
- **Invalidation.** A zone generation changes only when the resource is
  created, which covers attach, re-attach, and a future reload. Local changes
  invalidate per tile, through Detour's tile salt, and per link, through a
  link revision. A route records the tiles and links it depends on, and only
  those.
- **Runtime modification.** Supported navigation geometry can be added,
  removed, and moved at runtime. That includes creating and destroying
  walkable surfaces. Only the affected tiles are rebuilt, within a per-tick
  budget. Simple gating goes through link state instead.
- **Out of scope here.** All Kyusu tooling: the navigation overlay, debug
  path queries, and link authoring UX. `BrushRole::AreaTag` area painting is
  also out of scope.

## 2. Corrections to the approved review

Three statements in the review
(`~/.claude/plans/pasted-content-id-173e-sencha-iridescent-yao.md`) do not
survive a closer read of the tree.

1. **Brush cells are the whole static collision set.**
   - `Collider` has no `SENCHA_SCHEMA` or scene chunk
     (`engine/include/physics/components/Collider.h`), so no scene can
     author one.
   - Its only production producer is `LoadZoneCollision`, which creates one
     collider per cooked brush cell (`engine/src/physics/ZoneCollisionLoader.cpp:49`).
   - There are therefore no "placed static colliders bypassing the cook".
     Ticket A shrinks to extracting the shared triangle source, and it
     becomes step B1.
   - When `Collider` becomes authorable, it joins that shared source. That
     same change unblocks authored runtime contributors (section 6).
2. **Settings live in data assets, not in an `EngineConfig` section.**
   - The editor cook and the shipping runtime do not share an `engine.json`.
     Cook parameters arrive as editor cvars (`editor.cook.cell_size`,
     `editor/kyusu/src/project/CookSession.cpp:116`).
   - Build profiles and the area table therefore go in a
     `navigation.settings` data asset, read by the cook.
   - The cooked `.snav` is self-describing: it carries the profile
     parameters and area names, so the runtime never needs the settings
     asset.
   - Query policies are `navigation.policy` data assets, following the
     `movement.profile` precedent (`engine/include/movement/MovementProfileData.h`).
3. **Tile rebuilds run as intra-tick fork-join, not on the async lane.**
   - `AsyncTaskQueue` commits only at the frame drain, and it cannot cancel
     or wait on a running task (`engine/include/jobs/AsyncTaskQueue.h:18-23`).
   - So "finish synchronously if not ready at tick T+D" cannot be built on it.
   - Rebuilds instead run inside the fixed tick on `JobSystem::ParallelFor`,
     within a tile budget, and publish serially in canonical order.
   - `worker_count == 0` is the serial reference. Navigation lags a geometry
     change by exactly one tick.

### Changes made during implementation

- `NavigationSystem` is registered by the engine for every host rather than
  opt-in; with no navigation in any resident zone it does no work.
- `CollectReachable` bounds its search by cost only. The radius filters which
  regions are reported, so an entry cost is a region's true search cost even when
  the cheapest way in leaves the radius.
- Costs everywhere are measured between polygon-edge midpoints (the Detour
  convention). They are estimates to rank by; a flood and a goal search can
  disagree slightly about the same polygon.
- Tile rebuild runs inside `NavigationSystem::PostFixed` (detect, rebuild,
  publish, then link state), so a change is visible one tick later.
- The runtime reads `nav.rebuild.tiles_per_tick` and `nav.rebuild.move_threshold`
  as cvars.
- `NavigationGeometry` is runtime-only, like `Collider`: no schema, no scene chunk.
- Authored stable-id scene fields share `StableIdFieldCodec.h`; the zone and
  navigation codecs no longer each carry a copy.
- `CookZoneNavigation` returns the `NavigationFile` instead of encoded bytes;
  encoding and staging belong to the Kyusu step.
- `navigation.settings` is not a registered runtime data type. It is cook-only
  input, parsed by `ParseNavigationSettings` against `NavigationSettingsSchema()`.
- `navigation.policy` registers a `DataSchema` and lists its costs as records
  (`{ area, cost }`, `{ kind, multiplier, add }`) rather than tag-keyed maps,
  which a schema cannot describe.
- `NavigationSystem` composes `NavGeometryTracker`, `NavTileRebuilder`, and
  `NavLinkStateSync`; the query path is `NavQueryPlan` plus `NavSearch`.
- Default tile size is 32 cells. Measured with the `profile` preset (Release),
  pinned to one core, 30 builds each, on a 40 m floor with a 9 x 9 pillar grid
  at 0.15 m cells: 64-cell tiles took a median 2.75 ms (p90 7.0 ms), over the
  ~1 ms gate; 32-cell tiles took 0.31-0.40 ms (p90 0.35-0.54 ms) across two
  runs. The templates' settings use 32.

## 3. Vocabulary and identity

| Concept | Representation | Serialized? |
|---|---|---|
| Navigation link | `NavLink` component in the zone scene document. Cooked into `.snav`, then stripped from the runtime scene | authored |
| Link identity | `NavLinkId = StrongId<NavLinkIdTag, uint64_t>`, minted randomly, nonzero | yes |
| Traversal kind | gameplay tag, stored by name, bound to `GameplayTagId` at finalize | name only |
| Area | gameplay tag, stored by name. The dense backend index (≤ 63, 0 = default) is per `.snav` | name only |
| Build profile | gameplay tag plus parameters, stored in `.snav` | name + params |
| Runtime link state | `NavLinkState` component on any gameplay entity | not replicated in v1 (section 5.7) |
| `NavLocation`, `NavRegion` | `{ZoneId, profile index, generation, 64-bit ref, position}` | never |

`WorldDock` and `WorldLink` remain world-partition topology. Navigation never
reads or writes them. The one exception is the cook-time warning described
in section 4.

## 4. Ticket B: navigation cook product

### B1. Shared static-collision triangle source

- Move `CollectCellTriangles` out of `editor/kyusu/src/document/CellArtifactCook.cpp`
  into `engine/include/assets/cook/StaticCollisionGeometry.h` / `engine/src/assets/cook/StaticCollisionGeometry.cpp`:
  - `AppendCellCollisionTriangles(const BrushCell&, Vec3d offset, positions, indices)`
    appends one cell's triangles. The collision bake passes a zero offset, so
    it stays cell-local; navigation passes the cell origin, so it gets world
    space.
  - `CollectStaticCollisionGeometry(std::span<const BrushCell>)` returns
    `StaticCollisionGeometry { std::vector<Vec3d> Positions; std::vector<uint32_t> Indices; Aabb3d Bounds; }`
    in world space, in cell order.
- `EmitCellArtifacts` calls the cell-local form. Output must not change: the
  existing `.scol` bytes are asserted identical before and after, in
  `test/level_cook/BrushCollisionCookTests.cpp`.
- Record in the header comment that this is the single static-collision
  input for both collision and navigation. When `Collider` becomes
  authorable, it is added here.

### B2. Dependency and firewall

- `engine/CMakeLists.txt`:
  - FetchContent `recastnavigation` at tag `v1.6.0`, with
    `SOURCE_SUBDIR sencha-does-not-build-this` (the bc7enc precedent).
  - Build `Recast/Source/*.cpp` and `Detour/Source/*.cpp` into a static
    library `sencha_navigation_thirdparty` with hidden visibility and
    SYSTEM includes.
  - `DT_POLYREF64=1` is a PUBLIC compile definition of that target, so every
    translation unit that includes Detour agrees on the layout of
    `dtPolyRef`.
  - Link it PRIVATE into `sencha_engine`, and keep it out of `install(EXPORT)`.
- `cmake/CheckNavigationIsolation.cmake`, modeled on `CheckPhysicsIsolation.cmake`:
  - Fail any file under `engine/include` or `engine/src` outside
    `engine/src/navigation/` that includes `Recast*.h`, `Detour*.h`, or
    `DetourNavMesh*.h`.
  - Register it as ctest `navigation_isolation` in the root `CMakeLists.txt`.
- `docs/building.md`: list the new fetched dependency.
- `NOTICE`: add a recastnavigation (zlib) attribution.

### B3. Tile builder (engine, runtime-available)

- `engine/include/navigation/NavTileBuild.h` exposes a backend-neutral
  signature, the same way `BakeCollisionBlob` does:
  - `NavBuildProfile { radius, height, max_slope_degrees, max_climb, cell_size, cell_height, tile_size }`
  - `NavAreaVolume { convex footprint (Vec3d span), min_y, max_y, uint8 area index }`
  - `NavTileBuildInput { profile, tile x/z, triangles (world positions, indices), area volumes }`
  - `bool BuildNavTile(const NavTileBuildInput&, std::vector<std::byte>& tileData, NavTileBuildStats*)`
- `engine/src/navigation/NavTileBuilder.cpp` runs the Recast tile pipeline:
  1. rasterize with walkable-slope classification;
  2. filter low-hanging obstacles, ledges, and low-height spans;
  3. build the compact heightfield;
  4. erode by radius;
  5. mark convex area volumes;
  6. partition regions monotonically, which is simpler and deterministic;
  7. build contours;
  8. build the polygon mesh and a minimal detail mesh;
  9. call `dtCreateNavMeshData`.
- The tile border is `ceil(radius / cell_size) + 3` cells. Tiles use a
  world-anchored grid: tile (x, z) is `floor(p / tile_size)`, and Detour's
  `orig` is the world origin. That keeps cook tiles and runtime-rebuild tiles
  addressable by the same coordinates.
- The builder is single-threaded and allocates through `rcAllocSetCustom` and
  `dtAllocSetCustom`, which are hooked for tests. It is pure: the same input
  always produces the same bytes.

### B4. Settings data asset

- `engine/include/navigation/NavigationSettingsData.h` registers data subtype
  `navigation.settings` (version 1) through `DataAssetTypeRegistry`:
  - `profiles`: a list of `{ tag, radius, height, max_slope_degrees, max_climb, cell_size, cell_height, tile_size }`;
  - `areas`: an ordered list of area tag names. Index 0 is implicitly
    `navigation.area.default`. At most 63 entries.
- The cook finds the project's single `navigation.settings` asset through
  the asset registry, using `PeekDataAssetSubtype`.
  - None: the navigation step emits an `Info` diagnostic and publishes no
    product.
  - More than one: an error.
- `templates/*/assets/data/navigation.sdata`: add one humanoid profile
  matching the template `CharacterController` (radius 0.3, height 1.8,
  step 0.35, slope 50).

### B5. Authored link component and identity

- `engine/include/navigation/NavigationIds.h`: `NavLinkId`, plus
  `NavLinkIdToString` / `NavLinkIdFromString` and a `SceneFieldCodec`, with
  the all-zero value accepted for repair (the `WorldPartitionIds.h` pattern).
- `engine/include/navigation/NavLinkComponent.h`: `SENCHA_COMPONENT("sencha.navigation.link") SENCHA_SCHEMA("Nav Link") SENCHA_SCENE_CHUNK("NLNK")`. Its persisted fields are:
  - `id`;
  - `exit_offset`, local to the entity (the entity origin is the entry anchor);
  - `directions`, as `DockDirection*` bits (they carry no world-partition meaning);
  - `traversal`, as `InlineString<64>`;
  - `base_cost`;
  - `entry_radius`.
- Registration:
  - add it to `SENCHA_ENGINE_COMPONENT_HEADERS`;
  - add a `NavigationComponents` set;
  - add a row to the freeze table.
- `BuildPassthroughScene` strips `Nav Link` next to `baked_brush`
  (`editor/kyusu/src/document/DocumentCookInput.cpp:257`). All runtime link
  data comes from `.snav`.
- Kyusu authoring of this component is ticket E. Tests build it directly in
  an `EditorDocument` registry.

### B6. Structured cook diagnostics

- `engine/include/assets/cook/CookDiagnostic.h` (cook-only) defines
  `CookDiagnostic { Severity (Info|Warning|Error); CookDiagnosticSource Kind (NavigationSettings|NavLink); uint64_t SourceId; std::string Rule; std::string Message; }`.
- `DocumentCookResult` gains `std::vector<CookDiagnostic> Diagnostics`.
  - An `Error` fails the cook.
  - `WorldCookResult` aggregates the diagnostics with the zone ID.
  - The toolbar status shows counts. The inspection UI is ticket E.
- Navigation rules:
  - `nav.settings.invalid`: a non-positive or non-finite profile value, a
    duplicate profile or area tag, or more than 63 areas;
  - `nav.link.id_invalid` and `nav.link.id_duplicate`;
  - `nav.link.anchor_nonfinite` and `nav.link.degenerate`;
  - `nav.link.direction_invalid`;
  - `nav.link.traversal_empty`;
  - `nav.link.entry_unprojected` and `nav.link.exit_unprojected`, reported
    per profile, naming the profile;
  - `nav.zone.too_large`: the tile count exceeds the `DT_POLYREF64` tile
    budget.
- Disconnected islands are never diagnosed.
- A tag name that is unbound at runtime is a runtime diagnostic
  (section 5.2). The cook cannot know game-module vocabulary.

### B7. Cook step and `.snav`

- **Step IDs.**
  - `CookStepIds::Navigation = "navigation"` and
    `CookOutputFamilies::Navigation = "navigation"` go in
    `editor/common/src/project/CookProfile.h`.
  - The Full and NoLighting built-in profiles target it; LightingOnly does
    not.
- **Graph.** `CookGraph.cpp` adds
  `{Navigation, deps {brush_cells}, family Navigation, v1, selectable}` and
  grows `kSteps` to 12.
- **Snapshot.** `DocumentCookSnapshot` gains `std::vector<NavLinkInput> NavLinks`,
  collected only when selected. Each entry holds the world-space entry and
  exit, which come from `EditorScene::ComposeWorldTransform`, not
  `LocalTransform`, so parented links are correct.
  - It also gains an optional parsed `NavigationSettings`.
- **Engine cook function.**
  `engine/include/assets/cook/NavigationCook.h` / `engine/src/assets/cook/NavigationCook.cpp`
  exposes `CookZoneNavigation(const NavigationCookInput&) -> NavigationCookResult`
  (the `NavigationFile` plus diagnostics; the Kyusu step encodes and stages it).
  - Input: the geometry from B1, settings, links, and area volumes. The
    production cook passes no area volumes; tests pass fixtures.
  - For each profile, it computes the tile range from `Bounds` and builds
    each tile with `BuildNavTile`, skipping empty tiles.
  - It projects each link anchor with a temporary Detour mesh, through a
    helper in `engine/src/navigation/`, so the cook file never includes
    Detour.
  - It then writes the file.
- **Kyusu glue.** `editor/kyusu/src/document/DocumentNavigationCook.{h,cpp}`
  follows `DocumentProbeBake.cpp`.
  - It stages `.cooked/<stem>/navigation.snav`.
  - It calls `catalog.AddNavigation`.
  - It adds the `kNavigationFile` constant to `CookArtifactPaths.h`.
  - It adds `AssetType::Navigation = 16` before `Count`, with both string
    conversions.
  - It handles the family in `DocumentPublicationPlan` and in the receipt
    and published families.
- **Fingerprint.** Navigation inputs (the settings bytes, the links, and the
  format and builder versions) fold into the document fingerprint, and the
  `document_cook` version is bumped. There is no per-step reuse in v1,
  following the collision precedent; it is added if nav cooks prove slow.
- **Format.** `engine/include/navigation/NavigationFile.h` defines magic
  `SNAV`, `kNavigationFormatVersion = 1`, and `BinaryHeader` + `ChunkReader`
  chunks, following the `ProbeVolumeFormat.h` conventions. "A version bump
  means recook, never migrate." The chunks are:
  - `PROF`: profile tags and parameters, in order;
  - `AREA`: area tag names, in order;
  - `GEOM`: zone static triangles plus, per tile, a sorted index list of the
    triangles overlapping that tile's border-expanded bounds (so runtime
    rebuild never rescans the zone);
  - `TILE`: per profile, tiles in (x, z) order, each a Detour tile blob;
  - `LINK`: `NavLinkId`, traversal name, directions, base cost, entry, exit
    and radius, sorted by `NavLinkId`;
  - `HASH`: an FNV-1a over all chunks.
- **Reader.** `ReadNavigationFile(std::span<const std::byte>, NavigationFile&, std::string* error)`
  lives in `engine/src/navigation/NavigationFile.cpp`. It is runtime code
  that validates only; it builds no Detour objects.

### B tests

These go in `test/level_cook/NavigationCookTests.cpp`, plus the engine-level
tests in `test/navigation/` described under section 5.
- B1 leaves `.scol` bytes unchanged, and cell-local and world forms agree.
- A floor room cooks walkable tiles, and the polygon bounds sit inside the
  floor bounds, inset by the radius.
- A wall splits the floor into regions with no shared portal across it.
- Clearance: a gap narrower than `2 * radius` has no polygon.
- Height, step, and slope: a ceiling below the profile height, a step above
  `max_climb`, and a ramp steeper than `max_slope` each drop traversability.
  Relaxing the profile restores it.
- An area volume marks polygons with its index.
- Determinism: two `ForceRebuild` cooks of the same document produce
  byte-identical `.snav`.
- Diagnostics: each rule fires on a minimal fixture and carries the
  `NavLinkId`.
- Round trip: `ReadNavigationFile` reads what the cook wrote, a hash
  mismatch is rejected, and a version mismatch is rejected.
- Isolation: `navigation_isolation` passes, and it fails when a probe
  include is planted during a manual check.

## 5. Ticket C: runtime queries and links

### 5.1 Public types

These live in `engine/include/navigation/NavigationTypes.h`. They are POD
where practical, and there are no backend types.
- `NavStatus`: `Success, Partial, NoPath, InvalidStart, InvalidDestination, ZoneUnavailable, StaleLocation, CrossZoneUnsupported, SearchLimitReached, OutputCapacityReached, ProfileUnavailable`.
- `NavLocation { ZoneId Zone; uint16 Profile; uint32 Generation; uint64 Ref; Vec3d Position; }`.
- `NavRegion`: same identity fields as `NavLocation`, with no position.
- `NavQueryPolicy`: area costs keyed by area `GameplayTagId`, a forbidden
  area set, and traversal-kind cost multipliers and addends. It compiles
  from `navigation.policy` data assets (`engine/include/navigation/NavigationPolicyData.h`),
  which hot-reload like movement profiles.
- `NavQueryRequest { ZoneId Zone; GameplayTagId Profile; const NavQueryPolicy* Policy; std::span<const GameplayTagId> Capabilities; std::span<const NavLinkId> AvoidLinks; std::span<const NavLinkCostOverride> LinkCostOverrides; }`.
  The spans are caller-owned transient constraints, so an agent's memory of
  failed links stays with the agent.
- `NavRouteBuffer`: fixed capacities chosen at construction, with no growth
  during a query. It holds:
  - steps: `Walk { first corner, corner count }` or
    `Traverse { NavLinkId, traversal GameplayTagId, entry, exit, radius, direction, cost }`;
  - corners;
  - a dependency set of `{tile ref, salt}` and `{NavLinkId, revision}`;
  - the total cost, walk distance, and zone generation.
- `NavReachableBuffer`: a fixed capacity of
  `{ NavRegion, float EntryCost, GameplayTagId Area }`.
- Add `navigation/` to the ABI fingerprint glob (`engine/CMakeLists.txt:~308`),
  and add `sizeof` / `offsetof` asserts for `NavLocation`, `NavRegion` and
  the route step types in `test/runtime/ModuleAbiLayoutTests.cpp`.

### 5.2 Zone resource and load path

- **`ZoneNavigation`** (`engine/include/navigation/ZoneNavigation.h`,
  PIMPL; the implementation is in `engine/src/navigation/`) holds:
  - a `dtNavMesh` per profile, tiled, with `DT_TILE_FREE_DATA`;
  - the link table;
  - per-profile projected link endpoints, and an index from entry poly to
    links, as a sorted vector for binary search;
  - the link overlay (enabled flag, cost scale, revision);
  - the static geometry for ticket D;
  - the generation.
- **Load path.**
  - `LoadedLevel` extends its zone stage: the task thread reads and validates
    `navigation.snav` next to the probe file (the `MakeProbeStage`
    precedent), producing a plain `NavigationFile`.
  - `AttachSceneContent` creates `ZoneNavigation` on the owner thread and
    registers it in `zone.Resources`.
  - A missing file means the zone simply has no navigation; queries return
    `ZoneUnavailable`.
- **Binding on attach.** Traversal, area, and profile tag names bind through
  `GameplayTagRegistry::FindTag`. A link whose traversal tag is unbound is
  disabled, and one runtime diagnostic is logged naming the `NavLinkId`.
  An unbound area or profile tag gets a diagnostic, and that profile or area
  is unusable.
- **Generation.** It comes from a monotonic counter owned by
  `NavigationSystem`.

### 5.3 System and access

- `engine/include/navigation/NavigationSystem.h` and
  `NavigationRegistration.h`:
  `RegisterNavigation(EngineSchedule&, RuntimeWorld&, JobSystem&, const GameplayTagRegistry&)`.
  It is opt-in, like `RegisterPhysics`. The system holds its own
  `RuntimeWorld&`, as residency subscribers do
  (`engine/include/app/GameContexts.h:58-66`).
- `ZoneResidency(ctx)` maintains
  `NavLinkId → (ZoneId, link index)` for resident zones. It adds entries on
  `Attached`, and removes them on `Detaching` while the resource is still
  alive.
- `PostFixed(ctx)` runs after every `FixedLogic` system, so the result never
  depends on the order of game systems. It rebuilds the overlay from all
  `NavLinkState` components through a cached query over `Logic` plus the
  persistent partition:
  - a link with no state component is enabled with scale 1;
  - a revision is bumped only when a link's effective value changes;
  - queries in tick T+1 see state written in tick T.
- `Queries()` returns a `NavigationQuery` view. Game code reaches it through
  `Schedule().Get<NavigationSystem>()`.

### 5.4 Query context and search

- **`NavQueryContext`** (PIMPL) is caller-owned and configured with
  `{ MaxNodes, MaxCorridor }`. It owns:
  - one `dtNavMeshQuery`, re-bound per call with
    `init(mesh, MaxNodes)`, which reuses its pools once sized;
  - a Sencha node pool;
  - an open-addressed ref→node table sized to a power of two;
  - a binary heap;
  - a corridor scratch buffer.

  There is no global lock. Distinct contexts run concurrently against the
  same immutable meshes.
- **Walk edges.** These iterate Detour polygon links through
  `getTileAndPolyByRefUnsafe`. The portal is computed from the link's edge
  vertices, clamped by `bmin`/`bmax` on tile-boundary links. Cost is
  `|mid(portal) - node position| * areaCost[poly area]`. Forbidden areas
  are skipped.
- **Link edges.** These come from the entry-poly index, for each allowed
  direction. An edge passes only if:
  - the overlay has the link enabled;
  - its traversal kind `IsDescendantOf` a capability tag, compiled once per
    query into a bitset over the zone's distinct kinds (at most 64);
  - its ID is not in `AvoidLinks`.

  Cost is `base * kindMultiplier * overlayScale + kindAddend`, unless the
  caller overrides it.
- **Ordering.** The heap orders by `(f, g, ref)`, so identical data, inputs,
  and overlay give identical results. This guarantee holds within one
  process and build (`docs/plans/networking.md:60-69`).
- **Exhaustion.** Running out of nodes returns `SearchLimitReached` with the
  best partial result. Running out of buffer returns
  `OutputCapacityReached`. Nothing grows.
- **Area exclusion inside Detour calls.** `raycast`, `findNearestPoly` and
  `findStraightPath` need area exclusion that matches Sencha policy. Use
  the default non-virtual `dtQueryFilter` with per-area costs, and map
  forbidden areas to an exclude flag. Polys carry flag `1 << areaGroup`.
  Enable `DT_VIRTUAL_QUERYFILTER` only if that mapping proves insufficient.

### 5.5 Operations

`engine/include/navigation/NavigationQuery.h` defines a non-owning view.
Every operation also has a form taking `const ZoneNavigation&`, so tests and
tooling need no `RuntimeWorld`.
- `ProjectPoint(ctx, req, point, extents)` returns a `NavLocation` or
  `InvalidStart`.
- `NavRaycast(ctx, req, from, target)` returns a walkability hit. The
  header documents that it is not line of sight, and points to
  `PhysicsQueries::Raycast` for that.
- `Reachable` and `TravelCost(ctx, req, a, b)` run A* and skip route
  assembly. Endpoints in different zones return `CrossZoneUnsupported`.
- `FindRoute(ctx, req, a, b, NavRouteBuffer&)` runs A*, splits the corridor
  at link edges, and calls `findStraightPath` per walk leg. The dependency
  set is recorded as it goes.
- `CollectReachable(ctx, req, start, radius, maxCost, NavReachableBuffer&)`
  is a Dijkstra returning regions with their region-entry cost. It makes no
  sampling decision.
- `ClosestPointInRegion(region, point)` and `RegionBounds(region)` are
  deterministic, policy-free position primitives.
- `ValidateRoute(route)` runs in O(dependencies). It reports `Valid` or the
  first invalidated dependency: the zone generation, a tile salt, or a link
  revision.
- Any operation may take an optional `NavQueryDiagnostics*` out-parameter
  recording:
  - zone, profile, and projected ends;
  - policy, capability, and avoid sizes;
  - status and cost;
  - nodes visited and whether the search was exhausted;
  - links traversed.

### 5.6 Debug

- `NavigationDebugPanel` (`IDebugPanel`) shows:
  - per zone: profiles, tile and polygon counts, links with their
    enabled/scale/revision, and unbound-tag diagnostics;
  - the last N query diagnostics, from a fixed ring buffer the caller opts
    into;
  - the rebuild counters from ticket D.
- It adds no drawing.

### 5.7 Replication decision

`NavLinkState` is **not** `SENCHA_REPLICATED` in v1:
- clients never compute authoritative routes;
- the component can take part in replication later through the normal
  machinery if client-side debug or prediction queries need it;
- save integration waits for the save system.

### C tests

These live in a new `test/navigation/` executable: a glob plus a
`gtest_discover_tests` block in `test/CMakeLists.txt`, linking only
`sencha_engine`. A shared `NavigationFixture.h` cooks fixture geometry
through `CookZoneNavigation` and builds a `ZoneNavigation` directly.
- **Queries:** project a valid point and an outside point; a clear raycast
  and a boundary hit; reachable and unreachable; travel cost equals the
  route cost; route around a wall (Scenario A).
- **Areas (Scenario B, revised):** two corridors, one marked with a fixture
  area volume. Policy data makes corridor A cheaper and the route takes A;
  changing only the policy makes it take B. No recook happens.
- **Links (Scenarios C and D):**
  - a jump link fails without the capability and succeeds with it, the route
    shape being `Walk, Traverse, Walk`;
  - a one-way drop;
  - disable and re-enable through `NavLinkState`, with no recook;
  - a cost bias between two links;
  - a personal `AvoidLinks` choice that leaves another context using the
    avoided link;
  - discontinuous endpoints tens of metres and several tiles apart.
- **EQS readiness (Scenario E):**
  1. `CollectReachable` around a target;
  2. `ClosestPointInRegion` for a caller-chosen sample;
  3. `TravelCost`;
  4. rank externally, using public headers only.
- **Lifetime (Scenario F):**
  - detach the zone and the next query returns `ZoneUnavailable`;
  - re-attach, and old locations return `StaleLocation`;
  - a partition slot reused by another zone never resolves an old location;
  - a dormant zone's resource stays queryable.
- **Limits:** a small `MaxNodes` returns `SearchLimitReached`, and a small
  route buffer returns `OutputCapacityReached`.
- **Allocation:** after warm-up, N queries of each kind produce zero
  allocations, counted through `dtAllocSetCustom` plus a Sencha-side
  counter hook in the context.
- **Determinism:** one fixture is queried 64 times serially, and the same
  queries run under `JobSystem(4).ParallelFor` with per-worker contexts.
  Results are identical.
- **Overlay:** state written in tick T takes effect in tick T+1 regardless
  of registration order, which is tested by registering a writer system
  before and after navigation.

## 6. Ticket D: runtime tile rebuild

**Scope.** Supported navigation geometry can be added, removed, and moved,
including geometry that creates or destroys walkable surfaces. v1 supports
primitive `Collider` shapes only. Mesh-backed contributors log a diagnostic
and are never silently ignored.

`Collider` is not authorable yet (section 2), so contributors come from game
code. Authored contributors arrive when colliders become authorable.

- **Contributor tag.** `NavigationGeometry` is a zero-size tag component
  (`engine/include/navigation/NavigationGeometry.h`). An entity contributes
  when it has the tag, a `Collider`, and a `WorldTransform`. The tag is
  opt-in, so ordinary moving bodies never churn tiles.
- **Change detection.** This runs in `NavigationSystem::PostFixed` before
  the overlay sync, through a cached query.
  - A contributor table maps `EntityId` to `{shape hash, world bounds, transform}`.
  - A contributor that is new, removed (not seen this tick, generation
    checked), changed in shape, or moved past the `nav.rebuild.move_threshold`
    cvar dirties the tiles its old and new bounds overlap.
  - This happens for every resident zone and profile whose tile grid
    contains those tiles.
  - The dirty set is a sorted, coalesced set of
    `(ZoneId, profile, tx, tz)`.
- **Rebuild.** The cvar `nav.rebuild.tiles_per_tick` (default 4) sets the
  budget. The first tiles in canonical order form one batch.
  1. **Serially**, gather each tile's input: static triangles from the
     cooked `GEOM` per-tile index list, plus contributor triangles
     (box, sphere, and capsule triangulation in world space) whose bounds
     overlap the border-expanded tile.
  2. **Build** in `JobSystem::ParallelFor` with one slot per tile. There is
     no shared mutation.
  3. **Publish serially** in canonical order: `removeTile`, then `addTile`,
     which bumps the salt.
  4. Re-project links with an endpoint in a published tile, and bump their
     revisions. Rebuild that profile's entry-poly link index.
  5. Tiles left over remain dirty for the next tick.
- **Determinism.** With `worker_count == 0`, `ParallelFor` runs serially in
  index order, and that is the reference. Because inputs and outputs are per
  slot and publication is ordered, parallel results are byte-identical.
  Navigation reflects a geometry change one tick later; this is documented.
- **Telemetry.** Counters track dirty, rebuilt, and deferred tiles, build
  time per tick, the largest tile build, and skipped mesh contributors. They
  appear in the ticket C panel.

### D tests

- A box contributor bridging a gap: before, there is no path; after one
  tick, a path exists; after the box is removed, there is no path again.
  This covers a walkable surface being created and destroyed.
- A wall contributor blocks a corridor, and the route reroutes.
- Rebuilding one tile leaves a route through other tiles `Valid`, while a
  route through the rebuilt tile reports that tile.
- A `NavLocation` in a rebuilt tile is `StaleLocation`, and one elsewhere
  stays valid.
- A link whose entry sits in a rebuilt tile is re-projected, its revision
  bumps, and routes that used it are invalid.
- Budget: dirtying 10 tiles with a budget of 4 publishes over three ticks in
  canonical order.
- Serial against parallel: `JobSystem(0)` and `JobSystem(4)` publish
  byte-identical tiles and produce identical subsequent routes.
- A mesh-handle contributor is reported and skipped.
- Moving within the threshold dirties nothing.

## 7. Later tickets

- **E. Kyusu tooling.**
  - Navigation overlay built from cooked `.snav`, loaded in the editor
    process.
  - Debug path query and point inspection.
  - `PointEditTarget`, the `NavLink` adapter and recipe, and a duplicate
    remap line in `RemapWorldConnectionDuplicateSnapshots`.
  - A cook diagnostics UI.
  - The relationship with `EditorLineBatch` (W5) is decided there.
- **F. `BrushRole::AreaTag`.** A production area-volume source feeding the
  existing `NavAreaVolume` input, plus an end-to-end area-painting test.
- **Cross-zone planner (roadmap item 6).** It consumes `WorldPartitionIndex`
  and refines each leg with `NavigationQuery`. Two items are recorded for
  it:
  - `WorldLink` has no spatial endpoints;
  - a cook warning for `WorldDock` endpoints that do not project onto the
    zone's navmesh belongs with that ticket, since world topology is only
    available in the world cook.

## 8. Documentation to amend (landed with the tickets)

- **`docs/plans/engine-roadmap.md:282-287`** (with B).
  - Replace "from the same cooked collision geometry `ZoneCollisionLoader`
    consumes" with "from the shared static-collision triangle source that
    also feeds the collision bake".
  - Replace the claim at `:147` and `:285` that the NavPath decision lives
    in CLAUDE.md.
  - Link this document from the relationships table.
- **`docs/assets/pipeline.md:601`** (with B): the navmesh is baked per zone
  as `navigation.snav`, located by the cooked-scene path convention, like
  probes. Nothing is added to the manifest.
- **`docs/gameplay/movement.md:48,71,248-256`** (with C): jump is
  `MovementIntent.Jump`, not an ability. The navigation bridge sets it for a
  jump traversal.
- **New `docs/navigation/runtime.md`** (with C and D): the current
  architecture, written as a maintainer's reference, replacing this plan's
  status for landed parts.

## 9. Verification per ticket

- **Focused runs while iterating:**
  - `ctest --test-dir build -R 'Navigation|navigation_isolation|BrushCollisionCook' --output-on-failure`;
  - `build/test/navigation_tests --gtest_filter=...`.
- **Before handoff:**
  - `cmake --preset dev`;
  - `cmake --build --preset dev --parallel`;
  - `ctest --preset dev`, run serially;
  - `git diff --check`.
- **ABI:** ticket C also runs `ModuleAbiLayoutTests` and the component
  identity freeze.
- **Concurrency:** tickets C and D run their serial-against-parallel tests,
  plus the `tsan` preset where supported.
- **Performance:** ticket D records rebuild timings in the profile preset
  for a room-scale fixture:
  - tile build ms at the default tile size;
  - tiles per tick at the default budget.

  If a single tile build exceeds about 1 ms, retune the default tile size
  before landing.
