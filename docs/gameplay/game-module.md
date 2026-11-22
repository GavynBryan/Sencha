# Game modules

A Sencha game is a module (`game.so`) the host loads: a `Game` whose hooks the
engine calls in a fixed order. The engine boots, mounts content, loads levels,
spawns scenes, runs frames, and shuts down with a `Game` whose every hook is
empty; `templates/blank` is that game, and its test proves it. Everything a
player-facing game has beyond that is code in the module.

## Three tiers, no fourth

```
engine/      mechanisms, no policy; knows no game
templates/   one Game each; owns its composition root, steering, camera policy,
             which controller facilities its player gets, its data types, content
             (may duplicate ~40 lines of another template rather than share)
test/fixtures/content/  engine test content, cooked by the tests
```

The rule the split enforces: **a game composes engine mechanisms and owns every
policy; the engine never observes a game.** There is no shared template library
and no tier between a mechanism and a game. Two templates that need the same
policy duplicate it; a mechanism is promoted into the engine only when it stands
on its own justification, never because two templates happen to use it (the FPS
and the arena count as one consumer: the arena is the FPS with a session).

## The hook sequence

`Engine::Run` (`engine/src/app/Engine.cpp`):

1. `OnConfigure` -- the process's `EngineConfig`, including
   `RuntimeConfig::ContentRoots` (default `assets`, beside the working
   directory; `--content-root` overrides). **A game names itself here**
   (`App.Name`): the name titles the window and is the namespace its saved
   settings are filed under, so a game that ships should state one rather than
   inherit the host's fallback. The templates do.
2. `OnRegisterComponents` -- the module's components, into the engine's schema.
   The schema is sealed after this; storage for every schema component exists
   in every world.
3. `OnRegisterDataAssetTypes` -- the module's structured data subtypes, into the
   content stack's registries *before* the scan classifies `.sdata`. Called by
   the runtime and by the data editor through the same
   `RegisterGameDataAssets`.
   Before the content stack exists, the engine installs the vocabulary
   registries a game declares into (`InstallAbilityKitVocabulary`: tags,
   attributes, effects, abilities -- registries only; movement stays a
   feature the game opts into, so a game declaring a locomotion mode calls
   `InstallMovementVocabulary` in this same hook first, and the declaration
   then lands identically in the runtime World and in every editor's) and the
   runtime World's authored vocabulary (`InstallAuthoredVocabulary`: the verb
   catalog, with its own `runtime.resume` and `application.quit`, and the query
   and event catalogs), then calls `OnRegisterVocabulary` once. A game declares
   its annotated verbs, queries and events there through one
   `AuthoredVocabularyScope`, beside its tags, attributes and modes; the scope
   commits to all three catalogs or none, and the engine reads their
   installation errors afterwards and refuses to start on any. Declaration
   only: the implementations are bound from `OnStart` with `BindAuthoredApi`
   against `Engine::TryVerbs()` and `Engine::TryAuthoredQueries()`, events are
   published through `Engine::TryAuthoredEvents()`, and an editor calling the
   same hook on a document's World gets the names with nothing behind them.
   See `docs/gameplay/authored-api.md`.
4. The engine composes `RuntimeContent` (the asset stack), mounts the content
   roots, publishes the world's asset resources, connects the spawn services and
   the render pipeline, and constructs `LoadedLevel`. Under a cook-enabled
   build it also watches the mounted roots for hot reload.
5. `OnStart` -- the game sees a mounted, published stack through
   `Engine::Content()` and an empty level through `Engine::Level()`. This is
   where a game registers the world-storage features its world carries
   (`RegisterPhysicsComponents`, `RegisterMovement`, ...), binds its input, and
   installs its participant policies.
6. The startup script runs (`+map`, `+world`, `+host`, ...). `map` and `world`
   request a level change (see "Changing level"), carried out at the first
   frame's zone-residency phase; `zone`, `zones`, `scene.spawn`, and
   `scene.despawn` are engine commands over `LoadedLevel`.
7. `OnRegisterSystems` -- the composition root. The engine's own systems
   (hot-reload polling) register after the game's, so a game's ordering edges
   already exist.
8. Frames.
9. `OnShutdown` -- every lease a game took into `Content().Assets()` is
   released here, every binding and subscription token it holds
   (`AuthoredApiBindings`, `VerbBindingToken`, `AuthoredEventSubscription`) is
   reset here, and every active camera the game chose is cleared here.
