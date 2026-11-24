# What keeping level state costs

**Scenario.** `LevelStateBench.Generate` in `test/runtime/LevelStateBench.cpp`,
recorded by `scripts/bench_level_state.sh`.

**Method.**
- Profile preset (`build-profile`, `-O3 -DNDEBUG -g`), pinned to the
  performance cores (`taskset -c 0-11`, i7-13620H), run alone.
- A zone of N authored entities, each with three `Saved` components authored
  in content: `GameplayTags` (one tag), `Attributes` (two attributes) and
  `responses` (one once-binding). The game keeps level state
  (`ZoneStateSerialization` installed), so the importer records each entity's
  baseline and `CaptureZoneState` diffs each entity against it.
- Capture: the median of 9 live captures of the resident zone, untouched, then
  with every entity given one baseline tag (every entity patched).
- Attach: the median of 9 imports of a 500-entity zone with K patched entities
  installed in the store, against the same import with none; the difference
  over K is the cost a patched entity adds.
- Gained: the same 500-entity zone with G run-time pickups marked
  `KeptWithZone` (a transform and an `ItemPickup` each), captured live, then
  detached and imported with those G members in the store, against the same
  capture and import with none; the difference over G is what a gained member
  adds.
- Baseline bytes: the JSON text of the baseline records the store keeps, a
  proxy for their memory (the parsed records are larger).

## Capture

| Zone | Untouched | per entity | Every entity changed | per entity | Baseline text |
|---|---|---|---|---|---|
| 500 | 0.444 ms | 0.89 µs | 0.656 ms | 1.31 µs | 83,500 B |
| 5,000 | 4.50 ms | 0.90 µs | 6.61 ms | 1.32 µs | 835,000 B |

The landing budget is a 500-entity capture under 0.5 ms; the untouched capture,
the common case at every detach and save, is 0.444 ms with nothing gained
(0.445 ms before a zone could gain anything: the test for a marked entity
costs nothing measurable). The first measurement
was 1.06 ms (2.1 µs per entity). Two changes brought it under, with the same
patches:

- `CaptureEntityPatch` skips a component whose written form equals its
  baseline before it builds the patch's objects, and looks at a gained
  component only when the entity carries it: 0.52 ms.
- `IComponentSerializer::SavedFormEquals` lets a serializer say its saved form
  equals a record without writing it; the responses serializer answers from
  its bits and the interned bindings, so an untouched binding set is never
  copied: 0.445 ms.

The cost is linear: 5,000 entities is ten times 500. The 5,000 figure, 4.5 ms
untouched and 6.6 ms when everything changed, is the known ceiling on the owner
thread; change tracking is the answer at that scale (`docs/deferred.md`,
"Change tracking for zone capture").

## Attach

| Patched of 500 | Import, none patched | Import, patched | per patched entity |
|---|---|---|---|
| 50 | 0.599 ms | 0.688 ms | 1.76 µs |
| 500 | 0.598 ms | 1.416 ms | 1.64 µs |

A patched entity costs its patch's composition over a copy of its package
record, plus parsing the zone's encoded lists when it imports.

## What a zone gained

| Gained, beside 500 authored | Capture, none gained | Capture | per gained | Import, none gained | Import | per gained |
|---|---|---|---|---|---|---|
| 50 | 0.423 ms | 0.504 ms | 1.63 µs | 0.605 ms | 0.705 ms | 2.00 µs |
| 500 | 0.426 ms | 1.144 ms | 1.44 µs | 0.604 ms | 1.667 ms | 2.12 µs |

A gained member is written whole at every capture (its record and its
encoding) and made and read at every attach; nothing filters an unchanged
one yet, so 2,000 dropped items would add about 3 ms to every save of their
zone (`docs/deferred.md`, "Change tracking for zone capture").

## Placement (ABC2 stage 10)

Placement adds `LocalTransform` to the baseline of each entity a dynamic body
or a character controller holds, and the importer puts an owed `Saved`
component's default record there too. Re-run after it landed, with the stage 9
commit benched the same evening on the same machine for comparison (the
committed figures above were taken on an earlier day):

| Case | Stage 9 (3 runs) | Stage 10 (4 runs) |
|---|---|---|
| 500-entity capture, untouched | 0.453, 0.456, 0.487 ms | 0.452, 0.464, 0.466, 0.470 ms |
| 500-entity import, nothing patched | 0.612, 0.620, 0.620 ms | 0.669, 0.671, 0.679, 0.700 ms |

The untouched capture is unchanged within the run-to-run spread and stays under
the 0.5 ms budget: nothing per entity was added to it beyond one comparison in
`ZoneStateStore::BaselineOf`. The import is about 0.06 ms dearer for 500
entities: the importer asks each cooked component whether it holds placement
and looks up the components each entity is owed.

A zone of 5,000 dynamic props (a transform, a box collider and a dynamic
`RigidBody` each), nothing changed:

| | |
|---|---|
| baseline members | 10,000 (a transform and a body each) |
| baseline text | 465,600 B |
| import | 8.4 ms |
| capture, untouched | 8.9 ms (1.78 µs per prop) |

That capture is the figure the byte filter of stage 12 exists to bring down
(its budget is 0.5 ms for this case); until then a quicksave among 5,000
resident props costs about 9 ms on the owner thread.

## Raw

`level_state.json` and `level_state.csv` beside this file are the stage 10
run; the tables above it quote the earlier run, whose figures they keep.
