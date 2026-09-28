# Deferred Work

Work deliberately left for later. Each entry says what was deferred, where the
code lives, and the trigger that makes it due. When a trigger lands, do the
work and delete the entry.

## Spatial queries

- **Parallel candidate evaluation.**
  - *Code:* `engine/src/spatial/candidates/`.
  - *What's missing:* runs are serial and owner-thread only. Running them in parallel needs a concurrent-read path through ECS queries (`World::QueryDepth`, `Query`'s lazy archetype cache), per-worker scratch, and a non-allocating dispatch.
  - *Trigger:* a measured per-tick evaluation cost over ~1 ms in a real encounter.
- **Native candidate operations from game modules.**
  - *Code:* `engine/include/spatial/candidates/CandidateCatalogs.h`.
  - *What's missing:* the catalogs hold only the engine's operations. Game questions go through the `authored_query` measure.
  - *Trigger:* a game needs a native batch measure that `authored_query` is too slow for. The catalogs' names and contract revisions make this an addition, not a redesign.
- **Top-N bound pruning.**
  - *Code:* `CandidateEvaluator::RunCriterion`.
  - *What's missing:* skipping expensive measures on candidates that can no longer reach the top N, returning exactly what unpruned evaluation returns.
  - *Trigger:* profiling shows expensive measures spent on hopeless candidates.
- **Clearance measure.**
  - *What's blocking it:* `PhysicsQueries::SweepShape` and `OverlapShape` build a backend shape on every call, and `OverlapShape` fills a `std::vector`, so per-candidate use would allocate. Navigation's build profile already guarantees standing room against static geometry.
  - *Trigger:* `PhysicsQueries` accepts cached shapes and caller-owned output.
- **Multi-tick evaluation scheduler.**
  - *What's missing:* a budgeted scheduler that calls the same `Evaluate` on a subset of callers per tick.
  - *Trigger:* measured per-tick cost of real encounters exceeds budget.
- **`walkable_width` measure.**
  - *What's blocking it:* navigation exposes no distance-to-edge query (Detour has one).
  - *Trigger:* a consumer needs corridor width, for example to avoid or prefer choke points.
