# Singleplayer FPS template

A first-person game on Sencha: a pawn spawned from a prefab at the level's
player start, mouse look, planar steering, jumping, and a world that streams
around the player. It is the template with world content, and the one to copy
for a game that walks between rooms.

```
project.senchaproj      what the editor opens (name, module, content roots)
CMakeLists.txt          find_package(Sencha) + sencha_game_module(game ...)
src/FpsGame.*           the game module: composition root and console commands
src/FpsSessionPolicy.*  which gameplay features this game's world carries; its input
src/FpsSteeringSystem.* actions -> movement intent, in the body's yaw frame
src/PawnCameraSystem.*  the body's camera child: pitch, exclusion, activation
src/PawnStreaming.*     the world streams around the driven body
src/PawnSpawn.*         where a participant's body comes from
src/FpsStart.h          the level's player start (a tag)
assets/                 authored content; the cook writes assets/.cooked/
```

What this template deliberately lacks: abilities, animation clips, and
networking. It pays for none of them; the arena template is this game with a
session as its point.

## Build

Against an installed SDK:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
```

This produces `build/game.so`. Nothing here rebuilds the engine.

## Play

Open it in the editor (`kettle`, or `kyusu --project $PWD/project.senchaproj`)
and use Cook / Play, or run the host from this directory so its working
directory is the content root:

```sh
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/room_2
/path/to/sencha-sdk/bin/app --game build/game.so +world traversal3
```

Right mouse looks, WASD moves, Space jumps.
