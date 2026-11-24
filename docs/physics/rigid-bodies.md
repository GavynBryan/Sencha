# Rigid bodies

An entity with a `Collider` is static world geometry. `RigidBody`
(`physics/components/RigidBody.h`, scene key `RigidBody`) gives its collider a
body that moves:

| Field | Meaning |
|---|---|
| `motion` | `static`, `kinematic` (moved by setting its transform; pushes what is dynamic and ignores it) or `dynamic` (falls, collides, is pushed) |
| `mass` | kilograms; zero or less takes the mass from the shape |
| `gravity_scale` | how much of the world's gravity a dynamic body falls with |
| `linear_damping`, `angular_damping` | the fraction of velocity and spin lost each second |
| `linear_velocity`, `angular_velocity` | metres and radians per second in world space |
| `asleep` | a dynamic body that does not move until something touches it |

Every field has a default, so a level authors only what it changes:
`"RigidBody": {"motion": "dynamic"}` is a crate that falls. Kyusu offers the
choice as the collider section's Motion row: anything but Static adds the
component, Static removes it, and the component's own section shows the rest.

## The component is the round-trip state

`RigidBodyBinding` creates a body from the component, so the velocities and
`asleep` a level authors are what the body starts with: a prop that starts
moving, a stack that starts settled. After every step it pulls each awake
dynamic body's pose, velocities and sleep back into `LocalTransform` and
`RigidBody`, and it captures the same when a body is removed for a zone that
leaves physics. A body built again from the component therefore carries on
where it was. Damping is read when the body is created; gravity scale is
pushed every step, so a run-time edit acts on the next tick.

- **Sleep.** A body the backend puts to sleep has its last pose, zero
  velocities and `asleep` pulled once; while it sleeps the pull leaves its
  row alone, so a room of settled props writes nothing per step. A body
  created from a component that says `asleep` is created asleep
  (`BodyDesc::StartAsleep`) and at rest, so loading a room of sleeping props
  wakes none. A touch wakes it, and the next pull clears `asleep`. When a
  zone leaves physics, every leaving body's state is captured before any is
  removed: removing a floor wakes what rests on it, and a crate captured
  after its floor went would be saved awake.
- **Under a parent.** The backend holds world poses. The pull writes a
  parented body's pose relative to its parent's `WorldTransform`, so its
  `LocalTransform` composes to where the body is, as every other child's does.
- Kinematic and static bodies are pushed from the entity's world transform and
  never pulled.

## Saves

`RigidBody` is `Saved` (`docs/gameplay/saves.md`, "What a save holds of an
entity"): a level entity's patch keeps how its body differs from what the
level authored, its velocities and sleep included. A body code adds at its
defaults writes nothing. A dynamic body also holds its entity's placement, so
the patch keeps where it went (`docs/gameplay/saves.md`, "Placement"); a
kinematic body's transform is derived from whatever moves it and is not kept.
