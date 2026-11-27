# Equipment

What a held entry does while it is worn: the slots a wearer equips into, the
records of what is equipped, and the effects, tags and grants each record
maintains. Built on inventory (`docs/gameplay/inventory.md`), whose entries are
held records (`docs/gameplay/held-records.md`), and on AbilityKit
(`docs/gameplay/abilitykit.md`).

## Installing the kit

Equipment is a kit a game installs; the engine installs it for no game. Two
calls (`equipment/EquipmentKit.h`), which install inventory's too, since a
caller cannot have one without the other:

- `InstallEquipmentVocabulary(world)` in `OnRegisterVocabulary`: inventory's
  vocabulary, the `EquipmentLayoutRegistry`, the record lists, the move
  admission (see "Moving a worn entry") and the `equipment.slot` cost provider.
  Idempotent.
- `RegisterEquipmentSystems(schedule)` in `OnRegisterSystems`:
  `InventorySystem` when the game has not registered it, and `EquipmentSystem`,
  after the ability pass and before the inventory's. Idempotent.

After the systems hook the engine binds the `equipment.*` verbs and queries only
when `EquipmentSystem` is registered, and refuses to start when the layout
registry and the system are not both present or both absent, when equipment's
systems run without inventory's, or when another layer's move admission holds
equipment's place. A game without the kit answers `Unavailable` for its verbs,
and the content stack logs one warning for the layouts it holds
(`docs/gameplay/inventory.md`, "Installing the kit").

## Layouts

An `equipment.layout` asset says which slots something has:

```json
{ "type": "equipment.layout", "version": 1,
  "data": { "slots": [ { "key": "hand.main", "accepts": "Item.Weapon", "action": "Input.Fire", "priority": 10 },
                       { "key": "ring.left", "accepts": "Item.Ring", "action": "Input.Cast", "priority": 20 },
                       { "key": "ring.right", "accepts": "Item.Ring", "action": "Input.Cast", "priority": 21 },
                       { "key": "use.x", "accepts": "Item.Usable", "action": "Input.UseX", "on_depleted": "hold" } ],
            "lock_tags": [ "State.Attacking" ] } }
```

- **`key`** names the slot. A `.` puts it in a group: `ring` names both rings.
  An item equips into a group's first free slot, or into its first slot if
  none is free. A name is a slot or a group, never both: a layout with `ring`
  and `ring.left` is refused.
- **`accepts`** is the tag an item must carry, hierarchically. With none, the
  slot accepts anything.
- **`action` and `priority`** are laid over the binding of every grant an
  equipped item gives. Two rings granting one ability bind one action, and the
  higher priority casts first.
- **`order` and `socket`** are for presentation.
- **`on_depleted`** says what happens when the entry a slot holds is spent:
  - `release` (the default) unequips it;
  - `hold` keeps the slot's record with no source until another entry of the
    same definition arrives.
- **`lock_tags`**: while the wearer holds any of these, what a person asks for
  is refused. That covers equip, unequip, and moving an equipped entry. What
  the World decides, such as a spent entry's release, is never refused.

Layouts compose like any data definition: a slot list patches by `key`.
Content declares layouts into the World's `EquipmentLayoutRegistry`, by asset
path, after items.

**Wire keys.** A layout crosses the wire as the 32-bit key of its path, as an
item does. A second path under a key another holds is refused by
`NamedDefinitionRegistry::Reserve` and reported where content declares it,
naming both (`ContentVocabulary.TwoLayoutsWhoseWireKeysCollideAreReported`).

## Item fields

An `item.definition` says how it is worn:

```json
"equip": { "occupies": [ "ring" ],
           "effects": [ { "key": "e_glow", "effect": { "data": "asset://effects/glow.sdata" }, "scale": 1 } ],
           "lock_tags": [ "Item.Cursed" ],
           "require": { "tags": { "all": [ "Class.Mage" ], "none": [ "State.Hollowed" ] } } },
"grants": [ { "key": "g_bolt", "parameters": [ { "key": "power", "value": 25 } ] } ]
```

- **`occupies`** names the slots or groups it takes, one each. An item that
  occupies nothing is never equipped.
