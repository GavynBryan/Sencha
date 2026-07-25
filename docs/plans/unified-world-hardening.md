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

| # | Criterion | Baseline | Target | State |
|---|---|---|---|---|
| 1 | Owner-thread import, 20 000-entity zone | 6.49 ms | <= 2.0 ms (fits `AsyncCommitBudgetMs`) | met, 1.19 ms (Phase 2) |
| 2 | First propagation after attaching a 100-entity zone into a 320 000-entity world | ~24 ms (extrapolated from 12.05 ms at 160 000) | < 1.0 ms | met, 0.0048 ms at 160 000 and no longer entity-proportional (Phase 3) |
| 3 | Chunk census after 10 load/unload cycles vs after 1 | 71 vs 8 | equal | met, 8 vs 8 (Phase 4) |
| 4 | Steady-state iteration, 160 000 entities all active | 0.195 ms | no regression beyond 10% | holding, within noise through Phase 4 |
| 5 | Worst frame during a live streaming event | unmeasured | no frame over budget | met on the owner thread, 0.116 ms of a 16.7 ms budget (Phase 7); GPU side has no venue, see below |

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

### Sanitizer results

Both presets went unbuilt through Phases 2 to 4 because this machine had neither
`libasan` nor `libtsan` installed, so neither could link. With the runtimes in
place, both legs ran clean over the work of all four phases:

| Run | Result |
|---|---|
| ASan, full suite | 1824/1824, no reports |
| ASan, `StreamingBench.Generate` (imports, ten load/unload cycles, propagation) | no reports |
| ASan with LeakSanitizer, reclamation and partition suites | no reports |
| tsan, `jobs_tests`, `ecs_tests`, `runtime_tests`, `physics_tests` | 523 tests, zero warnings |
| tsan, `StreamingBench.Generate` (async commit boundary) | no warnings |

A clean sanitizer run is worth nothing without a negative control, so both were
checked: a standalone two-thread increment race is reported by tsan and a standalone
heap-use-after-free by ASan under the same flags, and the instrumented engine
libraries import 41 `__asan_*` and 38 `__tsan_*` symbols respectively. Leak
detection is off for the suite runs — the engine holds intentional process-lifetime
allocations — and enabled only for the reclamation paths, where the free list's
retained slabs are the thing worth checking.

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
commit boundary; `tsan` — deferred at the time for want of the runtime, run clean
since (see Sanitizer results).

**Conditional follow-on:** if the largest real zone still exceeds 2 ms, add the
bulk chunk blit — prebuilt column blocks memcpy'd into the archetype, EntityIds
minted owner-side, `Parent` remapped. That approaches the prior model's O(1)
attach. Triggered by the gate, not assumed.

## Phase 3 — Scoped cache invalidation (done)

**Invariant:** a structural change in one partition invalidates only what depends
on that partition. **Owner:** `PropagationOrderCache`, `RigidBodyBinding`,
`CharacterMoverPool`.

All three keyed off the global structural counter, so any spawn or despawn
anywhere rebuilt a world-global order and rescanned both physics bindings.

1. **Physics.** Both reconcilers already received a partition set, so they now
   gate on `World::StructuralVersion(const StoragePartitionSet&)` — the summed
   per-partition counters, which move only for churn inside the set they were
   handed. `CharacterMoverPool`'s whole-world `ForEachComponent` scan became a
   cached `Without<CharacterMoverLink>` query filtered by the same set. The
   duplicated `SamePartitions`/`CopyPartitions` helpers in both files collapsed
   into `StoragePartitionSet::operator==` and plain assignment.
2. **Transform order.** Splitting the cache into topology and addresses was the
   plan; what the measurements then showed is that the order should not have
   covered unparented entities in the first place. An entity with no parent needs
   no ordering, so those are now swept chunk-linearly by a partition-filtered
   query with `Changed<LocalTransform>` doing the skipping, and only parented
   entities enter the order. That makes both the order and every cost of
   maintaining it proportional to the hierarchy instead of to the world:
   topology rebuilds on hierarchy change, addresses re-resolve on a structural
   bump in O(parented), and a flat spawn or zone attach does neither.
3. **Whether the pointer cache earns its place** (plan step 3): measured, and it
   does. A depth-4 hierarchy bench was added to the harness and recorded on the
   pre-change binary first. Retaining cached row pointers for parented entities
   costs nothing against the old behaviour (0.132 -> 0.122 ms dirty, 0.049 ->
   0.032 ms clean at 20 000 entities), so no query-driven replacement was needed.

