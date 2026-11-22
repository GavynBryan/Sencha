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
| `SavedGame` | `save/SavedGame.h` | the plain data: summary, levels, sections |
| `EncodedZoneLists` | `zone/ZoneStateLists.h` | one zone's named lists exactly as the file holds them |
| `EncodeSavedGame` / `DecodeSavedGame` | same | bytes and back, no file access |
| `SaveSlotFiles` | `save/SaveSlotFiles.h` | where a slot's bytes live, and its backup |
| `SavedGames` | `save/SavedGames.h` | requests, capture, restore, sections |
| `save.write`, `save.restore` | `save/SavedGameVocabulary.h` | the verbs and console commands |
| `save.describe` | same | a developer command: what a save holds of one entity |

## What a save holds

- **Summary:** the level (`LevelReference`), a world's focus zone, when it was
  written, and `game`: a JSON object the game supplies and the engine never
  reads (an area name, a play time, a display name for the slot), absent when
  the game supplies none.
- **Levels:** for each level the game has changed this session, per zone (and
  per world scene, under `kWorldSceneZone`), a set of named lists:
  `destroyed`, the authored entities gone for good; `changed`, the patches of
  authored entities changed for good ("Level entities"); `held`, the runtime
  records whose holder is an entity of that zone, resident or waiting ("Held
  records"). Only what is never forgotten is written; `at_restore` state stays
  in the session and `at_detach` state goes when its zone unloads. A list this
  build does not read is carried and written back.
- **Sections:** named data the engine or a game registers
  (`SavedGames::RegisterSection`): a name, a version, a capture function and a
  restore function. Capture is given a `const World&`: it reads, and gives
  nothing an id ("Writing"). A section in a save that nothing registered is kept and
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
  which `SavedGames` writes itself after every section has captured. A restore
  puts it back before the registered sections, and never lowers it, so a
  restored game never mints an id it already holds.

## What a save holds of an entity

Each component declares whether a save holds it
(`IComponentSerializer::Persistence()`, `core/metadata/ComponentPersistence.h`):

| Persistence | Meaning | Declared by |
|---|---|---|
| `Authored` | content says it; a save never holds it on an entity content made | the default |
| `Saved` | a save holds how it differs from content | `SENCHA_SAVED` on the component |
| `Identity` | what the entity is known by; whoever makes the entity gives it | a `ComponentPersistenceOf` specialization |

The engine's `Identity` components are `PersistentIdComponent` and
`SceneInstance`. Its `Saved` ones are `GameplayTagContainer` (the baseline),
`AttributeSet` (the bases), `AbilityGrants` (the lasting kinds),
`EquipmentState`, `Responses`, `Counter`, `ItemEntry`, `ItemContainer`,
`ItemPickup`, `Held` and `LookOrientation`. Everything else is `Authored`,
including `LocalTransform`, `CharacterMovement` (so a body saved on a ladder
comes back in the default mode), meshes, colliders and lights. Which part of
a `Saved` component is durable stays inside its serializer: its scene form is
its saved form.

Saved is opt-in, like `SENCHA_FIELD` and `SENCHA_REPLICATED`, so session state
stays out of saves without a list; the cost is that a game component nobody
annotated is silently not saved. A hand-written serializer derived from
`TypedComponentSerializer` reads the annotation off its component; one that
is not overrides `Persistence()`.

The declaration is read in three places:

- **A body** and **a level entity** (entities content made) keep a patch of
  their `Saved` components only.
- **A held record** (an entity nothing in content made) keeps every component
  it carries that has a serializer, except `Identity` ones, whose values
  travel beside it.
- **A spawn** strips `Identity` components from a prefab
  (`SmapPackageOptions::StripAuthoredIdentity`).

The console command `save.describe <me | index:generation | persistent id>`
(`DescribeSavedEntity`, `save/EngineSaveSections.h`) says which of these an
entity is and lists each component it carries with its persistence and what a
save does with it. It is a developer command, not a verb.

## The entity patch

An entity in scene form is one record from component key to component value.
`world/serialization/EntityPatch.h` writes it and diffs it, and every save path
uses it; no caller loops over an entity's components itself.

- `WriteEntityRecord` writes the components in an `EntityScope` that the entity
  carries: those the scope lists by type, and those declaring a persistence
  the scope names. It is told its purpose, `SceneRecordPurpose::Authoring` or
  `Save`, and sets it on the `SceneSerializationContext` while it writes, so a
  serializer can leave out what belongs to the other form.
- `AuthoredComponentsOf` reads a package entity's baseline: content's record
  of each component in scope, with a null record for one content holds as
  runtime bytes, and an entry marked not in content for a listed type content
  lacks.
- `CaptureEntityPatch` is `PatchFrom` (`docs/assets/composition.md`, "Patches
  from a difference") from the baseline to the entity, over the registry's
  `EntitySchema()`, which joins every serializer's `CompositionSchema` under
  its key. The baseline list decides what the patch speaks about: a component
  it lists, and a component the entity carries that it does not list when the
  component declares a persistence in `alsoCarried`. Nothing else is written,
  so a baseline of two components costs two `Save` calls. A component whose
  written form equals its baseline is left out before the patch is built, and a
  serializer that can tell without writing says so through
  `IComponentSerializer::SavedFormEquals` (the responses serializer answers from
  its bits), which is what keeps a 500-entity zone's capture under half a
  millisecond (`docs/plans/evidence/level-state/results.md`).
- `ApplyEntityPatch` composes a patch over one package entity before it is
  built, any entity of the package: a member patches a component content has,
  adds one it lacks, and `null` removes one. It reports what it set aside in
  three lists: `Retuned` (the scene decides, below), `Misfit` (the patch no
  longer fits the component's form; content's record is left as it was) and
  `Unknown` (a member naming a component this build has no serializer for;
  nothing is done). What each list means for the entity is the caller's rule.

The rules every caller gets:

- **Member order.** A record's and a patch's members are sorted by component
  key, a removal's `null` among them. The order is one total order that does
  not depend on which serializer registered first, so two builds write the
  same bytes. Readers compose by member name, so a patch written in another
  order reads the same.
- **A `Saved` component content did not author** is diffed against its
  default record: what its serializer writes for a default-constructed value
  (`IComponentSerializer::SaveDefault`). One at its default writes nothing; one
  that differs writes only the difference, which a restore composes over the
  default. A generated serializer has a default record; it is written with no
  World, asset system or package member, so capture and a restore on a worker
  agree. A hand-written serializer writes one by overriding `SaveDefault`,
  and does so only where a component at its default holds nothing an absent
  one does not: `GameplayTags` (no tags), `Attributes` (no attributes),
  `AbilityGrants` (no grants) and `responses` (no bindings). A component
  whose default is not a value of its own has none: a tag component (its
  presence is its value), `Counter` (its name is required),
  `EquipmentState` (its layout is required), `ItemEntry` (its item is
  required), and `ItemContainer` and `ItemPickup` (a container or a bare
  pickup at its defaults is still one, and writing nothing would lose it on
  restore). Such a component is written whole, as an `Authored` component
  content did not author always is.
- **A component content holds as runtime bytes** has no record to diff. In
  scope, it is written whole while the entity carries it and `null` once it no
  longer does, so a removal at run time stays removed; a restore sets the
  member over it whole. It is therefore saved in full on every save, and a
  retune of it does not reach an old save. Cooked scenes hold records, so only
  packages built in code have such components.
- **The carry.** `CaptureEntityPatch` copies from a `kept` patch every member
  whose key no serializer claims, so data a newer build wrote survives an
  older one.
- **A patched component that does not decode follows content.**
  `ApplyEntityPatch` keeps content's form of each component it replaces in
  `PackageEntity::Unpatched`; when the patched record fails to decode at
  import (a saved name the content no longer declares, such as a deleted
  tag), the importer decodes content's form instead and logs a warning naming
  the component. A component the patch added has no content form to fall back
  to, so it still fails the import.

## Bodies

The local participant's body is kept as an entity patch against the prefab it
spawned from (`runtime/spawn/SceneSpawnOverlay.h`; see "The entity patch"): its
baseline is the prefab root's `Saved` components, and a `Saved` component it
gained is written as its difference from its default. A value the save did
not change follows the prefab, so a retuned prefab still applies to old saves; a
value it did change stays as saved. A keyed list whose order the body changed,
or that gained an element between others, is saved whole, so it restores in the
body's order and a retune of that list no longer reaches the save. The cooked record states every field, including those the prefab's
source leaves out (`"CharacterController": {}`): the cook writes each component
from the loaded document through its serializer (`SaveSceneJson`, from the
document cook's input snapshot), so an untouched body saves an empty patch and
a retuned default reaches old saves like any other value. The patch holds the body's `Saved` components only ("What a save holds of an
entity"), so it never holds identity, scene instance or placement, and a
component a body lost is a removal only when it is `Saved`. The body's root
transform and persistent id travel beside it. A body has an id only once
something references it by persistent id: a `PersistableRef` is made through
`MakePersistableRef`, which gives the body one from the runtime mint, and a
record the body holds names it that way. A body nothing references is saved
without an id and restored without one.

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
operator could not apply, and `ApplyEntityPatch` sorts it in two:

- **The scene decides, and the spawn goes on** (logged as a warning): the save
  removed a keyed element the prefab no longer has (`remove_unknown_key`); the
  save added to, or removed from, a list that now only takes new entries and
  already has that one (`redeclares_inherited`, `removes_inherited`); the save
  cleared a field the prefab now requires (`cannot_clear_required`). Each is
  a retune deciding a value the save also named; the prefab's value stands.
  Beside these, a patched component that does not decode follows content
  ("The entity patch"): the save named something the content no longer
  declares, and the prefab's record of that component stands.
- **The save no longer fits, and the spawn is refused**: every other
  diagnostic (`missing_key`, `invalid_key`, `key_mismatch`,
  `element_not_a_record`, `keyed_patch_not_a_list`,
  `keyed_patch_on_unkeyed_array`, and any code added later). These mean the
  component's form changed shape under the patch, so composing it would build
  a body nobody saved. The refused spawn is logged with every diagnostic, and
  the restore fails with that reason (`RestoreStatus()` is `Failed`), so the
  game decides what to offer rather than playing on with a different body. A
  patch member naming a component this build does not have refuses the spawn
  the same way.

A serializer whose `Save` fails while a body or a held record is captured is
logged and that component is left out, so it follows the prefab or is missing
from the record; a body whose prefab cannot be resolved is logged and not
saved. Grants made at run time by `ability.grant` are
part of the patch and come back as requested grants with their source, so
`ability.revoke` still takes them; tags given with
`tags.grant` are the container's baseline and come back; lasting effects do not.

## Level entities

A level entity is kept the way a body is, as an entity patch against what the
level authored, when the game asks (`SavedGames::KeepLevelState()`, which the
fps template and Loss Function call): its baseline is the cooked records of its
`Saved` components, recorded at import, and the patch is taken whenever its zone
is captured, at detach and for a save, on the authority only
(`docs/plans/runtime-stable-identity.md`, "Zone state memory"). An entity with
no `ZoneStateLifetime` keeps its patch for good, as it keeps its destruction: a
door's granted tag, a guard's attribute bases, a run-time ability grant, a spent
response. A patch that no longer fits a retuned level does not refuse the zone,
which has to load: what a retune set aside is a warning and the scene decides; a
member that no longer fits follows content, is logged as an error naming the
entity and component, and leaves the store. A game that does not keep level
state keeps destroyed sets only.

## Held records

`sencha.held` (`save/HeldRecordSaves.h`) keeps what is saved beyond bodies and
levels: a closure from roots. The roots are the world and the local
participant's body; the members are the records made at run time that the
held-record index lists under a root or as waiting for a holder that is no
level entity, then what those records hold, to the nesting bound
(`docs/gameplay/held-records.md`). What a level entity holds (a placed chest's
contents), resident or waiting for its zone, is the same closure from that
holder, written in the `held` list of the holder's level and zone
(`CaptureLevelHeldRecords`); on restore each record waits for its holder under
that level and zone, and a record of a level other than the restored one is set
aside until that level is entered. Counters and inventory are kept this way. A
record content authored is not saved, since its zone makes it again; a record
that stopped being what content authored took a runtime id when it changed.

Each record is kept whole, not as a patch: its persistent id, and its entity
record (`WriteEntityRecord` for `Authored` and `Saved` components, so every
component it carries that a serializer saves except `Identity` ones, in key
order), in scene form through the same serializers a scene uses; a serializer
of a component the record does not carry is not asked. Its `Held` names its holder by persistent id. A record takes its id
where it is made (`PlaceHeldRecord`, which counters and inventory entries go
through) or when it settles, and the settle names its holder, giving the
holder an id if it has none; capture gives neither. A write settles held
records before it captures ("Writing"), so a record held in the same tick is
saved; one still without an id after that is logged and left out.

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
phase, in three steps. Held records are settled (`SettleHeldRecords`): the one
structural change a write makes, which gives a record held since the frame's
last settle its id and names its holder, and destroys the records of a holder
that went. Every resident zone and the world scene are captured as an unload
would capture them (`LoadedLevel::CaptureState`) and the store's durable part
is exported, which copies the encoded blob of every zone it holds rather than
the zones' contents ("The file"). Then the sections are captured, reading only:
a write changes no entity, no archetype and not the id mint beyond what the
settle does. The result is encoded and written on the async lane, and held in
memory only until it is on disk. `WriteStatus()` moves from `Pending` through
`Working` to `Done` or `Failed`.

`RequestWrite(slot, arrival)` and `save.write`'s optional `arrival` name where
the saved body comes back; the body's record holds the name beside its
transform. Nothing resolves it yet: the body comes back where it stood, and the
name is read back and carried (`RestoredBody::Arrival`). Resolving it at the
level's landmark of that name lands with the level-change stage.

A write captures only a level that stands whole: resident, with no level
change under way or one that has given every participant its body back. A
request made while a level change is pending or loading, or before its bodies
arrive, is accepted and held `Pending` until then, so a save asked for during
a transition is a save of where the transition arrives; if the change fails
the write fails with that reason. A request with nothing loaded and no change
under way is refused (`NothingLoaded`).

A write needs the local participant's body. When a local participant exists
and has no body on a settled level (it died, or its body was released and not
yet given back), the write fails with "the local participant has no body" and
writes nothing, so the slot keeps its last good save, in memory and on disk.
During a level change the request waits as above, for the bodies the change
gives back. A world with no local participant, such as a dedicated host,
saves without a body. Holding rather than refusing keeps one
answer for every caller: an autosave fired by a level's arrival, a console
`save.write` typed during a load and a station's sequence all get the save
they asked for without polling and retrying.

At shutdown, before the async lane stops, `Finish()` writes on the owner
thread a request not yet carried out, or a write task not yet started; a task
already running is joined by the lane's stop. A quit right after a save
therefore loses nothing. A write still held for a level change at quit fails
and is logged.

## Restoring

`RequestRestore(slot)`, `save.restore`, or the console command. A save whose
disk write is in flight or failed is restored from memory, so a failed write
never sends the player back to an older file; it is the one save `SavedGames`
holds, and a successful write releases it. With no saves root it is the last
save written, which is all a session without files keeps. Otherwise the slot is
read and decoded
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

The bytes are a core `BinaryHeader` (`SSAV`, format 2), versioned chunks in a
stated order, and a final content hash (`core/serialization/HashedChunkFile.h`,
shared with the navigation file):

| Chunk | Version | Payload |
|---|---|---|
| `SUMM` | 1 | JSON: `level {kind, name}`, `focus`, `saved_at`, `game`; always first |
| `THMB` | 1 | reserved for a thumbnail (width, height, encoding name, bytes); optional |
| `HEAD` | 1 | 64-bit hash of every byte before it |
| `LVLS` | 1 | binary: levels, each a record of zones, each a record of named lists |
| `SECT` | 1 | JSON: `[{name, version, data}]` |
| `HASH` | 1 | 64-bit hash of everything before it |

The order is part of the format: a known chunk after one it should precede, a
first chunk other than `SUMM`, or a missing `HEAD` refuses the file. A reader
may stop after `HEAD`: `DecodeSavedGameSummary` reads the summary from the
file's prefix and checks it against `HEAD`, so a listing never reads the
levels; `HASH` still decides whether a file restores. This build writes no
`THMB` of its own and writes back one it read. A chunk this build does not know
is carried as read and written back where it was, before `HEAD` or after
`SECT`, and a restore carries it into the next save, as an unclaimed section is.

`LVLS` is a count of levels, then each level as a length-prefixed record (its
kind, its name, a count of zones), each zone a length-prefixed record (its id,
a count of lists), each list a name, a list version and a length-prefixed
payload. No record refers to bytes outside itself, so an unchanged zone's
bytes are copied from one save into the next without decoding: the zone store
holds a zone that is not loaded as that encoded blob (`EncodedZoneLists`) and
parses it only when the zone imports, and a write copies the pointer. A zone
holding `at_restore` state stays parsed while it is away, since that state is
not in the blob. The known lists, at version 1:

| List | Payload |
|---|---|
| `destroyed` | count, then each id |
| `changed` | count, then each id and its patch as JSON text, members in key order |
| `held` | count, then each record's id and its components as JSON text |
| `gained` | count, then each member as a length, a form name and a payload |

A list whose name this build does not know is kept as bytes and written back.
A known list at a newer version than this build reads makes the file one a
newer build wrote: the restore is refused with that reason before anything
changes, as a newer known chunk is. A `gained` member whose form this build
does not make (none yet) is kept as bytes, made into nothing, and logged when
its zone imports.

A file whose format version is newer, or that holds a newer version of a chunk
or a list this build reads, is refused as written by a newer build
(`DecodedSavedGame::Newer`): a restore of it fails with that reason rather than
falling back to an older backup, and the next write keeps it as the backup
rather than discarding it as corrupt. A format 1 file is refused by its version.
`test/fixtures/saves/example.v2.ssav` is format 2, checked in, holds a `game`
member, an unknown chunk, an unknown list and a `gained` member of an unknown
form, and must keep reading; the encoder must write exactly it.

## Not yet

Recorded in `docs/deferred.md`: a body's children and other participants'
bodies and what they hold, records held by runtime entities other than the
local body (a dropped item's pickup, a spawned NPC), a level entity's
placement, change tracking for capture, waiting records serialised instead of
kept alive, and restoring inside a network session.