10. The engine unloads the level, disconnects the stack's consumers, withdraws
    the game's data subtypes while the module is still mapped, and drops the
    stack.

## What the engine owns and what it will not decide

`RuntimeContent` (`engine/include/app/RuntimeContent.h`) is the process's
content: the asset stack, the mounted roots, the scene serialization context,
hot reload. `LoadedLevel` (`engine/include/app/LoadedLevel.h`) is what is
loaded: one cooked scene in the play zone, or one partitioned world streamed
around a focus. It loads and publishes content, and stops there.

The engine does **not**:

- activate a camera (`ActiveCameraService` is the game's to set; see
  [camera.md](camera.md)),
- admit a participant or ask for a body (`Engine::AdmitLocalParticipant` and
  `RequestParticipantBody` are called by the game when *it* decides content is
  ready),
- choose a body. `ParticipantBodySpawns`
  (`engine/include/participant_spawn/ParticipantBodySpawns.h`) holds a prefab
  body request from the spawn in flight to its assignment or withdrawal, but
  which prefab, where it stands, and when there is somewhere for it to stand
  are the callbacks a game installs into it,
- choose a streaming focus after the initial one (`WorldPartitionRuntime`'s
  focus API is the game's),
- add `LocalLookControl` or any other controller facility to the body this
  machine drives (`SetLocalControlSubject` publishes identity only),
- put anything in the replicated table outside a session
  (`SessionParticipantProjection` stamps replication state only when one
  exists, and `Engine::ProjectSessionStart` stamps retroactively when hosting
  begins).

## Console commands a session restricts

A command that changes the world declares the session's rules in its flags
rather than checking them in its callback. `ConsoleCommandFlags::Cheat` refuses
it in a session while `net.cheats` is off, the host included;
`ConsoleCommandFlags::AuthorityOnly` refuses it on a client. A command that
mutates gameplay state carries both, as `scene.spawn` and `nav.agent.goto` do;
a read-only verb such as a status report carries neither. Outside a session
neither rule applies. `ConsoleRegistry::CheckSessionRules` applies them before
the callback runs, with the same wording a refused `Cheat` or `Replicated` cvar
gets, and a deferred command is judged when it runs rather than when it was
queued. `map` and `world` carry neither: a client runs `map` itself when it is
admitted, and the level change admits a first load on any machine.

The console's `ConsoleAuthorityPolicy` and the World's `SimulationAuthority`
are one answer read twice: `Engine::PublishSimulationAuthority` writes both from
the session's role: at the start and end of the net pump, after a snapshot,
in the frame's update, and the moment `host`, `connect` or `disconnect` changes
the role. A callback therefore never needs to ask `IsSimulationAuthority` as
well: while the console says there is no session, the World says it decides.

## Learning that a level arrived

There is no level-loaded event. A load attaches a zone, and the attach reaches
the game through `ZoneResidencyContext` like every other residency change:

```cpp
void MySpawnSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    const ZoneId play = Owner->Level().PlayZone();
    for (const ZoneResidencyChange& change : ctx.Changes)
        if (change.Zone == play && change.Kind == ZoneResidencyChangeKind::Attached)
            /* publish where bodies go, request bodies, activate a camera ... */;
}
```

Every pawn template does exactly this; `templates/horror` activates the room's
authored camera from the same hook.

## Changing level

`Engine::LevelChanges()` replaces what is loaded with another level, or the same
one again, as one request. A request is built only through a named constructor
that says what the change keeps, so a caller written against an older request
fails to compile rather than meaning something else:

```cpp
const LevelChangeAdmission admission = engine.LevelChanges().Request(
    LevelChangeRequest::Travel(*engine.Level().Loaded(), LevelMemory::Authored, LevelChangeBodies::Fresh));
```

- `Travel(level, memory, bodies)`: the game goes on, in another level or this
  one again. `LevelMemory::Remembered` enters the level as the game left it:
  what its zones remember applies. `LevelMemory::Authored` enters it as content
  made it: what was remembered of that level, and the records waiting in it,
  are forgotten first; every other level's memory stays. `LevelChangeBodies::Fresh`
  releases the bodies and the game's prefab spawns in the new level.
  `LevelChangeBodies::Kept` carries the same bodies across: see "A body across a
  level change" below.
- `NewGame(level)`: every level's memory, every runtime held record and every
  section a restored save carried that nothing registered go.
- `Restore(level, remembered)`: what every level remembers is replaced by a
  save's; bodies come from the save. `SavedGames` makes this one.

`At(arrival)` names the landmark a body arrives at ("Arrivals" below) and
`Focused(zone)` a world's starting zone. `LevelChange::Kind()` says which kind
the latest accepted request was.

How the request and an entity's `ZoneStateLifetime` meet:

| Event | `never` | `at_restore` | `at_detach` |
|---|---|---|---|
| its zone unloads (streaming, or its level is left) | kept | kept | forgotten |
| `Travel`, `Remembered` | applies on arrival | applies | (already gone) |
| `Travel`, `Authored` | that level's forgotten | that level's forgotten | |
| a save is written | in the file | not in the file | |
| `Restore` | replaced by the file's | forgotten in every level | |
| `NewGame` | forgotten in every level | forgotten in every level | |

Anything permanent inside a level entered `Authored` (a course a player re-enters
from the start) must be a counter, since entering it that way forgets its
`never` state too.

The `map` and `world` console commands are `Travel(level, Authored, Fresh)`, so
the first load and every later one take the same path. `LoadedLevel::Loaded()` is a
`LevelReference` (`world/LevelReference.h`): a scene or a world, and the name it
was given. The request is checked when it is made (the level is cooked, no
other change is under way, and, unless nothing is loaded yet, this machine is
the authority and no network session is running) and carried out at the next
zone-residency phase, before streaming, where no frame view is live. A first
load is admitted anywhere because it changes nothing a peer was told: a client
loads the map its authority announced, and a host may load after it began
hosting, since the announcement is refreshed every frame. Carrying it out:

1. on the authority, lets every participant's body go
   (`Engine::ReleaseParticipantBody`, through the game's `ReapBody` policy);
   the participants stay. A `Travel` that keeps its bodies holds each one
   still instead;
2. unloads, so the game's residency systems see their content leave and each
   zone's state is captured;
3. sets the left level's waiting held records aside, so they are in no lookup
   while another level is loaded (`docs/gameplay/held-records.md`);
4. applies the request's kind: a `Travel` into a level `Remembered` puts that
   level's set-aside records back, one into a level `Authored` forgets that
   level and destroys its set-aside records; a `NewGame` forgets every level and
   destroys every runtime held record; a `Restore` installs the save's level
   state and destroys every runtime held record, which the save's sections and
   level lists bring back;
5. records the arrival name and the kept bodies (`LevelArrival`,
   `participant_spawn/LevelArrival.h`) and drops a restored body no spawn has
   taken (`RestoredBodies`), so only the restore that is under way can give one
   out;
6. clears the active camera if it went with the level;
7. loads the requested level.

The game provides bodies when the level arrives, as for any load. `Status()`
moves through `Pending`, `Loading`, `Resident` and `BodiesRestored` (every
participant whose body was let go has one again, and every kept body stands in
the new level), or ends at `Failed` with
`Failure()` saying why. `Serial()` counts accepted requests, so a caller that
holds a screen up while it waits can tell its own change from a later one.
Hiding the change is the game's: a fade, a door, a dialogue box.

Changing level inside a network session is refused: peers are told which map
to load only when they are admitted.

### A body across a level change

`LevelChangeBodies::Kept` keeps each participant's body alive across the change:
the same entity, with its lasting effects, cooldowns, abilities in progress,
leases, counters and held records, because nothing happens to them. Bodies live
in the persistent partition, which an unload does not touch; a request that
would keep a body living in a zone's partition is refused (`BodyInZone`).

Two things are owed in return, and the engine pays both:

- **Standing still while no level stands.** At the commit each kept body that
  has an actor clock takes a time-scale lease of zero, a hold
  (`docs/core-systems-map.md`, actor time), so its movement, effects, cooldowns
  and phases do not advance while there is nothing to stand on. A body whose
  leases are all in use is not held, and that is logged. An effect authored in
  `simulation` time keeps the simulation's clock and does age.
- **Arriving.** `LevelArrival::Kept` lists the bodies waiting for a place. The
  game's `ParticipantBodySpawns::Update` asks its `Select` for each, as it would
  for a new body, and records the root it answers once there is content to
  stand in; the level change then moves the body there through its character
  mover (a teleport), stops it, and lets go of its hold. A game that provides
  bodies itself fills `KeptArrival::Place` the same way.

A kept body arrives at rest and keeps its movement mode. When the change names
an arrival, the root `Update` records is that landmark's ("Arrivals"). What it referred to in
the old level (a target, a tether to geometry) is gone on arrival, as when a
zone unloads under a body in a world; generational ids make those references
fail safe, and ending abilities that depended on them is the game's. A failed
change lets go of its holds where the bodies stand.

### Arrivals

A level authors a named place with a `LevelLandmark` (`world/LevelLandmark.h`):
an entity with a transform and a `name`, placed like any other component in a
scene or in Kyusu (Place, "Level Landmark"). It is opt-in and on no entity by
default. A level change's `At(name)` and a save's `arrival`
(`docs/gameplay/saves.md`, "Writing") name one, and a body arriving through
either stands at it: at the landmark's position and facing in the world, at
rest, from its prefab's default movement mode when it is a new body.

One resolution serves every arrival, in `ParticipantBodySpawns`: a body the
change let go is spawned at the landmark (`LevelArrival::Spawning` lists the
participants whose next body arrives), a body a save restores is spawned at the
landmark its save names, and a kept body is moved there. The game's `Select`
still decides which prefab and supplies the fallback: when no loaded landmark
has the name, the body arrives where `Select` says, and the spawn logs it once.
A later respawn with no level change between is the game's, as before.

The landmark is looked for when the body is placed, which is once the level's
play zone is resident: in a scene level anywhere in it; in a world among the
zones resident then and the world scene. A save names the zone its arrival
stands in as its focus, so a restore streams that zone in first, whichever zone
the player stood in when it was written. A level change into a world focuses
its start zone, or the zone `Focused` names, so an arrival there must be in
that zone's neighbourhood or the world scene ("Finding an arrival in a zone
that is not loaded" in `docs/deferred.md`). Two landmarks of one name in a
level are an authoring mistake; the one with the lowest persistent id wins.

## Bodies from prefabs

A participant's body is asked for synchronously (`ParticipantPolicies::ProvideBody`,
which the engine also calls when a peer joins) or handed over later
(`Engine::AssignParticipantBody`). Both go through the same checks and the same
session projection. An assignment never destroys the entity it was offered: a
refused one is still the caller's.

A prefab spawns asynchronously, so somebody holds the request in between. That
is `ParticipantBodySpawns`: a game constructs one in `OnStart` over the engine's
world and spawn service and gives it three callbacks -- which prefab and where,
the engine's `AssignParticipantBody`, and an optional step that runs on the
landed root before assignment. Its `StartSpawn` is the lifecycle's
`ProvideBody`, answering no body now. The game's spawn system calls `Update`
each frame; a landed spawn is prepared and assigned, a spawn whose
participant left or found a body some other way is withdrawn, and a body a
level change kept is given the root `Select` answers ("A body across a level
change"). `CancelAllPending`
is the game's call when the content a request was made for goes away, and
`Close` runs at the start of `OnShutdown`, before anything the callbacks
captured.

Once assigned, nothing about the body is kept. The spawn is root-owned
(`SceneSpawnLifetime::OwnedByRoot`): when the root dies -- reaped on
retirement, or destroyed by gameplay -- the scene service despawns the rest of
the group at its next pump. A game that vetoes the reap keeps the whole group
for as long as the root lives. Root-owned lifetime is opt-in on any spawn and
acts only on spawns made in that process, so a client never destroys a group
the authority replicates; those leave through the snapshot.

The adapter knows nothing about sessions, cameras, input, or zones, and the
participant layer never learns that a body was a scene spawn
(`scripts/check_engine_layering.sh`, rule F). A game that builds bodies some
other way installs its own `ProvideBody`, or calls `AssignParticipantBody`, and
never constructs the adapter.

## The templates

| Template | What it demonstrates |
|---|---|
| `blank` | the engine needs no game -- and still has a working pause menu |
| `fps` | prefab pawn, mouse look, planar steering, a streamed world |
| `arena` | the FPS with a session; prefab identity on bodies; a networked possession sample |
| `platformer` | a game-made orbit camera; camera-relative steering; a body that faces where it runs |
| `horror` | an authored fixed camera activated on arrival; tank controls |

Each is a complete project that refers to nothing outside its own directory
and installs with the SDK under `share/sencha/templates/<name>/`. See
`templates/README.md` and [building.md](../building.md).
