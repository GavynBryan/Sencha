# Cameras

The engine's camera vocabulary is small on purpose: a component that says an
entity is a camera, a service that says which one is active, components that
say what a camera leaves out of its picture and whose view it presents, a
query, and two presentation effects that move or widen the view without moving
the camera. How a camera is placed, what it follows, and whose input turns it
are a game's decisions, made in the game's systems.

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
- `CameraSubject` (`camera/CameraSubject.h`) -- on a camera entity, whose point
  of view it presents: the pawn a first-person eye sits in, or the body an orbit
  boom follows from outside it. Runtime-only, set with `SetCameraSubject` by the
  camera policy. Presentation effects meant for one viewer reach only the view
  their subject owns (`CameraViewIsOwnedBy`); a camera that names no subject
  presents its own.
- `FirstAuthoredCamera(world, partition)` (`camera/CameraQueries.h`) -- the
  first `CameraComponent` in a storage partition, for a level that authors its
  own view.
- Camera shake and field-of-view scale -- below.

There is no rig, no mode, no follow system: those were one game's camera
written into engine vocabulary, and they are gone.

## Camera policy is game code

Three templates, three policies, none of them shared:

**First person** (`templates/fps/src/PawnCameraSystem.cpp`). The pawn prefab
places a camera as a child of the body. When the local control subject changes,
the system finds that child, makes it active, and gives it
`CameraExclusion{body}` and `CameraSubject{body}`. Each frame it writes the child's local pitch from the
body's `LookOrientation` plus whatever look input has accumulated since the
last tick; yaw and position come through the transform hierarchy from the body,
which `AimFacing` already turns. The camera is part of the body's authored
content, so it travels with the prefab.

**Orbit** (`templates/platformer/src/OrbitCameraSystem.cpp`). The game makes
its own camera entity in the persistent partition and places it every frame at
a fixed boom behind a pivot above the driven body. The look action turns the
orbit and only the orbit; the body does not aim. Its camera's `CameraSubject`
is the body, so effects for that player reach the boom. Steering reads the orbit's yaw
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

## Camera shake and field-of-view scale

Two presentation effects change what the active camera shows without moving
the camera. Both are cosmetic verbs a cue presentation, an animation event or a
response names, so a game adds one in data and writes no camera code. Each
machine that presents runs its own: the cue that asked for one replicates as
cues do (`docs/gameplay/abilitykit.md`, "Presentation"), and the effect itself
is local, never replicated and never read by the simulation.

| Verb | Kind | What it does |
| --- | --- | --- |
| `camera.shake` | instant | shakes the view once by a `camera.shake` profile |
| `camera.shake.hold` | lease | shakes it by a profile while held, then releases |
| `camera.fov_scale` | instant | multiplies the field of view for a moment |
| `camera.fov_scale.hold` | lease | multiplies it while held, then eases back |

They are separate on purpose. A shake moves the view's pose; a scale changes
its projection. A player can turn either off without the other
(`camera.shake.scale`, `camera.fov_scale.intensity`, both player settings), so
something a game makes readable through one still reads when the other is off.

**The envelope** (`camera/CameraEffectTiming.h`) is shared: `attack`, `hold`
and `release` seconds on smoothstep ramps. A held effect rises over `attack`,
stays at full strength for as long as its lease is held (`hold` does not
apply), and on release falls from wherever it had reached. A holder whose
entity is gone lets go.

**Time** is the fixed clock's: the last completed tick plus the frame's alpha.
An effect started by a tick's presentation is zero seconds old once that tick
has run. The same instant is the same shake at any frame rate, and a suspended
simulation (the pause menu) stops tick and alpha together, so effects freeze
and resume where they were.

**Whose view.** Both verbs take an optional `subject`. With one, only the view
whose `CameraSubject` is that entity presents the effect: a pawn's landing
shakes its player's view, not a spectator's or another player's. Without one,
every view does, which is what an earthquake wants.

### Shake profiles

A `camera.shake` data asset (`camera/CameraShakeProfile.h`):

```json
{
  "noise": { "rotation": [1.2, 0.6, 0.8], "translation": [0.02, 0.03, 0.0] },
  "kick":  { "rotation": [-1.5, 0, 0], "translation": [0, -0.06, 0] },
  "frequency": 18,
  "attack": 0.0, "hold": 0.04, "release": 0.3,
  "falloff": { "inner": 2, "outer": 12 }
}
```

Rotation is pitch, yaw and roll in degrees (positive pitch looks up);
translation is metres along the camera's right, up and back. `noise` is how far
each axis oscillates, at `frequency`; `kick` is a fixed displacement in one
direction. Both are scaled by the envelope, so a recoil is a pitch kick with no
attack and a short release, a landing a downward kick with some noise, and an
earthquake noise held by a lease. Noise is one-dimensional gradient noise per
axis, a pure function of elapsed time and the verb's `seed`.

`falloff`, with the verb's `point`, weakens the shake linearly from `inner` to
`outer` metres from the camera. A profile is refused when a value is not finite,
a time is negative, noise has no frequency, the frequency exceeds 120 Hz, or the
falloff is not `0 <= inner < outer`.

Every shake presented this frame is summed, then bounded per axis by
`camera.shake.max_angle` and `camera.shake.max_offset`. At most
`CameraShakeState::kCapacity` run at once; a new one replaces whichever
contributes least.

### Field-of-view scale

`camera.fov_scale` takes `scale`, the multiple of the camera's own field of view
at full strength (below one narrows, above one widens), and the envelope's three
times. A multiple rather than an angle, so a camera whose field of view a game
or a player set keeps it. Several multiply, the product held within
`kCameraFovScaleMin` and `kCameraFovScaleMax`. It applies to perspective cameras.

### Where they apply

Render extraction advances the effects to the frame's presentation time and
sums them for the camera it is about to draw (`PresentCameraEffects`,
`camera/CameraEffects.h`), so nothing runs where no window presents.
`CameraRenderDataSystem::Build` then composes the shake into the pose it
draws from, in the camera's own space, and scales the field of view it projects
with. The camera's transform is never written, so aiming, traces and anything
else that reads it see the camera where the simulation put it. The frustum is
built from the shaken view, so culling agrees with what is drawn. A child of the
camera, such as a held weapon, keeps its place in the world while the view
moves around it.

That view is `PresentCameraView` (`camera/PresentedView.h`): the shaken pose and
the scaled, clamped field of view, which `Build` draws from and anything laid
over the frame reads too. Because aiming reads the unshaken camera, a reticle at
the centre of the screen misreports the aim during a shake; one that follows it
projects the camera's forward through the presented view with
`ProjectDirectionToViewport` (`render/CameraProjection.h`). Effects are summed
during render extraction, so a reader earlier in the frame sees the view the
previous frame was drawn with.
