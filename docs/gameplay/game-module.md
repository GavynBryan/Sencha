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
4. The engine composes `RuntimeContent` (the asset stack), mounts the content
   roots, publishes the world's asset resources, connects the spawn services and
   the render pipeline, and constructs `LoadedLevel`. Under a cook-enabled
   build it also watches the mounted roots for hot reload.
5. `OnStart` -- the game sees a mounted, published stack through
   `Engine::Content()` and an empty level through `Engine::Level()`. This is
   where a game registers the world-storage features its world carries
   (`RegisterPhysicsComponents`, `RegisterMovement`, ...), binds its input, and
   installs its participant policies.
6. The startup script runs (`+map`, `+world`, `+host`, ...). `map`, `world`,
   `zone`, `zones`, `scene.spawn`, and `scene.despawn` are engine commands over
   `LoadedLevel`.
7. `OnRegisterSystems` -- the composition root. The engine's own systems
   (hot-reload polling) register after the game's, so a game's ordering edges
   already exist.
8. Frames.
9. `OnShutdown` -- every lease a game took into `Content().Assets()` is
   released here, and every active camera the game chose is cleared here.
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
- choose a body. `BodySpawns` (`engine/include/app/BodySpawns.h`)
  keeps the book on a prefab body request -- the spawn in flight, the landed
  group handed over by its root, the cleanup of what was never handed over or
  whose root died -- but which prefab, where it stands, and when there is
  somewhere for it to stand are the callbacks a game installs into it,
- choose a streaming focus after the initial one (`WorldPartitionRuntime`'s
  focus API is the game's),
- add `LocalLookControl` or any other controller facility to the body this
  machine drives (`SetLocalControlSubject` publishes identity only),
- put anything in the replicated table outside a session
  (`SessionParticipantProjection` stamps replication state only when one
  exists, and `Engine::ProjectSessionStart` stamps retroactively when hosting
  begins).

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

## Bodies from prefabs

A participant's body is asked for synchronously (`ParticipantPolicies::ProvideBody`)
and a prefab spawns asynchronously, so somebody has to own the request in
between. That owner is `BodySpawns`: a game constructs one in
`OnStart` over the engine's world and spawn service, gives it three callbacks
(which prefab and where, the engine's `RequestParticipantBody`, and an optional
step that runs on the landed root before assignment), and installs its
`ProvideBody` and `RequestDespawnBody` as the lifecycle's body policy. The
game's spawn system calls `Update` each frame; the book re-asks the lifecycle
when a spawn lands, withdraws a spawn whose participant left, and destroys a
group whose root died or was never handed over. `CancelAllPending` is the
game's call when the content a request was made for goes away, and `Close`
runs at the start of `OnShutdown`, before anything the callbacks captured.

The book knows nothing about sessions, cameras, input, or zones, and the
participant layer never learns that a body was a scene spawn
(`scripts/check_engine_layering.sh`, rule F). A game that builds bodies some
other way installs its own `ProvideBody` and never constructs the book.

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
