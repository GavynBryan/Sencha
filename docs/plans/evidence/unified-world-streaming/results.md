# Unified World streaming and iteration: baseline evidence

Baseline for the storage cutover from per-zone registries to one `World` whose
streamed zones are storage partitions. Establishes what the unified model costs
today so the hardening phases can be judged against a recorded number rather
than an impression.

Two questions are separated on purpose:

- **Does one partitioned World iterate as fast as N worlds did, and is a dormant
  partition free to skip?** Answered here, and the answer is yes.
- **What does publishing and unpublishing a zone cost the owner thread?**
  Answered here, and the answer is the reason the hardening work exists.

## Method

- Build: the `profile` preset (Release codegen, symbols, frame pointers). The
  `dev` preset carries asserts and a debug allocator and reports zone import
  roughly an order of magnitude slower, so it describes a build nobody ships.
  Runs record their configuration in the `build` field and
  `scripts/bench_streaming_compare.py` refuses to compare across configurations.
- Machine: 13th Gen Intel Core i7-13620H (6 P-cores + 8 E-cores, 24 MB L3),
  Linux 7.0.11. Pinned to a performance core.
- Harness: `StreamingBench.Generate` (`test/runtime/StreamingBenchGen.cpp`),
  driven by `scripts/bench_streaming.sh`. Medians; iteration over 200 passes,
  streaming over 30 repetitions, propagation over 15.
- Entity shape: `LocalTransform` + `WorldTransform` + `PointLightComponent`,
  about 129 rows per 16 KB chunk. Packages declare `LocalTransform` +
  `PointLightComponent` and the importer seeds the derived `WorldTransform`.
- `perf stat` counters taken separately on the same shapes, user-mode, P-core.
  Cycle and instruction columns come from that separate run, so they pair with
  its timing (0.2196 ms for the all-active shape) rather than with the recorded
  median below; the two differ by run-to-run drift, not by workload.

**Millisecond numbers are only comparable drift-normalized.** Every run records
`control_memory_stream_ms`, a fixed memory-streaming loop over a 20 MB buffer
that touches no engine code. The ratio between it and a real metric cancels
clock and thermal state; `bench_streaming_compare.py` reports both raw and
normalized change and judges regressions on the normalized figure. This was not
theoretical: partway through Phase 2 an untouched iteration metric read 33%
slower purely because the machine had been building for an hour, and an
independent binary running unchanged code confirmed the drift.

**The machine must be otherwise idle.** This is not boilerplate: the first
attempt at these numbers ran while a 16-way parallel build was in flight and
reported iteration at 1.01 ms with 37.8 M cache misses, against 0.22 ms and
2.31 M on a quiet machine. Core pinning does not protect a memory-bound
measurement from a neighbour evicting shared L3 — the misses are genuinely
suffered by the measured process, so the counter looks credible while describing
the neighbour. Re-run anything that disagrees with this file by more than the
compare script's tolerance on an idle machine before believing it.

## Iteration: the unification is free

One pass summing `WorldTransform` x `PointLightComponent` over 160 000 entities
(8 zones x 20 000), user-mode counters over 200 passes:

| Shape | ms/pass | Cycles | Instructions | Cache misses | Miss rate |
|---|---|---|---|---|---|
| Unified, 8 of 8 partitions active | 0.1954 | 473 M | 873 M | 2.31 M | 4.8% |
| Eight separate `World`s (prior model) | 0.2208 | 499 M | 991 M | 2.18 M | 4.6% |
| Unified, worst-case interleaved chunks | 0.1951 | 488 M | 874 M | 2.22 M | 4.6% |
| Unified, 2 of 8 partitions active | 0.0435 | — | — | — | — |

- **Parity holds.** Unified iteration runs about 11% faster than eight worlds on
  12% fewer instructions — one query and one archetype table instead of eight.
  Cache misses are equal within noise (2.31 M vs 2.18 M, both under 5%).