Two mitigations against under-invalidation, which produces stale transforms
rather than a crash: `ScopedInvalidationMatchesForcedFullRebuild` runs a churn
script — root move, streamed spawn, cross-partition re-parent, archetype growth,
partition sleep and wake, parent destruction — under both scoped and forced-full
invalidation and compares **every sweep**, not just the final state; and
`transform.force_full_propagation` forces the rebuild every sweep so a suspected
field bug is one console line to bisect.

Comparing only final state is what a first draft of that test did, and it passed
while a deliberately sabotaged hierarchy probe was in the tree: the wrong value
was overwritten by a later sweep. Per-sweep comparison catches it and names the
step.

**Result:** first sweep after attaching a 100-entity zone into a 160 000-entity
world, 10.83 -> 0.0048 ms, with order rebuilds per attach 1 -> 0; a sweep with
nothing dirty, 0.420 -> 0.0029 ms; a sweep with everything dirty, 0.768 -> 0.292
ms. Criterion 2 asked for under 1.0 ms at 320 000 and what remains scales with
chunk count. Iteration and import unchanged within noise.

Scoping invalidation also made a latent change-detection hole reachable: a row
appearing in a chunk was invisible to `Changed<T>`, which cost an entity spawned
after the first sweep its world transform. Blanket invalidation had been hiding it.
Fixed in Phase 4, where the same mechanism was needed for reclamation.

**Gate:** criterion 2 met; `SpawnInOneZoneDoesNotRebuildTransformOrder` enabled
and passing, joined by `FlatSpawnResolvesAddressesWithoutRebuildingOrder`;
`CrossPartitionParentChangeRebuildsTransformOrder` still green;
scoped-vs-global equivalence green; two new physics reconcile bounds; full suite
1818/1818; module ABI OK. Two latent bugs found and fixed on the way: a parent
whose entity slot had been recycled was inherited by index alone, ignoring the
generation, and the chunk-conservative dirty test needed a guard for a child
sitting in a clean chunk under a moved parent.

## Phase 4 — Chunk reclamation (done)

**Invariant:** resident chunk memory is bounded by the concurrent high-water
mark, not by cumulative streaming history. **Owner:** `Archetype` free list plus
`World::DestroyPartition`.

`Archetype::RemoveRow` leaves emptied chunks in place, and only the last chunk
per (archetype, partition) is reused, so each unload of a multi-chunk zone
orphans slabs: 71 chunks retained after ten cycles of a zone that needs 8, all
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

**Result:** the free list lives on `Archetype`, and `RemoveRow` returns a slab the
moment it loses its last row — which also covers ordinary entity churn, not only
zone unload, since both leak the same way. Chunk census after ten load/unload
cycles fell from 71 to 8, equal to one cycle, and the resident count for the
160 000-entity iteration shape fell from 1 240 to 1 219 because the transient
empty-signature slabs are now shared rather than held per partition. Iteration,
import, and propagation are unchanged or slightly better.

**Reclamation exposed a change-detection hole that had to be fixed with it.**
`AddComponent` never marked the destination chunk's columns as written, so a row
appearing in a chunk was invisible to `Changed<T>`. Nothing depended on that while
every structural change invalidated every cache — Phase 3's scoped invalidation is
what made it reachable, and it cost an entity spawned after the first sweep its
world transform entirely. Every path that creates a row now funnels through
`Archetype::AddRow`, which stamps the destination chunk's columns as this frame's
write; `MoveEntityToPartition`'s separate bump became redundant and was removed.
Measured cost: about 6 ns per entity on the incremental spawn path, and none on the
batch importer. `TransformPropagation.EntitySpawnedAfterAnEarlierSweepIsPropagated`
covers it and fails without it.

**Gate:** criterion 3 met; `StreamingChurnDoesNotGrowChunkCount` enabled and
passing, plus four `ChunkReclamationTest` bounds on the storage mechanism itself;
criterion 4 unchanged; full suite 1824/1824; ASan clean (see below).

## Phase 5 — Partition-targeted deferred creation (done)

**Invariant:** a system processing a zone can defer-create entities into that
zone. **Owner:** `CommandBuffer`.

