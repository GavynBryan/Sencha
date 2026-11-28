# Relocation and Character Traversal

Status: proposed execution specification, revision 2 (2026-11-27). Owns the geometry
kernel (Stage 1), the relocation operation (Stage 2) and character traversal (Stage 3).

## 1. Geometry kernel

Pure functions over explicit values in `engine/include/math/geometry/3d/`. They reach
into no ECS, renderer, physics backend, resource or cvar. Every consumer in this suite
uses them: character crossing, linked view cameras, particles, traces, the orbit boom,
light images, bake rays and body classification.

### 1.1 Operations

- signed distance of a point to an aperture plane;
- projection of a point into aperture right and up coordinates;
- point in rectangle with an explicit edge tolerance;
- sphere and capsule support interval along the aperture normal;
- segment against aperture: crossing fraction, crossing point, direction of crossing;
- the mapping from two frames, and its application to a point, direction, orientation,
  linear velocity and angular velocity;
- one-hop segment continuation: given a segment and a span of resolved links, the first
  aperture it passes through from the front, and the remainder of the segment carried
  through the mapping;
- the camera carried through a mapping;
- the destination plane as an oblique near plane for a given view;
- conservative screen bounds of an aperture.

One-hop segment continuation is the operation most consumers share. It is written once.

### 1.2 Tolerances

Named constants, one per purpose: plane side, rectangle edge, capsule fit reduction,
exit separation, clear hysteresis, parallel segment, upright. No shared epsilon. Tests
cover just below, at and just above each.

### 1.3 Tests

Table-driven: head on, oblique, vertical, upside down, near parallel, edge, corner,
high speed, inverse round trip, velocity and angular velocity under rotation, upright
classification, segment starting on the plane, segment ending on the plane.

## 2. The relocation operation (Stage 2)

### 2.1 Why it is its own stage

When this plan was written, `LevelCarry::Arrive` moved entities carried across a level
change through a rigid mapping: it placed the root, rotated rigid-body and kinematic
velocity, moved the group's storage partition, snapped transform history, cleared
perception memory and sensor pairs, and reissued a navigation order with its point
carried across. Link traversal needs the same operation with a different mapping.

Stage 2 extracted it, with no dependency on links. The owner then removed landmark
carry (2026-11-28): a level change moves bodies only, and moving through the world is
zone streaming. The operation and its rules stay. Until Stage 3 it has no caller and is
covered by its own tests.

### 2.2 Shape

```cpp
struct RigidRelocation
{
    Transform3f Mapping;                              // old space to new space
    std::optional<StoragePartitionId> Partition;      // unset: stay where it is
    float TickFraction = 1.0f;                        // where in the tick it happened
};

RelocationResult RelocateEntity(World&, EntityId root, const RigidRelocation&);
```

The caller supplies the destination partition. A link gets it from the link's
destination zone. The
operation never infers a zone from a position (D1).

### 2.3 What it transforms

A component that holds world-space state registers one relocation entry: transform a
point, rotate a vector, add to a yaw, or reset. The operation walks the entries for the
components the entity has. A later component adds an entry; nothing edits the operation.

Entries Stage 2 registers, from the state the character stack holds today:

| State | Entry |
|---|---|
| `LocalTransform`, `WorldTransform` | transform |
| character mover position (`CharacterMoverPool::SetPosition`) | transform; this is the authoritative position, writing the transform alone is undone next tick |
| `KinematicState.Velocity` | rotate |
| `RigidBody` linear and angular velocity, and the live body through `PhysicsBodyLink` | rotate; `PhysicsWorld::TeleportBody` then set both velocities, because the components are pull-only once a body exists |
| `MotionBursts` per-burst velocity, `MotionImpulse`, `MotionAxisOverride`, `LocomotionOutput`, `MotionRequest` | rotate |
| `MovementIntent.WishDir` | rotate |
| `LookOrientation.Yaw`, `PendingLookInput` | add the mapping's yaw; requires an upright mapping |
| `SupportState`, `Immersion` | reset; support is found again on the far side |
| `TetherSession`, `ClingSession`, `FlightSession` anchors | end the session through its own exit, not by editing its anchor |
| `ObservationMemory`, decision place and claims | clear |
| sensor pairs touching the group | forget |
| navigation order | reissue with its point carried through |
| `WorldTransformHistory` | section 2.5 |

Attachments and held records follow through `DispatchPartitionMoves`, which already
exists. They carry no history of their own.

Verify at stage start: that the list is complete. Search for every component with a
`Vec3` or `Quat` member whose meaning is a world position or direction. An entity
holding one with no entry is refused by name, not relocated silently wrong.

A caller's own refusals (which entities it is willing to move at all) stay with the
caller. They are policy, not part of the operation.

