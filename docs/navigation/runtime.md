# Navigation runtime

Status: **current architecture** (2026-10). The navigation module lives in
`engine/include/navigation/` and `engine/src/navigation/`; the cook side is in
`engine/src/assets/cook/Navigation*.cpp` and the Kyusu glue in
`editor/level_editor/src/document/DocumentNavigationCook.cpp`. Tests are in
`test/navigation/`, `test/level_cook/NavigationDocumentCookTests.cpp`, and
`test/core/NavigationAllocationTests.cpp`. The plan that produced it, and the
work still ahead, is `docs/plans/navigation-core.md`; navigation agents (the
"Agents" section) came from `docs/plans/navigation-agent.md`.

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

## Tooling access

`navigation/tooling/NavMeshSnapshot.h` hands a tool one profile's walkable
surface as plain data: polygon outlines with their `NavRegion` and area tag,
portals (the stretch of edge two polygons share, clamped at tile seams as the
search clamps it), and borders (edges where walkable surface ends). Polygons
come in tile-coordinate order, so two snapshots of one mesh are identical.

It is for tools that draw or measure the mesh, such as Kyusu's level analysis
and the planned navigation overlay. It allocates in proportion to the mesh,
holds no state, and adds nothing to `.snav`. It is compiled only with
`SENCHA_ENABLE_COOK`, and it sits outside the module ABI fingerprint, which
covers only the top level of `navigation/`. Questions about travel (reachable,
cost, route) stay with the queries: a snapshot's regions lead back into them,
and no tool searches the snapshot's graph itself.

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

## Agents

A navigation agent is an entity that can be sent somewhere and walks there by
itself. `NavAgent` (authored: a build profile name and an optional
`navigation.policy` asset) brings two derived components: `NavAgentRequest`,
its current order, and `NavAgentProgress`, what has become of it.
`NavAgentSystem` does the rest. A game opts in with `RegisterNavigationAgents`
after `RegisterMovementSystems`; an engine that registers it with no agents
does no work.

### Orders

`RequestNavigation` (a point, or an entity to approach or follow) and
`CancelNavigation` are the only writers of `NavAgentRequest`. Each order gets a
fresh per-agent `NavRequestId`, and `NavAgentProgress::Request` names the order
its other fields describe. `ReadNavigation(world, agent, id)` answers for one
order: `Superseded` once another replaced it, so an old order's outcome is never
read as a new one's. Content does the same through the `navigation.move_to`,
`navigation.approach` and `navigation.stop` verbs and the `navigation.phase`,
`navigation.failure` and `navigation.remaining` queries.

`Phase` is `Planning`, `Moving`, `Traversing` or `Holding` (following an entity
it has reached) while the order is live, and `Arrived`, `Failed` or `Cancelled`
once it is over; a terminal phase stays until the next order. `Failure` says why
an order failed, and `LastQuery` keeps the last search's own status apart from
it: a crossing that fails with no way round is `TraversalFailed` with
`LastQuery = NoPath`.

### Who writes `MovementIntent`

An agent's intent producer is chosen by its state. An entity tagged
`movement.controlled` is steered by input, and an order on it fails with
`TakenOverByInput`. Otherwise an agent with a live order is steered by
`NavAgentSystem`; when the order ends, the follower writes zero intent once and
stops, because nothing resets intent each tick. Traversal executors write only
the intent the follower lends them.

### One tick

`NavAgentSystem` runs in FixedLogic, on the authority only, ordered as a
movement producer (`OrderMovementAfterInput`) and after the game's decision
systems (`OrderNavigationAfter<T>`). It reads last tick's transforms and facts
and the navigation PostFixed published, in three passes:

1. **Sweep.** Take up new orders; fail orders whose agent is now controlled,
   zone-less, or whose target is gone; validate each standing route with
   `NavValidateRoute`.
2. **Plan.** Take up to `nav.agent.plans_per_tick` (16) entries from the
   planning queue, gather each into a self-contained `NavPlanInput` on the
   owner thread, plan them as one `JobSystem::ParallelFor` (each worker its own
   `NavQueryContext`, each plan its own staging route), and commit in queue
   order by swapping the staging route into the agent's route slot. Below
   `nav.agent.parallel_min_batch` plans, or with no workers, the same
   `PlanRoute` runs inline; that is the serial reference.
3. **Follow.** Advance each agent along its route and write its intent; cross
   links through executors; watch for deviation and for being stuck.

Nothing changes a `ZoneNavigation` during FixedLogic (tiles and link state
change in PostFixed, zones attach in the async commit and go in residency), so
the planning batch reads navigation that cannot move under it. A job reads only
its input and writes only its output, its staging route and its worker's
context.

