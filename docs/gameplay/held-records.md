# Held records

A held record is an entity owned by another entity, its holder, or by the world.
A counter is one (`docs/gameplay/counters.md`); an inventory entry is another
(`docs/gameplay/inventory.md`), and an entry that is a container holds records
of its own.
The relation is one component and one index, so everything that is "a keyed
record owned by a holder" is saved, replicated, ordered and cleaned up the same
way.

| Piece | Where | What it does |
|---|---|---|
| `Held` | `world/held/Held.h` | the relation on the record: its holder and its position |
| `HeldRecordIndex` | `world/held/HeldRecordIndex.h` | holder to records in order, record to holder, a revision per holder |
| `PlaceHeldRecord`, `HoldRecord`, `NoteHeldRecordChanged`, `SettleHeldRecords`, `ParkHeldRecords`, `DestroyHeldRecords` | `world/held/HeldRecords.h` | the World operations |
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
A new record is made with `PlaceHeldRecord(world, holder, values, position?)`,
which builds it already settled (its `Held`, a runtime persistent id,
`NetReplicated`, its anchor and its holder's storage partition) in one
placement, leaving the value columns the caller names to be filled with
`World::InitializeComponent`; counters and inventory entries are made this way.
When the holder's last record takes the last position (65535), the next
append first renumbers the holder's records 0..n-1 in their order, so rolling
pick-up-and-use never runs out. Only a holder that holds more records than
there are positions shares the last one: further records keep the order they
were held in, and `HeldRecordIndex::PositionsExhausted()` counts each time.

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
beneath it, or when `NoteHeldRecordChanged(world, record)` says a record
beneath it changed (a counter's value, an entry's count); it moves for every
holder up a nested chain, at most `kHeldDeepestNesting` (64) deep. A view of a
holder's records keeps the revision it was built from and rebuilds when it
moves. Revisions are stamps from one counter the index keeps, so a value is
never reused; a holder that holds nothing has revision 0 and the index keeps
nothing for it, whether it is alive or gone, on the authority or a client.

`Held` is written in place in two places only, both in `HeldRecords.cpp`, and
neither changes which entity holds a record or the order of its holder's
records, so the index stays true: `NameHeldHolder(world, record)` names the
record's listed holder by its persistent id, giving the holder one, and the
renumbering above writes each record the position the index gave it. Every
other change to a record's holder or position replaces `Held`, which runs the
hooks.

A record may also be keyed: a kind (its value component's type) and a name.
The value component's own hooks call `Key` and `Unkey`, independent of `Held`,
and `FindKeyed(holder, key)` answers the record of that key listed under that
holder, never one waiting for a holder. Counters key by their name.

Records nest at most `kHeldDeepestNesting` levels: `Depth(entity)` counts the
records from a record up to the first holder that is not one, and
`Height(entity)` the levels beneath. A user that nests records (inventory)
refuses to place one past the bound; walks up or down stop there.

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

## Authored records

A record a scene authored carries an authored persistent id, which its zone
uses to tell what it made from what is gone. Once the record is no longer what
the scene authored, it must not keep that id, or the zone would make the
authored one again beside it (after a rest, or the zone streaming back in):

- `HoldRecord` moving a record to another holder, and `NoteHeldRecordChanged`,
  give a record carrying an authored id a runtime one from the mint, and so
  for what it holds, whose `Held` follows the new id;
- moving to another holder also takes the record and what it holds out of the
  spawn group they were placed with, so a guard despawning does not take a
  purse a thief now holds.

The zone then records the authored id destroyed at its next capture, and the
record is a runtime record from then on.

## Records of authored holders

A runtime record held by an authored entity (a deposit in a placed chest) is
part of how the zone differs from its content, and is kept with it:

- **Detach.** Before a zone's partition is destroyed, `ParkHeldRecords` lists
  each runtime record held by an authored holder there as awaiting that
  holder's id and moves it to the persistent partition; what it holds follows
  it. A record the zone authored is not parked: the zone makes it again. A
  holder its zone forgets at detach (`ZoneStateLifetime` until detach) keeps
  nothing.
- **While away**, a parked record is anchored to nothing, reaching no peer,
  and does no work.
- **Import.** The holder's identity arriving lists its parked records under it
  again; they settle into its partition and its relevance.
- **Saves** keep parked records and runtime records of resident authored
  holders (`docs/gameplay/saves.md`, "Held records").
- **Level change.** A parked record belongs to its zone's memory, so a level
  change, which installs new zone state, destroys every record still waiting
  (`DestroyAwaitingHeldRecords`); a restore brings back the saved ones.

The zone state store itself is unchanged: a parked record is a live entity, not
a byte record, so it needs no codec and the save file format is the same.

## Authoring

In Kyusu, a `Held` component's holder is a choice among the document's
identified entities, never the record itself or what it holds, or the world;
its position is a number. A placed prefab's members name each other by that
placement's ids: `Held`'s schema declares its holder an entity reference, so
composition renames it (`docs/editor/scene-composition.md`).

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

What a save keeps is a closure from roots: the world, the local participant's
body and every authored holder, the runtime records listed under them and those
waiting for a holder, and what those records hold, to the nesting bound.
Restoring destroys every runtime record first; what content authored stays.
`docs/gameplay/saves.md`, "Held records".
