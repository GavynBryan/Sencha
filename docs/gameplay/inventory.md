# Inventory

What an actor holds: entries in containers, taken from pickups, moved, dropped,
used and spent. An entry is a held record (`docs/gameplay/held-records.md`):
its container is its holder, so entries are listed, ordered, saved, replicated,
moved between partitions and cleaned up by the held-record machinery, and the
inventory adds only what a possession is. Equipment, what a held entry does
while worn, builds on this (`docs/gameplay/equipment.md`).

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
  holds from one unit to `stack.max` (`ItemStackHolds`); a scene or save naming
  more, or none, is refused at load, since an empty entry is spent and never
  authored. Every change to a count goes through `SetItemCount`, which queues
  an entry that reaches zero for the inventory pass. Two entries merge only when they have the
  same definition, `stack.max` above 1, and both still hold units.
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
| `Transfer(entry, into, count, change?)` | Moves `count` units (0 is all), open stacks first. Part of a stack moves as a new entry and the rest keeps its identity; a stack merged away entirely is spent. |
| `Drop(entry, at, partition, change?)` | Takes the entry into a new pickup at `at`. |
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
| `inventory.take` | `pickup`, `into` | `Take` |
| `inventory.give` | `into`, `definition`, `count` (1) | `Give` |
| `inventory.transfer` | `entry`, `into`, `count` (0: all), `force` | `Transfer` |
| `inventory.drop` | `entry`, `at`, `force` | `Drop`, into its holder's zone |
| `inventory.use` | `entry`, `user` (its holder) | `Use` |
| `inventory.count` (query, int) | `container`, `definition?`, `tag?`, `within` (false) | Units held, spent entries not counted, of that definition and carrying that tag; `within` counts what entries hold too |

A response's condition gates on `inventory.count` with `require`
(`{ "min": 3 }` for "holds at least three"). A verb refused for any inventory
reason answers `Refused`.

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
while the chest's zone is away it waits in the persistent partition, and a save
keeps it (`docs/gameplay/held-records.md`, "Records of authored holders").

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
scene and its fate when its zone unloads, predicted gives and transfers,
weight, volume and grid rules, change events, possessions that outlive a body,
and shared containers.
