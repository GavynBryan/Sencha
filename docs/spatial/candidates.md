# Candidate Evaluation

Candidate evaluation answers one question: of the places or entities a caller
could choose, which qualify and how do they rank? It is a pure read over world
state. The caller gives it a compiled definition and a context. It returns a
ranked set, a status, and optionally a trace.

It is a spatial query, not an AI system. Bots are the obvious caller, but a
camera choosing a vantage point, a spawner choosing a free spot, or a physics
effect choosing where to land ask the same kind of question.

Code: `engine/include/spatial/candidates/`, `engine/src/spatial/candidates/`.
Sight test: `engine/include/spatial/sight/`. Tests: `test/spatial/`, plus
`test/core/CandidateAllocationTests.cpp`.

## What it owns and what it does not

It owns:

- generating candidates;
- sampling navigation into points (navigation supplies regions and costs, never sampling);
- running measures in cost order;
- rejecting and scoring;
- deterministic ranking and selection;
- status and trace.

It does not:

- choose goals or decide which question to ask;
- move anything or build routes;
- keep a spatial index, physics representation or navigation data;
- persist anything between runs;
- replicate.

The caller owns the loop: ask, read the ranked set, act through systems this
never touches.

## Vocabulary

| Term | Meaning | Type |
|---|---|---|
| Description | A definition compiled with no World: names resolved, arguments prepared, criteria ordered | `CandidateEvaluationDesc` |
| Evaluation | A description bound into one World (tag ids, authored queries) | `CandidateEvaluation` |
| Context | What one run is asked about: querier, slot bindings, navigation request, seed | `CandidateContext` |
| Slot | A named input. Slot 0 is always the querier, named `querier` | index from `FindSlot` |
| Generator | Registered operation that appends candidates | `candidates.generator.*` |
| Measure | Registered operation that computes one value and status per candidate | `candidates.measure.*` |
| Criterion | A measure plus a mode: Require (gate) or Score (curve and weight) | `CandidateCriterion` |
| Scratch | Caller-owned reusable storage | `CandidateScratch` |
| Trace | Caller-owned record of one run | `CandidateTrace` |

## Operations and catalogs

Generators and measures live in two `AuthoredCatalog` instantiations,
`CandidateMeasureCatalog` and `CandidateGeneratorCatalog`, held together by
`CandidateCatalogs`.

- **Ownership.** The host owns them, not a World. `RuntimeAssets::CandidateOperations` holds them, so every host (runtime, Kyusu, Data Editor) validates `candidates.evaluation` data the same way, in the load stage. They are filled with the engine's operations at construction and read-only afterwards.
- **Not the authored vocabulary.** The authored vocabulary is the contract between game code and content. These catalogs are internal operations of one algorithm and are not part of it.
- **What an entry carries.** Name, a `DataFieldSchema` for its arguments, cost class, applies-to, value kind, and three function pointers:
  - `Prepare`: argument JSON to a state, with no World.
  - `Bind`: optional; resolves a prepared state against a World on the owner thread.
  - `Measure` or `Generate`.
- **Contract revisions** compare schema, cost class, applies-to and value kind, never the function pointer.
- **Game-defined questions** go through the `authored_query` measure, over the existing `AuthoredQueryDispatcher`. Native game-module operations are deferred (`docs/deferred.md`).

### Generators

| Generator | Emits | Order |
|---|---|---|
| `points` | A slot's points; points carrying an entity become entity candidates | binding order |
| `entities` | Entities with `WorldTransform` and `GameplayTagContainer` matching a tag query within a radius of a slot | ascending `EntityId` |
| `ring` | Concentric rings around a slot | ring, then angle |
| `grid` | Square grid within a radius of a slot | row-major from −x −z |
| `reachable` | Points on the querier's navigation reachable within a cost budget, `grid` or `per_region` | grid order, or cheapest region first |

About generation:

- **Projection.** `ring` and `grid` project onto the querier's navigation unless told not to. A point that fails is dropped (counted in the trace) or kept unprojected.
- **Truncation.** `entities` keeps the lowest ids when more match than fit, so truncation depends on identity, never on chunk layout.
- **Partitions.** By default `entities` iterates the caller's partitions, or the persistent partition plus the querier's zone. `all_resident` widens that to every resident zone.

