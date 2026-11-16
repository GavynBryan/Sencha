# Entry-shaped entities and effect scans: Stage 0 measurements

These are the measurements Stage 0 of
`docs/plans/definition-composition-inventory-equipment.md` owes before D3
(inventory entries as entities) is settled. This file records numbers and
projections only. The owner rules on D3.

These numbers were taken on the `claude/composition` branch, at main 93476b54,
before the composition work was ported onto a later main. The benches came
across with the port; rerun both scripts to measure the code as it is now.

## Method

- **Machine:** i7-13620H.
  - Runs are pinned to its performance cores (`taskset -c 0-11`).
  - Linux, glibc 2.42, GCC 15.2.1.
- **Build:** `profile` preset (`-O3 -DNDEBUG -g -fno-omit-frame-pointer`).
- **Scripts:** `scripts/bench_replication.sh` (`EntityReplicationBench.Generate`
  and `AbilityReplication.AFullPawnsOwnerSeedIsMeasured`) and
  `scripts/bench_abilities.sh` (`AbilityBench.Generate`).
- **Raw output:** in this directory. `replication.json`/`.csv`,
  `replication.pawn.txt`, `abilities.json`/`.csv`.
- **Timing:** medians.
  - Publishes: 120 publishes after 30 of warm-up.
  - Ability passes: 240 ticks after 60.
  - Structural batches: 30 repetitions.
- **The entity measured** stands in for an inventory entry:
  - a 20-byte component (`EntityId` container, definition, count, position);
  - a codec, 24 bytes on the wire, every field owner-only;
  - optionally `PersistentIdComponent`, indexed;
  - optionally `NetReplicated` + `NetOwner`.
  - Entries are spread evenly over 16 containers, each owned by one peer.
  - Every entity is built at its final signature, so no row migrates.
- **Publish:** the bench drives the functions `ReplicationRuntime::Publish`
  calls: `ReplicationChangeStore::Update` once, then `ReplicationWriteSnapshot`
  per peer at the shipping budget (1,173 bytes). Each peer acknowledges every
  snapshot at once.
  - The full-session soak harness was not used. Its clients send no
    acknowledgements (`test/net/ReplicationSoakTests.cpp`, header), so every
    entity would ride as full state on every publish, which is not steady state.
- **Memory:** live heap (`mallinfo2().uordblks`) differences between stages,
  divided by the entry count. Chunk slabs, the entity registry and every
  container are heap allocations.

## Results

### Memory per entry

| Entries | Entity (chunk row + registry) | + persistent id (component + index) | + `NetReplicated`/`NetOwner` | Change store + authority identity (first publish) | Peer floors, 16 peers (after seeding) |
|---|---|---|---|---|---|
| 1,000 | 85 B | 40 B | 15 B | 946 B | 884 B |
| 10,000 | 79 B | 49 B | 3 B | 812 B | 896 B |
| 50,000 | 67 B | 54 B | 4 B | 826 B | 986 B |

**Reading the table:**
- The ECS and identity cost of an entry is about **120–130 bytes**.
- **Replicating** it costs a further **~820–950 bytes** on the authority whatever
  the peer count, plus **~55–60 bytes per peer**.
- At 16 peers, a replicated entry costs the authority about **1.8 KB**, about
  14 times its storage.

### Structural operations

| Batch | Create | Move partition | Destroy |
|---|---|---|---|
| 1 | 0.0015 ms | 0.0004 ms | 0.0001 ms |
| 100 | 0.008 ms | 0.003 ms | 0.003 ms |
| 10,000 | 0.64 ms | 0.26 ms | 0.33 ms |

### Authority publish

Medians per publish. "Write" is every peer's snapshot. Allocations are
operator-new calls during the publish.

