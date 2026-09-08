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

Smoothness is the engine's job, not the camera system's. Simulation runs at a
fixed rate the display does not share, and meshes are drawn between ticks from
`WorldTransformHistory`; a camera drawn from a tick pose would step against
them every frame. Three mechanisms make that hard to do by accident:

- a child of an entity with history is composed, in the presentation domain,
  from the parent's *interpolated* pose (`PropagateTransforms`), so a parented
  camera is drawn where its body is drawn and keeps its frame-fresh local pitch;
- a camera carrying its own history is extracted from that history at the
  frame's alpha (`CameraRenderDataSystem::Build`);
- a system placing a camera relative to an entity reads
  `PresentationPoseOf(world, entity, ctx.Presentation.Alpha)`, never that
  entity's `WorldTransform`. The orbit camera does.

The way to reintroduce the jitter is to read a followed entity's
`WorldTransform` in `FrameUpdate`; `PresentationPoseOf` exists so there is no
reason to.
