# Equipment per-tick cost

**Scenario.** `EquipmentBench.Generate` in `test/runtime/EquipmentBench.cpp`.

**Whose numbers.** The tables below were measured on the composition branch,
before inventory moved onto held records, and have not been measured again on
the profile preset since. A Debug run on this branch is at the end, for shape
only.

**Method.**
- Profile preset (`build-profile`), pinned to CPUs 0-7 (`taskset -c 0-7`).
- Mean over 100 ticks after 5 warm-up ticks, in microseconds per tick.
- Run with `SENCHA_EQUIPMENT_BENCH_OUT=build-profile/bench/equipment.json
  ./build-profile/test/runtime_tests --gtest_filter=EquipmentBench.Generate`.

## The ability pass over equipped instances

N wearers, each wearing one ring whose effect grants one ability, against the
same effect applied directly, with no per-instance grant list:

| Wearers | Direct | Equipped, linear instance lookup | Equipped, sorted instance lookup |
|---|---|---|---|
| 250 | 221 | 255 (+34) | 233 (+20) |
| 1000 | 1109 | 1605 (+497) | 1293 (+57) |
| 2000 | 2469 | 4985 (+2516) | 2560 (−185, noise) |

- With a linear lookup the overlay's cost grew with the square of the wearers.
- Sorted (what the engine does), it is within noise of applying the effects
  directly.
- The direct pass itself is about 1.2 µs per actor.

## The equipment pass

| Case | µs per tick |
|---|---|
| 250 wearers, one record each | 58 |
| 1000 wearers | 290 |
| 2000 wearers | 533 |

About 0.27 µs per wearer, so it grows linearly, and it stays under 1 ms at
2000 wearers.

## The movement tag projections, characters standing still

| Characters | µs per tick |
|---|---|
| 1000 | 36 |
| 4000 | 177 |

Each character costs one read per mode, and a still character is never
written.

## Rebinding with unrelated holders changing

N wearers, each with a record waiting in `use.x` and 20 other entries. One
wearer is given an entry every tick.

| Holders | Walks per tick | Equipment pass µs |
|---|---|---|
| 100 | 1 | 54 |
| 1000 | 1 | 384 |

Only the holder that changed is walked.

## A zone's equipped NPCs arriving at once

200 wearers, each arriving with two `Pending` records:

| Case | µs |
|---|---|
| First pass, every record derived | 1195 |
| Steady pass after | 75 |

- The first pass is over the 1 ms profile threshold.
- It is a one-tick cost when the zone loads, not per-tick work.
- The work is O(records × effects): each record's `Maintained()` scans every
  `ActiveEffect`.
- Recorded in `docs/deferred.md` ("Deriving a zone's equipped records as it
  loads").

## This branch, Debug build

`SENCHA_EQUIPMENT_BENCH_OUT=... timeout 600 ./build/test/runtime_tests
--gtest_filter='EquipmentBench*'`, dev preset (Debug, asserts on), unpinned;
the run took 6.7 s. Debug costs are several times the profile preset's, so
these confirm shape, not budget.

| Case | µs per tick |
|---|---|
| Ability pass, 2000 wearers, direct / equipped | 15010 / 15920 |
| Equipment pass, 250 / 1000 / 2000 wearers | 359 / 1423 / 2872 |
| Projections, 1000 / 4000 still characters | 103 / 403 |
| Rebinding, 100 / 1000 holders (1 walk per tick each) | 184 / 1516 |
| Adoption of 200 wearers: first pass / steady pass | 5835 / 479 |

- The equipped ability pass stays within about 6% of the direct one.
- The equipment pass grows linearly with wearers.
- Only the holder that changed is walked, through `HeldRecordIndex::Revision`.
