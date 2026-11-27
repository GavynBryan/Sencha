# Replication and Presentation

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stages 5 and 6.

Revision 1 did not mention any system in this document. Each one assumes ordinary space
or continuous motion somewhere, and each needs a defined answer for a link. They share
two mechanisms from `02`: the relocation serial with its per-frame mapping record, and
one-hop segment continuation. No system here gets its own notion of a portal.

## 1. Summary

| System | What a link breaks | Answer | Stage |
|---|---|---|---|
| Remote interpolation | Blends a remote entity through the space between apertures | Do not blend across a serial change | 5 |
| Owner prediction | Authority cannot correct facing; replay cannot reproduce a crossing | Crossing in the shared tick; commands name their serial | 5 |
| First-person camera | Nothing beyond `03`, section 8 | | 4 |
| Orbit camera | Camera on the wrong side; orbit yaw not carried | Boom continued through the link; yaw carried | 6 |
| Particles | Not visible through; do not pass through | Drawn per view; crossing step; linked emitter state | 4, 6 |
| Traces and projectiles | Stop at the recess | One-hop continuation | 6 |
| Navigation | No route through | In-zone link pair emitted at cook | 6 |
| Perception | Cannot see or hear through | Deferred; memory cleared on crossing | 2 |
| Sensors | Already correct | None | |
| Attachments, held records | Already follow | None | |
| Saves | Zone-owned entity saved under the wrong zone | `02` section 3.8, `04` section 2.5 | 7 |
| Audio | No spatial audio exists | None | |

## 2. Replication (Stage 5)

### 2.1 The model on `main`

Server-authoritative snapshots. The owning client predicts its pawn; reconciliation
restores authority state then replays unanswered commands through
`StepScaledCharacterTick`. Remote entities are drawn from `ReplicationInterpolation`,
which buffers `LocalTransform` samples and blends between the two that bracket the
render time. Determinism means serial equals parallel on one machine; it does not mean
two machines agree bit for bit.

Three facts shape this stage:

- `ReplicationInterpolation` has no notion of a discontinuity. Nothing on the wire
  marks one.
- `LookOrientation.Yaw` and `Pitch` are owner-local: the authority never sends them to
  the owner, and it overwrites its own copy from the yaw and pitch each command
  carries. An authority-side change of facing cannot reach the owner and is overwritten
  by the next command.
- `MovementIntent.WishDir` and command yaw are in world space as the client saw it when
  it sampled input.

### 2.2 Remote entities

`RelocationSerial` is replicated with the transform. `ReplicationInterpolation` records
the serial with each sample. When the two bracketing samples have different serials it
holds the earlier sample until the render time reaches the later one, then shows the
later. A remote entity is never drawn between two apertures.

This is not link-specific. It also fixes level carry and any debug teleport for remote
entities.

A later refinement, not in v0.1: carry the earlier sample through the mapping so the
approach and departure both stay smooth. It needs the mapping on the wire and is
deferred until the hold is seen to matter.

### 2.3 The owning client

The crossing runs inside the shared character tick (`02`, section 3.1), so:

- the owner predicts the crossing on the tick it happens and applies the relocation
  locally, including the yaw change to its own `LookOrientation` and
  `PendingLookInput`;
- reconciliation replays through the same tick function and reproduces the crossing.

For that to hold, replay must see the same links. `ResolvedLinks` is derived from
replicated zone residency and cooked data and is the same on both machines once the
zones are resident. `LinkCrossing` is added to the state replay resumes from, and to the
owner's replicated state.

### 2.4 Commands across a crossing

A command sampled before the client crossed, executed by the authority after the
authority crossed, carries a yaw and wish direction in the old frame. That happens on
every misprediction of the crossing tick, in either direction.

Each `PawnCommandTick` gains the relocation serial the client held when it sampled the
command. When the authority, or a replay, executes a command whose serial is behind the
pawn's, it carries the command's yaw and wish direction through the mappings it missed.
When the command's serial is ahead, it carries them back.

