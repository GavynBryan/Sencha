# Counters

A counter is a named integer that level data sets, adds to and tests: a boss
flag, the stars collected in each course, a quest's stage, a kill count, a
choice the player made. Unset is zero.

| Piece | Where | What it does |
|---|---|---|
| `Counter` | `logic/Counter.h` | the name (a gameplay tag) and the value, on one record entity |
| `counter.set`, `counter.add`, `counter.value`, `counter.changed` | `logic/CounterVocabulary.h` | the verbs, the query and the event |
| `WriteCounter`, `CounterValue`, `FindCounter` | same | the same operations for native code |

## What a counter is

One small entity per counter that is not zero, carrying `Counter { name, value }`
and `Held` naming its holder (`docs/gameplay/held-records.md`): the world, or an
entity, usually a participant's body for per-player progress. Setting a counter
to zero, or adding it down to zero, destroys its entity. A counter is therefore
saved, replicated, ordered and cleaned up as a held record, with nothing of its
own.

The name is a gameplay tag, so names are declared in content and picked from a
list, and a typo is a load error rather than a second counter. A counter's
value is a 32-bit signed integer.

### Why not an existing store

- **Tags.** A tag container holds 32 tags, and a condition sees only whether a
  tag is there. A star count, a stage number or a kill count is not presence.
- **Attributes.** An attribute set holds 16 attributes, belongs to an actor with
  effects folded over it, and responses cannot read one.
- **Zone state.** It remembers what happened to an authored entity in one zone;
  progress is not about one entity and outlives every zone.

## Authoring

Verbs run on the simulation authority only; a client is refused.

```json
{"verb": "counter.set", "arguments": {"name": {"tag": "boss.warden.defeated"}, "value": {"const": 1}}}
```

A door that opens once the warden is dead answers its use only when the flag is
set, through a numeric condition (`docs/gameplay/responses.md`, "Query
conditions"):

```json
{"on": "interact.use", "sequence": {
  "conditions": [{"query": "counter.value",
                  "arguments": {"name": {"tag": "boss.warden.defeated"}},
                  "require": {"min": 1}}],
  "commands": [{"verb": "responses.enable", "arguments": {"entity": {"port": "self"}, "binding": {"const": "open"}}}]}}
```

Stars per course are one counter each, named under the course:
`star.course3.red_coins`, `star.course3.boss`. A collectible adds one to its
course's counter with `counter.add` (the amount defaults to 1); a gate tests
`counter.value` with `require {min: 7}`.

A quest's stage is one counter set as the quest moves on: `counter.set
{name: quest.lighthouse.stage, value: 3}`. A response that should run only at
stage 3 requires `{min: 3, max: 3}`.

Per-player progress names a holder:
`counter.add {name: kills.wolves, holder: {port: "instigator"}}` counts against
the player who struck, `counter.value {name: kills.wolves, holder: ...}` reads
it. Without a holder a counter is the world's, shared by every player. An
invalid holder is the world's too.

## Native code

`CounterValue(world, name, holder)` reads one; `FindCounter` gives its entity.
`counter.changed {name, value, holder}` is published to the World's
`AuthoredEventDispatcher` whenever a verb changes a counter, with the holder as
its source, so a HUD can subscribe to every counter or to one body's. A value
that did not change announces nothing; unsetting announces zero. A client is
not sent the event: it reads `counter.value`, or watches its holder's revision
in `HeldRecordIndex`.

## Limits

- **Range.** A value beyond a 32-bit signed integer is held at the bound it
  passed. Each write held at a bound is counted in the World's
  `CounterSaturation`, and the first at each bound is logged; later ones are
  counted, not logged.
- **Lookup.** Finding a counter walks its holder's records: linear in what that
  holder holds, which a verb pays once.
- **Saves.** The world's counters and the local participant's body's are saved
  (`docs/gameplay/saves.md`, "Held records"). A counter held by any other
  entity lives as long as that entity and is not saved.
- **Unknown names.** A saved counter whose tag this content no longer declares
  is not restored, with a warning.

## Replication

A world counter reaches every peer; a body's counter reaches the body's owner
alone. Both follow from the held record's relevance. The name travels by its
tag's wire key.

## Not yet

Recorded in `docs/deferred.md`: totals under a name, clearing many at once,
an entity reacting to a counter changing elsewhere, journals and objectives,
counters that outlive a save slot, and what a death without a save keeps.