| Replicated entries | Peers | Changing | Change store update | Write | Allocations | Bytes (all peers) |
|---|---|---|---|---|---|---|
| 0 | 16 | 0 % | 0.0001 ms | 0.0009 ms | 1 | 368 |
| 1,000 | 1 | 0 % | 0.171 ms | 0.034 ms | 5,006 | 23 |
| 1,000 | 16 | 0 % | 0.172 ms | 0.525 ms | 5,036 | 368 |
| 1,000 | 16 | 1 % | 0.175 ms | 0.530 ms | 5,056 | 468 |
| 5,000 | 1 | 0 % | 0.913 ms | 0.169 ms | 25,006 | 23 |
| 5,000 | 16 | 0 % | 0.932 ms | 2.708 ms | 25,036 | 368 |
| 5,000 | 16 | 1 % | 0.924 ms | 2.699 ms | 25,100 | 868 |
| 10,000 | 1 | 0 % | 1.846 ms | 0.349 ms | 50,006 | 23 |
| 10,000 | 16 | 0 % | 1.970 ms | 5.486 ms | 50,036 | 368 |
| 10,000 | 16 | 1 % | 2.052 ms | 5.800 ms | 50,116 | 1,352 |

**Both costs are linear in replicated entities, and idle entities cost as much
as changing ones:**
- **The change-store update** costs about **0.18 µs per entity** per publish.
- **The write** costs about **0.034 µs per entity per peer**: each peer's write
  walks every live entity, whether or not that peer is owed anything about it.
- **Each publish makes five allocations per replicated entity.**
  `ReplicationChangeStore::Update` copies every live entity's state, with its
  per-component byte and generation vectors, into `LiveEntities` on every
  publish (`ReplicationChangeStore.cpp:258-276`).

**This is a property of today's replication, not of entries.** Every
replicated entity (pawns, projectiles, doors) pays the same per-publish cost.
Entries matter only because they could be numerous.

### Seeding a peer

| Case | Publishes until confirmed | At the default cadence (every 2nd tick at 60 Hz) |
|---|---|---|
| One peer, one container of 300 entries | 9 | 0.30 s |
| 16 peers, each with a container of 150 entries (2,400 entries) | 28 | 0.93 s |

### Pawn owner seed (one datagram, 1,173 bytes)

| Pawn | Seed |
|---|---|
| Moving, owned, prefab-spawned; 9 grants, 4 attributes, 2 tags (`AShippingPawnsOwnerSeedFitsOneSnapshot`) | 1,117 B |
| Every grant, attribute and tag slot full, an automatic firing (`AFullPawnsOwnerSeedIsMeasured`) | 1,129 B |

The worst case fits with 44 bytes to spare. Any growth in an owner-visible
pawn codec now has a measured ceiling.

### AbilityKit effect scans

Medians per tick. "Pass" is `RunAbilityPass` on the authority, which includes
`SyncEffectGrants`. "Resolve" is `ResolveAttributesWithEffects`. "Remove" is one
`RemoveEffectsOwnedBy`.

| Actors | Infinite effects | Grants per granting effect | Pass | Resolve | Remove |
|---|---|---|---|---|---|
| 100 | 600 | 0 | 0.032 ms | 0.019 ms | 0.0003 ms |
| 100 | 600 | 1 | 0.047 ms | 0.018 ms | 0.0003 ms |
| 100 | 600 | 4 | 0.063 ms | 0.018 ms | 0.0003 ms |
| 100 | 2,000 | 0 | 0.031 ms | 0.061 ms | 0.0009 ms |
| 100 | 2,000 | 1 | 0.073 ms | 0.063 ms | 0.0009 ms |
| 256 | 1,536 | 0 | 0.081 ms | 0.047 ms | 0.0007 ms |
| 256 | 1,536 | 4 | 0.164 ms | 0.048 ms | 0.0007 ms |
| 256 | 5,120 | 0 | 0.083 ms | 0.168 ms | 0.0021 ms |
| 256 | 5,120 | 1 | 0.191 ms | 0.168 ms | 0.0021 ms |
| 256 | 5,120 | 4 | 0.173 ms | 0.158 ms | 0.0021 ms |

