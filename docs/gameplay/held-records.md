# Held records

A held record is an entity owned by another entity, its holder, or by the world.
A counter is one (`docs/gameplay/counters.md`); an inventory entry is the next.
The relation is one component and one index, so everything that is "a keyed
record owned by a holder" is saved, replicated, ordered and cleaned up the same
way.

| Piece | Where | What it does |
|---|---|---|
| `Held` | `world/held/Held.h` | the relation on the record: its holder and its position |
| `HeldRecordIndex` | `world/held/HeldRecordIndex.h` | holder to records in order, record to holder, a revision per holder |
| `HoldRecord`, `SettleHeldRecords`, `DestroyHeldRecords` | `world/held/HeldRecords.h` | the World operations |
| `sencha.held` | `save/HeldRecordSaves.h` | the save section (`docs/gameplay/saves.md`) |

## The relation

`Held { PersistableRef Holder; uint16 Position; }`. The holder is a
`PersistableRef`, so a record names its holder by persistent id in a scene or a
save and by entity at run time. An invalid holder is the world. Records under
one holder are listed by position, then by the order they were listed in; a
record that does not care about order (a counter) takes position 0 and keeps
the order it was held in.

`HoldRecord(world, record, holder, position?)` is how the authority puts a
record under a holder: with no position it goes after the holder's last record.
When the last position (65535) is taken, further records share it and keep the
order they were held in; `HeldRecordIndex::PositionsExhausted()` counts each
time that happens. Positions never compact.

## The index

The index is kept by hooks only, never by a pass that walks records:

- `Held`'s `OnAdd` lists the record under the holder its reference resolves to.
  A reference to a persistent id nobody carries yet is *awaited*: the record is
  listed under that id and under no holder, not under the world.
- `Held`'s `OnRemove` unlists it. A replaced `Held` (a holder change, or a value
  arriving from the wire) lists the new holder before the old value is
  unlisted; each list is matched by one unlist, so the two hooks balance.
- `PersistentIdComponent`'s `OnAdd` moves records awaiting that id under the
  entity that now carries it; its `OnRemove` notes that a holder may have gone.

A holder is therefore always an identified entity on the authority:
`HoldRecord` and the settle give it a persistent id. A save names it by that
id, and its identity leaving is how the index learns it was destroyed.

Each holder has a revision that moves when a record is listed or unlisted
beneath it, or when `NoteChanged(record)` says a record beneath it changed (a
counter's value); it moves for every holder up a nested chain, at most
`kHeldDeepestNesting` (64) deep. A view of a holder's records keeps the
revision it was built from and rebuilds when it moves.

## Settling

A hook cannot change ECS structure, so what follows from a hook waits for
`SettleHeldRecords`, which `RuntimeWorld` runs at the end of residency
processing every frame, before partition moves are dispatched. On the
authority it:

- destroys what a holder that is gone held, and what those records held in
  turn (a holder whose identity left but which is still alive keeps its
  records);
- settles each record listed since the last call: it gets a persistent id and
  `NetReplicated`; a record of an entity is anchored to it for relevance
  (`NetRelevanceAnchor`) and moved into its storage partition; a record of the
  world loses any anchor and lives in the persistent partition; an awaited
  record is anchored to nothing, so it reaches no peer until its holder
  arrives. `HoldRecord` settles its record at once.

Elsewhere the queues are let go: a client's records are destroyed and moved by
replication.

Records follow their holder across storage partitions through
`PartitionFollowers`: when a holder moves, what it holds moves with it, and
what those hold on the next round.

## Replication

`Held` travels through a codec: the holder as a wire entity (zero for the world
or a holder the peer is not sent), and the position. Applying a snapshot
overwrites a hooked component the way `ReplaceComponent` does
(`WorldComponentSchema::SetComponentBytes`), so a client's index follows a
holder change. Both `Held` and the record's own components are
authority-materialized: a client never imports them from a scene.

A record is relevant where its holder is: a body's records reach the body's
owner only, the world's reach every peer. A client lists a record under the
world until its holder's entity reaches it.

## Saves

What a save keeps is a closure from roots: the world and the local
participant's body, the records listed under them, and what those records hold,
to the nesting bound. `docs/gameplay/saves.md`, "Held records".
