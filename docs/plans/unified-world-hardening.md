# Unified World hardening

Follow-on to [`unified-runtime-world.md`](unified-runtime-world.md). That plan
moved runtime storage from per-zone registries to one `World` whose streamed
zones are storage partitions. This plan closes the costs that move created,
so the model meets the streaming goal it inherited.

The measured starting point is
[`evidence/unified-world-streaming/results.md`](evidence/unified-world-streaming/results.md).
Two findings frame everything below:

- **Iteration is at parity and dormant zones are free to skip.** One partitioned
  World iterates 160 000 entities about 11% faster than eight separate worlds on
  12% fewer instructions, with equal cache misses, and two active partitions of
  eight cost a quarter of all-active. The unification does not cost throughput
  and it preserves participation tiers at chunk granularity.
- **Publishing a zone costs the owner thread, and the cost is unbounded.** A
  20 000-entity import stalls 6.5 ms inside one uninterruptible commit, and the
  next transform sweep costs 12 ms because a world-global cache rebuilt. The
  prior model attached a worker-built registry by pointer. That advantage is
  real, and recovering it is this plan's purpose.

## Success criteria

| # | Criterion | Baseline | Target |
|---|---|---|---|
| 1 | Owner-thread import, 20 000-entity zone | 6.49 ms | <= 2.0 ms (fits `AsyncCommitBudgetMs`) |
| 2 | First propagation after attaching a 100-entity zone into a 320 000-entity world | ~24 ms (extrapolated from 12.05 ms at 160 000) | < 1.0 ms |
| 3 | Chunk census after 10 load/unload cycles vs after 1 | 74 vs 11 | equal |
| 4 | Steady-state iteration, 160 000 entities all active | 0.195 ms | no regression beyond 10% |
| 5 | Worst frame during a live streaming event | unmeasured | no frame over budget |

Criterion 2 is the load-bearing one: it converts "the streaming hitch scales with
how much world is loaded" into "the hitch scales with the zone being streamed."

## Verification cadence

| Trigger | What runs |
|---|---|
| Every commit | focused tests for the touched invariant |
| Every phase boundary | `cmake --build --preset dev` -> full `ctest --preset dev` serially -> `git diff --check` -> both CI legs green |
| Perf phases (2, 3, 4) | `scripts/bench_streaming.sh`, then `scripts/bench_streaming_compare.py` against the recorded baseline; the delta goes in the phase's commit message and the evidence doc is re-recorded |
| Concurrency (2) or allocation (4) | `worker_count == 0` serial reference and the parallel path; `tsan`; ASan for 4 |
| Public-header changes | module ABI, layout, and isolation coverage (`scripts/check_module_abi.sh`) |
| Phase 6 | editor layering and mesh-edit dependency fitness |
| Phase 7 | live frame capture, long traversal, full matrix |

The phase-boundary local build is deliberate. The pre-rebase tip of this branch
was iterated through remote CI patch runs and never built locally: it carried
orphaned tests including deleted headers and a duplicate symbol definition. The
cadence exists so that cannot recur.

Baselines are re-recorded after each perf phase, so criterion 4 is checked
against the previous phase rather than only against the original — a few percent
of drift per phase would otherwise accumulate unnoticed.

## Phase 0 — Measurement harness (done)

No behaviour change. Landed:

- `World::RowMigrationCount()`, `World::ChunkCount()`, `World::EmptyChunkCount()`,
  `PropagationOrderCache::RebuildCount()` — following the `ReconcilePasses()`
  precedent already on the physics reconcilers. Every row copy between archetype
  rows routes through one private `World::MigrateRow` helper so the count cannot
  drift from the operations that pay for it.
- `StreamingBench.Generate` (`test/runtime/StreamingBenchGen.cpp`), skipped
  unless `SENCHA_STREAMING_BENCH_OUT` is set, emitting JSON and CSV.
- `scripts/bench_streaming.sh` (profile preset, P-core pinning) and
  `scripts/bench_streaming_compare.py` (unit-aware tolerances, refuses to
  compare across build configurations).
- `test/runtime/StreamingCostBoundsTests.cpp` — four bounds asserted on counted
  work rather than elapsed time, so they hold on every machine and in every
  build configuration.
- The recorded baseline and its artifacts under
  `evidence/unified-world-streaming/`.

Wall-clock numbers belong to the bench and the evidence doc; counted work
belongs to the suite. A benchmark nobody runs decays, but a counter assertion
fails in CI.

## Phase 1 — Lightmap resolution per partition