### 2.4 `RelocationSerial`

A component holding a counter, advanced once per relocation, replicated to every
machine. It answers "was this entity moved discontinuously since I last looked" for any
consumer on any clock:

- replication interpolation does not blend two samples with different serials (`06`);
- pawn commands name the serial they were sampled under (`06`);
- a presentation consumer that keeps its own history of an entity compares serials;
- the particle system's attached and distance emitters use it (`06`).

Beside it, a World resource keeps the mappings of the current frame's relocations keyed
by entity, so a consumer that wants to carry its own history across can, and one that
only wants to reset need not read it. The resource is cleared at the end of the frame's
presentation.

`TemporalDiscontinuityReason::Teleport` on the frame-wide bus is published as well. It
exists today and nothing publishes it.

This resolves `docs/deferred.md`, "Presentation consumers observing a teleport". That
entry asks for a transform-history contract change, not a workaround in one consumer;
the serial and the next section are that change. The particle plan's second phase waits
on the same entry. Whichever effort reaches it first lands Stage 2.

### 2.5 Presenting a relocation

`WorldTransformHistory` holds `Previous`, `Current` and `Snap`. A snap draws the entity
at `Current` for the whole frame, which loses up to one tick of motion. At a low tick
rate that is a visible hitch exactly when the player is watching the aperture.

Stage 2 lets history carry a relocation instead:

- `Previous` is stored already carried through the mapping, so `Previous` to `Current`
  is continuous motion in the new space;
- the tick fraction at which the plane was crossed is stored beside it;
- for a presentation alpha before that fraction, the presented pose is the interpolated
  pose carried back through the inverse mapping: the entity is still drawn on the side
  it came from;
- after it, the interpolated pose is used as is.

A caller with no meaningful mapping (a debug teleport) keeps using
`RequestTransformHistorySnap`.

`PresentationPoseOf` is the single reader, so cameras, attachments and extraction agree.
This is the only place where "which side is it drawn on this frame" is decided.

### 2.6 Tests

- `RelocationTests`: every registered entry, an unregistered world-space component
  refuses, serial advances once, partition move carries attachments and held records.
- A classification test fails until every engine component either has a rule or is
  listed as holding no world-space state.
- `TransformHistoryRelocationTests`: presented pose at alpha below, at and above the
  crossing fraction; a child presented through a relocated parent.
- A character mover moved between two active zones keeps its mover. `RigidBodyBinding`
  has this test (`PhysicsPartitionMigration.ActiveToActiveKeepsBackendBodyAndEntityIdentity`);
  `CharacterMoverPool` does not.

## 3. Character traversal (Stage 3)

### 3.1 Where it runs

In the shared character tick, `StepScaledCharacterTick`
(`engine/include/movement/CharacterTickStep.h`), after the mover's sweep. That function
is what the live tick, time-dilated characters and prediction replay all run, so a
crossing detected there is reproduced by replay without separate code. The crossing
reads the position before and after the sweep, which are both in hand at that point.

It reads `ResolvedLinks`. With an empty span it does nothing.

### 3.2 Crossing state

```cpp
struct LinkCrossing        // component on the character, not saved, replicated to the owner
{
    LinkId Link;
    std::uint8_t EntrySide;    // A or B
    CrossingPhase Phase;       // Clear, Overlapping, Committed
};
```

A plain enum with its transitions in one function. It is part of the state replay
resumes from, alongside `KinematicState` and the rest of that list in
`engine/src/movement/CharacterTickStep.cpp`.

It holds no live ids. The link is looked up by `LinkId` each tick; if it is no longer in
`ResolvedLinks` the state returns to `Clear` and the character stays where it is.

### 3.3 Candidates

Bounded work per character per tick:

1. links with an aperture in a zone that is in the Physics set;
2. a slab test of the capsule against each such aperture;
3. the exact capsule support and rectangle test for those that pass;
4. the lowest `LinkId` wins a tie;
5. one link holds the character until it clears (D8).

If measurement shows the slab test over all active links is not negligible, add a small
spatial index inside `ResolvedLinks`. Not before.

### 3.4 Traversable

An aperture is traversable for a character when:

- the link is upright (D4) and `Directions` allows this direction;
- the destination zone is resident and in the Physics set;
- the destination zone is in the Logic set if the character runs logic.

Otherwise the aperture is solid for that character: the crossing is refused, counted by
reason, and the character is held at the plane the way the existing dock clamp holds a
pawn at a dock whose zone is not ready (`templates/fps/src/PawnStreaming.cpp`). Lift
that clamp into the engine in this stage if both callers can share it; otherwise say why
not in the handoff.

### 3.5 Straddling