`CommandBuffer::CreateEntity()` took no partition, so deferred creations landed in
the persistent partition and a system iterating a streamed zone could not spawn
into it. There is now a `CreateEntity(StoragePartitionId)` overload; callers pass
the partition from the chunk they are iterating (`ChunkView::Partition()`), and the
no-argument form still means the persistent partition, matching
`World::CreateEntity()`.

Explicitly not an implicit "current partition" context: that is lifecycle state
derived from ambient context, and the partition is data that can be passed.

**Gate:** `StoragePartitionQueryTest.DeferredCreationLandsInTheIteratedPartition`
spawns from inside `ForEachChunkIn` and asserts the entity is owned by the iterated
zone rather than the persistent partition, and dies with `DestroyPartition`; it
fails without the wiring. `DeferredCreationCanBeGivenComponents` spawns and
initializes in one recording and finds the result in the very next query over that
zone. `ManyDeferredCreationsKeepTheirOwnComponents` covers the flush's batching of
like commands, where an unresolved ordinal loses every component rather than one.
`LifecycleHookTest.CommandBufferAddToAPendingEntityFiresOnAddWithALiveEntity` covers
the hook contract: `OnAdd` is where external handles are retained against an id, so
it must be handed the entity that now exists.

The original gate also asked for "skipped when the zone goes dormant". A spawn with
components would now be observable that way, but dying with `DestroyPartition` is
the stronger statement about ownership, so that is what the test asserts.

### Deferred creation made usable

A partition alone was not enough. `CreateEntity` did not expose what it created, so
a system could not give the entity it just spawned any components — most of what
"spawn into this zone" means — and the method had no consumers anywhere in the tree.
A correct parameter on an API nobody can use is not a phase, so `CreateEntity` now
returns a handle:

```cpp
const PendingEntity spawned = commands.CreateEntity(view.Partition());
commands.AddComponent(spawned, Projectile{ ... });
```

`PendingEntity` names a creation the buffer has not performed yet. It is
deliberately not an `EntityId` — no row exists until `Flush`, so there is nothing
the World could be asked about it, and the type makes that a compile error rather
than a runtime surprise. `Flush` records each created id in command order and
substitutes it wherever a command addresses an ordinal, including inside the
batching paths that group like commands. This mirrors `ZoneLocalEntityId`, which
already solves the same problem for a zone package built off the owner thread.

A handle also carries the recording it belongs to, and `Flush`/`Clear` end one, so a
handle held across either asserts instead of silently resolving to whatever entity
later takes its ordinal. Only `AddComponent` accepts a handle: removing a component
from, or destroying, an entity the same buffer is about to create has no caller and
would be speculative.

`Command::InitialComponents`, an unused placeholder for exactly this capability, is
deleted — the handle plus `AddComponent` is the mechanism it was standing in for.

## Phase 6 — Completion and hygiene (done)

| Item | Outcome |
|---|---|
| `docs/ecs/parallelization.md` | Live-surface list rewritten around `FrameZoneView`, `ForEachChunkIn`, and the single filtered `PropagateTransforms`. A "Zone-level parallelism, retired" section now maps each retired name to what replaced it, so the preserved history below it reads as history instead of as instructions. |
| `docs/core-systems-map.md` | Registry and `ZoneRuntime` ownership sections rewritten for `RuntimeWorld`, one entity namespace, and five partition-set domains; the ownership tree, the async zone flow ("detached `Registry`" -> detached `ZoneLoadPackage`), and the audio-domain sweep corrected with them. |
| `docs/ecs/storage-partition-queries.md` | Status changed from "Phase 2 substrate" to live, and the stale scope boundary ("does not yet replace `FrameRegistryView`") replaced by the `Members()` ordering caveat. |
| `docs/action-adventure-core-runtime.md` | Not in the original list. Its "Current Foundation" section asserted per-registry mechanisms as existing; corrected, with a note at the top that the product reasoning is unaffected. |
| `docs/ecs/decisions.md` | Not in the original list, and the most important correction here. D3.1 mandated an order over *every* transform entity and D3.2 explicitly rejected the chunk pass that Phase 3 went on to implement. Both are amended with the measurements, not overwritten: the ordering constraint they cite is real but binds only parented entities. |
| `docs/plans/phase1-implementation-notes.md` | Marked superseded, with what it introduced that is still live. |
| `ZoneParallelPropagation` cvar | Deleted — field, JSON parse, and its three test expectations. Unknown keys are ignored by the parser, so a config file still carrying it loads unchanged. |
| `world/registry/EntityRef.h` | Deleted. Verified genuinely unincluded: the editor hits a grep finds are `EntityRefGroup`, a different type. The concept is obsolete anyway — one World means an `EntityId` alone identifies an entity. |
| `Engine::Jobs()` | Not half-wired after all: the pool has real consumers (source watching, project content mount, texture recook), all editor and asset-side. Recorded at the declaration, along with why the runtime frame has none. |
| `RenderEntityKey` | Nothing to remove: the runtime already fills in `Entity` alone, and `MakeRenderEntityKey` is live in the editor, where documents really are separate registries. Documented the split at the type. |
| `StoragePartitionSet::Members()` | Ordering caveat commented at the accessor and in the partition-query doc. |
| Registry isolation | Still open, still scoped as its own follow-up against `unified-runtime-world.md` Phase 6. |

