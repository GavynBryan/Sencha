# Saves

A save is how the world differs from its authored content, keyed by persistent
identity. Writing one captures that difference; restoring one loads the saved
level again through the level change and puts the difference back. A game that
saves at a rest point and then restores it at once gets a world whose enemies
are back and whose opened doors and taken pickups stay as they were, because
what a save keeps is decided per entity by its zone state lifetime
(`docs/plans/runtime-stable-identity.md`, "Zone state lifetime").

| Piece | Where | What it does |
|---|---|---|
| `SavedGame` | `save/SavedGame.h` | the plain data: summary, zones, sections |
| `EncodeSavedGame` / `DecodeSavedGame` | same | bytes and back, no file access |
| `SaveSlotFiles` | `save/SaveSlotFiles.h` | where a slot's bytes live, and its backup |
| `SavedGames` | `save/SavedGames.h` | requests, capture, restore, sections |
| `save.write`, `save.restore` | `save/SavedGameVocabulary.h` | the verbs and console commands |

## What a save holds

- **Summary:** the level (`LevelReference`), a world's focus zone, and when it
  was written.
- **Zones:** for each zone, and for a world's world scene, the authored entities
  that are gone and never forgotten, and the component state their owners
  registered (`ZoneComponentMemory`), in id order.
- **Sections:** named data the engine or a game registers
  (`SavedGames::RegisterSection`): a name, a version, a capture function and a
  restore function. A section in a save that nothing registered is kept and
  written back by the next save, so content a newer build wrote survives an
  older one. The engine registers three (`save/EngineSaveSections.h`), in this
  order: `sencha.bodies`, `sencha.held` and `sencha.persistent_ids`.

## Bodies

The local participant's body is kept as a patch against the prefab it spawned
from (`runtime/spawn/SceneSpawnOverlay.h`): for each component on its root, the
difference between its scene form now and the prefab's cooked record, composed
back by the operator prefabs use (`core/composition/CompositionPatch.h` is the
inverse of `ComposeLayers`). A value the save did not change follows the prefab,
so a retuned prefab still applies to old saves; a value it did change stays as
saved. The patch leaves out what a spawn gives: identity, scene instance and
placement. The body's root transform and persistent id travel beside it, and
writing a save gives a body without an id one from the runtime mint.

What a body wears is in that patch: its `EquipmentState` names each record's
source entry by persistent id, and the entries are the body's held records,
kept by `sencha.held`. Nothing a record maintains is saved; the equipment pass
derives it again (`docs/gameplay/equipment.md`, "Records").

On restore the section fills `RestoredBodies`; the local participant's next body
spawn (`ParticipantBodySpawns`) takes it: the spawn stands where the body stood,
the patch is composed over the prefab's root before import, and the root gets
its saved id once published. Grants made at run time by `ability.grant` are
part of the patch and come back as authored grants; tags given with
`tags.grant` are the container's baseline and come back; lasting effects do not.
`sencha.persistent_ids` keeps the mint's counter, so a restored game never
mints an id it already holds.

## Held records

`sencha.held` (`save/HeldRecordSaves.h`) keeps what is saved beyond bodies: a
closure from roots. The roots are the world, the local participant's body and
every authored holder (a placed chest); the members are the records made at run
time that the held-record index lists under a root or as waiting for a holder
whose zone is away, then what those records hold, to the nesting bound
(`docs/gameplay/held-records.md`). Counters and inventory are kept this way. A
record content authored is not saved, since its zone makes it again; a record
that stopped being what content authored took a runtime id when it changed.

Each record is kept whole, not as a patch: its persistent id, and every
component a serializer saves, in scene form through the same serializers a
scene uses. Its `Held` names its holder by persistent id. Capturing gives a
record or a holder without a persistent id one from the runtime mint, which is
why the section is captured before `sencha.persistent_ids`.

Restoring destroys every runtime record the index lists, the old session's, at
once, so none can collide with a saved id, and leaves what the new level's
content authored; then it creates each saved record in the persistent
partition with its id and its components. A record of the world is listed at
once. A record of a body or of an authored holder waits for an entity with its
holder's persistent id: the restored body, or the chest when its zone imports,
takes that id, and its records move under it, follow it into its partition and
become relevant to its owner. A saved record that does not load whole, such as
a counter naming a tag the content no longer declares, is not restored and is
reported.

## Writing

`Engine::Saves().RequestWrite(slot)`, the `save.write` verb or the console
command of the same name. The request is carried out at the zone-residency
phase: every resident zone and the world scene are captured as an unload would
capture them (`LoadedLevel::CaptureState`), the store's durable part is
exported, and the sections are captured. The result is kept in memory for the
session, then encoded and written on the async lane. `WriteStatus()` moves
from `Pending` through `Working` to `Done` or `Failed`.

## Restoring

`RequestRestore(slot)`, `save.restore`, or the console command. A save this
session wrote is restored from memory, so a write that failed on disk never
sends the player back to an older save; otherwise the slot is read and decoded
on the async lane, falling back to the backup when the file is missing or
corrupt. The save's level is then requested from the level change with the
save's zone state, and its sections are restored once the change has unloaded
the old level. `RestoreChange()` names that level change, so a game holds its
cover up until `LevelChanges().Status()` reaches `BodiesRestored` for it.

Restore is refused inside a network session, as the level change is.

## The file

`<saves root>/<slug of App.Name>/saves/<slot>.ssav`, with the file it replaced
beside it as `.bak`. The saves root is `Runtime.SavesRoot`: a desktop host
sets it to the platform's data directory (`UserDataDirectory()`, under
`sencha`), `--saves <dir|none>` overrides it, and a headless host, a tool or a
test leaves it empty, which keeps saves in memory only.

The bytes are a core `BinaryHeader` (`SSAV`, format 1), versioned chunks and a
final content hash (`core/serialization/HashedChunkFile.h`, shared with the
navigation file):

| Chunk | Version | Payload |
|---|---|---|
| `SUMM` | 1 | JSON: `level {kind, name}`, `focus`, `saved_at` |
| `ZONE` | 1 | binary: zones, each with destroyed ids and component records |
| `SECT` | 1 | JSON: `[{name, version, data}]` |
| `HASH` | 1 | 64-bit hash of everything before it |

A reader skips a chunk it does not know or a chunk version newer than its own,
refuses a file whose format version is newer, and refuses any file whose hash
disagrees. `test/fixtures/saves/example.v1.ssav` is format 1, checked in, and
must keep reading.

## Not yet

Recorded in `docs/deferred.md`: a body's children and other participants'
bodies and what they hold, records held by runtime entities other than the
local body (a dropped item's pickup, a spawned NPC), changed fields of authored
entities beyond what a component registers, and restoring inside a network
session.
