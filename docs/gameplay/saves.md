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
  registered (`ZoneComponentMemory`), in id order. A capture drops component
  state recorded for an entity the zone did not author, living or destroyed,
  since no authored entity will ever claim it.
- **Sections:** named data the engine or a game registers
  (`SavedGames::RegisterSection`): a name, a version, a capture function and a
  restore function. A section in a save that nothing registered is kept and
  written back by the next save, so content a newer build wrote survives an
  older one. A registered section whose saved version is newer than the
  registered one fails the restore before the level changes, naming the
  section, since restoring the rest and writing over it later would lose it.
  Every registered section is restored on every restore, given null data when
  the save has none of it, so nothing of the game before the restore stays:
  a save written before a section existed clears what that section keeps.
  A section the restored save carried that nothing registered travels with
  that game only: it is written back by later saves until a level change that
  discards runtime state (a new game) drops it, and it is dropped as soon as a
  section registers under its name, so no save holds a name twice. The engine
  registers two (`save/EngineSaveSections.h`): `sencha.bodies` and
  `sencha.held`.
- **The id counter:** `sencha.persistent_ids`, the runtime mint's next value,
  which `SavedGames` writes itself after every section has captured, so a
  section that mints while it captures, in any order, is covered. A restore
  puts it back before the registered sections, and never lowers it, so a
  restored game never mints an id it already holds.

## Bodies

