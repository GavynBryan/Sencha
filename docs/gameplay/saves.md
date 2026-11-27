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
  older one.

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

Recorded in `docs/deferred.md`: entities created at run time (a body's state,
items, anything dropped or deposited in a zone), changed fields of authored
entities beyond what a component registers, and restoring inside a network
session.