**Notes on the table:**
- **Fewer granting effects at 4 grants.** A 16-grant table holds at most three
  grant-bearing effects of 4 grants each, against fifteen of 1 grant each.
  That is why the 4-grant rows are not always the most expensive.
- **Grant sync adds up to ~0.11 ms** to the pass at 5,120 effects.
- **Resolve** grows with effects: about 0.03 µs per effect.
- **Owner-matched removal** walks every effect: about 0.4 ns per effect.
- **Everything is under the 1 ms mark** at which `CLAUDE.md` considers
  parallelising a query.
- **No caching is justified by these numbers.**

## Projections to the RFC's populations (derived, not measured)

These are linear extrapolations from the rows above, which are linear across
1k, 5k and 10k.

| Population | Authority memory, replication | Per publish (every 2nd tick) |
|---|---|---|
| 16 players × 150 replicated entries (2,400) | ≈ 4.3 MB (2,400 × ~1.8 KB) | update ≈ 0.44 ms + writes ≈ 1.3 ms ≈ **1.75 ms**, and ≈ 12,000 allocations |
| One player × 300 entries, 1 peer | ≈ 0.27 MB | ≈ 0.07 ms |
| 200 NPCs × 8 entries, not replicated | ≈ 1,600 × ~125 B ≈ 0.2 MB | none |

**How these scale.**
- Under the RFC's §C.3 rule, an entry replicates only while a peer is relevant
  to its container. The NPC row therefore costs nothing in replication.
- The 16-player row is the case to decide on.
- About three quarters of its publish cost is writes that visit every
  replicated entity for every peer. A per-entity relevance filter that skipped
  irrelevant entities before encoding would remove most of that: its writes
  would be ≈ 2,400 × 0.034 µs ≈ 0.08 ms. That filter is not built, and the
  figure is a projection.
- The update half, and its allocations, belong to today's change store. They
  apply to every replicated entity alike.

## Stage 4: the per-entity relevance filter, measured (D3 condition 3)

- **Setup.** Same harness, preset and machine as above: `scripts/bench_replication.sh`,
  profile preset, performance cores, Stage 4 branch. The raw run is in
  `replication.stage4.{json,csv}`.
- **What the new rows add.** The `publish_anchored` rows give each entry a
  `NetRelevanceAnchor` to its peer-owned container.
- **What they are compared with.** The `publish` rows of the same run, which
  repeat the Stage 0 baseline within 3%: every peer is sent every entry, and
  owner-only fields gate what it reads.

| 16 peers, entries | Write, all peers: baseline | Write, all peers: anchored | Change store update | Allocations | Bytes, all peers |
|---|---|---|---|---|---|
| 1,000, 0% changing | 0.51 ms | **0.12 ms** | 0.17 ms (both) | 5,036 (both) | 368 (both) |
| 10,000, 0% changing | 5.53 ms | **1.05 ms** | 1.86–1.91 ms (both) | 50,036 (both) | 368 (both) |
| 10,000, 1% changing | 5.56 ms | **1.05 ms** | 1.90 ms (both) | 50,116 (both) | 1,352 (both) |

- **The filter cuts per-peer writes about fivefold.** At 2,400 entries over 16
  players the write is ≈ 0.25 ms, against the 1.3 ms projected without it.
- **The projection of 0.08 ms was optimistic.** Each peer still walks every live
  entity twice to skip it, so the skip is O(live × peers) with a small constant
  rather than free.
- **Bytes are unchanged.** The same state reaches the same owners.
- **The change store update and its allocations are unchanged.** They are the
  per-entity copy recorded in `docs/deferred.md` ("Replication change store
  copies every live entity each publish"). At 2,400 entries that is ≈ 0.44 ms
  and ≈ 12,000 allocations per publish, on every second tick.
- **Total publish for the RFC's 16-player population** is ≈ 0.7 ms, against ≈
  1.75 ms unfiltered.
- **Verdict:** under the 1 ms mark, so inventory replication can be called
  production-ready at the RFC's populations. The change store copy stays the
  next cost to remove, on that entry's trigger.
