# EquipmentState on the wire

**Scenario.** `AbilityReplication.AShippingPawnsSeedWithEquipment_*` in
`test/runtime/AbilityReplicationTests.cpp`:
- a moving pawn with nine grants, four attributes and two tags, as
  `AShippingPawnsOwnerSeedFitsOneSnapshot` builds it;
- N entries in its container;
- with (`worn`) or without (`bare`) an `EquipmentState` wearing all N, every
  source set.

One first snapshot under `kNetDefaultSnapshotBytes` (four datagrams), to the
owner or to another peer.

**Method.** `BytesWritten` and `EntitiesUnsendable` of that snapshot, Debug
build, on this branch (entries are held records, so each carries `Held`).

| Records | Peer | Bare | Worn | Unsendable when worn |
|---|---|---|---|---|
| 0 | owner | 990 B | 999 B | 0 |
| 4 | owner | 1069 B | 1174 B | 0 |
| 16 | owner | 1318 B | 1711 B | 0 |
| 0 | observer | 252 B | 261 B | 0 |
| 4 | observer | 252 B | 325 B | 0 |
| 16 | observer | 252 B | 517 B | 0 |

- A pawn wearing all 16 records reaches its owner, with its 16 entries, in one
  two-datagram snapshot.
- An observer's record costs about 16 B; an owner's about 26 B with its source.
- One record changing resends one record: each is a run of its own.

**Before snapshots spanned datagrams** (measured on the composition branch,
where a snapshot was one datagram of 1173 B): the same pawn's owner seed was
1117 B with no equipment, so an `EquipmentState` of any size left the pawn
unsendable to its owner. A first seed also sent every owner-only field even at
its default (128 B of empty sources). Both were fixed in the channel and the
change store rather than in equipment's codec.