- **`effects`** are Infinite effects maintained while it is worn; any other
  duration is refused at declaration (`equip_effect_not_maintained`). An
  effect's `scale_by` names a number property of the item
  (`docs/gameplay/inventory.md`, "Properties") its `scale` is multiplied by
  when the definition resolves, so a piece of armour states its weight once
  and an effect adding it to an `EquipLoad` attribute while worn follows it,
  through a reload too. Naming a property the item does not state as a number
  refuses the definition.
- **`lock_tags`** lock the one record whose source entry holds one (see
  "Locks"). An item with `equip.lock_tags` or `instance_tags` gives each entry
  a tag container.
- **`require.tags`** is a tag query on the wearer (`all`, `any`, `none`, each
  name matching the tag or one beneath it), the same `GameplayTagQueryNames`
  abilities, sequences and reaction profiles hold. A wearer with no tag
  container holds no tags. A name the tag registry lacks refuses the
  definition. It is admission only: `Equip` asks it, so `Cycle` and native
  callers honour it, while the equipment pass, a derivation, a restore and an
  authored record never do. A record worn when the wearer later loses the tag,
  or when content retunes the requirement, stays worn; a game that wants the
  item to stop working puts the condition on what the item grants.
- **`grants`** give parameters to the grants its equip effects give, by grant
  key; naming a grant no equip effect gives is refused.

## Records

`EquipmentState` is what a wearer equips into and what it has equipped: its
layout, and one record for each logical equip. A two-handed sword is one record
holding two slots. The row holds the layout, the count and the next key, and
names its records in the World's `EquipmentRecordLists`
(`docs/ecs/row-lists.md`), so a wearer is stored by what it wears. Code reads
them with `EquipmentRecordsOf` and changes them through `Equipment`, or with
`EditEquipmentRecords`, `WearEquipmentRecord`, `RemoveEquipmentRecord` and
`SetEquipment`; scenes, snapshots and Kyusu hold them by value as an
`EquipmentRecordList`. Wearing a record ends every record holding one of its
slots and appends it under the next key, by one rule (`WearRecord`) that an
equip and a Kyusu edit both use; `FindEquipmentRecordHolding` answers which
record holds a slot, and `ItemTakesSlotFirst` whether an item's first slot can
be that one.

| Field | What it is |
|---|---|
| `Key` | Unique on its wearer and never reused; the maintained effects are owned by it. |
| `Definition` | The item. |
| `Slots`, `Primary` | The layout slots it holds, as a mask, and the slot its first `occupies` name took. |
| `Source` | The entry it came from, as a `PersistableRef`; none while a `hold` slot waits. |
| `Derivation` | Where its derivation stands (see "Derivation"). Never saved or sent. |

The layout and the records are one component because a record's slots are
indices into that layout: they load, save and replicate together.

**Why the relation stays on the wearer.** The record list is the one place the
worn relation lives; it was not moved onto the item entry, for three reasons
the code gives:
- A `hold` slot's record outlives its entry (the entry is spent and destroyed,
  the record waits), and an authored, loaded or replicated record exists before
  its source resolves. Neither has an entry to live on, so the wearer would
  keep a second form of the same relation for them.
- Entries are sent to their owner alone, and everyone who sees a wearer is
  sent what it wears. A wearer-side public summary would have to carry each
  record's key, definition, slots and primary slot, which is the record
  without its source.
- Every reader (slot lookup, slot resolution, the wire, the save, Kyusu, the
  rebind fingerprint) would then join two or three forms. One list, stored by
  its length, removes the fixed cost that prompted the move.

- **Relations persist; contributions are derived.** A record is saved and
  replicated. The effects it maintains, and their tags and grants, never are:
  they are derived from the record again.
- **Scene form.** The layout and definitions are named by asset path, a
  record's slots by key with the primary first, and its source by the
  persistent id the entry carries when it is written:

  ```json
  "EquipmentState": { "layout": "asset://equipment/humanoid.sdata", "next_key": 4,
                      "records": [ { "key": 3, "definition": "asset://items/ring.sdata",
                                     "slots": [ "ring.left" ], "source": "00000000000c4e57" } ] }
  ```

  A record naming a slot the layout lacks, a slot another record holds, no
  slot, a reused key, or a layout the World does not define, refuses the
  scene. A placed prefab's record sources are renamed with its members
  (`docs/editor/scene-composition.md`).
