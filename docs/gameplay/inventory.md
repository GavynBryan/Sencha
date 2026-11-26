# Inventory

What an actor holds: entries in containers, taken from pickups, moved, dropped,
used and spent. An entry is a held record (`docs/gameplay/held-records.md`):
its container is its holder, so entries are listed, ordered, saved, replicated,
moved between partitions and cleaned up by the held-record machinery, and the
inventory adds only what a possession is. Equipment, what a held entry does
while worn, builds on this (`docs/gameplay/equipment.md`).

## Installing the kit

Inventory is a kit a game installs, as it does movement; the engine installs it
for no game. Two calls (`inventory/InventoryKit.h`):

- `InstallInventoryVocabulary(world)` in `OnRegisterVocabulary`: the
  `ItemRegistry` content declares items into, and the `inventory.item` cost
  provider. Idempotent.
- `RegisterInventorySystems(schedule)` in `OnRegisterSystems`: `InventorySystem`,
  after the ability pass. Idempotent.

A game that installs equipment calls equipment's two instead, which install
these (`docs/gameplay/equipment.md`, "Installing the kit"). After the systems
hook the engine binds the `inventory.*` verbs and query only when
`InventorySystem` is registered, and refuses to start when the registry and the
system are not both present or both absent, naming the missing call. A game
without the kit pays nothing in its World or its tick: the names stay declared
in every build (the component schema, the verb and query catalogs and the data
type registry are build-wide, so a scene or a save naming `ItemEntry` stays
readable), and a verb answers `Unavailable` ("no implementation"), as physics
verbs do in a game without physics. Item definitions content holds for a World
without the kit are not declared; the content stack logs one warning for them
when it publishes ("12 item.definition assets, and this game installs no
inventory", `UninstalledKitContent`). Editors install every kit
(`InstallEveryKitVocabulary`), so Kyusu and the Ability Workbench read any
project's items.

## Item definitions

An `item.definition` asset says what a kind of item is:

```json
{ "type": "item.definition", "version": 1,
  "data": { "name": "Red Potion", "tags": [ "Item.Usable" ], "stack": { "max": 10 },
            "keep": true,
            "take": { "effects": [ { "key": "t_heal", "effect": { "data": "asset://effects/heal_10.sdata" } } ] },
            "use": { "ability": { "data": "asset://abilities/drink.sdata" },
                     "parameters": [ { "key": "power", "value": 25 } ] } } }
```

- **`stack.max`** is how many units one entry holds. With 1, every unit is its
  own entry with state of its own. An entry authored, saved or edited in Kyusu
  holds from one unit to `stack.max` (`ItemStackHolds`). Kyusu refuses an edit
  outside that range. A scene or save naming more, or none, still loads the
  entry with its count clamped into the range (`ItemStackClamp`) and logs a
  warning naming the entry, its item, the count it named and the count it got:
  a save made before content lowered `stack.max` keeps a full entry instead of
  failing the whole load, and a count of none becomes one, since an empty
  entry is spent and never authored.
  Every change to a count goes through `SetItemCount`, which queues an entry
  that reaches zero for the inventory pass. Two entries merge only when they
  have the same definition, `stack.max` above 1, and both still hold units.
- **`keep: false`** means taking it applies its `take.effects` (Instant
  effects), scaled by the pickup's count, and keeps nothing: ammunition that
  becomes an attribute.
- **`attributes`** seed each new entry's own `AttributeSet` (its charges, which
  effect costs targeting the `source` spend), and **`instance_tags`** its own
  baseline tags. Both are an entry's own state, so either needs `stack.max` 1: a
  split or a merge would mix one entry's state with another's. An entry holds
  at most 16 attributes and 32 tags; a definition naming more is refused.
- **`use`** is the ability `inventory.use` runs, with the entry as its source,
  through a transient grant under the key `use`.
