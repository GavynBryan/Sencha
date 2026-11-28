# Crossing Entities and Rigid Bodies

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stages 7 to 11.

Four things share one logical entity and have four owners: the gameplay entity and its
components, its storage partition, its physics representation, and its render
representation. None stands in for another.

## 1. What physics is today

Read at `fbe262a3`. Revision 1 assumed most of the right-hand column.

| Area | On `main` |
|---|---|
| Ownership | `PhysicsStepSystem` owns `PhysicsWorld`, `RigidBodyBinding`, `CharacterMoverPool` and `CollisionShapeCache` by value. One per simulation. |
| Keys | `PhysicsBodyLink { PhysicsBodyId }` on the entity; the entity packed into the body's user data. |
| Facade | Linear and angular velocity get and set, `TeleportBody`, `SetBodyTransform`, `WakeBody`, `IsBodyActive`, gravity scale. |
| Not in the facade | Any impulse, mass or inverse mass, inertia, centre of mass, motion quality, explicit sleep. |
| Contacts | No contact listener, contact modification, per-contact filter or solved-impulse readout anywhere. |
| Filtering | Five fixed object layers. No per-pair or per-body filter. |
| Constraints | One driven-pose drive that overwrites the follower's velocity each step. Locked mode only. No bilateral joint. |
| Stepping | `CollisionSteps` fixed at 1, default discrete motion. |
| Characters | Jolt `CharacterVirtual`, upright capsule, update called with empty body and shape filters. Rigid bodies pass through characters (`docs/deferred.md`). |
| Zones | A zone leaving Physics participation removes its bodies and captures their state; re-entry recreates them. A move between two active zones keeps the body. |
| ECS mirror | After creation, `RigidBody` velocities and a dynamic body's `LocalTransform` are pulled from the backend, not pushed. Code that moves a body calls `PhysicsWorld`. |
| Isolation | ctest `physics_isolation` fails on Jolt outside `engine/src/physics/`. |

None of the missing facade pieces is recorded in `docs/deferred.md`. Stage 9 is their
first consumer.

## 2. Stage 7: uncoupled body transfer (v0.2)

Decision D15. A dynamic body crosses whole.

### 2.1 Rule

A body's swept centre of mass passing through an aperture from the front commits a
relocation: `RelocateEntity` with the mapping, which for a body means
`PhysicsWorld::TeleportBody` followed by setting linear and angular velocity, both
rotated. The body is then wholly in the destination for collision.

### 2.2 Detection

After the physics step, for awake dynamic bodies only:

1. bodies whose bounds overlap an active aperture's slab, found by an overlap query per
   active aperture in a Physics-participating zone, not by a scan of all bodies;
2. previous and current centre of mass against the aperture rectangle reduced by the
   body's bounding radius.

Stage 7 adds the centre-of-mass getter to the facade, since the body origin is not the
centre of mass. That is the only facade addition this stage needs.

The previous position is kept in a small table owned by this stage's binding, keyed by
entity, holding only bodies currently near an aperture.

### 2.3 Limits, stated

- A body collides with the recess while its centre is still on the entry side. A body
  whose bounding radius exceeds the recess depth cannot get its centre across and stays
  out. Cook reports recess depth; the designer sizes it for what must pass.
- A body partway through does not collide with anything on the far side until it
  commits. Something standing just beyond the far aperture can be overlapped for a
  tick.
- Rigid bodies do not collide with characters at all today; that is unchanged.
- There is no continuous collision. A body moving more than the recess depth in one
  tick is caught by the swept centre test, which is why detection is swept.

These are the reasons coupling exists as v1.0. The tier is honest about them.

### 2.4 Projectiles

Projectiles are not rigid bodies. `ProjectileSystem` flies them with its own sweep, and
they cross in v0.1 through that sweep (`06`, section 5), not by this rule.

### 2.5 Zone ownership

A zone-owned body that commits moves to the destination zone's partition and is kept
with that zone (`02`, section 3.8). For an authored body, the residency record is built
here: `ZoneStateStore` records the entity as having left its authored zone and arrived
in the other, so unload, reload, save and load all find it where it went. The `departed`
list that landmark carry used was removed with it; build the record with the
zone-membership work, as one mechanism for every runtime zone change.

Active to active keeps the body. Active to a zone not in Physics is refused: the
aperture is not traversable for a body whose destination cannot simulate it.

### 2.6 Tests

Sphere, box, capsule and compound through links at 0, 90 and 180 degrees and floor to
wall; velocity and angular velocity after; body larger than the recess stays out; body
handle unchanged across the partition move; destination not in Physics refuses; save
and load with a migrated body; 10,000 ticks of repeated traversal with bounded energy
against a no-link control.

