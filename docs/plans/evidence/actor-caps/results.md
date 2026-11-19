# Grants and worn records off the actor's row

What moving `AbilityGrants` and `EquipmentState` from fixed sixteen-entry rows
to pooled lists (`docs/ecs/row-lists.md`) cost and bought.

## Method

- **Before**: commit `292d90da` (the parent of this work) exported to a scratch
  directory, with only the two bench files of this change copied in so both
  sides run the same scenarios. Cases the old storage cannot hold (more than
  sixteen grants or records) have no before figure.
- **After**: commit `4f94ebad`.
- Both built with the profile preset (Release, `-O3 -g -fno-omit-frame-pointer`),
  run pinned with `taskset -c 0-11`, each under `timeout 900`:
  `SENCHA_ABILITY_BENCH_OUT=… runtime_tests --gtest_filter=AbilityBench.Generate`
  and `SENCHA_EQUIPMENT_BENCH_OUT=… runtime_tests --gtest_filter=EquipmentBench.Generate`.
- Three runs a side; each figure below is the median of the three. Ability pass
  figures are each run's median over 240 ticks after 60 of warm-up, in ms;
  equipment figures are each run's mean over 100 ticks after 5, in µs.
- Run-to-run spread on the same build was about 2% (the 1000-actor idle pass
  ran 0.2966, 0.2976 and 0.2989 ms before).
- Seed sizes: `SENCHA_REPLICATION_BENCH_OUT=1 runtime_tests
  --gtest_filter='AbilityReplication.AFullPawnsOwnerSeedIsMeasured*:AbilityReplication.AShippingPawns*'`,
  one first snapshot under `kNetDefaultSnapshotBytes`, deterministic.
- Raw tables: `abilities.csv`, `equipment.csv` (name, before, after, change),
  `seeds.before.txt`, `seeds.after.txt`.

## Footprint

| | Before | After |
|---|---|---|
| `AbilityGrants` row | 520 B | 20 B, plus 32 B a grant in `AbilityGrantLists` |
| Ability rows per actor | 2,424 B | 1,924 B |
| `EquipmentState` row | 656 B (16 records of 40 B) | 20 B, plus 40 B a record in `EquipmentRecordLists` |

Sizes are `sizeof` on each commit. An actor granted one ability carries 52 B of
grants where it carried 520; one granted sixteen carries 532. A wearer of 32
records carries 1,300 B; one wearing nothing carries 20 B where it carried 656.

## The ability pass

| Actors × grants | Idle before | Idle after | Firing before | Firing after |
|---|---|---|---|---|
| 1000 × 1 | 0.297 | 0.313 (+5.4%) | 0.557 | 0.564 (+1.3%) |
| 1000 × 4 | 0.299 | 0.306 (+2.4%) | 0.564 | 0.565 (+0.2%) |
| 256 × 14 | 0.079 | 0.080 (+1.9%) | 0.145 | 0.147 (+1.7%) |
| 16 × 16 | 0.0052 | 0.0054 (+3.6%) | 0.0096 | 0.0096 (−0.5%) |
| 16 × 64 | — | 0.0055 | — | 0.0096 |
| 256 × 64 | — | 0.081 | — | 0.147 |

- **The common small actor.** 1000 actors with four grants each cost the pass
  2.4% more idle and the same firing; with one grant 5.4% idle. That is at most
  one list lookup through the World's resource table per actor per press or
  sweep, about 16 ns an actor. It is the measured price of storing by content.
- **At four times today's content.** An actor with 64 grants costs the pass what
  one with 16 costs: a press reads only the grants bound to its action, and the
  sweep reads the list only for an actor holding a transient grant.

### Lasting effects that grant

| Actors | Effects | Grants an effect | Before | After |
|---|---|---|---|---|
| 100 | 600 | 0 | 0.038 | 0.040 (+4.0%) |
| 100 | 600 | 4 | 0.065 | 0.070 (+6.9%) |
| 256 | 1,536 | 4 | 0.167 | 0.181 (+8.5%) |
| 256 | 5,120 | 1 | 0.196 | 0.217 (+10.6%) |
| 256 | 5,376 | 3, every actor at 64 grants | — | 0.727 |

- Effect-held grants cost 4–11% more to keep in step than they did: each grant
  an effect gives is looked for in its target's list through the resource
  rather than in the row.
- **The full-room row is the new worst case.** 256 actors each holding 63
  effect-held grants plus their own cost 0.73 ms a tick, every tick, because the
  sync matches every effect's entries against every grant its target holds.
  That is quadratic in an actor's effect grants and is recorded in
  `docs/deferred.md` ("Effect grants are matched against every grant the target
  holds"), with the trigger.

## The equipment pass

| Scenario | Before | After |
|---|---|---|
| 1000 wearers, one ring each | 90.7 | 92.9 (+2.4%) |
| 2000 wearers, one ring each | 187.5 | 186.3 (−0.6%) |
| 1000 wearers × 4 records, 4 grants | 299.7 | 246.5 (−17.8%) |
| 100 wearers × 16 records, 16 grants | 101.9 | 78.7 (−22.7%) |
| 100 wearers × 32 records, 64 grants | — | 150.8 |
| 1 wearer × 32 records, 64 grants | — | 1.47 |
| 1000 holders, one waiting record, one walk a tick | 114.2 | 118.5 (+3.8%) |
| 200 wearers' records arriving at once, first pass | 491.9 | 599.9 (+22.0%) |
| the same, steady | 31.2 | 27.5 (−12.0%) |

- A still wearer costs what it did, or less with several records: the pass no
  longer looks a wearer's records up by key once per record.
- 32 records and 64 grants on one wearer: 1.5 µs a tick for the equipment pass
  and 4.0 µs for the ability pass.
- The adoption burst's first pass is 22% slower (about 0.5 µs a record): every
  derived record's state is written into a list the row names rather than in
  place. It runs once, when a zone's wearers arrive.

## Seed sizes

| Pawn | Peer | Before | After |
|---|---|---|---|
| Shipping pawn, nine grants, no equipment | owner | 990 B | 929 B |
| Shipping pawn, nine grants, no equipment | observer | 252 B | 255 B |
| The same wearing 16 records | owner | 1,711 B | 1,653 B |
| The same wearing 16 records | observer | 517 B | 523 B |
| The same wearing 32 records | owner | — | 2,369 B |
| The same wearing 32 records | observer | — | 779 B |
| Full pawn, 16 grants | owner | 1,002 B | 1,005 B |
| Full pawn, 64 grants | owner | — | 1,773 B |

- **Proportional to grants held.** Grants cross in pages of four; a page no
  grant reaches is not sent. The owner pays about 24 B a grant (the nine-grant
  pawn's seed fell 61 B, since three of its pages are full and it no longer
  sends sixteen grants' worth of words), and 64 grants cost 768 B more than
  16, all in one snapshot of 4,687 B.
- An observer is sent no grants; it pays 3 B more for the longer field mask of
  two components with more runs.
- A worn record costs an observer 16 B and its owner about 24 B, as before; 32
  records reach either in one snapshot with nothing unsendable.