- Overlap begins when any part of the capsule is inside the aperture prism.
- The character is on the entry side while its centre is.
- It may stop and it may back out. Backing out relocates nothing.
- The crossing commits when the swept centre passes from front to back through the
  rectangle reduced by the capsule radius, so the centre cannot pass where the capsule
  would clip the rim. Authored geometry handles sliding along the rim.
- On commit, `RelocateEntity` runs with the mapping, the destination zone's partition
  when the character is zone-owned, and the crossing fraction. The remainder of the
  tick's displacement is carried through and applied.
- The capsule is placed a named separation in front of the exit plane.
- The phase stays `Committed` until the capsule clears the exit plane by a hysteresis
  distance. Reverse traversal is possible only after that.

The capsule centre has to move behind the entry plane before it commits. The recess
(D6) is the room for that.

### 3.6 Speed and multiple apertures

Detection uses the whole pre-sweep to post-sweep segment, not trigger events. The
aperture is not a sensor: relocation already produces Exit at the source and Enter at
the destination for real sensors, and a sensor aperture would re-trigger on arrival.

If the segment passes two apertures, the earlier wins, the remainder is carried through
it, and traversal stops for that tick. A deferred-second-crossing counter records it.

The segment is the chord of the tick's motion. A curved path inside one tick is not
reconstructed (`00`, section 9).

### 3.7 Focus and demand

A committed crossing by a focus source calls `WorldPartitionRuntime::RelocateFocus`,
with the destination zone taken from the link.

Verify at stage start: `RelocateFocus` takes a position and resolves the zone by bounds.
D1 forbids that for links. Either the overload that names a zone
(`SetFocus(ZoneId)`) covers it or `RelocateFocus` gains a zone argument.

Before the crossing, the destination is a one-hop neighbour through the link's demand
edge and is already resident. No lease is needed for a focus character.

A lease is needed when a non-focus character or body straddles a link whose far zone is
not otherwise held. The crossing holder acquires a Physics lease on both zones at
overlap and releases at clear. Leases have never had a production holder, and three
gaps must be closed in `WorldPartitionRuntime` when this first uses them, as general
work:

- a holder learns that its zone failed to load or was torn down (today it must poll,
  and a failed load is silent);
- forced teardown invalidates leases before entities are destroyed (documented, not
  wired);
- the reason is reported as its own demand reason, not `ExplicitPin`.

Until the lease is honoured the aperture is not traversable for that entity.

### 3.8 Zone ownership

- A pawn in the persistent partition: relocate, no partition move.
- A runtime-spawned zone-owned character: relocate with the destination partition,
  then keep it with the destination zone through `KeepWithZone`, as projectiles and
  inventory drops do.
- An authored zone-owned character: not traversable in v0.1. Removing landmark carry
  removed `KeepArrivedWithZone` and the `departed` zone list, so nothing records an
  authored entity leaving its zone; it would reappear in its old zone on reload.
  `docs/plans/runtime-stable-identity.md` names "the first design need that moves an
  authored entity between zones at runtime" as the trigger for a residency record.
  Stage 7 builds it, together with the zone-membership work in
  `docs/plans/zone-membership-and-hardening.md`, rather than reviving the removed list.
  Until then the aperture is solid for such an entity, refused with its own reason.

### 3.9 Forced teardown

If a zone is torn down while a character overlaps its aperture, the crossing returns to
`Clear` and the character is placed in front of the plane on the side its centre is on.
No mapping to a destination that no longer resolves is ever applied.

### 3.10 Authored event

A committed crossing delivers a `link.crossed` event with the instigator to both
aperture entities through `ResponsesSystem`. It is declared through the annotated-vocabulary path so
`docs/reference/vocabulary.md` regenerates.

### 3.11 Counters and debug draw

Counters: overlap starts, commits, back-outs, refusals by reason, deferred second
crossings, forced-teardown resolutions, lease acquisitions and releases.

Debug draw: capsule support interval, crossing point and fraction, phase, the reason an
aperture is not traversable.

### 3.12 Tests

Headless, at the layer that owns each rule:

- walk through and back; stop halfway; back out after several ticks;
- centre exactly on the plane;
- slide along the rim without crossing; approach a corner diagonally;
- cross faster than the recess is deep per tick;
- velocity and facing after links at 90 and 180 degrees of yaw;
- support is found again on arrival; exit over empty space falls;
- non-upright link refuses; one-way link refuses the reverse;
- destination not resident, resident but not in Physics, becoming ready while waiting;
- link invalidated while overlapping; zone torn down while overlapping;
- two apertures on one segment;
- the same input at two render rates gives the same ticks;
- a time-dilated character crosses on the same tick as an undilated one given the same
  scaled input;
- zone-owned character keeps its `EntityId` and mover, and belongs to the far zone.
