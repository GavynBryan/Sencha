# Navigation runtime

Status: **current architecture** (2026-09). The navigation module lives in
`engine/include/navigation/` and `engine/src/navigation/`; the cook side is in
`engine/src/assets/cook/Navigation*.cpp` and the Kyusu glue in
`editor/kyusu/src/document/DocumentNavigationCook.cpp`. Tests are in
`test/navigation/`, `test/level_cook/NavigationDocumentCookTests.cpp`, and
`test/core/NavigationAllocationTests.cpp`. The plan that produced it, and the
work still ahead, is `docs/plans/navigation-core.md`.

Navigation answers where an agent can travel, what the trip costs, and which
special traversals a route needs. It does not decide why an agent wants to go
somewhere, and it does not perform traversals: a route names each link crossing
and its traversal kind, and gameplay performs it through movement and abilities.

## Backend and firewall

Recast builds tiles and Detour stores and queries them. Both are compiled from
pinned sources into `sencha_navigation_thirdparty`, linked PRIVATE, and confined
to `engine/src/navigation/` by `cmake/CheckNavigationIsolation.cmake` (ctest
`navigation_isolation`). Public headers expose only Sencha types; cook code
reaches the builder through `NavTileBuild.h`.

Sencha owns the path search. Detour supplies polygon adjacency, projection,
nav-raycast, and corner extraction; the A* and Dijkstra over that graph, and the
navigation-link edges, are Sencha's (`NavSearch.cpp`). Detour's own
off-mesh connections are not used: they only reach the same or an adjacent tile,
and they would have to be re-baked into every rebuilt tile.

## Data path

1. **Source geometry.** `CollectStaticCollisionGeometry` (the one static-collision
   triangle source) gives the zone's brush-cell triangles in world space. The
   collision bake reads the same triangles per cell.
2. **Settings.** The project's single `navigation.settings` data asset lists build
   profiles (agent radius, height, slope, climb, voxel size, tile size) and the
   authored area names. Only the cook reads it. `NavigationSettingsSchema()`
   checks its shape; `IsValidNavBuildProfile` owns the build-profile ranges.
3. **Cook.** `CookZoneNavigation` builds every profile's tiles on a
   world-anchored grid, checks that the zone's `NavLink` anchors reach walkable
   space, and returns the `NavigationFile` with `CookDiagnostic` records naming
   the link or settings at fault. The document cook's `navigation` step encodes
   it to `<scene stem>/navigation.snav`; any error fails the cook. Identical
   input gives identical bytes.
4. **Load.** `LoadedLevel` reads and decodes the file on the task thread beside the
   scene parse and calls `AttachZoneNavigation` while the zone is still hidden.
   That builds a `ZoneNavigation` -- a Detour mesh per profile, the link table, and
   link state -- and registers it as a zone resource, so it dies with the zone.
   Names bind to gameplay tags there; an unregistered traversal, area, or profile
   name is a diagnostic and leaves the named thing unusable.

The `.snav` file is self-describing (`NavigationFile.h`): profiles with their
parameters, area names in index order, the zone's static triangles with each
tile's triangle list, each profile's tiles, and the links. The runtime needs
nothing else.

## Module layout

Public headers hold the vocabulary (`NavigationTypes.h`), the file format
(`NavigationFile.h`), the zone resource (`ZoneNavigation.h`), queries
(`NavigationQuery.h`), and the system. Everything touching Detour or Recast is a
private header in `engine/src/navigation/`:

| Part | Owns |
|---|---|
| `NavTileBuilder` | one tile from triangles: voxelize, mark areas, build polygons, encode |
| `NavTileMesh` | a profile's Detour mesh and the anchor-attachment tolerance rule |
| `ZoneNavigation` | a zone's meshes, link table, link endpoints and edges per profile |
| `NavQueryPlan` | one request compiled against one zone: area costs, usable traversal kinds |
| `NavSearch` | the A* and Dijkstra over polygons and link edges |
| `NavigationQuery.cpp` | endpoint checks, route assembly into walk legs and traversals |
| `NavGeometryTracker` | which tagged colliders were added, moved, reshaped, or removed |
| `NavTileRebuilder` | the dirty-tile set and budgeted, ordered rebuild and publication |
| `NavLinkStateSync` | folding `NavLinkState` components into each zone's link state |
| `NavigationSystem` | the composition of the last three, bound to the schedule and cvars |

The `.snav` codec (`NavigationFile.cpp`) writes records through the
`core/serialization/Serialize.h` vocabulary inside `BinaryFormat.h` chunks.

## Identity and staleness

- `NavLinkId` is the only stable identity: authored, minted by the editor, cooked,
  and used by routes, `NavLinkState`, and caller-held avoidance lists.
- `NavLocation` and `NavRegion` are runtime references `{zone, generation,
  profile, polygon}`. They are never serialized.
- A zone's navigation generation changes only when it is loaded, so a reference
  made before an unload and reload is `StaleLocation`. References never key on
  `StoragePartitionId`, so a reused partition slot cannot alias another zone.
- A rebuilt tile gets a new Detour tile salt. References into that tile go stale;
  references into every other tile stay valid.
- A route records the tiles it passes over (with their revisions) and the links it
  crosses (with theirs). `NavValidateRoute` reports the first dependency that
  changed; nothing else invalidates it.

## Queries