- **Saves.** A body's `EquipmentState` is part of its prefab patch
  (`sencha.bodies`, `docs/gameplay/saves.md`), and the entries its records name
  are held records of the body (`sencha.held`). After a restore the body's
  records are `Pending`, their sources resolve by id once the entries are back,
  and the next pass derives them: nothing derived is in the file. A record
  whose source names an id no entity carries yet, and that it has never
  resolved this session, waits for it with nothing derived, however many
  passes run first; once its source resolves the pass notes the entity and
  derives the record again, and only an entry it has seen and that is then
  destroyed ends it
  (`EquipmentGateTest.WhatABodyWearsSurvivesASaveIntoAFreshWorldAndIsAttributable`).
  A zone NPC's equipment follows its zone's state: its authored record comes
  back with the zone, and a change made at run time is kept only as far as the
  zone record keeps the entities involved.
- **Wire.** The records are a replicated list (`docs/plans/networking.md`
  6.2) of at most `kEquipmentRecordCapacity`. Everyone is sent the layout, the
  count and each record's key, definition, slots and primary slot. Only the
  owner is sent the sources, which are a run of each record of their own. A
  change to one record resends that record. Removing a record moves the last
  into its place, which resends that one record and the count. Record order
  means nothing: records are found by slot, source or key.
- **Seed size.** A snapshot may span datagrams (`docs/plans/networking.md`), so
  a pawn wearing all 32 records reaches its owner in one snapshot
  (`docs/plans/evidence/actor-caps/results.md`).

## Equipping

`Equipment` (`equipment/Equipment.h`) changes what a wearer wears. It runs on
the authority, on the owner thread, outside any query, and takes the tick it
runs in.

| Operation | What it does |
|---|---|
| `Equip(wearer, entry, slot?)` | Puts the entry in the slots its item occupies, replacing what holds them. |
| `Unequip(wearer, slot)` | Ends the record holding the slot. |
| `UnequipRecord(wearer, record)` | Ends the record of that key, as `Equip` returned it, without naming a slot. |
| `Cycle(wearer, group)` | Equips into the group's first slot the next entry the wearer holds, however deep, whose first `occupies` name covers the slot and which it accepts. A candidate the equip refuses is passed for the next; it wraps, and unequips when nothing equips. |

**The check.** An equip is checked whole before anything changes, against the
state after the replacement. It is refused, with nothing changed, when:

| Refusal | When |
|---|---|
| `Missing` | the wearer, its layout, the entry or its definition is gone, or the entry is spent |
| `NotPermitted` | the wearer may not use the entry: inventory's `MayUseItem` says no |
| `NotEquippable` | the item occupies nothing |
| `AlreadyEquipped` | the entry is already worn |
| `NoSlot`, `NotAccepted` | a slot is not found, or does not accept the item |
| `Locked` | the wearer holds a lock tag, or a record it would replace is locked |
| `Unmet` | the wearer's tags do not meet the item's `equip.require` (`EquipRequirementMet`) |
| `NotMaintained`, `Undeclared` | an equip effect is not Infinite, or content never declared the entries it would be worn with (also asserts in a debug build: see "Worn grant entries") |
| `MissingAttribute` | an equip effect modifies an attribute the wearer does not have |
| `GrantRoom` | the grants would not fit the wearer's grant table once what it replaces has gone |
| `TagRoom` | the tags its effects grant would not fit the wearer's tag container once what it replaces has gone (a new tag past the capacity, or a tag already at 0xFFFF stacks, by `CanHoldAll`), or the wearer has none |

**One rule for "may use".** Equip, the pass's holder rule, cycle's candidates
and the slot cost all ask `MayUseItem(world, wearer, entry)`
(`docs/gameplay/inventory.md`, "Who may use an entry"); equipment never walks
holders itself to decide it.

**Slot choice.** For each `occupies` name, the item takes the slot asked for
(first name only), else the first free slot, else the first
(`ResolveEquipmentSlots`).