The pawn keeps a short ring of its recent relocation mappings by serial for this. The
ring length is a stated limit; a command older than the ring is executed as sampled and
counted.

This is an additive change to the command wire format. Record the contract version in
the change.

### 2.5 Facing after a mispredicted crossing

The owner applies the yaw change itself when it predicts a crossing. If the authority
crossed and the owner did not, or the reverse, reconciliation sees the pawn's replicated
`RelocationSerial` differ from the predicted one and applies the missing mapping's yaw
to the owner's `LookOrientation` and `PendingLookInput`, using the mapping ring that is
replicated to the owner.

### 2.6 Refusal is authoritative

Whether an aperture is traversable depends on zone participation, which the authority
decides. A client that predicts through an aperture the authority refused is corrected
by ordinary reconciliation plus section 2.5.

### 2.7 Tests

Using the existing replication and prediction test harnesses:

- a remote pawn crossing is never sampled between apertures at any render time;
- the owner's predicted crossing matches the authority on the same tick with no
  correction;
- the authority crosses one tick earlier than the owner, and one tick later: final pose
  and facing agree, and the correction is bounded;
- a command sampled before the crossing and executed after moves the pawn in the
  direction the player meant;
- a refused crossing predicted as accepted is corrected;
- replay across a crossing reproduces the recorded ticks;
- a hosted authority plus client run, both walking through.

## 3. Cameras (Stage 6)

There is no engine camera rig. Camera policy is template code on the presentation
clock. The engine supplies `PresentationPoseOf`, `ResolvedLinks`, the frame's relocation
mappings and one-hop continuation; the templates consume them.

### 3.1 Orbit camera

`templates/platformer/src/OrbitCameraSystem.cpp` places a camera entity at pivot plus
boom from the body's presented pose, with no collision or smoothing. Its yaw lives in
the game resource `OrbitCameraState`, and steering reads that yaw during fixed logic.

Two changes, both in the template:

- **Boom.** The boom is a segment from the pivot back to the camera. It is run through
  one-hop continuation. Immediately after the pawn crosses, the boom passes back
  through the far aperture, so the camera is placed on the side the pawn came from,
  looking through the aperture at the pawn. As the pawn walks on, the boom stops
  crossing and the camera is on the pawn's side. The camera never jumps and never sits
  inside a wall because of the link.
- **Yaw.** When the body's relocation serial changes, the orbit yaw takes the mapping's
  yaw. Held input then keeps moving the pawn the way the player is pushing.

The camera's zone for visibility is the zone it is drawn from. Since both zones are
Visible while the pawn is near the link, this needs nothing further.

### 3.2 Other camera state

Camera tilt treats very high speed between frames as a teleport; it reads the serial
instead. Shake and bob have no world-space history and need nothing.

### 3.3 Tests

Headless, on the camera maths: boom fully on one side; boom crossing; pawn crossing
with the stick held; yaw after links at 90 and 180 degrees.

## 4. Particles (Stages 4 and 6)

Particles go through portals. This section depends on the particle runtime being on
`main` (`docs/particles/runtime.md`), and its second half on that runtime's attached
emitters.

### 4.1 What the runtime is

One `ParticleRuntime` per presenting World, stepped on the presentation clock.
Particles are not entities and hold no zone. They live in `ParticleBlockPool` columns,
chained per emitter state, with per-emitter bounds grown during the step. Motion over a
step is an exact solution under constant acceleration and drag. Extraction builds the
frame's render form for one camera. Every limit refuses past itself and counts.

### 4.2 Seen through a link (Stage 4)

Particle extraction and drawing run per view (`03`, section 4.2). An emitter whose
bounds are in a linked view's frustum is extracted for that view and sorted for that
view's camera. The oblique near plane clips particles behind the exit plane.

The drawn-particle budget (`render.particles.max_instances`) is per frame, not per
view. The main view is filled first; linked views take what is left and count what they
dropped.

### 4.3 Passing through a link (Stage 6)