- **`equip`** and **`grants`** say how it is worn
  (`docs/gameplay/equipment.md`, "Item fields"). An item with
  `equip.lock_tags` gives each entry a tag container, as `instance_tags` does.

Definitions compose like any data definition (`extends`, `includes`,
`abstract`). Content declares them into the World's `ItemRegistry` after
abilities, by asset path. An entry read before its definition is declared
keeps the name.

**Wire keys.** A definition crosses the wire as the 32-bit FNV-1a key of its
path. `NamedDefinitionRegistry` refuses a second path whose key another path
already holds, and content declaration reports it, naming both paths
(`ContentVocabulary.TwoItemsWhoseWireKeysCollideAreReported`). Abilities, which
share the registry, are reported the same way.

## Components

| Component | What it is | Where it lives |
|---|---|---|
| `ItemEntry { Definition, Count }` | One possession, on an entity whose `Held` names its container and position. `Count` 0 is depleted. | Authority-materialized; replicated by codec to the peer its holder makes relevant |
| `ItemContainer { Capacity, Accepts }` | What may go in (a tag, hierarchically) and how many entries | Authority only |
| `ItemPickup { Definition, Count }` | Something in the world to take: the entries it holds, or an authored count of a definition | Authority only |

- **Passive entries.** An entry is an entity per possession, never per unit:
  no transform, body or renderer, and no work each tick.
- **Nested containers.** An entry that carries `ItemContainer` holds entries of
  its own: a bag, a bottle. What it holds follows it wherever it goes.
- **Dropped entries.** A dropped entry is held by its pickup. Only taking the
  pickup moves it; a transfer or drop of an entry lying in a pickup is refused.
- **Authority only.** Only the simulation authority creates entries. A client
  imports none, not even a chest's contents, and is sent the entries relevant
  to it.

**Scene form.** Definitions are named by asset path and tags by name; the
container is the entry's `Held`:

```json
"ItemEntry":     { "definition": "asset://items/arrow.sdata", "count": 20 }
"Held":          { "holder": "00000000000c4e57", "position": 4 }
"ItemContainer": { "capacity": 6, "accepts": "Item.Wearable" }
"ItemPickup":    { "definition": "asset://items/arrow.sdata", "count": 12 }
```

## Operations

`Inventory` (`inventory/Inventory.h`) changes inventories in one tick, which
an effect it applies is timed in. Every operation runs
on the simulation authority alone (`NotAuthority` elsewhere), on the owner
thread outside any query, and checks everything before it changes anything, so
a refusal leaves the World as it was.

| Operation | What it does |
|---|---|
| `Give(container, definition, count)` | Fills open stacks in container order, then makes entries of up to `stack.max` units; all or none. |
| `Take(pickup, into)` | Moves one entry the pickup holds into `into`, or gives what the pickup counts. A counted pickup applies its definition's `take.effects` to `into` scaled by the units it counts (a box of 25 shells is one effect at 25 times its magnitude), and with `keep: false` keeps nothing. An entry lying in a pickup was taken once already, so taking it back applies nothing. The pickup goes once it holds nothing. |
| `Transfer(entry, into, count, change?)` | Moves `count` units (0 is all), open stacks first. Part of a stack moves as a new entry and the rest keeps its identity; a stack merged away entirely is spent. A whole entry moved into the container already holding it takes no new room and keeps its place, so it succeeds in a full container; part of a stack moved there is a new entry and still needs room. |
| `Drop(entry, at, partition, change?)` | Takes the entry into a new pickup at `at`, in `partition`. |
| `Use(user, entry)` | Asks `user` to run the definition's use in the next ability pass. |

**Refusals.** `Missing` (something named is gone or is not what the operation
needs), `NotAccepted` (the container does not accept it, or it would go inside
itself), `ContainerFull`, `TooDeep`, `NotPermitted` and `Withheld`.