### Measures

| Measure | Value | Class |
|---|---|---|
| `distance` | metres to a slot, min or max over its points | Geometric |
| `height` | candidate height minus slot height | Geometric |
| `facing` | horizontal degrees between a slot point's forward and the candidate, 0 in front | Geometric |
| `entity_tags` | 1 when the candidate's tags match a tag query | Entity |
| `authored_query` | a game query's Bool, Int or Float answer about the candidate entity | Entity |
| `reachable` | 1 when reachable within `max_cost` (`reachable_set`, or `exact` per candidate) | NavigationBatch / NavigationExact |
| `travel_cost` | navigation cost from the querier (`region_entry` or `exact`) | NavigationBatch / NavigationExact |
| `visibility` | sight between the candidate and a slot, observer on either side; `any`, `all` or `fraction` | Physics |
| `route_visibility` | whether a slot's observers see the querier's route to the candidate | NavigationExact |

Measure details:

- **`region_entry` cost** is the cost of entering the candidate's region. It is low by up to one region's width, so it is for ranking, not measuring.
- **Estimates.** `reachable` and `travel_cost` take `outside_zone`: `reject` (default) or `estimate`, which uses straight-line distance and reports `Estimated`.
- **`route_visibility` sampling.** It samples the route's start, every corner, and points along each leg at `spacing`.
- **Trace detail codes:**
  - `visibility`: the first failing observer's `SightOutcome`, with the blocking hit point and entity.
  - `route_visibility`: the index of the first sample an observer saw.
  - `reachable` and `travel_cost`: the `NavStatus` of their search.

## Authored form

A `candidates.evaluation` `.sdata` asset. Operation arguments are checked
against each operation's own schema when the asset loads.

```json
{
  "name": "firing_position",
  "slots": [{ "name": "target" }, { "name": "allies", "required": false }],
  "generators": [{ "generator": "candidates.generator.ring",
                   "arguments": { "center": "target", "points_per_ring": 16, "outer_radius": 10 } }],
  "criteria": [
    { "measure": "candidates.measure.reachable", "arguments": { "max_cost": 100 }, "require": { "expect": true } },
    { "measure": "candidates.measure.visibility", "arguments": { "slot": "target", "observer": "candidate" },
      "require": { "expect": true } },
    { "measure": "candidates.measure.distance", "arguments": { "slot": "target" },
      "score": { "curve": { "shape": "band", "low": 6, "preferred_low": 8, "preferred_high": 12, "high": 14 },
                 "weight": 2 } }
  ],
  "selection": { "mode": "top_n", "count": 3 },
  "limits": { "candidates": 64, "exact_nav_searches": 16, "raycasts": 128, "reachable_regions": 512 }
}
```

- Each criterion has exactly one of `require` (`min`/`max`, or `expect` for 0/1 values) and `score`.
- A Scalar measure needs a scoring curve; UnitInterval measures score their value directly unless a curve is given.
- `"unmeasured": { "score": s }` keeps candidates whose value could not be measured. They score `s`, or pass a Require. Without it they are rejected.

Loading has two stages, the same shape as verb bindings:

1. **Load stage, off-thread.** `CompileCandidateEvaluationDesc` runs against the catalogs and needs no World.
2. **Owner thread.** `BindCandidateEvaluation` resolves tags and authored queries.

`CandidateEvaluationBinding` holds an asset handle and the bound form. Its
`Refresh` rebinds when the asset's reload version, a catalog's generation, or
the tag vocabulary moves (`BindingDependencyStamp`). Reload replaces the
description in its cache slot. That is safe because reload commits on the
owner thread between frames, and runs are synchronous on the same thread.
Callers hold the binding, never a pointer into the cache.

## Running

`Engine::TryCandidates()` returns the evaluator. It is built after
`OnRegisterSystems`, with this host's physics if the game registered it.
`Evaluate(evaluation, context, scratch, results, trace)` runs in five steps:

1. **Check.** The definition is current, it fits the scratch, required slots are bound, and the querier's zone is resident.
2. **Generate** in definition order, up to the candidate budget.
3. **Criteria**, in order of cost class, then Require before Score, then authored order. A criterion sees only survivors. Operations that need a missing service (physics, navigation, dispatcher) report `NotApplicable`.
4. **Score.** A weighted mean of curve outputs, summed in authored order. Ties go to generation order. Comparison is exact, with no epsilon.
5. **Select.** Best, TopN, AllQualified, or PickFromBand. PickFromBand draws the smallest `HashBytes64(order, seed)` among candidates scoring at least best − width.

Budgets are counts (candidates, exact searches, rays, reachable regions),
never time, so identical inputs stop at the same place. After warm-up a run
allocates nothing. For `authored_query` that guarantee covers the engine's side
of the call with non-string constants; the game's implementation is its own.

### Freshness

A run in FixedLogic sees one snapshot: the end of the previous tick. That
covers transforms (`WorldTransform` is propagated after Physics), physics
bodies, and navigation as published in the last PostFixed.

The context comes from the same snapshot, and the evaluator enforces that
rather than trusting the caller:

- When the querier or a slot point carries an entity with a `WorldTransform`, its position and forward are read from there. `Origin` and `Forward` apply only without one.
- Entity-less slot points (a remembered position, a hazard centre) are the caller's. Take them from the same tick-old state, or distances are off by a tick of motion.

### Zones and navigation

Navigation answers only within the querier's zone (`Navigation.Zone`). Three
cases differ:

- **The querier's zone is not resident.** The run fails with `QuerierZoneUnavailable`.
- **The zone is resident but has no navigation data, or no zone was named.** The run sets `NavigationUnavailable`. Navigation measures report `NotApplicable`, `reachable` generates nothing, `ring` and `grid` keep their points unprojected, and everything else runs.
- **An entity candidate is in another zone.** Its zone comes from its storage partition. It is `OutsideZone`, unless the criterion opted into the estimate.

A point candidate that does not project onto the querier's navigation reports
`OffNavigation`. In a trace, a ring or grid point that landed in the next room
shows as `OffNavigation`. That is the per-zone rule, not a hole in the navmesh.

### Threads

Runs are serial and owner-thread only. ECS query iteration is not safe
concurrently (`World::QueryDepth` is a plain counter, and a `Query` refreshes
its archetype list lazily). The authored-query dispatcher tracks nesting
depth. `ParallelFor` allocates per call. Parallel evaluation is deferred until
a measured encounter exceeds the ~1 ms gate (`docs/deferred.md`).

## Sight

`TestSight(PhysicsQueries, SightObserver, target, targetEntity, filter)`
applies range, then the view cone, then one ray, so rejected targets cost no
ray. A target counts as seen when the ray reaches it or first hits its own
entity.

- **Triggers.** It uses the filtered `PhysicsQueries::Raycast`, which skips triggers by default and ignores the observer's own entity.
- **Characters.** Characters have no query bodies (`CharacterVirtual` without an inner body), so they never block sight.

It lives beside candidate evaluation, not inside it, because perception and
other callers need the same geometry.

## Determinism and authority

- **Pure function.** A run's output is a pure function of its inputs on one build: the bound definition, the context and seed, the limits, and world state as of the snapshot.
- **No hidden order.** Nothing keys on addresses or unordered iteration. Entity candidates sort by id. The reachable set is a sorted array.
- **No replication.** Results are never replicated. A run belongs wherever its caller has authority. A client may evaluate for presentation, but must not feed the result into authoritative state.

## Status and trace

- **Run statuses:** `Success`, `NoCandidates`, `NoneQualified`, `ContextMissing`, `DefinitionStale`, `ScratchTooSmall`, `QuerierZoneUnavailable`.
- **Flags:** `GenerationTruncated`, `OutputTruncated`, `BudgetExhausted`, `QuerierOffNavigation`, `UsedEstimates`, `NavigationUnavailable`.

A `CandidateTrace` at Summary level holds the header and per-criterion counts:
statuses, rejections, searches and rays. At Full level it adds every candidate
in generation order, with its outcome, each criterion's raw value, status,
curve output and weighted contribution, and a measure's detail record. It is
plain owned data. Filling it never changes a result.

## Response curves

`math/ResponseCurve.h` (Linear, Step, Band over authored bounds) is shared
with the future utility and consideration tables named in the roadmap.
Normalization is always against authored bounds, never the current candidate
set, so adding a candidate never reshuffles the others.