A particle whose step carries it through an aperture from the front continues on the
far side with position and velocity carried through the mapping.

**Finding the particles.** Per emitter state per step, test the emitter's bounds against
the slab of each active aperture. Only emitters that pass run the per-particle test.
With no active link this is one empty-span check per update; with links, one bounds
test per emitter per nearby aperture. The particle loop for an emitter that passes
nothing is unchanged.

**The per-particle test.** The segment from the particle's position before the step to
after it, against the rectangle. That is the chord of an arc; the difference is
documented as a limit, the same one sensors record.

**Continuing exactly.** Motion over a step is exact, so the step splits without error:
advance to the crossing fraction, carry position and velocity through the mapping,
advance the remainder. Gravity and the emitter's authored acceleration are world-space
and stay as they are: on the far side a particle falls the way that side's down points.

**Where a crossed particle lives.** Leaving it in its emitter's chain would stretch the
emitter's bounds across both places and ruin culling. A crossed particle moves to a
*linked emitter state*: a second state of the same emitter in the same effect instance,
keyed by `LinkId` and direction, with its own block chain and bounds, created when the
first particle crosses. Moving is a swap-removal from one chain and an append to the
other, constant time. The linked state emits nothing and retires with its last particle;
the instance retires when every state has.

This also meets the particle plan's ruling that an origin zone must not decide the
visibility of particles that have left it: a linked state is culled by its own bounds.

**Limits**, added to the table in `docs/particles/runtime.md`:

| Limit | Default | Past it |
|---|---|---|
| Linked emitter states per effect instance | 4 | a particle that would need another expires at the aperture; counted as `RefusedCrossings` |
| Crossings per particle per step | 1 (D7) | the remainder of the step is flown straight |

A particle in a linked state may cross again on a later step, including back. Crossing
back returns it to the state it came from, not to a new one.

`Directions` is respected. An aperture approached from behind is not a crossing; what a
particle does at a wall is the particle plan's collision phase, not this suite's.

**Sprites at the plane.** A particle is a point with a billboard. A billboard whose
centre has not crossed is cut by the aperture surface's depth; after crossing it is
drawn whole on the far side. For one step, half a sprite is missing at the plane. Stated
as a limit; soft particles, when they exist, hide it.

### 4.4 Attached and distance emitters (with the particle runtime's second phase)

`vfx.attach` holds an effect on an entity. When the entity relocates:

- particles already emitted are in world space and stay where they are, on the side
  they were emitted;
- the emitter frame follows the entity to the far side;
- distance-based emission reads the entity's `RelocationSerial`. On a change it does
  not emit along the line between the old and new pose. With the frame's mapping record
  it emits along the true path instead: up to the entry plane, then from the exit
  plane.

This is the consumer `docs/deferred.md` names for "Presentation consumers observing a
teleport". The mechanism is Stage 2's; the particle phase consumes it.

An emitter attached to a part of an entity that has passed the plane while the entity
straddles reads its frame from the entity's carried half (`04`, section 3). That is
v0.2 behaviour and needs the crossing record.

### 4.5 Authored world emitters

When the particle runtime gains authored emitters with zone policy, an emitter in a
zone seen through a link is active because that zone is Visible. No link-specific rule.

### 4.6 Tests and benchmarks

- a particle crossing mid-step lands where the unsplit exact solution carried through
  the mapping puts it;
- crossing is independent of how the same total time is partitioned into steps, up to
  the chord limit, shown on a straight path where the chord is exact;
- a linked state is created on first crossing, culled by its own bounds, and retired
  when empty; block-count invariant holds for both chains;
- the linked-state cap refuses, counts and disturbs nothing already held;
- crossing back returns to the original state;
- the same seed gives the same particles on both sides;
- a golden image of a burst through a link, seen from the near side;
- `scripts/bench_particles.sh`: every existing scenario unchanged with no link present,
  and one new scenario with a dense emitter firing through an aperture. A run that
  refused or dropped work fails.