`NavigationQuery.h` holds the operations. Each has a form taking a
`ZoneNavigation` and a form on `NavigationQuery`, which finds the zone by
`ZoneId` among resident zones (dormant ones included) and returns
`ZoneUnavailable` otherwise. Game code gets one from
`Schedule().Get<NavigationSystem>()->Queries()`.

| Operation | Answers |
|---|---|
| `ProjectPoint` | nearest walkable point within extents |
| `NavRaycast` | whether walkable surface continues in a line -- not line of sight |
| `Reachable`, `TravelCost` | whether a route exists, and its cost, without assembling it |
| `FindRoute` | walk legs (corners) and `Traverse` steps, one per link crossing |
| `CollectReachable` | regions reachable within a cost budget, with entry costs, cheapest first |
| `ClosestPointInRegion`, `RegionBounds` | positions inside a region; the caller decides sampling |
| `ValidateRoute` | whether a stored route still holds |

Every query takes a caller-owned `NavQueryContext` (node pool, open list, corridor
scratch, sized once) and never allocates after warm-up. There is no lock: give
each worker its own context and query the same zone concurrently. A search that
runs out of nodes returns `SearchLimitReached`; a full output buffer returns
`OutputCapacityReached`. Endpoints in different zones return
`CrossZoneUnsupported`; cross-zone planning is a separate layer above this one.

Costs are measured between the midpoints of the polygon edges a search crosses,
weighted by area cost: an estimate to rank by, not seconds and not the exact
shortest distance. Ties break on polygon reference, so results are identical for
identical navigation, link state, policy, and inputs, on any thread.

### Requests and policy

A `NavQueryRequest` names the zone and the build profile (a gameplay tag) and
carries the agent's policy and transient constraints:

- `NavQueryPolicy`: per-area costs, forbidden areas, and per-traversal-kind cost
  multipliers and addends. Authored as `navigation.policy` data assets, whose
  schema (`NavigationPolicySchema()`) is registered for the Data Editor, and bound
  to tags with `BindNavigationPolicy`. Changing a policy reroutes without any
  rebuild:

  ```json
  { "type": "navigation.policy", "version": 1,
    "data": {
      "area_costs": [ { "area": "navigation.area.water", "cost": 1.4 } ],
      "forbidden_areas": [ "navigation.area.hazard" ],
      "traversal_costs": [ { "kind": "navigation.traversal.jump",
                             "multiplier": 2.0, "add": 1.0 } ] } }
  ```
- `Capabilities`: the traversal kinds the agent can perform. A link is usable when
  its kind is one of them or below one in the tag hierarchy.
- `AvoidLinks` and `LinkCostOverrides`: the agent's own memory -- a jump that just
  failed, a lift it distrusts. Navigation reads them for the query and keeps
  nothing.

A heuristic search is used only when it is admissible: a usable link cheaper than
walking its own span (a teleport) makes the search fall back to Dijkstra.

## Link state

`NavLinkState { Link, Enabled, CostScale }` is ordinary gameplay data on whatever
entity decides it. `NavigationSystem` applies every such component to its link
once per fixed tick, in `PostFixed`, so state written during tick T is seen by
every query in tick T+1 regardless of system order. Several entities naming one
link combine order-independently: enabled only if all enable it, the largest
scale wins. A link no entity names is enabled at its authored cost. The
component is not replicated (clients do not compute authoritative routes), and
save integration waits for the save system.

## Runtime geometry

An entity tagged `NavigationGeometry` with a primitive `Collider` and a
`WorldTransform` changes walkable space: it can create a walkable surface (a
bridge deck) or remove one (a crate in a corridor). `NavigationSystem`, in
`PostFixed` before link state:

1. compares the tagged colliders with the last tick's, and dirties the tiles in
   every resident zone that an added, removed, reshaped, or moved collider
   touches (movement below `nav.rebuild.move_threshold` accumulates rather than
   being forgotten);
2. rebuilds up to `nav.rebuild.tiles_per_tick` dirty tiles from the tile's cooked
   triangles plus the contributors overlapping it, in parallel with one output
   slot per tile;
3. publishes them one at a time in (zone, profile, tile) order and re-projects
   link endpoints in the profiles that changed, bumping the revisions of links
   whose endpoints moved.

The serial path (no worker threads) is the reference and the parallel path
produces the same tiles. Tiles over the budget wait for later ticks in the same
order. Navigation therefore reflects a geometry change one tick after it happens.
A zone that streams in after a contributor exists has the contributor applied to
its cooked tiles on the next tick.

Collider meshes (a `Collider` referencing a cooked shape) do not contribute yet --
there is no backend-neutral copy of their triangles at runtime -- and are counted
in `LastRebuildStats().SkippedMeshContributors` instead of being ignored.
`Collider` has no authored form today, so contributors come from game code.

## Diagnostics

- Cook: `DocumentCookResult::Diagnostics` (rule, severity, and the `NavLinkId` or
  settings at fault).
- Load: `ZoneNavigation::Diagnostics()`, logged by `LoadedLevel`.
- Query: an optional `NavQueryDiagnostics` out-parameter reporting status, cost,
  nodes visited, exhaustion, constraint counts, and links traversed.
- Runtime: `NavigationSystem::LastLinkStateStats()` and `LastRebuildStats()`, and
  the "Navigation" debug-overlay panel listing each zone's profiles, tiles, links,
  and binding problems. There is no in-world drawing yet.
