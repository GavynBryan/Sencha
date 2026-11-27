# Fixed wire images and empty component mentions: what they cost

What a networked game pays today for components that cross as fixed images,
and for naming every component an entity carries each time the entity is sent.
Measured before deciding whether a snapshot should leave out a component
nothing is owed on, and whether replicated lists are due
(`docs/deferred.md`, "A replicated list field").

## Method

- **Machine:** i7-13620H, runs pinned to its performance cores
  (`taskset -c 0-11`). Linux, glibc 2.42, GCC 15.3.1.
- **Build:** `profile` preset (`-O3 -DNDEBUG -g -fno-omit-frame-pointer`).
  Byte counts are deterministic and identical in the dev build.
- **Before:** commit `916ec8a9` plus the measuring tests of this change.
- **Commands**, each under `timeout`:
  - `SENCHA_REPLICATION_BENCH_OUT=… runtime_tests --gtest_filter='AbilityReplication.*IsMeasured*'`
    (deltas; raw lines in `deltas.before.txt`, `deltas.after.txt`);
  - `SENCHA_REPLICATION_BENCH_OUT=… runtime_tests --gtest_filter=EntityReplicationBench.Generate`
    (publish cost; the `actors_*` rows, three runs a side, in
    `bench.before.csv`, `bench.after.csv`, median last).
  - `scripts/bench_replication.sh` runs both.
- **The pawn** is the shipping pawn of `AbilityReplication`: nine grants, four
  attributes, two tags, character movement, look, spawn prefab, owner and
  driver. It moves every tick; a snapshot goes every second tick.
  `wearing` adds an `EquipmentState` with four records. Figures are the
  pawn's own record in the snapshot (`SnapshotWriteResult::DeltaBits`).
- **The actors** in the bench are unowned: a transform, `AbilityGrants` with
  three grants (and the five components it derives), and an `AttributeSet`
  with four attributes. `grants_0` rows carry the transform alone. One
  observer peer acknowledges every snapshot.
- **Image work** (`image_build_compare_copy`) repeats, over the same store,
  what `ReplicationChangeStore::Update` does with each component's image:
  zero-fill, `ToWire`, snap, whole compare, copy. It leaves out the column
  lookups, the component search and the per-entity bookkeeping, so it is the
  images' own share of `Update`.
- Run-to-run spread was under 6%.

## A moving actor's delta to an observer

| | Plan (computed) | Measured | Of it, mentions owed nothing |
|---|---|---|---|
| Shipping pawn | 23 B of masks | 40.3 B | 28.0 B (70%), 13 of 14 components |
| The same wearing four records | 33 B of masks | 47.4 B | 35.1 B (74%), 14 of 15 components |
| Unowned ability actor (bench) | 23 B of masks | 31.9 B | 18.8 B over a bare transform's 13.1 B |

- **The plan undercounted.** It counted six list-shaped components (grants,
  active abilities, cue log, leases, tags, attributes; 135 mask bits). The
  pawn carries no leases, but it does carry seven more components nothing
  moves: four predicted movement components, look orientation, owner, driver
  and spawn prefab. Thirteen indices and 120 mask bits are 28 B, against a
  transform delta of about 12 B.
- **The scenario.** Sixty moving pawns cost each observer about 50 KB/s of
  empty mentions at 30 Hz, not 41 KB/s; sixty unowned ability actors about
  34 KB/s. The budget ceiling is 140 KB/s.

## A tag and an attribute change to the owner

| | Plan (computed) | Measured | Of it, mentions owed nothing |
|---|---|---|---|
| One tag gains a stack | 256 B resent | 287.6 B | 27.8 B |
| One attribute's current value changes | 64 B resent | 95.6 B | 28.0 B |

- The resend sizes hold: the tag container's owner run is 257 B with its
  mask, the attribute set's current run 65 B. What the plan left out is the
  28 B of mentions owed nothing riding with every one of them.

## The authority's publish