## 5. Traces and projectiles (Stage 6)

### 5.1 Traces

`PhysicsQueries::Raycast` and `SweepShape` see one Jolt world and know nothing about
links. A caller that wants a trace to pass through uses a wrapper that performs the
query, checks with one-hop continuation whether an aperture is reached before the hit,
and if so repeats the query from the exit with the remaining length.

The wrapper lives with the queries in `engine/include/physics/` and is what the
authored `physics.trace` and `physics.clear_path` call. A hit reports that it was
reached through a link and which one, so a consumer can carry a direction back.

It is opt-in per call site. Sensor sweeps and the character mover do not use it.

### 5.2 Projectiles

`ProjectileSystem` flies each projectile with its own per-tick sweep and already moves
a projectile between zones (`KeepInZoneAt`). The flight's sweep uses the wrapper. When
it passes through an aperture, the projectile's position and velocity are carried by
`RelocateEntity`, so its serial advances and remote clients do not see it blend across.

Verify at stage start: how `ProjectileFlightWire` represents a flight to clients, and
whether a remote flight is extrapolated from launch parameters. If it is, the wire
needs the crossing, and that is part of this stage.

### 5.3 Tests

A trace through a link hits the far target; a trace beside the aperture hits the wall;
a trace through reports the link; a projectile fired through hits on the far side with
its velocity carried; splash centred on the far side.

## 6. Navigation (Stage 6)

Each zone has its own navmesh and a link table; `NavLink` is an authored in-zone link
with an entry, an exit that may be anywhere, and a traversal kind. Queries across zones
return `CrossZoneUnsupported`.

- **Same-zone link.** Cook emits a navigation link per traversable direction of an
  upright link, entry in front of one aperture, exit in front of the other, with a
  traversal kind tag of its own so a profile can deny it. An agent walks into the
  aperture, the character crossing relocates it, and its order is reissued by the
  relocation as `LevelCarry` does today. The planner already falls back from the
  straight-line heuristic when a link is cheaper than its span.
- **Cross-zone link.** Not routable until the cross-zone planner exists. The link's
  positions are in the manifest from Stage 1 for that planner to use.

`nav.reachable` and `space.graph` in the editor see the emitted links through the same
in-memory navigation build they already use.

Tests: an agent ordered to a point beyond a same-zone link takes it when shorter;
avoids it when its traversal kind is denied; resumes its order on the far side.

## 7. Perception

Sight is a range and cone check and one straight ray. Hearing is straight-line distance.
`ObservationMemory` stores world positions.

- On relocation, the relocated entity's observation memory is cleared, as `LevelCarry`
  does. That is in Stage 2's registered entries.
- An agent does not see or hear through a link. An observer standing in front of an
  aperture is unaware of a target visible through it.

That is a real limit for encounters built around a link and is recorded in
`docs/deferred.md` with its trigger. The fix when it is due is the mapped image of the
target tested with the trace wrapper, for links near the observer only.

## 8. Systems that need nothing

- **Sensors.** `CharacterMover::SetPosition` and `PhysicsWorld::TeleportBody` reset the
  sweep origin, so a relocation gives Exit at the source and Enter at the destination
  with no sweep through the gap. Apertures are not sensors.
- **Attachments and held records.** They follow the root through
  `DispatchPartitionMoves` and are presented with it.
- **Audio.** No listener, attenuation or occlusion exists. A zone seen through a link
  plays its sounds if it is in the Audio set.

## 9. Saves

Nothing transient in this suite is saved: not `LinkCrossing`, crossing records, proxies
or linked emitter states. `RelocationSerial` is not saved; it restarts at zero and
every machine agrees.

A save taken while a character overlaps an aperture restores the character on the side
its centre is on, with `LinkCrossing` clear. If it is still inside the prism the next
tick finds the overlap again.

Zone-owned entities that crossed are covered in `02`, section 3.8, and `04`,
section 2.5.

Test: save while overlapping, load, walk on through.