## 3. Stage 8: clipped render representations (v0.2)

### 3.1 Rule

A renderable whose bounds cross an active aperture plane inside the rectangle draws
twice:

1. at its own transform, clipped to the side it is on;
2. at the carried transform, clipped to the far side.

The ordinary extractor must not also emit it unclipped.

Which entities straddle comes from one source: an immutable list of crossing records
published for the frame by whoever owns each kind (the character crossing, the body
binding). Extraction does not test every renderable against every aperture.

### 3.2 Where each half draws

The near half draws in the main view. The far half is in the far aperture's space, so
it appears in the linked view of that side, and in the main view of anyone standing
there. Each half is lit by the lights, lightmap and probes of the space it is in (D11);
movable objects have no lightmap allocation and sample probes.

### 3.3 Identity

`RenderEntityKey` is `{ Scope, EntityId }` and the runtime leaves `Scope` at 0. The
carried half uses the same `EntityId` with `Scope` set from the `LinkId` and side.
Shadow slots and deterministic sort ties use that key. It is stable for the session,
which is all the caches need.

### 3.4 The clip plane

This needs a per-draw plane, which the oblique near plane in `03` cannot give.

- Enable `shaderClipDistance` through `VulkanBootstrapPolicy`. Features have no hook to
  request a device feature today (`Renderer.h` says so), so the policy is where it goes.
  If the device lacks it, straddling renderables draw unclipped on the near side only,
  logged once.
- A dedicated pipeline variant writes one clip distance. The ordinary opaque variant
  stays free of it, so content with no crossing pays nothing.
- The vertex position is shared and marked `invariant` so the outline mask's depth
  matches the forward pass. The clipped variant keeps that sharing.
- Push constants are 112 bytes (the source asserts it; `docs/renderer/constraints.md`
  says 80 and is corrected in this change). The plane goes in the instance or view
  data, whichever measurement prefers.
- The same clip applies to forward colour, shadow depth and the outline mask.

### 3.5 Skinned meshes

The pose is evaluated once and written to one posed buffer in model space. Both halves
draw that buffer with different world matrices and clip planes. Animation never
advances twice.

Skinned meshes cast no shadows today. That is unchanged and not this suite's to fix.

### 3.6 Characters

A character's mesh straddles while its capsule overlaps the aperture, so its crossing
record feeds this path. With the first-person body, the player sees their own far half
in the linked view.

### 3.7 Tests

Opaque, blended, double-sided, unlit, normal-mapped and skinned meshes straddling; no
unclipped duplicate; each half lit by its own side; shadow from each half; golden images
at head-on and oblique; the zero-crossing frame uses no clipped variant.

## 4. Stage 9: physics facade prerequisites (v1.0)

Added to `PhysicsWorld` as backend-free functions, each with a test, each consumed by
Stage 10 or by ordinary rigid-body behaviour. Not a mirror of the backend.

| Addition | Needed by |
|---|---|
| Impulse at centre, at a world point, and angular impulse | impulse relay (candidate A) |
| Mass, inverse mass, inverse inertia in world space | effective-mass correction (candidate B), relay scaling |
| Motion quality per body | fast bodies near an aperture |
| A contact hook: per-contact accept or reject by a caller-supplied predicate over backend-free contact data (both entities, point, normal) | contact-domain filtering |
| Solved contact impulse readout for flagged bodies | impulse relay (candidate A) |
| A per-pair exclusion between two bodies | a body and its own proxy |
| A second body sharing an existing shape from `CollisionShapeCache`, not bound to an entity | the proxy |
| A bilateral transform constraint between two bodies | candidate B only |

The contact hook is the one general mechanism here. It is shaped for known consumers
beyond portals: `docs/deferred.md` records character-versus-body contacts and surface
identity on hits as waiting on contact information. Shape the hook so those add a
consumer and do not reopen it.

Jolt types stay inside `engine/src/physics/`. If any addition cannot be expressed
without leaking them, stop (`00`, stop condition 8).

## 5. Stage 10: coupling investigation

Do not choose from intuition. Build both in a physics-only harness against the same
world, measure, keep one, delete the other.

### 5.1 Candidate A: one body and a collision proxy

- one body owns mass, inertia, sleep and the ECS pose;
- a proxy with the same shape sits at the carried pose;
- the proxy's contacts are limited to the far side;
- solved impulses on the proxy are carried back and applied to the body;
- the body's pose resynchronises the proxy each step;
- when the centre of mass crosses, body and proxy swap sides.

Risks: impulses arrive a step late; correction injects energy; friction and resting
stacks relay poorly.

### 5.2 Candidate B: two bodies and a transform constraint