| Actors | Carrying | `Update` still | `Update` moving | Image work | Share | Image bytes a publish |
|---|---|---|---|---|---|---|
| 300 | transform | 0.055 ms | 0.072 ms | 0.020 ms | 36% | 12,000 |
| 300 | three grants | 0.436 ms | 0.455 ms | 0.298 ms | 68% | 948,000 |
| 1,000 | transform | 0.187 ms | 0.244 ms | 0.068 ms | 36% | 40,000 |
| 1,000 | three grants | 1.544 ms | 1.648 ms | 1.034 ms | 67% | 3,160,000 |

- **Bytes touched.** A grant-holding actor carries 3,160 B of images whatever
  it holds, 1,540 B of them grants. At 300 actors that is 0.46 MB of grants,
  as the plan computed, and 0.95 MB in all, where it computed about 1.2 MB
  (it counted lease and animation-request tables this actor does not carry).
- **The plan was wrong about the time.** It called the images "small next to
  the store's per-entity cost already ledgered (0.18 µs)". A bare entity does
  cost 0.19 µs a publish. A grant-holding actor costs 1.54 µs, and its images
  are 1.03 µs of that: two thirds of `Update`, five times the per-entity cost.
- **The list trigger.** The deferred list field names image build and
  compare above 10% of `Update` with 1,000 or more replicated grant-holding
  actors as a trigger. This bench measures 67%. No shipping session has
  1,000 such actors; whether the bench alone fires the trigger is the owner's
  decision, and is recorded in `docs/deferred.md`.

## Decision

The condition for leaving out components nothing is owed on holds: empty
mentions are 70% of a moving pawn's delta to an observer, 10% of a tag change
to its owner and 29% of an attribute change.

## After: a component nothing is owed on is not named

Rerun on the commit that leaves such components out, by the same commands.
The `owed nothing` figures in `deltas.after.txt` are what those components
would cost if named; they are no longer written.

| | Before | After | Change |
|---|---|---|---|
| Moving pawn's delta to an observer | 40.3 B | 12.3 B | −70% |
| The same wearing four records | 47.4 B | 12.3 B | −74% |
| Moving unowned ability actor, per actor (bench) | 31.9 B | 13.1 B | −59%, a bare transform's cost |
| A tag gaining a stack, to the owner | 287.6 B | 265.9 B | −8% |
| An attribute's current value, to the owner | 95.6 B | 73.6 B | −23% |
| Full pawn owner seed, 64 grants | 1,773 B | 1,773 B | none: a seed names everything |

- **To the owner** a change still names the pawn's four predicted movement
  components (about 4.6 B), because that mention seeds its prediction shadow.
- **The authority's publish** is unchanged within noise: `Update` at 1,000
  grant-holding actors 1.44 ms still and 1.51 ms moving (before 1.54 and
  1.65), image work 0.98 ms (before 1.03). Writing 16 peers over 10,000 still
  entries fell from 5.29 to 4.49 ms (one run each; `bench.*.csv` hold only
  the actor rows).
- **A firing actor** (`AbilityReplication.CueBandwidthPerFiringActorIsMeasured`,
  bytes a second to one peer, aim sweeping): moving 1,710 → 1,050, firing
  2,050 → 1,570. Firing costs every peer 480 B/s less, but its cost over a
  moving actor rose from 340 to 520 B/s: the components firing changes
  (chiefly `ActiveAbilities`, a 36-run mask) used to be named by the moving
  actor too, and now are named only while firing. The test's 360 B/s bound
  was on that difference, which is no longer a cost, so the test now bounds
  the two totals instead (moving 1,150, firing 1,730, about 10% above these
  figures) and reports the difference without asserting on it.

## After: replicated lists

Rerun once `AbilityGrants`, `EquipmentState`, `GameplayTagContainer` and
`AttributeSet` cross as replicated lists (net protocol 12), by the same
commands. Bytes from the dev build at the parent of the list work
(`seeds.before.txt`) and at the last port (`seeds.after.txt`); publish times
from three profile runs of `EntityReplicationBench.Generate`, pinned to the
performance cores, under `timeout 900` (`bench.lists.csv`, median last; the
`before` column is `bench.after.csv`, the stage that left out components
nothing is owed on, measured 61 commits before this work began, so its
times are a reference rather than a clean baseline; its bytes are exact).