Three of the plan's own claims did not survive checking, which is the reason the
gate says "every referenced name verified": the stale-doc list was incomplete
(`decisions.md` and `action-adventure-core-runtime.md` also asserted the old model),
and two items filed as deletions turned out to be documentation because the
mechanisms have live consumers.

**Gate:** `git diff --check` clean; every referenced path, name, and link verified
(the only two link-check hits are C++ lambda syntax inside code fences); editor
layering, mesh-edit dependency, and module ABI fitness green; full suite 1829/1829.

## Phase 7 — Live validation and the game-module port

Items 1 to 3 are done; item 4 is the remaining work on this plan.

### The venue problem, found on starting

The plan assumed a live streaming venue existed to capture frames from. None does.
`WorldPartitionRuntime` — the demand policy that decides which zones are resident —
has no consumer outside its own unit tests, and SceneViewer loads exactly one zone
and refuses a second (`a map is already loaded or loading`). No application in this
repo has ever streamed more than one zone.

So the live validation was built where it could be built and measured what could be
measured honestly: a traversal harness driving the real streaming path headlessly
(`test/runtime/StreamingTraversalFixture.h`), asserted two ways — counted work in
`StreamingTraversalTests.cpp`, wall clock in `StreamingBench.Generate`. That is the
whole owner-thread cost of a streaming event: demand, async build and commit,
residency processing, frame view, transform sweep, every frame, over four laps of an
eight-zone chain with zones attaching ahead of the focus and unloading behind it.

| Measure | Value |
|---|---|
| Worst frame that attached or unloaded a zone | 0.116 ms |
| Worst frame that did not | 0.039 ms |
| Median frame that did not | 0.002 ms |
| Resident chunks after lap 1 / lap 4 | 16 / 16 |
| Order rebuilds over 720 frames | 106 (about one per zone event) |

1. **Worst frame during a streaming event — criterion 5.** Met on the owner thread:
   0.116 ms against a 16.7 ms budget. Room-scale zones, matching the product shape;
   for a larger zone the Phase 2 import number bounds it at 1.19 ms for 20 000
   entities.
2. **Structural-churn rate.** 106 order rebuilds and 106 address resolves over 720
   frames, tracking zone events rather than frames.
   `OrderRebuildsTrackZoneEventsNotFrames` bounds it, and states the churn it needed
   so it cannot pass by streaming nothing.
3. **Long-traversal memory.** The chunk census after four laps equals the census
   after one, across 50 attaches. `ResidentChunksPlateauAcrossLaps` bounds it.
   `RestreamedZoneMatchesAFreshAttach` covers the composition the other three phases
   have to survive together: a zone unloaded and re-streamed several times, through
   reclaimed slabs and recycled partition ids, produces identical world transforms.
4. **Port the game module repo off `ZoneRuntime`/`Registry`.** Outstanding. The
   engine branch cannot merge while its only real consumer does not build.

**Not measured, and it needs a venue.** GPU frame cost during a streaming event, and
render extraction volume as zones come and go. Building that means either teaching
SceneViewer to drive `WorldPartitionRuntime` with a multi-zone cooked world, or
getting it for free from the game-module port — which is the natural place, since a
game is the thing that legitimately owns a focus position and a streaming policy.
Until then the renderer's behaviour under streaming rests on the Phase 1 lightmap
tests and extraction's per-partition filtering, not on a capture.

**Gate:** full suite 1832/1832 serially; the traversal green under ASan;
`git diff --check` clean; evidence re-recorded.