**The apply.** The apply has no refusal path:
1. it ends what the replaced records maintained;
2. it writes the record;
3. it applies each equip effect, owned by the record's source entry
   (`EquipmentRecordOwner`).

**Ownership.** Because the source entry owns the effects, the grants they give
name the entry as their `source`:
- a weapon's fire pays from its own `Clip`;
- `HasLiveActivationFrom` sees what the entry supplied;
- the effects end with the entry.

A record with no source maintains nothing.

**Worn grants.** An equip effect's grants are worn as its slot and item say. The
slot's binding is laid over each grant entry's, and the item's `grants`
parameters over the entry's. The instance grants those entries in place of its
definition's (`AbilityEffectGrants::SetInstance`), so two rings granting one
bolt bind one action at two priorities.

## Capacities

| Limit | Value | When it is reached |
|---|---|---|
| Slots in a layout | `kMaxEquipmentSlots` (32): a record's slots are a 32-bit mask | the layout is refused when it is compiled |
| Records on a wearer | `kEquipmentRecordCapacity`, which is `kMaxEquipmentSlots`: a record holds at least one slot and no two hold the same one | with every slot taken, `Equip` swaps (it takes the first slot its item names); there is no refusal for a full list. A scene whose records name no slot, share one, or number more than 32 refuses to load. |
| Grants on a wearer | `kAbilityGrantCapacity` (64), counting what is reserved for effects not yet granted | `Equip` answers `GrantRoom`; a record derived at load or reload goes `RefusedForRoom`, traced once, and is derived when there is room |
| Tags on a wearer | `GameplayTagContainer::Capacity` (32) | `Equip` answers `TagRoom`; at a derivation the effect layer counts and reports the tags the container refuses (`docs/gameplay/abilitykit.md`) |

Each has a test at its boundary in `test/runtime/EquipmentTests.cpp`
(`AWearerWearsARecordInEverySlotALayoutCanHaveAndTheNextEquipSwaps` for
records). Neither records nor grants are a fixed row on the wearer or a fixed
image on the wire: both are lists stored by their length and sent by it, so
the wire follows `kEquipmentRecordCapacity` and `kAbilityGrantCapacity` rather
than bounding them (`AbilityReplication.AWearersImageIsSizedByWhatItWears`,
`AnActorsGrantImageIsSizedByWhatItHolds`).

## Worn grant entries

A worn entry is a grant entry like any other: it crosses the wire by its
content's key, and a client resolves it in its own `AbilityGrantEntries`. So
every machine must hold it, not only the authority that equipped the item.

- **Declared with content.** `DeclareWornGrantEntries`
  (`equipment/WornGrantEntries.h`) interns, for each item, each layout and each
  slot the item's first `occupies` name resolves to and whose `accepts` it
  meets, what its equip effects grant worn there. `DeclareContentVocabulary`
  calls it once items, layouts and effect grants are declared, and
  `RefreshContentVocabulary` again each frame (a no-op unless one of them
  moved). Every runtime, editor and preview World receives content that way,
  on every machine.
- **Looked up, never interned, at equip.** `Equip` and the derivation find the
  declared entries (`AbilityGrantEntries::IndexOf`). The authority never
  interns one lazily: that would be an entry its clients cannot resolve. Code
  that defines items outside content (a test fixture) calls the declare
  function itself; an equip of an undeclared entry is refused `Undeclared` and
  asserts in debug.
- **Bounded by content.** Entry identity is its content, so items without
  parameters worn in one slot share an entry: the count is bounded by distinct
  grant, slot binding and parameter sets, not by items. A collision (two entries
  of one ability with one wire key) or a full table is reported at declaration,
  naming the item, the slot and the layout.
- **Parameters are definition-level.** Only an item's `grants.<key>.parameters`
  take part, so they are enumerable from content. A value of one instance never
  goes into an entry: an ability reads it from its `source` (the worn entry's
  own attributes), which every machine already has.

**Reconcile.** `ReconcileEquipment` derives every record of a wearer again (see
"Derivation"). It is idempotent.

## Derivation