The local participant's body is kept as a patch against the prefab it spawned
from (`runtime/spawn/SceneSpawnOverlay.h`): for each component on its root, the
difference between its scene form now and the prefab's cooked record, composed
back by the operator prefabs use (`PatchFrom` is the inverse of `ComposeLayers`,
`docs/assets/composition.md`, "Patches from a difference"). A value the save did
not change follows the prefab, so a retuned prefab still applies to old saves; a
value it did change stays as saved. A keyed list whose order the body changed,
or that gained an element between others, is saved whole, so it restores in the
body's order and a retune of that list no longer reaches the save. The cooked record states every field, including those the prefab's
source leaves out (`"CharacterController": {}`): the cook writes each component
from the loaded document through its serializer (`SaveSceneJson`, from the
document cook's input snapshot), so an untouched body saves an empty patch and
a retuned default reaches old saves like any other value. The patch leaves out what a spawn gives: identity, scene instance and
placement. The body's root transform and persistent id travel beside it, and
writing a save gives a body without an id one from the runtime mint.

What a body wears is in that patch: its `EquipmentState` names each record's
source entry by persistent id, and the entries are the body's held records,
kept by `sencha.held`. Nothing a record maintains is saved; the equipment pass
derives it again (`docs/gameplay/equipment.md`, "Records").

On restore the section fills `RestoredBodies`; the local participant's next body
spawn (`ParticipantBodySpawns`) takes it: the spawn stands where the body stood,
the patch is composed over the prefab's root before import, and the root gets
its saved id once published. Every level change clears `RestoredBodies` before
the restore's sections run, and a failed change clears it again, so a body a
failed or replaced restore left behind is never given to a later spawn.

Composing the patch over a prefab retuned since the save reports what the
operator could not apply, and `ApplySpawnOverlay` sorts it in two:

- **The scene decides, and the spawn goes on** (logged as a warning): the save
  removed a keyed element the prefab no longer has (`remove_unknown_key`); the
  save added to, or removed from, a list that now only takes new entries and
  already has that one (`redeclares_inherited`, `removes_inherited`); the save
  cleared a field the prefab now requires (`cannot_clear_required`). Each is
  a retune deciding a value the save also named; the prefab's value stands.
- **The save no longer fits, and the spawn is refused**: every other
  diagnostic (`missing_key`, `invalid_key`, `key_mismatch`,
  `element_not_a_record`, `keyed_patch_not_a_list`,
  `keyed_patch_on_unkeyed_array`, and any code added later). These mean the
  component's form changed shape under the patch, so composing it would build
  a body nobody saved. The refused spawn is logged with every diagnostic, and
  the restore fails with that reason (`RestoreStatus()` is `Failed`), so the
  game decides what to offer rather than playing on with a different body.

A serializer whose `Save` fails while a body or a held record is captured is
logged and that component is left out, so it follows the prefab or is missing
from the record; a body whose prefab cannot be resolved is logged and not
saved. Grants made at run time by `ability.grant` are
part of the patch and come back as requested grants with their source, so
`ability.revoke` still takes them; tags given with
`tags.grant` are the container's baseline and come back; lasting effects do not.

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
component it carries that a serializer saves, in scene form through the same
serializers a scene uses; a serializer of a component the record does not
carry is not asked. Its `Held` names its holder by persistent id. Capturing gives a
record or a holder without a persistent id one from the runtime mint.

Restoring destroys every runtime record the index lists, the old session's, at
once (a save without this section restores it as empty, which does the same),
so none can collide with a saved id, and leaves what the new level's content
authored; then it creates each saved record in the persistent
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

A write captures only a level that stands whole: resident, with no level
change under way or one that has given every participant its body back. A
request made while a level change is pending or loading, or before its bodies
arrive, is accepted and held `Pending` until then, so a save asked for during
a transition is a save of where the transition arrives; if the change fails
the write fails with that reason. A request with nothing loaded and no change
under way is refused (`NothingLoaded`). Holding rather than refusing keeps one
answer for every caller: an autosave fired by a level's arrival, a console
`save.write` typed during a load and a station's sequence all get the save
they asked for without polling and retrying.

At shutdown, before the async lane stops, `Finish()` writes on the owner
thread a request not yet carried out, or a write task not yet started; a task
already running is joined by the lane's stop. A quit right after a save
therefore loses nothing. A write still held for a level change at quit fails
and is logged.

## Restoring

`RequestRestore(slot)`, `save.restore`, or the console command. A save this
session wrote is restored from memory, so a write that failed on disk never
sends the player back to an older save; otherwise the slot is read and decoded
on the async lane, falling back to the backup when the file is missing or
corrupt; `RestoredFromBackup()` then says so and the reason the file did not
read is logged. `Holds(slot)` answers with the same reader, so it is true when
only a readable backup is left and false for a corrupt file with none. The
save's level is then requested from the level change with the save's zone
state, and its sections are restored once the change has unloaded the old
level.

`Remembers(slot)` answers from memory alone: true once this session has
written the slot or restored it, false for a file an earlier run left on disk.
A game that starts every launch fresh asks it rather than `Holds` when deciding
whether there is a save of this session to return to; the file stays on disk
for an explicit restore, and the session's first write replaces it as usual.

`RestoreStatus()` is the one answer to "is the restore done": it moves from
`Pending` (accepted) through `Reading` (a disk read, skipped for a save this
session holds) and `Loading` (the level change, during which the sections are
put back) to `BodiesRestored`, once the level is resident and every
participant has its body again, or to `Failed`, with `RestoreFailure()` saying
why: the slot did not read, a section is newer, the level change was refused,
failed or was replaced by another before it arrived. `Restores()` counts
accepted requests, so a game holding a cover can tell a restore it has seen
through from a later one, however quickly the next goes from `Pending` to
`Loading`. A second restore is `Busy` until the first arrives or fails. A
write's outcome is `WriteStatus()` and `WriteFailure()`, apart from the
restore's.

Restore is refused inside a network session, as the level change is.

## The file

`<saves root>/<slug of App.Name>/saves/<slot>.ssav`, with the last file it
replaced that still read beside it as `.bak`: a write copies the current file
over the backup only when the current file decodes, so a corrupt file never
replaces a good backup, and the slot always has a current file while the copy
and the replace happen. Each file is replaced in one step by `WriteFileBytes`
(`core/io/FileBytes.h`): the bytes go to a staging file unique to the write,
are synced to disk, and the staging file is renamed over the old one; a rename
that fails leaves the old file as it was. The saves root is `Runtime.SavesRoot`: a desktop host
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

A reader skips a chunk it does not know and refuses any file whose hash
disagrees. A file whose format version is newer, or that holds a newer version
of a chunk this build reads, is refused as written by a newer build
(`DecodedSavedGame::Newer`): a restore of it fails with that reason rather than
falling back to an older backup, and the next write keeps it as the backup
rather than discarding it as corrupt. `test/fixtures/saves/example.v1.ssav` is format 1, checked in, and
must keep reading.

## Not yet

Recorded in `docs/deferred.md`: a body's children and other participants'
bodies and what they hold, records held by runtime entities other than the
local body (a dropped item's pickup, a spawned NPC), changed fields of authored
entities beyond what a component registers, and restoring inside a network
session.
