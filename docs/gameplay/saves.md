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
| `ListSaveSlots` | `save/SaveSlotListing.h` | every slot's summary, read from file prefixes |
| `SavedGames` | `save/SavedGames.h` | requests, capture, restore, sections, slots |
| `save.write`, `save.restore` | `save/SavedGameVocabulary.h` | the verbs and console commands |
| `save.delete`, `save.copy` | same | the slot verbs |
| `save.describe` | same | a developer command: what a save holds of one entity |

## What a save holds

- **Summary:** the level (`LevelReference`), a world's focus zone, when it was
  written, and `game`: a JSON object the game supplies and the engine never
  reads (an area name, a play time, a display name for the slot), absent when
  the game supplies none ("Slots").
- **Thumbnail:** a picture the game supplies for a load menu, absent when it
  supplies none ("Slots").
- **Levels:** for each level the game has changed this session, per zone (and
  per world scene, under `kWorldSceneZone`), a set of named lists:
  `destroyed`, the authored entities gone for good; `changed`, the patches of
  authored entities changed for good ("Level entities"); `gained`, the
  run-time entities the zone keeps, such as a dropped item ("What a zone
  gained"); `held`, the runtime records whose holder is an entity of that
  zone, resident or waiting ("Held records"); `occupied`, what was inside each
  of the zone's sensors (`docs/physics/sensor-overlaps.md`, "Across a load");
  `departed`, the zone's entities a level change carried to another level
  (`docs/gameplay/game-module.md`, "Carrying").
  Only what is never forgotten is written; `at_restore` state stays
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
`ItemPickup`, `Held`, `LookOrientation`, `RigidBody` (velocities and sleep
included, `docs/physics/rigid-bodies.md`), and the character and actor state
of "Characters and actors": `KinematicState`, `CharacterMovement`,
`DecisionAgent` and `NavAgentRequest`, `AppliedEffects` ("Lasting
effects"), and a kept projectile's `ProjectileMotion` and `ProjectileFlight`
(`docs/gameplay/projectiles.md`, "Kept in a save"). Everything else is `Authored`,
including `LocalTransform` (a level entity's is kept only by its placement,
"Placement"), meshes, colliders and lights. Which part of a `Saved` component
is durable is its serializer's: a generated one writes its fields as "Run-time
fields" says, and a hand-written one decides itself.

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

### Run-time fields

`SENCHA_FIELD` makes a member part of the scene form, a tool's row, the wire
form when the component travels, and the saved form when it is `Saved`.
Config and run-time state share one component often enough (a combatant's
authored profile beside its rallied flag, a character's capsule state beside a
tether that will not exist after a load) that two cases need saying apart, per
field, on a `SENCHA_SAVED` component (`ecs/ComponentAnnotations.h`):

- `SENCHA_RUNTIME`: in the saved form only. A scene written for authoring
  (`SceneRecordPurpose::Authoring`) leaves it out, and a tool does not offer
  it: the serializer's `RuntimeFields`, which an inspector, the document's
  carry of unknown fields and the cooked schema fingerprint read, list only the
  other fields. A component whose every field is run-time state has no
  authoring form (`IComponentSerializer::Authorable` is false): an authoring
  write leaves the whole component out, and Kyusu neither offers it nor shows
  it as authored; it is listed with what its owner brings.
- `SENCHA_UNSAVED`: in the scene form as declared, never in a save
  (`SceneRecordPurpose::Save`), and so never in a default record either.

The wire is not one of these forms: a run-time field travels when its
component does, under the audience its own annotations give it. So a
character's velocity stays on the wire to its owner while being run-time
state, and the replication table does not move.

Either annotation needs a member initializer, since a record that leaves the
field out reads it as its default; the generator refuses one without, one on a
component that is not `SENCHA_SAVED`, both on one member, and a run-time asset
reference.

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
  written form equals its baseline is left out before the patch is built. A
  caller that knows a component's saved form is what its `kept` patch was taken
  from names it in `unchanged`: its member is copied from `kept` and its
  serializer is not asked, which is how a zone's capture skips what has not
  changed ("Capture at scale").
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
  content did not author always is. A `Saved` component an entity is owed
  (one a component it carries derives, such as the tags `AbilityGrants`
  brings) is in a level entity's baseline at its default record, put there
  by the importer, so it is compared like any authored member and an owed
  component at its default writes nothing.
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

## Entity references

Capture reads and mints nothing, so a saved form can name only an entity that
already has a persistent id: a level entity was authored with one, a kept
run-time entity takes one when it is marked, and a body takes one when it is
assigned in a game that keeps level state ("Bodies"). An `EntityId` field in
a saved form (`SceneFieldCodec<EntityId>`) is written as its target's
persistent id when the target has one and as nothing otherwise; a reference to
anything else could not be followed after a load whatever were written.

A read resolves the id against the World read into. When the target is not
resident yet, as a body is not until frames after its zone, the field is left
unset and recorded in `PendingReferences` (`world/identity/PendingReferences.h`),
which fills it the moment an entity with that id arrives (the
`PersistentIdComponent` hook the held-record index uses). A waiting reference
is dropped when its owner dies and at every level change, and code that sets
the component afresh forgets it (`ForgetPendingReferences`). The resource is
made by the first reference a load leaves waiting, so a game that never loads
one pays a null check per identified entity made. Where an owner must not give
up on a reference while it waits, it asks `Awaits` (a navigation order,
"Characters and actors").

## Characters and actors

What a character and an actor are doing comes back after a load, each as its
own saved form against its default record (an owed component's default is in
a level entity's baseline, so one at rest writes nothing):

- **Velocity.** `KinematicState`'s velocity is a run-time field. A quicksave
  mid-jump continues the arc: a body or character built with a
  `KinematicState` takes it as its first velocity.
- **Movement mode.** `CharacterMovement` is `Saved`, and its serializer writes
  the mode by name only when no `LocomotionModeLease` on the entity names it.
  A mode a character is simply in (crouched, a stance) comes back; a mode a
  holder put it in does not, since the holder is not restored (a body saved on
  a ladder comes back free).
- **Alertness.** `DecisionAgent` keeps its engagement and last contact
  (`docs/gameplay/decision-agent.md`); a loaded agent starts its clock and
  chooses again from what it last knew, and perception finds its target again.
- **Orders.** `NavAgentRequest` keeps its order and takes a fresh id when it is
  read; a follow order on a body that lands later is held, not lost
  (`docs/navigation/runtime.md`, "Orders"). The route is planned again.
- **Place and facing.** A `CharacterController` holds placement ("Placement");
  `LookOrientation` is saved.

A save naming an arrival keeps no motion ("Writing"). What a player still
notices after a quickload (an attack, a reload or an ability in progress,
animation poses) is listed in `docs/deferred.md`.

A game's own actor state is the game's to tag with these annotations. A corpse
is the game's saved state too: an entity that stays after death must save that
it is dead, or a load stands it up idle with no health, and its on-death
responses may run again.

## Lasting effects

A lasting effect is its own entity in the persistent partition, aimed at its
target (`docs/gameplay/abilitykit.md`, "Effects"). A save keeps the ones active
when it is written, with their time left, on every identified entity, in a game
that keeps level state: a poison on the player, a buff on an NPC, an ability's
cooldown (an unowned effect on its actor) and an ability's cost effects. A
quicksave does not cleanse; a rest point ends what it chooses before it writes
(`effect.remove`, below).

**Which are kept is the applier's to say**, since only it knows whether it will
apply the effect again when the world is rebuilt: `ApplyEffect`'s
`EffectKeeping` is `ByOwner` by default (kept when nothing owns it, not when
something does), `Kept` or `NotKept`, and `ActiveEffect::Kept` records the
answer. So a cooldown, a cost and a poison from `effect.apply` are kept; what
equipment maintains and what a running ability holds are owned and not kept,
and their holder makes them again. An effect whose owner will not make it again
(an attacker's lingering hit) is applied `Kept`: its owner is written by
persistent id and owns it again if resident when it is restored, and otherwise
it runs out its time unowned. Setup that applies a passive whenever an entity
spawns applies it `NotKept`, so the spawn and the save do not both apply it.
`effect.apply` takes `keep` with the same default.

A toggled or held ability (a cloak, a sprint) holds its effects through a
lease, and ability records are not saved, so after a load the ability is off
and so are its effects; an ability does not restart, it ends
(`docs/deferred.md`, "Abilities and animation in progress across a load").

**Where it is saved.** On its target, as a member of the target's patch:
`AppliedEffects` (`effects/AppliedEffects.h`, scene key `AppliedEffects`), a
keyed list of entries, each an effect name and an ordinal as its key
(`"name#0"`), `remaining_ticks` (absent: the definition's whole duration),
`ticks_to_period`, `scale`, and `owner` with `owner_key` when it has one. Time is counted in the ticks the game runs at,
never as an absolute tick. An effect therefore inherits its target's level,
zone, lifetime and parking with no rule of its own, and the body's ride in the
body's patch; an NPC's survives its zone's round trip.

- **The row is added at the settle step, never at apply.** Before every
  capture (a save, and a zone's detach), beside `SettleHeldRecords`,
  `SettleAppliedEffects` gives an identified entity under a kept effect the
  component if it lacks it, and reads what every such entity is under, once.
  The serializer writes from that reading, so a game that does not keep level
  state never carries the component, and one that does carries it only on
  entities that were under a kept effect at a capture. Its saved form does not
  follow its row, so its carriers are diffed at every capture.
- **On load** the entries sit in the row, and on the entity's first active tick
  the effect pass puts each back (`RestoreEffect`) and empties the row. A
  restore grants what the effect grants and takes its time-scale lease, with no
  admission, no application hook and no application log entry: it was admitted
  when it was applied.
- **Content** may author entries in a scene file (a barrel that starts
  burning); no tool offers the component. An authored entry that ran out is
  absent from the next capture, which the keyed list records as a removal, so a
  load does not put it back. An entry naming an effect no longer defined is
  left out, with a warning.
- **Capacity.** A row holds 16. A save of an entity under more fails in a
  development build, naming it (`OverfullAppliedEffects`); in a release build,
  and at any zone's detach, the effects with the least time left are left out,
  counted and logged as an error.

`effect.remove {target, effect | kinds}` ends every lasting effect on `target`
of one definition, or whose definition's `Kinds` a tag query matches (`kinds`:
`all`, `any` and `none` lists of tag names, each matching the tag and those
beneath it), and answers how many it ended. It takes exactly one of the two,
and refuses `nothing_named` or `both_named` otherwise. A rest point's sequence
runs it with a "cured by rest" kind before `save.write`; a story debuff of
another kind, ended by name by the story's own trigger, survives the rest.

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
transform and persistent id travel beside it. In a game that keeps level
state, the body is given a runtime persistent id when it is assigned to its
participant (`SessionParticipantProjection`), so whatever a save writes about
it by id, such as a sensor it stands in, finds it after a load, and a capture
never has to give it one. Otherwise it has an id only once something
references it by persistent id: a `PersistableRef` is made through
`MakePersistableRef`, which gives the body one from the runtime mint, and a
record the body holds names it that way. A body nothing references is then
saved without an id and restored without one.

What a body wears is in that patch: its `EquipmentState` names each record's
source entry by persistent id, and the entries are the body's held records,
kept by `sencha.held`. Nothing a record maintains is saved; the equipment pass
derives it again (`docs/gameplay/equipment.md`, "Records").

On restore the section fills `RestoredBodies`; the local participant's next body
spawn (`ParticipantBodySpawns`) takes it: the spawn stands at the save's
arrival landmark, or where the body stood when the save names none,
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
`tags.grant` are the container's baseline and come back; lasting effects come
back with their time left in a game that keeps level state ("Lasting
effects").

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
state keeps destroyed sets only, records no baseline and diffs nothing.

An entity forgotten at detach (`at_detach`) records no baseline and is not
diffed: nothing about it outlives its zone, and a patch is only ever composed
when the zone imports again. A level change that forgets the level it leaves
(a new game, a restore, or entering the same level as authored) does not
capture that level's zones as they unload (`RuntimeZoneRecord::CapturedAtDetach`),
since the store forgets the result at once.

## Placement

Where a level entity stands is kept only when nothing can recompute it.
Whether that is so is a question about an instance, so each serializer answers
it for a cooked record (`IComponentSerializer::HoldsPlacement`): `RigidBody`
when its motion is dynamic, `CharacterController` always, and nothing else. A
kinematic body is moved by something else (time, a tag, an attachment), so its
transform is derived; a static entity does not move. When any cooked component
of an entity answers true, the importer adds `LocalTransform` to its baseline,
and its patch keeps where it went. With `RigidBody` saved, a crate comes back
where it was left, moving as it was and asleep if it was, after its zone's
round trip and in a save (`docs/physics/rigid-bodies.md`; a parented body's
pose is pulled as local, so what is kept composes to where it was).

`ZoneStateLifetime`'s `placement` overrides the components, per entity and
inherited as `forgotten` is: `kept` for something nothing will move again (a
statue a response teleports once), `authored` for dynamic debris nobody needs
back, and `by_components`, the default, to ask the components.

A scripted mover saves its driver, not its transform. A door a response opens
keeps the tag or counter that says it is open, and whatever moves it reads
that on load. A platform that is a function of time saves its own elapsed
time; no saved form holds an absolute tick, since the process tick is never
restored (Loss Function's `Sway` keeps its cycle). A turret aimed by
`AimFacing` has its facing rebuilt.

A save carries no contact cache, so a simulation resumed from a save diverges
from the unsaved one after the first contacts, and a settled stack may settle
again by a hair. In a session only the authority captures.

## What a zone gained

A run-time entity that belongs in the world, such as a dropped item, is kept
with the zone it lies in when it carries `KeptWithZone` (`zone/KeptWithZone.h`,
`sencha.kept_with_zone`), an empty marker on no entity by default. A zone's
partition holds run-time entities meant to stay and entities meant to pass (a
projectile, a spawned effect) alike; the marker is what tells them apart.
`KeepWithZone(world, entity)` adds it on the authority and gives the entity a
runtime persistent id (bit 63) if it has none; it refuses a level entity, which
its patch already keeps, and a held record, which its holder keeps.
`inventory.drop` marks the pickup it makes unless told `keep: false`, and puts
it in the zone where it lands (`docs/gameplay/inventory.md`, "Where a drop
lands").

- **Capture.** With the zone's capture, at detach and for a save, on the
  authority and only when the game keeps level state, every live marked entity
  of the zone (its partition, or a world scene's entities) is written in one of
  two forms, each its runtime persistent id and then JSON text:
  - `from_source`, for the root of a scene instance whose source resolves (a
    prefab spawn: a grenade, a bloodstain): the source's asset id, the root's
    transform, and the entity patch from the source's root to the entity, over
    `Saved` components, exactly as a body is kept ("Bodies"). The lookup is the
    body's (`SpawnSourcePackage`, `runtime/spawn/SceneSpawnOverlay.h`), which
    `ZoneStateSerialization::SourcePackage` gives the zone's capture, built once
    per source per capture. A value the patch does not name follows the prefab,
    so a retuned prefab reaches the member, and its children come back with it
    because the whole prefab is spawned again; what the children carry is not
    saved (`docs/deferred.md`, "Saving what was created at run time").
  - `whole`, for anything else (a pickup a drop made in code): its entity
    record (`WriteEntityRecord` for `Authored` and `Saved` components,
    `LocalTransform` included, `Identity` ones beside it), the held record's
    form. Only the entity itself is written; one with children loses them. A
    scene instance root whose source no longer resolves is written whole from
    then on, with an error naming it that says a whole record does not hold its
    children: the one place the two forms differ in what they keep.

  The zone's `gained` list is exactly the marked entities alive at that
  capture, and the members whose spawn has not landed yet (below), so one taken
  or destroyed since is simply absent.
- **Lifetime.** A marked entity without a `ZoneStateLifetime` is never
  forgotten. One carrying the component follows it: `at_restore` is kept
  across its zone's unload and forgotten at a restore and not written to a
  save; `at_detach` is not captured at all.
- **Identity.** Its id is a runtime one, but for an entity a level change
  carried from another level, which keeps its authored id
  (`docs/gameplay/game-module.md`, "Carrying"). The importer refuses runtime
  ids in a package, and refuses a member naming an id the zone's own package
  authored, so a gained entity cannot take an authored one's place; the saved
  id counter keeps later mints clear of restored ones.
- **What it holds.** Its records (the entry under a dropped pickup) are
  written into the zone's `held` list at detach and made again after it at
  import, exactly as an authored chest's are ("Held records"), except that what
  an `at_restore` gained entity holds is not written to a save, since the
  entity is not.
- **Restore.** When the zone imports, inside its hidden import, after its
  authored entities exist and its authored set is recorded, each `whole`
  member is made in the zone's partition with its id and the marker, then its
  record is read (`ReadEntityRecord`); the records the zone holds are made
  after every member. Being before the zone is published, residency systems
  see whole members arrive with their zone. A member that does not read whole
  is dropped from the store and logged as an error naming it and the
  component; the records it held are then dropped as records whose holder did
  not arrive, logged with their count, rather than left waiting for it. A
  member naming an id its zone authored is refused the same way, and one the
  zone's `departed` list names is forgotten; one whose id a live entity already
  has is left in the store and not made. A client imports its
  zone as authored and makes none.
- **A member from its source lands later**, as a body does. The import puts it
  in `GainedSpawnRequests` (`zone/GainedSpawnRequests.h`), and the engine's
  `GainedMemberSpawns` (`runtime/spawn/GainedMemberSpawns.h`), at the async
  drain, asks the spawn service for it once its zone is attached: the source
  into the zone's partition with the patch composed over its root and the
  member's id given to the root at publication. The pump that publishes it is
  followed at once by marking the root kept and standing it at its saved
  transform, so no capture sees it unmarked. Until then a capture keeps the
  member as it was read, and a record it holds waits for it in the zone's
  partition instead of being dropped (`docs/gameplay/held-records.md`,
  "Import"); a detach before it lands keeps both, and withdraws the spawn. A
  member whose spawn fails, or that publishes without a root it can keep, is
  dropped from the store and logged, and so are the records it held. Every
  level change forgets the requests, as it forgets `PendingReferences`.

A kept projectile is a member saved from its source; only a prefab that says
`kept` makes one (`docs/gameplay/projectiles.md`, "Kept in a save").

A game that marks nothing pays one component test per identified entity of a
zone at each capture. A marked entity costs one write per capture in which its
rows changed ("Capture at scale"): its whole record, or for a member from its
source a patch against a prefab package built once per source per capture. A
whole member costs one read per attach, a member from its source one spawn
(`docs/plans/evidence/level-state/results.md`).

## Capture at scale

A zone's capture skips what has not changed since the last one. Beside each
resident authored entity's baseline and each gained member's kept record, the
store keeps an image of its rows (`ZoneCaptureImage`,
`zone/ZoneCaptureImage.h`): the bytes, as the last capture read them, of every
component the capture writes for it whose serializer says equal bytes always
write the same saved form (`IComponentSerializer::SavedFormFromRowAlone`). A
capture compares the entity with its image first:

- When it carries the same serialized components (its archetype, or one that
  differs only in components no serializer writes, such as the link a physics
  body adds) and every component the capture writes has a row in the image
  equal to its row now, the kept patch or the kept member stands and nothing is
  written or encoded.
- Otherwise only the components whose rows differ are written and diffed; an
  equal row's member is copied from the kept patch (`CaptureEntityPatch`'s
  `unchanged`), and the entity is imaged again. A gained member that changed is
  written again in full, in its form ("What a zone gained").

The filter only decides what is written, never what a patch says: unequal
padding, or a field the saved form leaves out (an attribute's current value),
costs one diff that finds nothing new. A serializer that cannot say so (the
default) is written at every capture of an entity that carries it, and the
rest of that entity's components are still filtered.

The import takes the first image. An authored entity the store holds no patch
for is imaged as it is built, standing for an empty patch, and a gained member
is imaged as it is read, standing for the record it was read from. Both hold
because a cooked record is its serializer's own form (the cook writes every
component through its serializer, "Bodies") and a record a capture wrote reads
back to the same record. A patched entity is not imaged at import, so its first
capture writes it in full, which also drops what a retuned scene set aside. A
zone detached straight after its import therefore writes only what changed in
between. A record built in code that leaves out fields its serializer writes
is still read as untouched until it changes; a full write would have stated
the left-out fields at their defaults, to the same effect.

Which serializers say their saved form follows their row:

- **Generated** (`ComponentSerializer<T>`): when `T` is trivially copyable and
  no field is an asset reference, an entity reference (`PersistableRef`, or an
  `EntityId`, written as its target's id), or a type whose scene codec writes
  through a lookup. So `KinematicState` and `DecisionAgent` do, and
  `NavAgentRequest`, `ProjectileFlight` and `ProjectileMotion` do not. A codec says it writes from
  the value alone with `WrittenFromValue` (`world/serialization/SceneFieldCodec.h`):
  the primary template, the caption enums, inline strings and the stable ids
  (zone, dock, link, nav link, scene instance, asset id, persistent id) do. A
  codec that does not say so is taken to look something up, as a named id
  will.
- **Hand-written**, per case: `responses` (a binding set is content-addressed
  and never changes in a World, so its key in the row stands for the same
  bindings), `GameplayTags` and `Attributes` (names from registries that only
  grow, so an id in the row always names the same tag or attribute),
  `ItemEntry`, `ItemPickup` and `ItemContainer` (definition paths and tag
  names, likewise). `AbilityGrants`, `EquipmentState`, `Counter`,
  `CharacterMovement`, `AppliedEffects` (written from what its entity is under,
  not from its row) and every other hand-written serializer answer no.

The comparison, the per-entity image and their cost are measured in
`docs/plans/evidence/level-state/results.md` ("Capture at scale"). What it does
not do: spread a capture whose every entity changed over frames, or capture off
the owner thread (`docs/deferred.md`).

## Held records

`sencha.held` (`save/HeldRecordSaves.h`) keeps what is saved beyond bodies and
levels: a closure from roots. The roots are the world and the local
participant's body; the members are the records made at run time that the
held-record index lists under a root or as waiting for a holder that is no
level entity, then what those records hold, to the nesting bound
(`docs/gameplay/held-records.md`). What a level entity or a gained entity holds (a placed
chest's contents, a dropped item's entry), resident or waiting for its zone, is
the same closure from that holder, written in the `held` list of the holder's
level and zone (`CaptureLevelHeldRecords` for resident holders and live waiting
records; the zone store's own list for an absent zone, `docs/gameplay/held-records.md`,
"Records of authored holders"). A restore installs every zone's `held` list
into the zone store with the rest of the save's levels, and each zone's
records are made when it imports. Counters and inventory are kept this way. A
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
authored; then it creates each saved record of the section in the persistent
partition with its id and its components. A record of the world is listed at
once. A record of a body waits for an entity with its holder's persistent id:
the restored body takes that id, and its records move under it, follow it into
its partition and become relevant to its owner. A record of an authored or
gained holder comes from its zone's `held` list instead, made inside the zone's
import under a holder already there. A saved record that does not load whole,
such as a counter naming a tag the content no longer declares, is not restored
and is reported.

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
the saved body comes back: the level's `LevelLandmark` of that name
(`docs/gameplay/game-module.md`, "Arrivals"). With one, the body's record holds
the name and no transform, and its patch keeps no motion: the `KinematicState`
and `CharacterMovement` members are left out, so the body comes back at the
landmark at rest in its prefab's mode, never mid-fall or crouched. Without one
(a quicksave), the record holds where the body stood and how it was moving. A write with an arrival in a
world sets the save's focus to the zone the landmark stands in, so a restore
streams that zone in first even when the player stood in a neighbouring one; a
landmark that is not loaded when the save is written is logged, the focus stays
the player's zone, and the restore falls back to the game's spawn if the
landmark is not loaded around it either. The name is read back into
`RestoredBody::Arrival`.

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
read is logged. `Holds(slot)` answers from the prefixes of the slot's files, as
a listing does ("Slots"): true when the file's or, failing it, the backup's
prefix verifies, so true when only a readable backup is left and false for a
file corrupt in its prefix with none; a file corrupt only past its `HEAD` is
held, and its restore falls back to the backup or fails then. The
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
may stop after `HEAD`: `DecodeSavedGameSummary` reads the summary and the
thumbnail chunk from the file's prefix and checks them against `HEAD`, so a
listing never reads the levels ("Slots"); `HASH` still decides whether a file
restores. `THMB` is written when the game gives a thumbnail at capture, and
version 1 is the thumbnail's width and height (32 bits
each), its encoding name (a length-prefixed string) and its bytes (a 32-bit
count, then the bytes): `EncodeSavedGameThumbnail` and
`DecodeSavedGameThumbnail`. A `THMB` of a version this build does not read is
carried, and offers no picture. A chunk this build does not know
is carried as read and written back where it was, before `HEAD` or after
`SECT`, and a restore carries it into the next save, as an unclaimed section is.

`LVLS` is a count of levels, then each level as a length-prefixed record (its
kind, its name, a count of zones), each zone a length-prefixed record (its id,
a count of lists), each list a name, a list version and a length-prefixed
payload. No record refers to bytes outside itself, so an unchanged zone's
bytes are copied from one save into the next without decoding: the zone store
holds a zone that is not loaded as that encoded blob (`EncodedZoneLists`) and
parses it only when the zone imports, and a write copies the pointer; the
records its entities held, in a game that keeps level state, are a second blob,
the zone's `held` list, copied the same way. A zone holding `at_restore` state
stays parsed while it is away, since that state is not in the blob. The known lists, at version 1:

| List | Payload |
|---|---|
| `destroyed` | count, then each id |
| `changed` | count, then each id and its patch as JSON text, members in key order |
| `held` | count, then each record's id and its components as JSON text |
| `gained` | count, then each member as a length, a form name and a payload: `whole` and `from_source` members by id, after any member of a form this build does not make |
| `occupied` | count, then each sensor's id and its occupant's id, sorted; a pair is forgotten with its sensor |
| `departed` | count, then each id, sorted: an entity of the zone carried away by a level change; its import makes none of them, and neither `ForgetLevel` nor any lifetime forgets one, only `Clear` (a new game) and `Install` (a restore) |

A list whose name this build does not know is kept as bytes and written back.
A known list at a newer version than this build reads makes the file one a
newer build wrote: the restore is refused with that reason before anything
changes, as a newer known chunk is. This build makes two `gained` forms, each a
64-bit runtime persistent id, then length-prefixed JSON text
(`EncodeGainedMember`): `whole`, whose text is the entity record, the form a
`held` record's entry takes; and `from_source`, whose text is `{source, root,
patch}`. A later layout is a new form name, not a new version of one. A member
whose form this build does not make is kept as bytes, written back unchanged
before the members it makes, made into nothing, and logged when its zone
imports; so a build that made only `whole` carries a `from_source` member
untouched.

A file whose format version is newer, or that holds a newer version of a chunk
or a list this build reads, is refused as written by a newer build
(`DecodedSavedGame::Newer`): a restore of it fails with that reason rather than
falling back to an older backup, and the next write keeps it as the backup
rather than discarding it as corrupt. A format 1 file is refused by its version.
`test/fixtures/saves/example.v2.ssav` is format 2, checked in, holds a `game`
member, an unknown chunk, an unknown list and a `gained` member of an unknown
form, and must keep reading; the encoder must write exactly it.

## Slots

A slot is a name, `[A-Za-z0-9_-]{1,64}`; what the player sees of it (an area,
a play time, a display name) is the game's, in the summary's `game` member.

`ListSaveSlots` (`save/SaveSlotListing.h`) answers a load menu: every slot
with a file or a backup (`SaveSlotFiles::Slots`), in name order, each as a
`SaveSlotSummary` read from its file's prefix. `SaveSlotFiles::ReadPrefix`
reads through `ReadFilePrefix` (`core/io/FileBytes.h`), which reads no byte
past what `SavedGamePrefixWanted` asks for: the header, each chunk header,
and `SUMM`, `THMB` and `HEAD`, never `LVLS` or what follows it.
`SaveSlotListing::BytesRead` counts what it read. The prefix is checked
against `HEAD`; one that verifies does not promise the rest restores, since
`HASH` decides that at restore, and a menu shows the restore's failure if it
comes.

- A slot whose file does not read (missing, cut short, or corrupt in its
  prefix) carries the reason in `Error`, and its backup's summary is offered
  with `FromBackup`, as a restore would read the backup.
- A file a newer build wrote (its format, or a newer `SUMM`) lists `Newer`,
  and its backup is not offered in its place, as a restore does not read it.
- `ReadSaveSlotThumbnail` reads the picture the same way: the slot's file, or
  its backup when the file does not read.

`SavedGames` gives a game the same reads off the owner thread, and what a
slot holds:

- **`SetSummary`** takes a `SavedGameSummaryDefinition`: `Describe` returns
  the `game` member and `Thumbnail` a `SavedGameThumbnail` (width, height, an
  encoding the game names, such as `png` or `rgba8`, and the bytes), each
  asked at every capture with the `const World&`. Either may be empty, and a
  thumbnail callback may return nothing; a save then has no `game` member or
  no `THMB` chunk. The engine interprets neither. The pixels are the game's:
  a capture runs at the zone-residency step, before rendering, and the
  renderer has no readback of the last presented frame (`docs/deferred.md`,
  "A thumbnail of the last presented frame"), so until it has, a game gives a
  picture per area.
- **`RequestList(done)`** runs `ListSaveSlots` on the async lane and answers
  `done` on the owner thread at a later drain, never inside the request. The
  one save not yet on disk ("Restoring") is listed from memory in its slot's
  place, as a restore would read it. Every thumbnail a listing passes is read
  with it, so its size is the game's to keep small. A slot deleted after the
  request is left out of the answer, though the files were read before the
  delete.
- **`RequestThumbnail(slot, done)`** reads one slot's picture the same way, or
  answers nothing, as it does for a slot deleted after the request.
- **`RequestDelete(slot)`** removes the slot's file and its backup at once and
  forgets the slot: `Remembers` is false after it and the unwritten save held
  for it is dropped, so a deleted slot can come back neither from disk nor from
  memory. It answers `Busy` while a write of that slot waits or is on its way
  to disk, while a copy reads or writes it, and while a restore of it waits,
  reads or loads, until the restore's bodies are back or it fails, so a
  restore never finishes from bytes read before the delete; another slot's
  write or restore does not hold it up. A file that cannot be removed is
  logged.
- **`RequestCopy(from, to)`** is a write of `to` with what a restore of `from`
  reads: `to`'s file and backup become `from`'s, byte for byte, or, when
  `from`'s latest save is the one held in memory, that save, with `from`'s
  file as the backup. It goes on the async lane and is reported by
  `WriteStatus()` and `WriteFailure()`, so it is `Busy` while any write or copy
  is under way, and a restore of `to` is `Busy` until it is done. Once it
  lands, `to` is remembered exactly when `from` is, and an unwritten save held
  for `to` is dropped. A `from` holding nothing fails the copy and leaves `to`
  as it was. With no saves root there is no file to copy, and the copy fails.

The verbs `save.delete {slot}` and `save.copy {from, to}` call
`RequestDelete` and `RequestCopy` and answer `SaveAdmission` as their reason
(`refused: busy`); a name that is not a slot is an invalid argument naming it.
Listing and thumbnails are C++ calls for the menu, not verbs.

Autosave rotation, quicksave naming and the menu itself are the game's.

## Ticks

The process tick is monotonic and is never restored, and no saved form holds an
absolute tick. A restore to an earlier tick inside one process would silence
every sensor response until the tick passed its old value, leave response
cooldowns stale and repeat ability activation identities; nothing a save keeps
needs it. So a duration is saved as the time left (an effect's
`remaining_ticks`, a projectile's `remaining` seconds), a loaded projectile's
launch tick is the tick it is loaded on, and game code that is a function of
time saves its own elapsed time (Loss Function's `Sway` keeps its cycle). The
animation clock is presentation and restarts. Every saved form is checked by a
test that restores it in a process whose tick differs.

## What a player notices after a quickload

A save is how the world differs from content, not a snapshot of the simulation.
What a quicksave anywhere, loaded later, gives back:

| What | After a load |
|---|---|
| Things are where they were, moving as they were, asleep if they were | Kept ("Placement") |
| Characters keep their velocity; a jump continues | Kept ("Characters and actors") |
| Crouched in a vent restores crouched | Kept: a mode no lease holds, and the game's own capsule state |
| Standing in a trigger does not fire it again; plates do not double-count | Kept (`occupied`, `docs/physics/sensor-overlaps.md`) |
| NPCs alerted or idle as they were, heading for what they last knew | Kept ("Characters and actors") |
| A follow order or a walk order continues | Kept, its route planned again |
| Poisons, buffs, cooldowns, with their time left, on everyone | Kept ("Lasting effects") |
| A grenade in the air | Kept when its prefab says `kept` (`docs/gameplay/projectiles.md`) |
| Dropped items, containers, doors, counters | Kept ("What a zone gained", "Held records", "Level entities") |
| **A toggled or held ability (cloak, sprint, aim) is off, with its effects** | **Lost.** Ability records have no saved form: toggled abilities end on a quickload |
| **A reload, a charge, a long action in progress** | **Lost.** It ends; the player starts it again |
| **An NPC's attack in progress** | **Lost.** It decides again within its decision interval, from what it knew |
| **Animation poses** | **Lost.** Everything starts from its idle pose and blends |
| **A rate-limited trigger's cooldown** | Kept within one run of the game, since the tick is monotonic. **Lost after quitting**: system memory with no saved form |
| **The first few frames** | Actors and props act before the body and members saved from their source land; a game holds its cover until `RestoreStatus()` is `BodiesRestored` |
| **A corpse, a broken object's debris** | The game's saved state ("Characters and actors"): kept if the game marks it so |
| **Contact-exact physics** | A stack may settle by a hair; the simulation diverges from the unsaved one |

The rows marked lost share two causes, ability records and animation playback
with no saved form, and system memory with no section (`docs/deferred.md`,
"Abilities and animation in progress across a load", "System memory with no
saved form"). A snapshot of the simulation would close them only by describing
every component and every system's memory well enough to survive a process,
tied to the build that wrote it; it is not planned, and neither is bit-exact
resume.

## Not yet

Recorded in `docs/deferred.md`: a thumbnail of the last presented frame, what
a body's or a member's children carry, other participants' bodies and what
they hold, a run-time entity nothing marked (a spawned NPC), abilities and
animation in progress, system memory with no saved form, the frames before a
restored body lands, a capture spread over frames or taken off the owner
thread, and restoring inside a network session.