- **The per-chunk partition test does not show up.** `ForEachChunkIn` adds one
  indexed word load and a bit test per 16 KB chunk. Filtered all-active is not
  measurably different from the unfiltered eight-world walk.
- **Interleaving is not measurably worse.** Allocating entities round-robin
  across zones produces the pessimal chunk ordering a real streaming history can
  reach, and it lands within noise of sequential allocation (0.1951 vs 0.1954 ms,
  identical instruction and miss counts). Chunk-granular partitioning means the
  inner loop never sees the interleaving: it is still one linear column walk per
  chunk either way.
- **Dormant partitions are free to skip.** Two active partitions cost 0.0435 ms
  against 0.1954 ms for eight: cost tracks the active set, not the resident set.
  This is the participation-tier property the prior per-registry model provided,
  preserved at chunk granularity.

## Zone streaming: the owner-thread cost

Import runs inside one `AsyncTaskQueue` commit at `FramePhase::DrainAsyncTasks`.
`AsyncCommitBudgetMs` (default 2.0) is checked *between* commits and the first
commit of a drain always runs, so a single zone's import cannot be split by the
budget — it runs to completion on the owner thread whatever the budget says.

| Zone entities | Import (ms) | Row migrations | Chunks | Detach (ms) |
|---|---|---|---|---|
| 1 000 | 0.059 | 0 | 8 | 0.013 |
| 5 000 | 0.298 | 0 | 38 | 0.065 |
| 20 000 | 1.189 | 0 | 152 | 0.291 |

Recorded after Phase 2. The first measurement of this path, before the importer
built rows at their final signature, was 0.327 / 1.622 / 6.489 ms with exactly
three row migrations per entity — one per declared component plus the derived
transform, each copying the columns added before it. Normalized against the
drift control, the change is 5.7x; raw, 5.5x. `import_20000_ms` at 1.19 ms now
fits inside the 2.0 ms `AsyncCommitBudgetMs`, which is criterion 1.

Two changes produced it, in this order: building each row once at its final
archetype signature (6.489 -> 2.480 ms), then a one-entry memo on archetype
lookup plus hoisting the transform-type lookups out of the per-entity path
(2.480 -> 1.189 ms). The second pair was worth more than the profile suggested
because hashing a 256-bit signature and probing dominates a lookup whose answer
is almost always the previous one.

### Chunk reclamation

Ten load/unload cycles of one 1 000-entity zone through a recycled partition
slot:

| Metric | Value |
|---|---|
| Chunks after 1 cycle | 8 |
| Chunks after 10 cycles | 71 |
| Empty chunks retained | 71 |

`Archetype::RemoveRow` leaves emptied chunks in place and only the last chunk
per (archetype, partition) is reused, so each unload of a multi-chunk zone
orphans slabs. The free list recycles the partition *index*; the memory is not
returned. Every retained slab is also walked and skipped by every query.

## Transform propagation: the streaming hitch, and its removal

Per-zone entity count is held at 20 000 and the world grows, so the numbers
isolate whether the cost tracks the streamed zone or everything resident. The
attached zone is deliberately tiny — 100 entities — so anything above a plain
sweep is invalidation blast radius rather than the new zone's own work.

Three sweeps are measured because they cost different things. *All dirty* never
advances the frame, so every `LocalTransform` column reads as written at or after
the last sweep and the whole active world recomputes. *Clean* advances the frame
and writes nothing, leaving only the cost of deciding to skip — the common case
in a real scene. *After attach* is the first sweep once a 100-entity zone joins
the domain.

Before, with one world-global order over every transform entity, keyed on the
global structural counter:

| World entities | All dirty (ms) | Clean (ms) | After attach (ms) |
|---|---|---|---|
| 40 000 | 0.129 | 0.093 | 2.466 |
| 80 000 | 0.267 | 0.183 | 5.230 |
| 160 000 | 0.768 | 0.420 | 10.829 |

After Phase 3 — unparented entities swept chunk-linearly, parented entities
through an order invalidated by hierarchy change rather than by any structural
change:

| World entities | All dirty (ms) | Clean (ms) | After attach (ms) |
|---|---|---|---|
| 40 000 | 0.069 | 0.0008 | 0.0013 |
| 80 000 | 0.136 | 0.0014 | 0.0029 |
| 160 000 | 0.292 | 0.0029 | 0.0048 |

- **The hitch is gone, and so is its growth with world size.** 10.83 ms to
  0.0048 ms at 160 000, and order rebuilds per attach from 1 to 0. What remains
  is the new zone's own 100 entities plus one skipped-chunk test per resident
  chunk. Criterion 2 asked for under 1.0 ms at 320 000; the measured 160 000 cost
  is three orders of magnitude below that, and what is left scales with chunk
  count rather than entity count.
- **Deciding to skip was costing more than the arithmetic.** A sweep with nothing
  dirty ran 0.42 ms at 160 000 because the order walk touched a chunk header per
  entity, in hash order. Testing one column version per 16 KB chunk instead is
  143x cheaper.
- **Recomputing everything also got faster** — 0.768 ms to 0.292 ms — which is
  the same effect from the other side: the old sweep chased cached pointers in
  breadth-first order, so a full recompute was a random walk over every chunk.
- **The hierarchy path did not regress.** 20 000 entities in depth-4 chains,
  roots written every rep: 0.132 ms to 0.122 ms dirty, 0.049 ms to 0.032 ms
  clean. Parented entities keep the cached-pointer order, and this is the
  measurement that says keeping it is not costing anything (unified-world
  hardening Phase 3, step 3).

Two physics reconcilers (`RigidBodyBinding`, `CharacterMoverPool`) gated on the
same global counter, so any spawn or despawn anywhere — a projectile, an expiring
effect, a zone detach — rescanned every collider and controller in the world.
Both now gate on the summed structural version of the partitions they were handed
(`World::StructuralVersion(const StoragePartitionSet&)`), and the mover pool's
whole-world component scan is partition-filtered. `ReconcilePasses()` bounds this
in `test/physics/*ChurnOutsideTheActiveSetDoesNotReconcile`.

**Machine state for this comparison.** These runs were taken with an editor
process holding about one core, so the control read 0.461 ms against 0.365 ms for
the recorded baseline — every millisecond figure here is inflated by roughly a
quarter. Rather than compare against the recorded baseline across that gap, the
pre-change binary and library were frozen aside and the two were run interleaved,
twice, on the same machine state; the controls agree within 2.5% and the two
rounds agree within a few percent. Contamination inflates both sides, so each
improvement above is a lower bound, and the absolute after-numbers are upper
bounds. Re-record on an idle machine to tighten them.

## Bounds derived from these numbers

Machine-independent assertions live in
`test/runtime/StreamingCostBoundsTests.cpp` and run in the normal suite. Three
are disabled because the current implementation does not meet them; each names
the phase that enables it, and each fails today for the reason its comment
states:

| Bound | State | Target |
|---|---|---|
| `ImportPerformsNoRowMigrationsPerEntity` | live, passing (Phase 2) | 0 |
| `SpawnInOneZoneDoesNotRebuildTransformOrder` | live, passing (Phase 3) | no rebuild |
| `FlatSpawnResolvesAddressesWithoutRebuildingOrder` | live, passing (Phase 3) | resolve, no rebuild |
| `StreamingChurnDoesNotGrowChunkCount` | disabled, 71 after 10 cycles vs 8 | equal |

`CrossPartitionParentChangeRebuildsTransformOrder` is live from the start and
must stay live: cross-partition parenting is legal, so the order is genuinely
world-global and a hierarchy change in any partition must still rebuild it. It
exists so scoped invalidation cannot satisfy its own bound by invalidating less
than correctness requires.

## Artifacts

- [`streaming.json`](streaming.json) — the recorded run.
- [`streaming.csv`](streaming.csv) — the same metrics, flat.

Re-record with `scripts/bench_streaming.sh` and diff with
`scripts/bench_streaming_compare.py streaming.json <new>.json`.