A record's effects, and their tags and grants, are derived from what it reads:
its item definition, its layout, and each equip effect's definition and grant
list. Its `Derivation` says where that stands, and only the equipment pass
moves it:

| State | Meaning | Leaves when |
|---|---|---|
| `Pending` | Loaded, restored, authored, replicated, rebound, or something it reads moved | the next pass derives it |
| `Maintained` | Exactly its derivation's effects are applied | a definition it reads moves (→ `Pending`) |
| `RefusedForRoom` | Its derivation does not fit the wearer's grant table | the table has room for it (a cheap check each pass) |
| `RefusedByContent` | An equip effect is not Infinite, an entry was never declared, or the item or layout is gone | a definition moves (→ `Pending`) |

- **Every input.** When the item, layout or effect registries, or the effect
  grant lists, move, every record goes back to `Pending`. That is reload-time
  work, and deriving is idempotent.
- **Whole.** A derivation is checked before anything changes: content first,
  then the grant room the whole new set needs, as though every instance the
  record has now had ended. Only then are the old effects removed and the new
  applied. A refusal touches nothing, so a record is never left with some old
  effects and some new.
- **Instances are kept only while current.** An instance of the right effect
  and scale is kept, its grants worn again. One whose definition moved since it
  applied is replaced (`IsEffectInstanceCurrent`): it baked in that
  definition's tags and timing, though its modifiers fold from the definition
  as it is.
- **Representable or not.** A record whose slots its item can no longer take
  (a slot gone or no longer accepting it, its `occupies` changed) ends. A
  record whose content is wrong stays worn and inactive, so a typo during a
  reload never becomes a lost equip a save would keep.
- **Refusals are traced** once, as a record becomes refused, as `AbilityTrace`
  `GrantRefused` on the wearer (`Capacity` for room, `Undefined` for content).

**Layouts are redefined through equipment.** `RedefineEquipmentLayout`, which
content declaration calls, keeps every live record on the slots it held by
key, so reordering a layout moves nothing, and ends a record whose slots went.

## The equipment pass

`EquipmentSystem` runs on the authority after the ability pass and before the
inventory's, so it sees an entry a commit spent before the reaper destroys it.
For every wearer with records:
- **A record whose wearer may no longer use its source ends** (`MayUseItem`).
  This covers a source dropped, moved (with a bag that carries it, too), merged
  into another stack, or destroyed.
- **A spent source** settles once nothing it supplied still runs, so the last
  bomb finishes its throw. Then, under `release`, the record ends. Under
  `hold`, the record stays with no source and maintains nothing.
- **A waiting `hold` record** takes the first unspent entry of its definition
  the wearer holds, however deep (`HeldEntries`: a bag's contents follow the
  bag), that nothing else wears, and is derived again from it. The wearer is
  walked only when what it holds or its records moved since its last walk: the
  held-record index's revision for the wearer, which every change beneath it
  moves (`HeldRecordIndex::Revision`), and a fingerprint of its records' keys
  and sources (an entry another record let go is a candidate with no inventory
  change at all). Another holder's changes never walk it
  (`EquipmentRebindWalks` counts).
- **A record that is `Pending`**, or refused for room once there is room, is
  derived (see "Derivation").

Its per-tick work is bounded by records: a still wearer costs a few reads per
record, and no allocation. On the profile preset, a wearer of 32 records and
64 grants costs the equipment pass 1.5 µs a tick and the ability pass 4.0 µs;
1,000 wearers of four records cost 18% less than on the sixteen-record row, and
the first pass after 200 wearers' records arrive costs 22% more (once). Method
and tables: `docs/plans/evidence/actor-caps/results.md`; the composition
branch's figures are in `docs/plans/evidence/equipment-overlay/results.md`.

## Locks

Two kinds, never confused:
- **The wearer's** (`EquipmentWearerLocked`): while the wearer holds one of its
  layout's `lock_tags` ("while attacking"), nothing it wears changes at a
  person's asking.
