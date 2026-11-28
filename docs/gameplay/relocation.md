# Relocation

Moving an entity discontinuously is one operation with one owner:
`RelocateEntity` (`engine/include/world/relocation/Relocation.h`). It moves an
entity and everything below it through a rigid mapping, from the space it is in
to the space it arrives in, and relocates every piece of world-space state the
group holds: positions are mapped, velocities and directions turned, facings
turned about world up, and state that only makes sense where it was found is
reset or ended. It advances each moved entity's `RelocationSerial`, records the
mapping for the frame, and carries the transform history across so the move is
drawn without a streak or a hitch.

A level change's carried entities arrive through it (`app/LevelCarry.h`,
`docs/gameplay/game-module.md`, "Carrying"). Link traversal and any later
teleport call it as well; none of them writes its own transfer.

## The operation

```cpp
struct RigidRelocation
{
    Transform3f Mapping;                          // old space to new space
    std::optional<StoragePartitionId> Partition;  // unset: stay where it is
    float TickFraction = 0.0f;                    // where in the tick it crossed
};

RelocationResult RelocateEntity(World&, EntityId root, const RigidRelocation&);
```

The caller supplies the destination partition. A level change takes it from
`ZonePlacement::PartitionAt`; a link will take it from the link's destination
zone. The operation never infers a zone from a position.

What moves is the root and its descendants through `Parent`
(`RootAndDescendants`, `world/transform/TransformHierarchy.h`). An attachment is
parented to what wears it, so it is one of them. Held records follow through the
partition move's followers (`DispatchPartitionMoves`), as they do for every
partition move.

In order, it:

1. refuses a root that is gone, a root that has a parent (its transform is in
   its parent's space, not the space the mapping maps), a mapping that is not
   rigid (unit scale within `kRigidScaleTolerance`), and a group in which any
   member carries a component whose rule refuses. Nothing has changed when it
   refuses, and a refusal names the component by its stable name;
2. moves every member into the partition, when one is given;
3. runs every `Maps` and `MapsWhenUpright` rule for every member, then every
   group rule once, then every `Mirrors` rule;
4. advances each member's `RelocationSerial` and records each member's
   relocation in `FrameRelocations`.

It runs on the owner thread outside any query. It is structural only when it
moves partitions, or when a member has no `RelocationSerial` yet: the serial is
owed by `WorldTransformHistory` (its `DerivedComponents`), so anything drawn
between ticks, every character included, already carries it and is relocated in
place. The cost is a scan of every `Parent` in the World to find the group, then
rules times members: a one-off for an event, never a per-frame walk.

### Policy stays with the caller

The operation decides what a relocation does to state. Who may be relocated and
where to is the caller's. A level change refuses bodies, held records, parented
entities and childed entities that are not scene instance roots, takes a runtime
id when the level it enters holds the same one, keeps the arrival with its zone,
delivers `level.arrived`, and snaps the history of every member of the group
because they were out of every simulation during the load. A refusal loses the
entity and the log names the refusing component. None of that is in the
operation.

## Rules

A component that holds world-space state registers one rule in the World's
`RelocationRules` resource (`world/relocation/RelocationRules.h`). A component
with no rule holds none. A later component adds its own rule, from the feature
that owns it or from a game module; nothing edits the operation.

| Kind | Runs | For |
|---|---|---|
| `Maps` | under every rigid mapping | state that maps, turns or resets |
| `MapsWhenUpright` | only under a mapping that keeps world up | state held about world up |
| `Mirrors` | after every `Maps` rule of the group | a backend's copy of relocated state |
| `Refuses` | never: the entity is refused | world-space state nothing defines carrying |

`SetRelocationRule<T>` registers one, `RefuseRelocation<T>` registers a refusal,
and `RelocateValue<T, fn>` adapts a function of the component's own value. One
rule per component; a second replaces the first. A rule may write components and
resources but not change archetypes, and sees the whole group in
`RelocationFrame::Group`. Group rules (`AddGroupRule`) run once per relocation
with the whole group. No engine component refuses today; the kind exists for one
whose state nothing can carry and nothing can end.

Classification is closed: `RelocationClassification` fails for an engine
component that has neither a rule nor a place in its list of components holding
no world-space state, so a new component is classified when it is added.

Each feature registers its rules beside its component registration, and
`RegisterEngineRelocation` (`world/RuntimeComponentSchema.h`) asks each in turn.
`RuntimeWorld` calls it, so every runtime World has them. Rules that reach a
backend need the backend and are installed by the host that owns it: the engine
calls `InstallPhysicsBackendRelocation` once the game's physics is registered.

### What the engine registers

| State | Rule |
|---|---|
| `LocalTransform` | mapped, on an entity with no parent; a child's is in its parent's space |
| `WorldTransform` | mapped |
| `WorldTransformHistory` | carried ("Presenting" below) |
| `KinematicState.Velocity`, `MovementIntent.WishDir`, `MotionBursts` velocities, `MotionImpulse`, `MotionAxisOverride.PlanarVelocity`, `LocomotionOutput` and `MotionRequest` velocity and up axis | turned |
| `SupportState`, `Immersion` | reset; found again by the first sweep on the far side |
| `TetherSession` | a point anchor mapped; an entity anchor left alone |
| `ClingSession` | anchor and normal carried when the surface is in the group; otherwise a forced `RequestLocomotionMode` to the free mode, so the mode's own exit ends it |
| `LookOrientation.Yaw` | the mapping's turn about world up added (`MapsWhenUpright`) |
| `RigidBody` linear and angular velocity | turned |
| `ProjectileMotion.Velocity` | turned |
| `ObservationMemory` | emptied, as the level change always did |
| `DecisionAgent` | `ForgetDecisionPlace`: claim, target, destination and contact let go |
| `NavAgentRequest` | given again with a fresh id, a point order's point mapped |
| sensor pairs touching the group | forgotten (group rule) |
| contact lines that are in the group or out from a member | drawn home through their own exit, `RetractContactLinesOf` (group rule); the tip stays where it was while it draws home (`docs/deferred.md`, "A contact line drawing home across a relocation") |
| `PhysicsBodyLink` | the live body teleported to the relocated pose; a dynamic, awake body's backend velocities set from the turned `RigidBody` (`Mirrors`) |
| `CharacterMoverLink` | the mover moved to the relocated position without a snap (`Mirrors`) |

A rigid body's pose and a dynamic body's velocities are pulled from the backend
each step once the body exists, and a character's position is its mover's, so
writing the components alone would be undone by the next step. That is why the
two backend rules exist, and why they mirror rather than map: they read the
components the `Maps` rules already relocated. A relocation outside the physics
step is assumed; a relocation that splits a physics step is for the caller to
avoid.

### State another entity holds about a member

Some state about a relocated entity lives on another entity: a contact line's
tip and the leash it holds its instigator by, a tether whose anchor is a member,
a cling whose surface is a member. A rule for the holder's component, or a group
rule, decides it, since only the relocation knows the group. What is still
meaningful is carried (a tether's or cling's anchor that moved with the group);
what is anchored in the space left behind ends through its owner's own exit (a
contact line draws home, so its leash lets go). A line drawing home keeps its tip
in the space the instigator left, so its span stretches across until it is
home; a level change never shows that, a link crossing would, and it is
deferred to that stage. A tether whose anchor entity
stays on the far side of a mapping is the caller's question: link traversal
answers it, this stage does not.