**Invariant:** each mesh samples the baked atlas of its own zone.
**Owner:** `RenderExtractionSystem`.

Extraction resolves one atlas per world today
(`ForEachComponent<ZoneLightmapComponent>`, last wins) and stamps it on every
emitted item. Under the prior model this ran once per registry, so each zone's
meshes received their own zone's atlas; with several lightmapped zones resident
in one World, one zone's atlas now smears across all of them. Resolve a
partition -> `{lightmap, ao}` map once per extract and look it up per chunk from
`ChunkView::Partition()`.

**Gate:** a regression test with two lightmapped partitions that fails before the
fix because items carry the wrong atlas index; criterion 4 unchanged.

## Phase 2 — Batch import

**Invariant:** importing a package costs one row construction per entity, not one
per component. **Owner:** `ZonePackageImporter` plus one `WorldComponentSchema`
write-into-row entry point.

The importer creates each entity empty and adds components one at a time, so a
K-component entity pays K row migrations and each copies the columns added
before it — measured at exactly three per entity for the benchmark shape, 60 000
migrations for a 20 000-entity zone. `CreateEntityWithSignature` already exists
and is unused by this path.

1. Pre-pass `ZoneLoadPackage::Parents()` into a child -> parent map so `Parent`
   joins the initial signature instead of costing a later transition.
2. Compute each entity's full signature: declared components, plus the derived
   `WorldTransform` that `SeedDerivedTransform` supplies for `LocalTransform`,
   plus `Parent` where parented.
3. Create the row at that signature, write component bytes into it, then fire
   `ComponentTraits::OnAdd` per component. Lifecycle hooks are contract: the
   batch path must preserve them and their registration order.
4. Reserve chunk capacity up front — `Chunk` zero-fills 16 KB per allocation, so
   a multi-chunk import otherwise pays a memset per slab.
5. Keep the existing rollback: on failure, destroy only the entities this call
   created.

Components carrying `SerializedJson` route through `IComponentSerializer::LoadIntoWorld`,
which adds internally. Start by including their types in the signature and
writing into the existing column; if that contract change proves invasive, leave
JSON payloads on the incremental path and re-measure. The gate decides, not
preference.

**Gate:** criterion 1; enable
`StreamingCostBounds.DISABLED_ImportPerformsNoRowMigrationsPerEntity`; full
suite; `worker_count == 0` and the parallel path, since this sits on the async
commit boundary; `tsan`.

**Conditional follow-on:** if the largest real zone still exceeds 2 ms, add the
bulk chunk blit — prebuilt column blocks memcpy'd into the archetype, EntityIds
minted owner-side, `Parent` remapped. That approaches the prior model's O(1)
attach. Triggered by the gate, not assumed.

## Phase 3 — Scoped cache invalidation

**Invariant:** a structural change in one partition invalidates only what depends
on that partition. **Owner:** `PropagationOrderCache`, `RigidBodyBinding`,
`CharacterMoverPool`.

All three key off the global structural counter, so any spawn or despawn anywhere
rebuilds a world-global order and rescans both physics bindings. The
per-partition counters needed to fix this already exist
(`World::StructuralVersion(StoragePartitionId)`) and nothing reads them.

1. The physics reconcilers already receive a partition set: gate them on the
   summed structural version of that set, and make `CharacterMoverPool`'s
   whole-world component scan partition-filtered at the same time.
2. Transform order cannot go per-partition, because cross-partition parenting is
   legal and the order is genuinely global. Split the cache instead: rebuild
   topology only on hierarchy change (it already computes `Changed<Parent>`; add
   a transform-entity add/remove signal), and on a plain structural bump
   re-resolve the cached row pointers in O(order) without the BFS and maps.
3. Measure whether the pointer cache earns its place at all. `PropagationEntry`
   caching raw `LocalPtr`/`WorldPtr`/`ChunkPtr` is precisely why any structural
   change forces invalidation, since swap-remove relocates rows. A/B it against
   a query-driven parent-before-child sweep and take the winner.

Under-invalidation is the danger here — it produces stale transforms and
un-reconciled bodies rather than a crash. Two mitigations: an equivalence check
that runs a churn scenario under scoped and forced-global invalidation and
asserts identical output, and a cvar that forces global invalidation so a field
bug has a one-line bisect.

**Gate:** criterion 2; enable
`StreamingCostBounds.DISABLED_SpawnInOneZoneDoesNotRebuildTransformOrder`;
`CrossPartitionParentChangeRebuildsTransformOrder` must stay green — it exists so
this phase cannot satisfy its own bound by invalidating less than correctness
requires; scoped-vs-global equivalence; full suite.