- **A record's own** (`EquipmentRecordLocked`): while the record's source entry
  itself holds one of its item's `equip.lock_tags` ("this ring is cursed"),
  that record alone stays. The tag is on the instance, so two cursed rings are
  locked by their own curses, and lifting one frees only that ring. An entry
  comes to hold one by being authored with it, by a lasting effect laid on it
  (lifted by removing the effect), by `tags.grant` (lifted by `tags.revoke`),
  or from its item's `instance_tags`, which a new entry starts with as its own
  baseline (`stack.max` must then be 1).

Locks refuse what a person asks for: `Equip` (of the slots a locked record
holds), `Unequip`, `UnequipRecord`, `Cycle`, and a requested move of a worn entry. They never
hold back what the World decides: the pass's endings and derivations, and a
**forced** change (`ItemChangeClass::Forced`, the `force` argument of
`equipment.unequip`, `inventory.transfer` and `inventory.drop`), such as a
death's loot. Forcing skips the locks and nothing else: authority, cycles,
what a container accepts and its room are checked all the same.

## Moving a worn entry

Equipment installs inventory's `ItemMoveAdmission` (`InstallItemMoveAdmission`).
The World holds one: an installer finds another layer's admission there and
leaves it, answering false, and the engine refuses to start a game whose
equipment does not hold it (`HoldsEquipmentMoveAdmission`). The admission:
- **Asked before** a requested transfer, take or drop: it refuses (`Withheld`)
  a move of a worn entry, or of one holding a worn entry, whose record is
  locked.
- **Told after** any move: it ends what the move took from the former holder,
  in the same transaction.

## Vocabulary

Authored content reaches equipment through these names, declared with the
engine's own (`equipment/EquipmentVocabulary.h`):

| Name | Arguments | Does |
|---|---|---|
| `equipment.equip` | `wearer`, `entry`, `slot` (optional) | `Equip` |
| `equipment.unequip` | `wearer`, `slot`, `force` | `Unequip` |
| `equipment.cycle` | `wearer`, `group` | `Cycle` |
| `equipment.in_slot` (query, entity) | `wearer`, `slot` | The entry a slot holds; unavailable when it holds none |
| `equipment.sources` (query, entities) | `wearer`, `tag` | Every worn entry whose item carries the tag |

A verb refused for any equipment reason answers `Refused` with the reason's
code, which every `EquipmentRefusal` value but `None` declares by name, so a
response's log line, the console and the vocabulary reference read it as
`refused: tag_room` (`docs/gameplay/authored-api.md`, "What a verb answers").
Native code compares `VerbInvocationResult::Detail` with the enum, or reads its
name through `EnumToString`.

**The `equipment.slot` cost provider.** It spends units from the entry a slot
holds: `{ "slot": "use.x", "count": 1 }`. It owns no resource: it spends
through inventory's `inventory.item` provider, translating each row to the entry
the slot holds, so an item row and a slot row naming one entry in one commit are
checked as one spend, and the item provider's `MayUseItem` check is the only
one. A slot that holds nothing, or holds a sourceless record, cannot pay.

## Authoring

In Kyusu an `EquipmentState` shows its layout and one row per slot
(`EquipmentStateEditorAdapter`). A slot's row is an entity reference
(`editor/ARCHITECTURE.md`, `authoring/`) whose picker offers the identified
entries the wearer holds, by the document's `Held` components, whose item's
first `occupies` name covers that slot and which the slot accepts; clearing it
empties the slot. Picking one writes the record an equip would leave
(`ResolveEquipmentSlots`), replacing what held its slots; it is one undoable
edit. A worn entry the wearer no longer holds reads as such. The layout changes only while
nothing is worn, since records name slots of the layout they were made in. The
editor derives nothing: an authored record is `Pending` until a running World's
pass derives it.

## Not yet

Recorded in `docs/deferred.md`: more than thirty-two slots in a layout, tracing equipped grants that tie on an action, deriving a zone's equipped
records as it loads, one-shot effects when an item is equipped, a second layer
that takes part in moving an entry, owner-only fields a former owner keeps,
predicted equips ("Predicting gives and transfers"), authoring a waiting
record in Kyusu, attribute bounds in an equip requirement, a requirement on a
layout slot, and asking whether an equip would be admitted without doing it.

A verb to lift a seeded curse is not among them: `tags.revoke` takes a
baseline stack from an entry, which is what `instance_tags` seeds.
