# Cameras

The engine's camera vocabulary is small on purpose: a component that says an
entity is a camera, a service that says which one is active, a component that
says what a camera leaves out of its picture, and a query. How a camera is
placed, what it follows, and whose input turns it are a game's decisions, made
in the game's systems.

## Mechanisms

- `CameraComponent` (`components/CameraComponent.h`) -- projection parameters
  on an entity with a transform. Authored in scenes.
- `ActiveCameraService` (`components/ActiveCameraService.h`) -- a world
  resource naming the entity rendered from. The engine never sets it; a game
  does, and clears it in `OnShutdown`. With none active, or with an active
  entity that has died, render extraction draws nothing and does not fail.
- `CameraExclusion` (`camera/CameraExclusion.h`) -- on a camera entity, the one
  entity that camera does not draw. Runtime-only: what a camera excludes
  follows from who is looking through it. Read by `render/extract/Camera.cpp`;
  a dead excluded entity excludes nothing.
- `FirstAuthoredCamera(world, partition)` (`camera/CameraQueries.h`) -- the
  first `CameraComponent` in a storage partition, for a level that authors its
  own view.

That is all of it. There is no rig, no mode, no follow system: those were one
game's camera written into engine vocabulary, and they are gone.

## Camera policy is game code

Three templates, three policies, none of them shared:

**First person** (`templates/fps/src/PawnCameraSystem.cpp`). The pawn prefab
places a camera as a child of the body. When the local control subject changes,
the system finds that child, makes it active, and gives it
`CameraExclusion{body}`. Each frame it writes the child's local pitch from the
body's `LookOrientation` plus whatever look input has accumulated since the
last tick; yaw and position come through the transform hierarchy from the body,
which `AimFacing` already turns. The camera is part of the body's authored
content, so it travels with the prefab.

**Orbit** (`templates/platformer/src/OrbitCameraSystem.cpp`). The game makes
its own camera entity in the persistent partition and places it every frame at
a fixed boom behind a pivot above the driven body. The look action turns the
orbit and only the orbit; the body does not aim. Steering reads the orbit's yaw
back out of one resource, so the two agree.

**Fixed** (`templates/horror/src/FixedCameraSystem.cpp`). The level authors the
camera. When the play zone attaches, `FirstAuthoredCamera` on that partition
becomes active; when it detaches, nothing is. Nothing follows anybody. Cutting
between several authored cameras as the player crosses a threshold is the
obvious next step and deliberately absent: it wants a trigger mechanism the
engine does not have.

## Writing one

A camera system is an ordinary `FrameUpdate` system. It reads the presentation
snapshot (`InputActionState::Frame()`), never the tick record, and declares no
ordering edge against input resolution (see [input.md](input.md), "Reading
actions"). It owns which entity is active and clears it when its subject goes
away. It reads whatever simulation state it presents -- an orientation, a
position -- and writes the camera's transform; it does not write the simulation.

A parented camera inherits its parent's tick pose through transform
propagation, which is exact but steps at the tick rate; a camera the game places
from an interpolated pose is smoother and is the game's to write.