### The planning queue

`NavPlanQueue` holds at most one entry per agent, ordered by an order tick and
then `EntityId`, so its order never depends on chunk layout. A **blocking**
entry (a new order, an invalidated route, deviation, being stuck, a failed
crossing: the agent stands still meanwhile) is ordered at the tick it is queued.
A **refresh** (a moving target, with the route still valid and still walked) is
ordered `nav.agent.refresh_defer_ticks` later, so it yields to blocking work
queued after it but is never starved. A new order replaces the agent's entry at
the back; a further trigger for the same order keeps its place.

How many plans run each tick never depends on the worker count, so serial and
parallel runs plan the same agents on the same ticks and produce the same
outcome (`test/navigation/NavAgentDeterminismTests.cpp`). A host with no
workers pays the serial cost of the same budget.

### Following

The path-following rules are pure functions in `NavRouteFollowing.h`:

- **Corners.** An agent passes a corner within `nav.agent.corner_radius`, or
  once it has crossed the plane that bisects the turn there while staying
  within `nav.agent.deviation` of the next segment. Farther than that from the
  segment it is following, it is **deviated**: it stops and replans from where
  it is, which covers knockbacks and teleports.
- **Arrival.** A point is reached at the route's end (the point projected onto
  the mesh); an entity by standing near it. The last leg brakes within the
  distance free-locomotion friction needs to stop the agent.
- **Stuck.** Progress is measured over windows of the agent's own time
  (`ActorTime`), counting only ticks its motion was its own
  (`MotionRequest::PlanarOverridden` false): being carried or held by an ability
  is never being stuck. A window short of `nav.agent.stuck_progress` of what the
  steering asked for replans; `nav.agent.stuck_repaths` of those in a row fail
  the order.
- **Invalid routes are never walked.** A route whose tiles or links changed is
  dropped on the tick the change is visible; only an agent mid-crossing finishes
  its crossing first, unless the crossed link itself changed.

### Crossing links

A route's `Traverse` step is carried out by the executor registered for the
link's traversal kind, or its nearest registered ancestor, in the World's
`NavTraversalExecutors` (`RegisterNavTraversal`). An executor is a pair of
function pointers: `Step`, called each tick until it reports `Succeeded` or
`Failed`, and `Abort`, called when the order is replaced or ended mid-crossing.
The follower enforces `nav.agent.traversal_timeout`.

An agent may plan through a link only when one of its own gameplay tags is the
link's kind or an ancestor of it, and an executor resolves for that kind; a
crossing nobody can carry out is never planned, so no agent walks off a ledge
waiting for one. A failed crossing puts its link on the agent's avoid-list for
`nav.agent.avoid_seconds` and replans. The engine ships `NavTraverseByWalking`
(drops and gaps a step crosses) and `NavTraverseByJumping`, and registers
neither.

### Route storage

Routes live in the system's route slots, not in components: a slot is taken on
an order and released when it ends, and `NavAgentProgress::RouteSlot` is honoured
only while the slot's owner is that entity. When the world's structure changes,
slots whose owner died or lost `NavAgent` are freed, zeroing a live owner's
intent. Steady state allocates nothing in the follower; with workers, the one
allocation a tick is `JobSystem`'s per-batch control block
(`test/core/NavigationAllocationTests.cpp`).

### Inspecting agents

`nav.agent.debug.draw` draws every agent's remaining route and destination, and
the selected agent's over the scene with its wish direction. The "Navigation
agents" debug panel lists agents with their phase and failure; selecting one
shows its order, route cursor, crossing, last search and stuck window
(`NavAgentSystem::Inspect`). `nav.agent.list`, `nav.agent.goto <agent> <x> <y>
<z>` (or `me`), `nav.agent.follow` and `nav.agent.stop` drive agents by hand.

Measured costs, and why planning is batched rather than run in the background,
are in `docs/plans/navigation-agent.md` and
`docs/plans/evidence/navigation-route-cost/`.

## Diagnostics

- Cook: `DocumentCookResult::Diagnostics` (rule, severity, and the `NavLinkId` or
  settings at fault).
- Load: `ZoneNavigation::Diagnostics()`, logged by `LoadedLevel`.
- Query: an optional `NavQueryDiagnostics` out-parameter reporting status, cost,
  nodes visited, exhaustion, constraint counts, and links traversed.
- Runtime: `NavigationSystem::LastLinkStateStats()` and `LastRebuildStats()`, and
  the "Navigation" debug-overlay panel listing each zone's profiles, tiles, links,
  and binding problems. There is no in-world drawing yet.