- both are dynamic;
- a bilateral constraint enforces the mapping between them;
- contacts on either propagate through the solver;
- effective mass and inertia are corrected so the pair is one body;
- one publishes the ECS pose.

Risks: doubled mass or inertia; overconstraint and oscillation; unstable sleep; drift at
high angular velocity.

### 5.3 Rubric, written before results

Against equivalent no-link controls:

1. bounded position and angular coupling error;
2. bounded linear and angular momentum error;
3. no doubled gravity and no doubled resistance to an impulse;
4. stable static and dynamic friction;
5. stable rest on one side, the other, and both;
6. predictable rim jams;
7. a contact on the far side torques the whole body;
8. stable sleep and wake;
9. no body recreation on partition move;
10. lower CPU cost and fewer allocations;
11. smaller backend-specific surface.

If neither clears it, stop.

## 6. Stage 11: coupled bodies

### 6.1 Owner

A new retained owner inside `PhysicsStepSystem`, beside `RigidBodyBinding`, holding one
crossing record per straddling body: the entity, the `LinkId` and side, the proxy
handle, which side has authority, and hysteresis. It is not a component and is not
saved. After a load, a body resting in an aperture re-enters coupling by overlap, which
is how every body enters it.

It replaces Stage 7's centre-of-mass rule for bodies it couples. Stage 7's rule remains
for bodies that are not couplable (section 6.6).

### 6.2 Activation

A body enters coupling when its actual shape intersects the aperture prism. Broadphase
uses bounds; the decision uses a shape query. Not the origin, not the centre of mass,
not a sensor.

### 6.3 Contact domains

While coupled, both representations exist in full and each contributes contacts only
where it is valid.

- The near representation keeps contacts in front of the near plane, with the wall
  outside the rectangle, and with the rim. It rejects contacts behind the plane inside
  the rectangle: that region is the far space, and what is physically there is the
  recess.
- The far representation mirrors that.
- A body never contacts its own proxy.

This is why a coupled body, unlike a Stage 7 body, is not limited by recess depth.

### 6.4 Authority and storage are separate

Authority is which representation publishes the pose. It changes when the swept centre
of mass crosses, and the proxy stays alive through a clear band so contacts do not
vanish that tick.

Storage partition changes at the next point outside a query after the authority change,
through `MoveEntityToZone`. Both zones are pinned in Physics by lease from activation to
clear (`02`, section 3.7). The partition label never decides which contacts are valid.

### 6.5 The long pole

The acceptance object is a long rectangular pole:

1. across solid wall beside the aperture, it is blocked;
2. a tip entering the aperture activates the far representation before the centre
   crosses;
3. far-side support can torque the whole pole;
4. the near part still collides with the near rim, the far part with far geometry;
5. it can pivot and jam diagonally, slide through under gravity, and be pulled back;
6. the centre crossing changes authority with no visible or physical snap;
7. linear and angular velocity are continuous under the link's rotation;
8. no contact is solved twice.

Judged from numeric traces, not screenshots.

### 6.6 Not couplable

Classified before v1.0 closes, each with a tested outcome:

- a body driven by a driven-pose constraint (a carried object): it follows its driver
  through with the constraint's existing `Teleported` flag, uncoupled;
- a jointed assembly: none exists in the engine today, so nothing to do until one does;
- a sleeping body resting in an aperture: stays coupled and asleep.

### 6.7 Sleep, clear, teardown

A coupled body stays awake while overlap is changing, either side has contacts, or
authority or a partition move is pending. On clear the proxy is destroyed, leases are
released and the binding confirms the partition.

On entity destroy, collider removal, link invalidation or zone teardown: remove the
proxy and any constraint first, release leases, and leave the body on its authority
side. A zone torn down under a straddling body resolves the same way a character does
(`02`, section 3.9).

### 6.8 Limits

Recorded in `docs/physics/`:

| Limit | Past it |
|---|---|
| Coupled bodies at once | further bodies use the Stage 7 rule; counted |
| Proxy bodies | same cap |

### 6.9 Counters

Candidates, shape overlaps, coupled bodies, proxies, contacts accepted and rejected per
side, authority changes, partition moves, maximum coupling error, bodies refused by the
cap.

### 6.10 Tests

- Facade conformance for every Stage 9 addition.
- The harness: sphere, box, capsule, convex, compound, pole, plate; links at 0, 90, 180
  degrees and floor to wall; free flight, contact on one side, the other, both;
  friction; rim jam; stacks; sleep and wake; back and forth; link invalidated; entity
  destroyed; 10,000 ticks.
- Identical input gives identical ordered traces on the serial path. Zone load order
  does not change results.
- The `asan` preset over create, overlap, proxy, authority change, partition move,
  clear, teardown and shutdown.
- `physics_isolation` passes.