A held physics object, when it exists (a body carried by a driven pose), registers
a rule that follows its holder across a relocation. It never drops what it holds.

### State that needs no rule

Searched for every component with a position, direction or heading member and
left without a rule on purpose. The list is data in
`test/runtime/RelocationClassificationTests.cpp`, beside every component that
holds no world-space state at all:

- offsets in the entity's own frame: `PresentationOffset`, `Attachment` offsets,
  `AimOrigin`, `HearingSensor.EarOffset`, `SightSensor.EyeOffset`,
  `SightTarget.SampleOffset`, `NavLink` offsets (authoring only);
- extents: `IrradianceVolume`, `WorldDock`;
- records of something that happened where it happened, which stays there:
  `AbilityCueLog` entries and `ActiveAbilities.LastPoint`;
- `NavAgentProgress`: it describes the order the reissue supersedes, and the
  follower replaces it when it plans again;
- `FlightSession`: it holds no world-space state;
- `PendingLookInput`: a World resource of look displacement not yet consumed. A
  displacement from the aim, not a heading, so it needs no turn once the aim's
  yaw has turned.

Pose state held outside components (the animation pose pool's model-space poses,
the camera's presentation state) is not the operation's: the pose pool is local
to the skeleton, and the cameras answer a relocation in their own stage.

## Facing and a tilted mapping

`LookOrientation` is yaw and pitch about world up, and the character capsule
never tilts. A mapping keeps world up when the cosine between up and the mapped
up is within `kUprightCosineTolerance` of one (`math/geometry/3d/RigidMapping.h`).
Under such a mapping the yaw rule adds the mapping's turn about up
(`YawAboutWorldUp`).

Under a mapping that tilts world up, a yaw has no meaning to carry. The
operation keeps every `MapsWhenUpright` rule's state as it was, still relocates
everything else, and says so in `RelocationResult::KeptUprightState`. It does not
refuse: a tumbling body that happens to carry an aim crosses as a body. Link
traversal does not offer a character a tilted mapping at all; that is the
link's policy, not the operation's.

## `RelocationSerial`

A replicated per-entity counter (`world/relocation/RelocationSerial.h`), owed by
`WorldTransformHistory` and otherwise added by the first relocation, advanced by
one on every relocation of the entity and of each entity below it, since each
one's world pose jumped. A replicated entity drawn between ticks sends the zero
once in its first snapshot; after that it costs nothing until it changes. A consumer that
remembers the value it last saw knows the entity moved discontinuously since then
when the two differ. Only equality means anything; it may wrap.
`RelocationSerialOf` answers zero for an entity never relocated.

It answers the question for any clock: remote interpolation, pawn commands, and
a presentation consumer that keeps its own history of an entity's pose
(an emitter that emits along an entity's path) can each compare it. It is not
saved: a load is a discontinuity of its own.

Moves that are not a mapping do not advance it: placing a spawn, a debug or
level-change body placement (`CharacterMoverPool::SetPosition`), a reconciliation
restore. They snap the history instead (`docs/deferred.md`, "Discontinuous
moves without a mapping").

## `FrameRelocations`

A World resource listing the frame's relocations in order: entity, serial
after, mapping, tick fraction. A consumer that wants to carry its own history
across reads the mapping; one that only needs to reset compares serials and
never reads it.

The engine's `EndFrame` phase empties it with `ClearFrameRelocations` after the
`EndFrame` systems have run. A host that drives its own frames (a test, a tool
that ticks a World by hand) calls `ClearFrameRelocations` at its own frame end;
one that never does sees the record grow by one entry per member per
relocation.

Its bound per frame is the sum over the frame's relocations of each group's size:
the members moved, times how often. It keeps its storage across frames, so a
frame with no more relocations than an earlier one allocates nothing, and
`FrameRelocations::HighWater` reports the largest frame seen. Both the frame's
count and the high-water mark are reported with the frame's timing sample
(`TimingWorldCounts`): the timing panel shows them, and a frame capture writes
them as `relocations_count` and `relocations_high_water_count`. There is no cap:
each entry is an event something caused, and the work is proportional to it.

## Presenting

`WorldTransformHistory` blends the last two captured poses at the frame's
presentation alpha. A snap would draw a relocated entity at its new pose for the
whole frame, losing up to a tick of its motion exactly when the player watches
it cross. A relocation is carried instead:

- the relocation marks the history `Carrying` with the mapping (a rotation and a
  translation; a rigid mapping has no scale) and the tick fraction. A frame drawn
  before the next capture, which only a relocation outside a tick allows, draws
  the blend carried through the mapping: where the entity now is;
- the capture that closes the tick stores `Previous` carried through the mapping,
  so `Previous` to `Current` is continuous motion in the new space, and marks the
  history `Crossing`;
- for a presentation alpha before the crossing fraction, the blended pose is
  carried back through the inverse mapping: the entity is still drawn on the
  side it came from. At and after it, the blend is drawn as is;
- the next capture clears it.

A tick fraction means where in the tick that the next capture closes the entity
crossed, which is where the presented blend crosses. It defaults to zero, the
right answer for a relocation outside a tick: drawn on the far side for the whole
of the next blend. A relocation with no continuity worth keeping is followed by
`RequestTransformHistorySnap` on every member. The history's lifecycle is one
enum (`Snapping`, `Steady`, `Carrying`, `Crossing`), so a snap outranks a carried
relocation whichever was asked for first: carrying a snapping history does
nothing. A second relocation before the capture composes with the first and
crosses at the later of the two fractions; one blend cannot show two crossings.

`ResolvePresentationPose` is the one place that decides which side of a
relocation an entity is drawn on. `PresentationPoseOf`, mesh and shadow-caster
extraction and presentation-domain transform propagation all go through it, so
cameras, attachments, children and extraction agree. A child is drawn through
its parent's resolved pose.

## The frame-wide discontinuity bus

`TemporalDiscontinuityReason::Teleport` is not published by a relocation. The
bus is frame-wide: marking a discontinuity collapses every entity's transform
history for the frame and tells every subscriber to drop its history. A
relocation is a per-entity event; the serial and `FrameRelocations` are its
signal.