## Phase 4 — Chunk reclamation

**Invariant:** resident chunk memory is bounded by the concurrent high-water
mark, not by cumulative streaming history. **Owner:** `Archetype` free list plus
`World::DestroyPartition`.

`Archetype::RemoveRow` leaves emptied chunks in place, and only the last chunk
per (archetype, partition) is reused, so each unload of a multi-chunk zone
orphans slabs: 74 chunks retained after ten cycles of a zone that needs 11, all
empty. Every retained slab is also walked and skipped by every query, which
slowly feeds iteration cost.

Add a per-archetype free list of empty slabs. `DestroyPartition` returns the
partition's emptied chunks; allocation pulls from the list and re-stamps
`Chunk::Partition` before allocating fresh. Slot indices stay stable, so no
`EntityLocation` fixup is needed — that is why the free list is preferable to
chunk swap-remove.

**Stretch, gated on measurement:** a per-partition chunk index to remove the
all-archetypes-by-all-chunks scan in `DestroyPartition`. Cheap today precisely
because the leak fix bounds the census; measure before adding structure.

**Gate:** criterion 3; enable
`StreamingCostBounds.DISABLED_StreamingChurnDoesNotGrowChunkCount`; criterion 4
unchanged; ASan, since slab reuse is the shape that produced the last
use-after-free in this tree; full suite.

## Phase 5 — Partition-targeted deferred creation

**Invariant:** a system processing a zone can defer-create entities into that
zone. **Owner:** `CommandBuffer`.

`CommandBuffer::CreateEntity()` takes no partition, so deferred creations land in
the persistent partition and a system iterating a streamed zone cannot spawn into
it. Add an explicit `CreateEntity(StoragePartitionId)` overload; callers pass the
chunk's partition from the view.

Explicitly not an implicit "current partition" context: that is lifecycle state
derived from ambient context, and the partition is data that can be passed.

**Gate:** a test that spawns into a streamed partition from inside
`ForEachChunkIn`, asserting the entity lands in that partition and is skipped
when the zone goes dormant.

## Phase 6 — Completion and hygiene

| Item | Action |
|---|---|
| `docs/ecs/parallelization.md` | Remove `ForEachRegistryParallel`, `FrameRegistryView`, and the zone-parallel `PropagateTransforms` overload from the live-surface list — all three are deleted. Restate the partition model. |
| `docs/core-systems-map.md` | Rewrite the registry and `ZoneRuntime` ownership sections for `RuntimeWorld` and partitions. |
| `docs/plans/phase1-implementation-notes.md` | Mark superseded by the cutover. |
| `ZoneParallelPropagation` cvar | Delete: parsed and validated, zero readers, and the overload it gated is gone. |
| `world/registry/EntityRef.h` | Delete: zero consumers. |
| `Engine::Jobs()` | Decide. No runtime ECS consumer exists (editor and cook only). Either record the absence of intra-frame ECS parallelism as intentional, or anchor the pool to a declared consumer. Not left half-wired. |
| `RenderEntityKey` | Stop populating the vestigial `Kind`/`RuntimeRegistry` fields at runtime and document that the editor supplies document-registry identity while the runtime keys on `EntityId`. |
| `StoragePartitionSet::Members()` | Comment the LIFO-recycle ordering caveat: partition ids depend on load/unload history, so ordered cross-partition accumulation is not reproducible across differing streaming histories. |
| Registry isolation | `Registry` is behaviourally editor-only but still compiled into the shipping engine through render and serialization headers. Scope as its own follow-up against `unified-runtime-world.md` Phase 6. |

**Gate:** `git diff --check`; every referenced path, name, and command verified;
editor layering and module ABI fitness; full suite.

## Phase 7 — Live validation and the game-module port

1. Live frame capture across a streaming event (render bench harness plus chrome
   trace, foreground, `SENCHA_PRESENT_MODE=IMMEDIATE`). Criterion 5. This decides
   whether Phase 2's batching sufficed or the chunk blit is required.
2. Structural-churn rate in a representative combat scene: how often each cache
   actually invalidates, via the Phase 0 counters.
3. Long-traversal resident chunk count and megabytes; must plateau.
4. Port the game module repo off `ZoneRuntime`/`Registry`. The engine branch
   cannot merge while its only real consumer does not build.
5. Full matrix: dev suite, both CI legs, `tsan`, ASan.
