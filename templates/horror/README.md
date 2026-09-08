# Fixed camera survival horror template

Tank controls under a camera the level places. W and S walk the body along its
own facing, A and D turn it in place, and the room's authored camera is what the
player looks through -- the controls do not depend on where it is, which is what
lets a fixed camera cut between angles without the stick flipping.

```
src/HorrorGame.*            composition root: physics, movement, input
src/HorrorSessionPolicy.*   which features this game's world carries; its input
src/TankSteeringSystem.*    stick -> walk along facing, turn in place
src/FixedCameraSystem.*     the room's authored camera becomes the view when the room arrives
src/PawnSpawn.*             which prefab a body is and where it stands; the play zone
src/HorrorStart.h           the level's player start (a tag)
assets/                     one room with one authored camera
```

What it deliberately lacks: camera cuts between rooms, triggers, inventory,
interaction. Several authored cameras and switching between them as the player
crosses a threshold is the obvious next step; it wants a trigger mechanism the
engine does not have yet.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
cmake --build build
/path/to/sencha-sdk/bin/app --game build/game.so +map levels/horror_room
```
