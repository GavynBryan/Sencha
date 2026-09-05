# Multiplayer arena shooter template

The FPS template with networking as its point: one room, a prefab pawn, mouse
look, planar steering, and a session. `host [port]` opens one, `connect
<address>` joins one, and every peer's pawn is spawned by the authority from the
same prefab and predicted on its own machine.

It is a copy of `fps/`, not a layer over it. The two share their steering,
camera, and spawn code by duplication, on purpose: a game copied from either
one should not find half of itself living in the other.

```
project.senchaproj        what the editor opens
CMakeLists.txt            find_package(Sencha) + sencha_game_module(game ...)
src/ArenaGame.*           composition root: physics, movement, input, controller, net
src/ArenaSessionPolicy.*  which gameplay features this game's world carries; its input
src/ArenaSteeringSystem.* actions -> movement intent, in the body's yaw frame
src/PawnCameraSystem.*    the body's camera child: pitch, exclusion, activation
src/PawnSpawn.*           where a participant's body comes from; NetSpawnPrefab
src/ArenaStart.h          the level's player start (a tag)
samples/turret/           networked possession: a turret a player can take
assets/                   authored content; the cook writes assets/.cooked/
```

The pawn carries `NetSpawnPrefab` so a later host knows how peers instantiate a
body that already exists; whether it is *replicated* is the engine's decision,
made when a session exists. Hosting after playing alone stamps the player who
was already here.

## The turret sample

`samples/turret/` is one deletable directory: remove it and the one `include`
line in `CMakeLists.txt`, and the three calls in `ArenaGame.cpp` that install
it. It exists to exercise networked possession end to end -- a client asks the
authority for a thing it can only name through replication, and gets to drive
it -- and it is how the two-process tests prove that path.

## Build and play

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/arena_room +host 0
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/arena_room +connect 127.0.0.1:27500
```

`turret place` puts a turret down; `turret` takes the nearest one or leaves the
one you are in.
