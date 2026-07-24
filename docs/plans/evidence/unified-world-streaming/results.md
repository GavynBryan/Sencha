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
| Chunks after 1 cycle | 11 |
| Chunks after 10 cycles | 74 |
| Empty chunks retained | 74 |

`Archetype::RemoveRow` leaves emptied chunks in place and only the last chunk
per (archetype, partition) is reused, so each unload of a multi-chunk zone
orphans slabs. The free list recycles the partition *index*; the memory is not
returned. Every retained slab is also walked and skipped by every query.

## Transform propagation: the streaming hitch

`PropagationOrderCache` holds one world-global parent-before-child order and
invalidates on any structural change. Per-zone entity count is held at 20 000
and the world grows, so the numbers isolate whether the cost tracks the streamed
zone or everything resident. The attached zone is deliberately tiny — 100
entities — so anything above the steady sweep is invalidation blast radius
rather than the new zone's own work.

| World entities | Steady sweep (ms) | First sweep after a 100-entity zone attaches (ms) |
|---|---|---|
| 40 000 | 0.107 | 1.926 |
| 80 000 | 0.234 | 4.076 |
| 160 000 | 0.638 | 10.069 |

The hitch scales with total world size, not with the streamed zone: about
20x the steady sweep, and growing linearly with everything loaded. The prior
model kept one cache per registry, so the same attach rebuilt only the attaching
zone's order. Two physics reconcilers (`RigidBodyBinding`, `CharacterMoverPool`)
gate on the same global counter, so any entity spawn or despawn anywhere — a
projectile, an expiring effect, a zone detach — pays this too.

## Bounds derived from these numbers

Machine-independent assertions live in
`test/runtime/StreamingCostBoundsTests.cpp` and run in the normal suite. Three
are disabled because the current implementation does not meet them; each names
the phase that enables it, and each fails today for the reason its comment
states:

| Bound | State | Target |
|---|---|---|
| `ImportPerformsNoRowMigrationsPerEntity` | live, passing (Phase 2) | 0 |
| `StreamingChurnDoesNotGrowChunkCount` | disabled, 74 after 10 cycles vs 11 | equal |
| `SpawnInOneZoneDoesNotRebuildTransformOrder` | disabled, rebuilds | no rebuild |

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