**Requested or forced.** A transfer or drop is `ItemChangeClass::Requested`
unless the World decides it (`Forced`, a verb's `force`). Only a requested one
is asked of `ItemMoveAdmission`, the World resource through which a layer above
(equipment's worn lock) refuses what a person asks for and never what the World
decides (`docs/gameplay/equipment.md`, "Moving a worn entry"). After any move
it is told who held the entry, which is how a moved entry is unequipped.

**Moving an entry** is `HoldRecord`: its holder, position, relevance anchor and
storage partition change together, and what it holds follows it.

## Searching a container

One filter serves every walk over what a container holds (`inventory/Inventory.h`):
`ItemFilter { Definition, Tag, Within }` matches an entry of that item (any when
invalid), whose item carries the tag or one beneath it (any when invalid), and
with `Within` looks inside what the container's entries hold too, however deep.

- `ForEachItemEntry(world, container, filter, visit)` visits matching unspent
  entries in container order, a nested entry's contents right after it, until
  the visitor returns false. A spent entry is passed over; what it holds is
  still walked.
- `FindItemEntry(world, container, filter, pick)` answers the first match, or
  the last with `ItemEntryPick::Last`, or an invalid id.
- `CountItemUnits(world, container, filter)` sums the matches' counts.

`inventory.count`, `inventory.entry` and the `inventory.item` cost provider's
container row all read through it. It is a search: "last" means the last in the
container's order, never the entry most recently given. Today a give appends, so
the two agree, but placing an entry at a position (`docs/deferred.md`, "Placing
an entry at a position") would part them. Content that must act on the entry a
give or take made reads that verb's result instead (`docs/gameplay/responses.md`,
"Results"), which is exact: given a second sword, the sequence equips the one it
was given and not the first.

## Who may use an entry

`MayUseItem(world, actor, item)` is the one rule a use, an item cost and
equipment (equip, cycle, the pass that ends a record, and `equipment.slot`
through the item cost) ask. Today an actor may use an entry when the actor is the first
holder above it that is not itself an entry (`ItemHolder`): what a bag holds is
its holder's. A container several actors share, such as a party's bag, is a
change to that function alone (`docs/deferred.md`, "Shared containers").

## Capacities

| Limit | Value | When it is reached |
|---|---|---|
| Entries in a container | its `Capacity`; 0 means `kItemContainerMostEntries` (65535) | `ContainerFull`, and nothing changes. A `Give` that would make more entries than that is refused before it makes any. |
| Position under a holder | 16 bits | held records': the holder's records are renumbered in order when the last position is taken; only past 65535 records do later entries share it, counted by `HeldRecordIndex::PositionsExhausted()` |
| Nesting | `kHeldDeepestNesting` (64) levels of records | `TooDeep`: an entry and everything it holds must lie within the bound |
| An entry's own state | 16 attributes, 32 tags | the definition is refused at declaration |

## Vocabulary

| Name | Arguments | Does |
|---|---|---|
| `inventory.take` | `pickup`, `into` | `Take`; answers the entry it went into, absent for an item that is not kept |
| `inventory.give` | `into`, `definition`, `count` (1) | `Give`; answers the entry the units went into |
| `inventory.transfer` | `entry`, `into`, `count` (0: all), `force` | `Transfer` |
| `inventory.drop` | `entry`, `at`, `force`, `keep` (true) | `Drop`, into the zone where it lands; a kept pickup stays with that zone |
| `inventory.use` | `entry`, `user` (its holder) | `Use` |
| `inventory.count` (query, int) | `container`, `definition?`, `tag?`, `within` (false) | Units held, spent entries not counted, of that definition and carrying that tag; `within` counts what entries hold too |
| `inventory.entry` (query, entity) | `container`, `definition?`, `tag?`, `within` (false), `last` (false) | The first matching unspent entry in container order, or the last with `last` (`FindItemEntry`); unavailable when none matches, which fails the command reading it |

A response's condition gates on `inventory.count` with `require`
(`{ "min": 3 }` for "holds at least three"). A verb refused for any inventory
reason answers `Refused` with the reason's code, which every `InventoryRefusal`
value but `None` declares by name, so a response's log line, the console and the
vocabulary reference read it as `refused: container_full`
(`docs/gameplay/authored-api.md`, "What a verb answers"). Native code compares
`VerbInvocationResult::Detail` with the enum, or reads its name through
`EnumToString`. A sequence that takes or gives an item and then acts on it
names the verb's result (`docs/gameplay/responses.md`, "Results"): "pick it up
and raise it" is a take `as` an entry, then an equip of that entry. A command
that acts on an entry found by what it is ("select weapon 3", "drink a potion
from the pack") reads `inventory.entry` as its argument, asked when the command
runs; an ability's `query` op stores the same answer in a local.

## Spending, depletion and the inventory pass

- **The `inventory.item` cost provider** spends units in an ability's commit,
  in one transaction with its other costs (`docs/gameplay/abilitykit.md`,
  "Costs"):
  - `{ "entry": e, "count": n }` spends from `e`;
  - `{ "container": c, "count": n }` spends from the first entry `c` holds that
    still has units, which is how a bottle's contents are drunk;
  - only from what the paying actor may use (`MayUseItem`);
  - rows are summed per entry, so two groups each taking an arrow check for two.
- **Depletion.** Spending the last unit leaves the entry at `Count` 0. It is
  never refilled (a give makes a new entry beside it), and the commit does not
  destroy it.
- **`InventorySystem`** runs after the ability pass, on the authority. It
  destroys a depleted entry once `HasLiveActivationFrom(spender, entry)` says
  nothing the spender runs still uses it, so the arrow just fired finishes
  first; a stack merged away goes at once. Its work is the depleted list,
  nothing else.

## Authored entries and zones

An entry authored in a zone keeps its authored id only while it is what the
zone authored. Moving it to another holder, or changing its count, gives it a
runtime id (`docs/gameplay/held-records.md`, "Authored records"), so the zone
records the authored one gone and never makes it again beside the one the
player carries.

What is put into an authored chest is a runtime record of an authored holder:
while the chest's zone is away it is bytes in the zone's record in a game that
keeps level state, and a live entity waiting in the persistent partition in one
that does not; a save keeps it either way (`docs/gameplay/held-records.md`,
"Records of authored holders").

**Where a drop lands.** `inventory.drop` puts its pickup in the zone at `at`,
not in the partition of whoever held the entry: the play zone of a scene
level; in a world, the innermost resident zone whose manifest bounds hold the
point, else the focus zone (`LoadedLevel::ZoneAt`, answered to the verb through
the `ZonePlacement` World resource). With no level loaded (an editor preview, a
test world) it lands beside its holder. So a drop by a body in the persistent
partition goes with its zone when the level is left and never follows the
player into the next one. With `keep` (the default) the pickup is marked
`KeptWithZone`: it stays where it fell across its zone's unload and in saves,
with its entry and count, until it is taken (`docs/gameplay/saves.md`, "What a
zone gained"). With `keep: false` it is a passing thing, destroyed with its
zone and its entry with it.

## Replication

- **Who is sent an entry.** An entry is anchored to its holder, so it reaches
  only the peer that owns the root of that chain (`docs/plans/networking.md`,
  per-entity relevance): a player's items reach that player alone, a bottle's
  contents reach the bottle's holder, an unopened chest's contents and a
  dropped entry reach nobody.
- **Codec.** `ItemEntryWire` carries the definition by wire key and the count;
  `Held` carries the holder and position.
- **Measured** on the composition branch: 10,000 entries over 16 peers write in
  1.05 ms, against 5.5 ms with no filter
  (`docs/plans/evidence/entry-entities/results.md`).

## Not yet

Recorded in `docs/deferred.md`: item display data, a dropped item's world
scene, predicted gives and transfers,
weight, volume and grid rules, change events, possessions that outlive a body,
and shared containers.
