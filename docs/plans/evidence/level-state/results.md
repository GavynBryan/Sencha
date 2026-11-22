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
- Baseline bytes: the JSON text of the baseline records the store keeps, a
  proxy for their memory (the parsed records are larger).

## Capture

| Zone | Untouched | per entity | Every entity changed | per entity | Baseline text |
|---|---|---|---|---|---|
| 500 | 0.445 ms | 0.89 µs | 0.677 ms | 1.35 µs | 83,500 B |
| 5,000 | 4.46 ms | 0.89 µs | 6.63 ms | 1.33 µs | 835,000 B |

The landing budget is a 500-entity capture under 0.5 ms; the untouched capture,
the common case at every detach and save, is 0.445 ms. The first measurement
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
| 50 | 0.574 ms | 0.669 ms | 1.91 µs |
| 500 | 0.575 ms | 1.413 ms | 1.68 µs |

A patched entity costs its patch's composition over a copy of its package
record, plus parsing the zone's encoded lists when it imports.

## Raw

`level_state.json` and `level_state.csv` beside this file are the run the
tables quote.