### Bytes

| | Before | After | Change |
|---|---|---|---|
| A tag gaining a stack, to the owner | 265.9 B | 14.9 B | −94% |
| An attribute's current value, to the owner | 73.6 B | 15.3 B | −79% |
| Shipping pawn's owner seed (nine grants, two tags, four attributes) | 929 B | 467 B | −50% |
| The same, observer seed of a full grant list | 205 B | 200 B | −2% |
| The same wearing 4 records, owner | 1,116 B | 650 B | −42% |
| The same wearing 32 records, owner | 2,369 B | 1,913 B | −19% |
| The same wearing 32 records, observer | 779 B | 754 B | −3% |
| Full pawn, 64 grants, 32 tags, 16 attributes, owner | 1,773 B | 1,816 B | +2.4% |
| Full pawn, 16 grants, owner | 1,005 B | 1,030 B | +2.5% |
| Unequipping one of three records, owner | 98 B | 54 B | −45% |
| Moving pawn's delta to an observer | 12.3 B | 12.3 B | none |
| A firing / moving actor to one peer | 1,570 / 1,050 B/s | 1,570 / 1,050 B/s | none |

- **Within 5% at every seed.** A seed is smaller wherever a list holds less
  than its most, because the fixed images sent a whole run (the tags' 64-word
  owner run, a page of four grants) for any part of it. A list at its most
  costs a little more than the full fixed image: each element pays a present
  bit and its run mask (three bits a grant), against one mask bit a page.
- **What a change resends** is now the element and run that moved: a tag's
  count, an attribute's current value, the record moved into a removed one's
  place, and the count.

### The authority's publish

| Actors | Carrying | `Update` still | `Update` moving | Image work | Share | Image bytes a publish |
|---|---|---|---|---|---|---|
| 300 | three grants, before | 0.422 ms | 0.441 ms | 0.293 ms | 69% | 948,000 |
| 300 | three grants, after | 0.410 ms | 0.431 ms | 0.292 ms | 71% | 364,800 |
| 1,000 | three grants, before | 1.440 ms | 1.507 ms | 0.984 ms | 68% | 3,160,000 |
| 1,000 | three grants, after | 1.382 ms | 1.446 ms | 0.975 ms | 71% | 1,216,000 |
| 1,000 | transform, before | 0.184 ms | 0.239 ms | 0.066 ms | 36% | 40,000 |
| 1,000 | transform, after | 0.212 ms | 0.267 ms | 0.067 ms | 32% | 40,000 |

- **Bytes touched fell 62%**, from 3,160 B to 1,216 B an actor: its grants
  are 88 B where they were 1,540, its attributes 64 B where they were 192,
  its tags 24 B where they were 384.
- **Time did not follow.** Image work at 1,000 grant-holding actors is
  0.975 ms against 0.984: images remain 71% of `Update`'s 1.38 ms, and the
  list trigger's figure (10%) is still exceeded. The work is not proportional
  to bytes; what does not shrink with a list (each component's translation,
  its image reset and copies) is the likely remainder, not measured per
  component. Of the 1,216 B left, 1,000 are the fixed slot tables
  `ActiveAbilities` (480 B) and `AbilityCueLog` (520 B) the bench's actor
  carries.
- **A bare entity's publish measured slower**, 0.212 against 0.184 ms at
  1,000. The `before` column was measured at the commit that left out
  components nothing is owed on, 61 commits before this work began, so it is
  not a clean baseline for this row. Holding the list count stamps inside
  `ChangedAt` instead of beside it, so that `ComponentState` is no larger than
  before lists, measured the same 0.211 ms (three runs; tried and not kept), so
  the 32 B of count stamps are not the cause. Not attributed further.
- **What is left is the slot tables.** The deferred "slot tables as replicated
  lists" names this profile after the port, with those tables the largest
  remaining share of `Update`, as its trigger. By bytes they are 82% of what
  the actor carries; the bench does not time components separately, so their
  share of the 0.975 ms is not measured.
