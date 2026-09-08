# Third person platformer template

A body that runs where the stick points, relative to a camera that orbits it,
and jumps. The body faces where it runs; nothing aims. The look action turns the
camera and only the camera.

```
src/PlatformerGame.*                composition root: physics, movement, input
src/PlatformerSessionPolicy.*       which features this game's world carries; its input
src/CameraRelativeSteeringSystem.*  stick -> wish in the camera's frame; the body faces it
src/OrbitCameraSystem.*             the game's own camera, orbiting the driven body
src/PawnSpawn.*                     which prefab a body is and where it stands; the play zone
src/PlatformerStart.h               the level's player start (a tag)
assets/                             one room; the cook writes assets/.cooked/
```

What it deliberately lacks: authored abilities. The ability kit itself is
registered, because movement resolves a pawn's speed through the kit's
attributes, but no ability is authored on the pawn. Double jump, dash, and
wall cling are the axis this template grows along.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/platformer_room
```
