# Driven Pose Constraints and Physics Binding Ownership

Status: approved plan (2026-07-20). Supersedes the earlier driven-pose-constraint
draft that modeled constraints as free-standing entities and introduced a
`PhysicsConstraintScene`; the sections below record why that shape was rejected.

Audience: whoever implements any phase here (human or agent), and reviewers. Every
claim about the current tree in Section 2 was verified against source at the time of
writing; re-verify against the tree before relying on one, per the repository rule
that plans are not proof.

Scope: physics backend, per-registry physics bindings, fixed-step orchestration,
diagnostics, and tests. One prerequisite ownership refactor is included. Game
integration (abilities, input, cameras, pickup rules) is out of scope; games consume
the mechanism through components and events.

---

## 0. Decisions up front

1. **The solver is global, bindings are registry-local, and a driven constraint
   belongs to the follower.** The constraint definition is a component on the driven
   body's entity, not a separate constraint entity. The follower's registry owns the
   backend constraint.
2. **Per-registry physics bindings move out of the ECS `World` resource bag into
   `Registry::Resources`**, where `ResourceRegistry`'s own documentation already
   places them. `PhysicsScene` is renamed `RigidBodyBinding` in the same refactor.
3. **`PhysicsStepSystem` orchestrates explicit global passes** with a barrier
   between body reconciliation and constraint reconciliation across all
   participating registries.
4. **Cross-registry target resolution is a span-scoped lookup over the physics
   view**, built per step. No general registry directory class is introduced;
   `EntityRef` has exactly one consumer today, so the general primitive is not yet
   earned.
5. **A driven constraint requires a target refresh every fixed step.** A constraint
   whose owner did not refresh it expires terminally inside the step. This one rule
   gives dormant-owner semantics without any registry bookkeeping in the backend.
6. **An endpoint registry leaving the physics view is terminal**, same as unload.
   Suspension, residency pinning, and cross-registry entity migration are deferred
   world-partition features, not hidden constraint behavior.
7. **Terminal events are published into one engine-owned buffer** on
   `PhysicsStepSystem`, which outlives every registry, so unload-path events cannot
   die with the registry that produced them. Teardown events raised between steps
   land in a pending staging list that the next step folds into the published
   buffer before clearing, so the per-step clear cannot destroy them unread.
8. **Terminal completion consumes the definition.** Ending a constraint for any
   reason removes `DrivenPoseConstraint` from the follower along with the runtime
   components. The component is a transient runtime request; rearming is
   structural — the game adds a new component. Persistent authored constraints
   arrive in P7 as an authoring component that instantiates transient requests.
9. **`PhysicsConstraintId` is generational.** Constraint handles live in ECS link
   components and survive churn; slot reuse without a generation is an ABA defect
   waiting to happen. `RemoveConstraint` on a dead handle is a safe no-op.
10. **Three update categories, three mechanisms**: topology through
    structural-version gating, configuration edits through
    `Changed<DrivenPoseConstraint>`, per-tick target state unconditionally.
    `Changed` never doubles as a rearm signal — decision 8 makes that impossible,
    because a terminal constraint no longer has a component to be marked changed.
11. **The binding owns frame and velocity composition.** The backend receives an
    already-resolved world-space driven frame with velocities evaluated at the
    attachment point; it never sees the target's ECS-side local attachment frame.

Rejected shapes, argued where cited:

- Constraint entities with an arbitrary owning registry (Section 4).
- A `PhysicsConstraintScene` that reads every registry and owns a global lookup
  (Sections 4, 8).
- A general `RegistryDirectoryView` world primitive (Section 8.3).
- A universal joint hierarchy or bilateral joint graph (Section 19).

---

## 1. Goal

Add a reusable physics mechanism that drives one physics body toward a pose defined
relative to another entity while preserving collision response.

The invariant: a follower body attempts to maintain a configured position and
orientation relative to a moving target frame. The solver resolves the follower
against the world, and the constraint reports when the relationship can no longer be
maintained within configured limits.

This supports suspended actors, physics-object pickups, grabs, moving machinery
attachments, magnetic and tractor mechanics, breakable carries, and rigid or
compliant pose following. No gameplay vocabulary, ability behavior, input handling,
camera behavior, or player-character assumption enters the engine API.

---

## 2. Current foundation (verified)

- `PhysicsStepSystem` owns the one shared `PhysicsWorld` and `CollisionShapeCache`
  by value and outlives every zone registry
  (`engine/include/physics/PhysicsStepSystem.h`). Its `Physics(PhysicsContext&)`
  already iterates the physics participation span: `EngineFramePhases.cpp` sets
  `PhysicsContext.ActiveRegistries = ctx.Registries.Physics`. No change of span
  source is needed.
- `PhysicsScene` is the per-registry ECS-to-body bridge: dense owned records,
  structural-version-gated reconciliation, `PhysicsBodyLink` for hash-free
  steady-state sync, destructor removes the zone's bodies from the shared world
  (`engine/include/physics/PhysicsScene.h`). `CharacterMoverPool` follows the same
  pattern for character movers (`engine/include/physics/CharacterMoverPool.h`).
- Both bindings are stored in the ECS `World`'s internal resource storage, while
  `Registry` carries a dedicated `ResourceRegistry Resources` member whose comment
  names "physics worlds" among its intended tenants
  (`engine/include/world/ResourceRegistry.h`, `engine/include/world/registry/Registry.h`).
  `ActiveCameraService` already lives in `Registry::Resources` across engine,
  editor, and examples. Two per-registry resource stores exist and physics is in
  the one the documentation does not assign it to. Section 3 corrects this.
- `EntityRef` (`RegistryId` + generational `EntityId`) exists at
  `engine/include/world/registry/EntityRef.h` and currently has no consumer.
  `RegistryId` allocation is monotonic with generation fixed at 1
  (`ZoneRuntime::AllocateRegistryId`); indices are not recycled today.
- `FrameRegistryView` carries the global registry plus per-domain spans; the global
  registry participates in every span and is ordered first
  (`ZoneRuntime::BuildFrameView`). Zone lifecycle changes only at drain points
  outside a live frame view (asserted in `ZoneRuntime`), so the physics step never
  observes a registry appearing or vanishing mid-step.
- Dormancy is routine, not exotic: `WorldPartitionRuntime` demotes the previous
  focus zone to dormant during normal streaming. A dormant registry is attached and
  alive but absent from every frame span.
- Transform propagation runs after the physics step inside the Simulate phase
  (`EngineFramePhases.cpp`), so during physics a parented entity's `WorldTransform`
  is the previous tick's. `CharacterControllerSystem` is scheduled after
  `PhysicsStepSystem`, so a character-driven pose read during physics is also one
  tick old.
- Resource destruction order is unspecified in both `World`'s resource bag and
  `ResourceRegistry` (both are hash maps of type-erased owners). Nothing may depend
  on sibling-resource destructor order within a registry.
- `RigidBody` is documented linear-only. It already carries a `GravityScale` field
  that no code pushes to the backend. `PhysicsBodyId` is a non-generational
  backend-width handle; that is safe for bodies because body records and links are
  reconciled as a unit within one registry.

---

## 3. Refactor R1: honest binding ownership

Move the per-registry physics bindings to `Registry::Resources` and name them for
what they mechanically do:

```text
PhysicsStepSystem            owns PhysicsWorld, CollisionShapeCache,
                             step orchestration, terminal event buffer

Registry::Resources          RigidBodyBinding      (renamed from PhysicsScene)
                             CharacterMoverPool
                             DrivenPoseBinding     (new, Section 8)

Registry::Components (World) authored components, runtime link components
```

Mechanics:

- `RigidBodyBinding` keeps `PhysicsScene`'s exact contract: bind `Collider`,
  `RigidBody`, and transforms from one registry to bodies in the shared world.
  Rename only; no behavior change. Header moves to
  `engine/include/physics/RigidBodyBinding.h`.
- `PhysicsStepSystem` ensures bindings through `reg->Resources.Ensure<...>(...)`
  instead of the `World` resource bag. The unload guarantee is unchanged: the
  binding dies with the registry and its destructor removes the registry's backend
  objects; the shared world outlives all registries exactly as before.
- Direct tests keep constructing a bare `World` and may hold the binding as a plain
  local object; storage location is orthogonal to the binding's API, which takes
  `World&` explicitly.
- Because sibling-resource destruction order is unspecified, every backend removal
  path must be order-independent: removing a body whose dependent constraints were
  already removed, and removing a constraint whose body already vanished, are both
  safe no-ops (Sections 7, 10).

This is the smallest correction that stops the tree carrying two competing
per-registry resource stores with physics in the undocumented one. It lands as its
own commit before any constraint work.

---

## 4. Constraint ownership: a component on the follower

```cpp
struct DrivenPoseConstraint
{
    EntityRef Target;

    Transform3f FollowerLocalFrame;
    Transform3f TargetLocalFrame;

    LinearPoseDriveSettings LinearDrive;
    AngularPoseDriveSettings AngularDrive;
    ConstraintBreakSettings Break;
};
```

The component lives on the follower entity — the body receiving the drive. The
follower's registry owns the backend constraint. There is no constraint entity.

Why this wins over constraint entities:

- One unambiguous owner. The rejected shape assigned backend ownership to
  "whichever registry holds the constraint entity," which forced placement guidance,
  a third-registry lifecycle, and an owner that could unload independently of both
  endpoints.
- The follower and its body binding share a registry, so constraint discovery and
  follower-body binding are own-registry concerns where structural-version gating
  genuinely works. Cross-registry coupling shrinks to const reads of the target.
- The target never needs a backend body. The driven frame consumes the target's
  transform and, when present, its `RigidBody` component velocities — component
  reads, never backend handles. The all-registries body barrier still exists
  (Section 9) but almost nothing crosses it.
- Lifecycle collapses: follower destroyed or its registry unloaded ends the
  constraint with the definition; target loss is detected by the running owner.

Accepted limitation, recorded deliberately: **one pose driver per follower.** One
component per type per entity is an ECS invariant, so competing drives (two tractor
sources fighting over one prop) cannot be expressed. Two complete pose drives on one
body are overconstrained in the locked case and only meaningful as a blended spring
tug-of-war, which no current requirement needs. If a real requirement for competing
drives appears, that is the point where a relationship-entity mechanism gets earned —
and the `PhysicsWorld` API below is component-model-agnostic, so that migration
would not touch the backend.

The desired follower frame each step:

```text
desiredFrame = targetWorldTransform * TargetLocalFrame
```

driving the follower's `FollowerLocalFrame` toward it. Two local frames support
center attachment, held offsets, sockets, and off-center machinery. A pure helper
constructs both frames from current world poses; it does transform math only and
owns nothing.

---

## 5. Runtime components

```cpp
struct DrivenPoseLink
{
    PhysicsConstraintId Constraint;
};

enum class DrivenPoseState : uint8_t
{
    Pending,
    Active,
    Broken,
    Invalid,
};

struct DrivenPoseTelemetry
{
    DrivenPoseState State = DrivenPoseState::Pending;

    float PositionError = 0.0f;
    float AngularError = 0.0f;
    float AppliedForce = 0.0f;
    float AppliedTorque = 0.0f;
    float FailureDuration = 0.0f;
};
```

`DrivenPoseLink` mirrors `PhysicsBodyLink` and `CharacterMoverLink`: the runtime
handle that makes steady-state work a contiguous column walk with no hashing.

`DrivenPoseTelemetry` is a published copy, written after each step the same way
`RigidBody.LinearVelocity` is pulled from the backend. The binding's dense records
(Section 8) are canonical for all constraint state; the component never feeds back
into the binding. Games read it with const access.

Because terminal completion removes all three components in the same flush
(Section 10), `Broken` and `Invalid` are never observable through
`DrivenPoseTelemetry` — the component only ever shows `Pending` or `Active`, and
terminal outcomes reach the game exclusively through the `DrivenPoseEnded` event.
The enum's terminal values exist for the binding's records and diagnostics.

No backend or Jolt type enters ECS storage. All three components —
`DrivenPoseConstraint`, `DrivenPoseLink`, `DrivenPoseTelemetry` — register in
`RegisterPhysicsComponents` (`engine/src/physics/PhysicsRegistration.cpp`) before
any entity creation, per ECS rules.

---

## 6. Drive and break settings

```cpp
enum class PoseDriveResponse : uint8_t
{
    Locked,
    Spring,
};

struct LinearPoseDriveSettings
{
    PoseDriveResponse Response = PoseDriveResponse::Locked;

    float FrequencyHz = 0.0f;
    float DampingRatio = 1.0f;
    float MaxForce = std::numeric_limits<float>::infinity();
};

struct AngularPoseDriveSettings
{
    PoseDriveResponse Response = PoseDriveResponse::Locked;

    float FrequencyHz = 0.0f;
    float DampingRatio = 1.0f;
    float MaxTorque = std::numeric_limits<float>::infinity();
};
```

Two structs rather than one reused struct with a context-dependent limit field: a
member named `MaxForce` must not mean torque when the struct sits in the angular
slot.

`Locked` requests rigid relative-pose preservation. `Spring` uses frequency and
damping and may lag under collision or acceleration. These are the two response
profiles real use cases need today: strict attachments lock both, held props soften
both, mixed configurations lock translation and soften orientation. No vague
interpolation speed; response is solver-oriented frequency, damping, and force and
torque limits.

```cpp
struct ConstraintBreakSettings
{
    float MaxPositionError = std::numeric_limits<float>::infinity();
    float MaxAngularError = std::numeric_limits<float>::infinity();
    float MaxForce = std::numeric_limits<float>::infinity();
    float MaxTorque = std::numeric_limits<float>::infinity();
    float RequiredDuration = 0.0f;

    float MaxTargetTranslationPerStep = std::numeric_limits<float>::infinity();
    float MaxTargetRotationPerStep = std::numeric_limits<float>::infinity();
};
```

`RequiredDuration` permits forgiving pickup behavior without game policy in the
engine; strict attachments use zero. Error thresholds carry internal reset
hysteresis so a value hovering at the threshold does not accumulate and clear
failure time on alternating ticks. Break evaluation lives in the binding, not the
backend, so the backend contract stays small.

---

## 7. PhysicsWorld constraint API

Backend-neutral vocabulary, no Jolt type crossing the firewall:

```cpp
struct PhysicsConstraintId
{
    uint32_t Index;
    uint32_t Generation; // generational: link components survive churn safely
};

struct DrivenPoseConstraintDesc
{
    PhysicsBodyId Follower;
    Transform3f FollowerLocalFrame;

    LinearPoseDriveSettings LinearDrive;
    AngularPoseDriveSettings AngularDrive;

    uint64_t OwnerTag = 0;  // opaque; the binding packs its RegistryId
    uint64_t UserData = 0;  // opaque; the binding packs the follower EntityId
};

struct DrivenPoseTarget
{
    Transform3f WorldFrame;  // already composed: targetWorld * TargetLocalFrame
    Vec3d LinearVelocity;    // evaluated at the frame origin, not the target origin
    Vec3d AngularVelocity;
    bool Teleported = false;
};

struct PhysicsConstraintTelemetry
{
    float PositionError;
    float AngularError;
    float AppliedForce;
    float AppliedTorque;
};

struct ExpiredConstraint
{
    PhysicsConstraintId Id;
    uint64_t OwnerTag;
    uint64_t UserData;
};

PhysicsConstraintId AddDrivenPoseConstraint(const DrivenPoseConstraintDesc& desc);
void RemoveConstraint(PhysicsConstraintId id);          // dead handle: safe no-op
bool IsConstraintValid(PhysicsConstraintId id) const;

void SetDrivenPoseTarget(PhysicsConstraintId id, const DrivenPoseTarget& target);
PhysicsConstraintTelemetry GetConstraintTelemetry(PhysicsConstraintId id) const;

std::span<const ExpiredConstraint> TakeExpiredConstraints(); // drained post-step
```

Contract:

- The backend's whole contract is: drive this follower-local frame toward this
  world-space frame. It never sees the target entity or its local attachment
  frame; frame and velocity composition belong to the binding (Section 12), which
  keeps `TargetLocalFrame` single-owner on the ECS side. The target is a driven
  frame, not necessarily a body: dynamic, kinematic, animated, scripted, or any
  entity with a valid world transform. The backend owns whatever hidden
  representation realizes the frame (kinematic anchor, mass scaling, or another
  Jolt-side mechanism); the choice never leaks.
- One-way: the follower receives constraint forces, the target receives no solver
  feedback, and the follower continues colliding normally.
- **Refresh-or-expire.** `SetDrivenPoseTarget` must be called between steps for
  every live constraint. `Step` terminally expires any constraint not refreshed
  since the previous step, disables it for that step, and queues it in the expired
  list with its opaque tags. Expiry sweeps in creation order, so the list is
  deterministic. This is the whole dormancy mechanism: a registry that leaves the
  physics view stops refreshing, and its constraints expire inside the shared
  world without the backend knowing what a registry is. `OwnerTag` and `UserData`
  exist so the orchestrator can publish a meaningful event afterward, mirroring
  the `BodyDesc::UserData` entity-packing precedent.
- `RemoveBody` first invalidates every constraint referencing the body (an internal
  body-to-constraint reverse mapping or equivalent backend mechanism), then removes
  the body. Invalidated handles answer `IsConstraintValid` false; removing them is
  a no-op. This holds on every path, including registry teardown, where
  sibling-resource destructor order is unspecified.
- Telemetry is readable after a step until the constraint is removed.

---

## 8. DrivenPoseBinding

A per-registry resource in `Registry::Resources`, the driven-pose analogue of
`RigidBodyBinding`. It touches its own registry's components and the shared
`PhysicsWorld`; the only foreign access is const reads of resolved targets.

```cpp
class DrivenPoseBinding
{
public:
    void Reconcile(World& world, PhysicsWorld& physics);

    void PrepareStep(World& world,
                     std::span<Registry* const> physicsSpan,
                     PhysicsWorld& physics,
                     float dt);

    void CollectResults(World& world,
                        PhysicsWorld& physics,
                        DrivenPoseEventSink& events,
                        float dt);
};
```

### 8.1 Records and update categories

The binding keeps a dense owned-record array — the canonical constraint state —
holding follower `EntityId`, constraint handle, resolved target, previous target
transform (for velocity derivation and teleport classification), failure
accumulation, and state. As with body records, the dense array is what makes
destroy-detection possible: `DestroyEntity` fires no hook and the components vanish
with the entity.

Three update categories, three mechanisms:

- **Topology** (component or entity created or destroyed in this registry):
  structural-version gated, exactly like body reconciliation. A steady frame is one
  integer compare. This gate is sound here precisely because the definition lives
  on the follower in this registry; the rejected cross-registry design broke it,
  since a foreign registry's structural changes never bump the owner's version.
- **Configuration** (an existing `DrivenPoseConstraint` edited: retuned spring,
  changed frames, changed thresholds): detected with
  `Changed<DrivenPoseConstraint>` and pushed to the backend. Chunk-conservative is
  acceptable — re-pushing a chunk's settings occasionally is cheap, and a spurious
  neighbor-write mark can never resurrect a terminal constraint because
  termination removes the component itself (Section 10). All read paths use const
  access so reads never mark the column.
- **Per-tick target state**: resolved and refreshed unconditionally for every
  active constraint. This is not reconciliation; the steady-state test in
  Section 16 is worded against topology passes only.

Structural changes the binding itself needs (adding `DrivenPoseLink` and
`DrivenPoseTelemetry`, stripping them on termination) go through its reused
`CommandBuffer`, flushed at the pass boundary.

### 8.2 Per-step responsibilities

- Reconcile: create backend constraints for followers whose body link exists
  (the registry's body pass has already run — Section 9), remove backend
  constraints whose definition vanished, sweep records for dead followers.
- PrepareStep: resolve each record's `Target` ref against the physics span, read
  the target pose, compose the driven world frame
  (`targetWorld * TargetLocalFrame`) and its velocities at the frame origin —
  for an off-center frame on a rotating target the linear velocity is
  `v + ω × r`, not the target origin's velocity — classify teleports, and call
  `SetDrivenPoseTarget`. Composition is pure transform math with focused
  table-driven tests. Resolution failure or target death is recorded for terminal
  handling in CollectResults; the constraint is not refreshed, so the backend
  also disables it for the step.
- CollectResults: pull telemetry, evaluate break thresholds with hysteresis and
  `RequiredDuration`, publish terminal events, remove terminal backend
  constraints, and remove the terminal followers' `DrivenPoseConstraint`,
  `DrivenPoseLink`, and `DrivenPoseTelemetry` in one command-buffer flush
  (Section 10). Surviving constraints get their `DrivenPoseTelemetry` copies
  updated in place.
- Destructor: remove all owned backend constraints and publish `OwnerUnloaded`
  events through the sink pointer, which outlives every registry (Section 11).

### 8.3 Target resolution

Resolution is a linear scan of the physics span comparing `Registry::Id`, wrapped
in a small free function. The span holds the global registry plus a handful of
zones; a scan beats building any map at this size, and it is span-scoped on
purpose: a registry outside the physics view is unresolvable, which is exactly the
Section 10 semantics. No `RegistryDirectoryView` class is introduced — `EntityRef`
has one consumer, and the repository rule is that abstractions are earned by real
variation, not anticipated. If a second cross-registry consumer appears, promote
the lookup to `FrameRegistryView` then.

Foreign-registry reads are const-only. Non-const access counts as a write and would
poison change detection in a registry this binding does not own; const access also
keeps the binding compatible with any future decision about parallelizing
per-registry work, from which the constraint passes are permanently excluded — they
are serial orchestration by construction (Section 9).

---

## 9. Fixed-step orchestration

`PhysicsStepSystem::Physics` becomes explicit global passes. Each pass completes
for every participating registry before the next begins:

```text
1. For each registry: rigid-body reconcile + kinematic push   (RigidBodyBinding)
2. For each registry: driven-pose reconcile                   (DrivenPoseBinding)
3. For each registry: resolve targets, refresh driven frames  (DrivenPoseBinding)
4. Step the shared PhysicsWorld once
5. Drain expired constraints; publish their terminal events   (PhysicsStepSystem)
6. For each registry: collect telemetry, evaluate breaks,
   publish terminal events, flush deferred ECS changes        (DrivenPoseBinding)
7. For each registry: pull dynamic body transforms/velocities (RigidBodyBinding)
```

The barrier between pass 1 and pass 2 guarantees every registry's bodies exist
before any registry binds constraints. With follower-owned constraints the
same-registry case would survive interleaving, but the barrier costs nothing, keeps
the rule simple, and stays correct if a future mechanism does reference foreign
bodies. Passes run serially on the simulation thread in span order (global first,
zones in attach order), so event and reconcile ordering is deterministic.

Body transforms are still pulled on the step a constraint breaks (pass 7 follows
pass 6). Telemetry is collected before any backend constraint is destroyed.

---

## 10. Lifecycle and termination

States: `Pending -> Active -> Broken | Invalid`. Terminal completion consumes the
request: in one command-buffer flush the binding publishes `DrivenPoseEnded`,
removes the backend constraint, and removes `DrivenPoseConstraint`,
`DrivenPoseLink`, and `DrivenPoseTelemetry` from the follower. This is what makes
"broken constraints do not recreate" structurally true — after the flush there is
no definition left for topology reconciliation to rediscover, and no component
left for a chunk-conservative `Changed` mark to touch. Rearming is unambiguous:
the game adds a fresh `DrivenPoseConstraint`. The `Invalid` path consumes the
request the same way, with a loud debug diagnostic. When P7 introduces authored
persistent constraints, the authoring component instantiates transient requests;
the runtime contract here does not change.

- **Pending**: definition exists, follower body link not yet bound. Because the
  registry's body pass precedes its constraint pass, the common case binds in the
  same step the collider appears. Pending persists until the body exists; it is
  re-checked per step at O(pending) and surfaced by diagnostics (Section 14), never
  silently timed out.
- **Invalid** (immediate, statically checkable): target ref equals the follower,
  invalid `EntityRef`, follower has no dynamic `RigidBody` component, nonfinite
  frames, invalid drive values. Fails loudly in debug, safely in release.
- **Broken**: sustained position or angular error, force or torque limit, target
  teleport beyond per-step bounds — all evaluated by the binding with hysteresis
  and `RequiredDuration`.

Terminal reasons:

```cpp
enum class DrivenPoseEndReason : uint8_t
{
    Removed,            // component removed by game code
    FollowerDestroyed,  // follower entity destroyed while active
    PositionError,
    AngularError,
    ForceLimit,
    TorqueLimit,
    TargetTeleported,
    TargetDestroyed,    // target entity dead in a resolvable registry
    TargetUnavailable,  // target registry unloaded or absent from the physics view
    OwnerUnloaded,      // follower's registry destroyed (published by teardown)
    OwnerInactive,      // follower's registry left the physics view (expiry path)
    InvalidConfiguration,
};
```

Registry participation semantics, stated once:

- **Target registry absent from the physics view** — loaded-but-dormant and
  unloaded are indistinguishable through the span lookup, and deliberately so —
  is terminal `TargetUnavailable`, evaluated by the running owner binding.
- **Owner registry absent from the physics view**: the binding does not run, its
  constraints are not refreshed, and the backend expires them terminally inside
  the step (Section 7). `PhysicsStepSystem` drains the expired list and publishes
  `OwnerInactive`, reconstructing the follower ref from the packed tags; the
  target ref in these events is invalid, which consumers must tolerate. If the
  registry later returns, its binding finds dead handles, cleans records and
  components, and publishes nothing further.
- **Owner registry unloaded**: the binding destructor removes backend constraints
  and publishes `OwnerUnloaded` with full refs through the engine-owned sink.

The practical consequence must be documented loudly for game integrators: world
partition demotes the previous focus zone to dormant as routine streaming behavior,
so carrying a follower or targeting an entity whose zone demotes breaks the
constraint by design. The engine-side remedies — residency pins that keep an
endpoint's zone participating, or migrating a carried entity into the global
registry — are world-partition features to be built there when a game needs them,
not hidden constraint behavior.

---

## 11. Terminal events

```cpp
struct DrivenPoseEnded
{
    EntityRef Follower;
    EntityRef Target;            // invalid in OwnerInactive events
    PhysicsConstraintId Constraint;
    DrivenPoseEndReason Reason;

    float PositionError;
    float AngularError;
    float AppliedForce;
    float AppliedTorque;
};
```

`PhysicsStepSystem` owns the event sink (built on the existing primitive at
`engine/include/core/event/EventBuffer.h`; domain services own their buffers by
design). Bindings hold a non-owning sink pointer with the same lifetime argument
`RigidBodyBinding` already uses for its raw `PhysicsWorld*`: `EngineSchedule`
outlives `ZoneRuntime`, so registry teardown can still publish.

The sink is two containers, because teardown publishes outside the step. Zone
lifecycle mutates at drain points before `ScheduleTicks` builds the frame view, so
an `OwnerUnloaded` raised by a binding destructor exists before the next physics
step begins — a single buffer cleared at step entry would destroy it unread.

```text
published : EventBuffer<DrivenPoseEnded>   read by consumers
pending   : append-only staging            written by teardown, between steps

at physics-step entry:
    published.Clear()
    move pending into published
during the step:
    bindings and the expiry drain append to published
```

Everything runs on the simulation thread (teardown at drain points, publication
inside the step), so no synchronization is involved.

Visibility: consumers in the same fixed tick's `PostFixed` phase see that tick's
events, including any teardown events staged since the previous step. Frame-lane
(`Update`) consumers must not assume one tick per frame — a frame runs zero or
several fixed ticks, and only the last tick's events survive to `Update`. Systems
that must not miss events consume them in `PostFixed`. A frame that runs zero
ticks leaves staged teardown events pending; they surface in the next tick that
actually runs, which is the next moment physics state is coherent to act on.

The engine reports the physical result only. It does not drop objects, end
abilities, restore input, play cues, destroy game entities, or apply cooldowns;
games consume the event and decide what it means.

---

## 12. Target pose and velocity

Pose source: `WorldTransform` when present, `LocalTransform` otherwise — the same
preference `RigidBodyBinding` uses at body creation. Two staleness facts are part
of the v1 contract and are documented, not fixed:

- Transform propagation runs after physics, so a parented target's `WorldTransform`
  is one tick old during the step. Sockets on moving parents therefore lag one
  fixed step. If this matters for a use case, the future option is on-demand
  propagation of the target's parent chain at resolve time, bounded per constraint;
  v1 recommends top-level or physics-driven targets.
- `CharacterControllerSystem` runs after `PhysicsStepSystem`, so a character-driven
  target's consumed pose is one tick old. Spring response masks it; locked response
  will visibly trail by one step.

Velocity: if the target carries a `RigidBody` component, use its linear and angular
velocity values (component reads — the target's backend body, if any, is never
touched). Otherwise derive from fixed-step transform deltas; angular velocity from
the quaternion delta. The binding's record keeps the previous target transform.

The binding then evaluates velocity at the driven frame's origin before refreshing
the backend: for an attachment offset `r` from the target origin, the frame's
linear velocity is `v + ω × r`. Skipping this hands the backend the target
origin's velocity and makes off-center attachments on rotating targets drag
behind their true path; the frame-composition helper owns this math and its
table-driven tests.
Discontinuities beyond the per-step translation and rotation bounds classify as
teleports before velocity derivation, so a teleport never manufactures an enormous
synthetic velocity — it either breaks the constraint (per break settings) or is
passed with `Teleported = true` so the backend can snap rather than chase.

---

## 13. Rigid-body completion (prerequisite capability)

Robust pose constraints need full body state. Complete the public component:

```cpp
struct RigidBody
{
    BodyMotion Motion = BodyMotion::Dynamic;

    float Mass = 1.0f;
    float GravityScale = 1.0f;

    Vec3d LinearVelocity = Vec3d::Zero();
    Vec3d AngularVelocity = Vec3d::Zero();

    float LinearDamping = 0.0f;
    float AngularDamping = 0.0f;
};
```

`PhysicsWorld` adds `GetAngularVelocity`, `SetAngularVelocity`, `SetGravityScale`,
and `WakeBody`. `RigidBodyBinding` pushes initial values (including the existing,
currently-unsynchronized `GravityScale`) and pulls both velocities after
simulation.

Gravity belongs to the body, not the constraint: a suspended actor uses a
zero-gravity body, a held prop keeps gravity. The constraint never mutates
body-wide properties behind the game's back.

---

## 14. Collision and diagnostics

The follower remains an ordinary body colliding with static, kinematic, dynamic,
and layered geometry. The driven-frame representation carries no collision shape.
Follower-versus-target-source collision is ordinary collision filtering, never
hardcoded constraint behavior. Solver contacts and telemetry are the authority on
whether the relationship held; queries and sweeps are diagnostic aids only.

Debug drawing: target and follower frame axes, desired frame, position error
vector, angular error arc, force and torque magnitudes, per-state coloring, break
thresholds. Console:

```text
physics.constraints.debug
physics.constraints.log_breaks
physics.constraints.count
physics.constraints.warn_pending_steps   (diagnostic warning threshold, 0 = off)
```

Counters: active, pending, created and removed this step, breaks by reason,
expirations, reconcile passes, target resolution failures. No debug feature needs a
Jolt type outside the physics implementation.

---

## 15. Phases

**R1 — binding ownership refactor.**
Move `PhysicsScene` and `CharacterMoverPool` into `Registry::Resources`; rename
`PhysicsScene` to `RigidBodyBinding`; update `PhysicsStepSystem`,
`CharacterControllerSystem`, registration, tests, and stale doc references.
Behavior-preserving. Exit: full suite green with bindings living in
`Registry::Resources`; a regression test asserts the binding is reachable there
and absent from the `World` bag.

**P1 — body capability completion.**
Angular velocity, gravity-scale sync, linear and angular damping, wake support;
`RigidBodyBinding` pushes and pulls full state; direct `PhysicsWorld` and binding
tests. Exit: dynamic bodies preserve and report full linear and angular state
through ECS synchronization.

**P2 — backend constraint foundation.**
Generational `PhysicsConstraintId`; driven-pose descriptors; create, refresh,
telemetry, validity, removal; one-way locked driving; refresh-or-expire with the
deterministic expired list; `RemoveBody` constraint invalidation; idempotent
removal. Direct `PhysicsWorld` tests, no ECS. Exit: a dynamic body follows a
translating and rotating driven frame while colliding, the source receives no
feedback, and an unrefreshed constraint expires deterministically.

**P3 — compliance and limits.**
Spring response, frequency and damping, force and torque limits, telemetry values,
tunable validation, deterministic fixed-step tests. Exit: the same mechanism
expresses rigid following and forgiving pickup-style following.

**P4 — ECS binding and orchestration.**
Components and registration; `DrivenPoseBinding` with the frame- and
velocity-composition helper; global pass restructure in `PhysicsStepSystem`;
span-scoped target resolution; engine-owned event sink with pending staging;
owner teardown publishing; expired-list draining. Exit: constraints bind and
unbind across registries without leaking backend objects or scanning topology on
steady frames, and teardown events survive to the next executed tick.

**P5 — breaking and events.**
Threshold accumulation, hysteresis, teleport classification, `RequiredDuration`,
full reason coverage including `OwnerInactive` and `TargetUnavailable`, no
auto-recreation. Exit: obstruction, solver overload, and participation loss all
produce deterministic, inspectable terminal events.

**P6 — diagnostics and example.**
Debug drawing, cvars, counters, and an engine physics example scene: translating
and rotating targets, box and capsule followers, walls and ceiling, locked and
spring configurations, threshold controls, live telemetry. Exit: the mechanism can
be tuned and inspected without game code.

**P7 — authoring (deferred until the runtime contract stabilizes).**
`TypeSchema`, scene-serialization decision, editor frame gizmos, undoable
commands, unloaded-endpoint authoring behavior. No persisted format commitment
during the experimental backend phase.

---

## 16. Required tests

PhysicsWorld (direct, headless, no Jolt headers, fixed stepping):

- locked position and orientation follow translation and rotation
- off-center follower traces the correct rotational path
- zero-gravity follower stays suspended; follower collides with static geometry
- one-way target unaffected by follower collision
- spring follower lags and recovers; force and torque limits enforced
- removal invalidates handles; removing a dead handle is a no-op
- `RemoveBody` invalidates dependent constraints, in both destruction orders
- unrefreshed constraint expires exactly once, in creation order
- generational id: a recycled slot does not answer to a stale handle
- identical fixed-step runs produce identical results

ECS integration:

- constraint binds in the same step its follower body binds; stays pending before
- component removal, follower destruction, target destruction, and target-registry
  unload each end the constraint with the correct reason and remove all three
  components from a surviving follower in the same flush
- after a break, a write to a different entity's `DrivenPoseConstraint` in the
  same chunk does not resurrect or rebind the broken follower
- an off-center frame on a rotating target receives attachment-point velocity
  (`v + ω × r`), verified against the analytic path
- owner-registry unload removes backend constraints and publishes `OwnerUnloaded`;
  the event survives to the next executed tick's `PostFixed` consumer even when
  the unload happens on a frame that runs zero fixed ticks
- owner-registry dormancy expires constraints and publishes `OwnerInactive`;
  the returning registry cleans up without publishing a second event
- target-registry dormancy ends with `TargetUnavailable`
- a global-registry follower drives against a zone-registry target
- editing an existing component's settings reaches the backend (`Changed` path)
- steady-state ticks perform no topology reconciliation (per-tick target refresh
  is expected and excluded from the assertion)
- broken constraints do not recreate

Stress and fitness:

- cost scales with active constraints; thousands of inactive entities are free
- no steady-state heap allocation; repeated create-destroy cycles leak nothing
- constraint counts return to zero after registry destruction
- invalid data fails loudly in debug and safely in release

The R1 refactor keeps every existing `PhysicsScene` and `CharacterMoverPool` test
passing under the new names and storage.

---

## 17. Performance expectations

Steady-state complexity, stated honestly:
`O(active bodies + active constraints × physics registries)` — each active
constraint's target resolution scans the physics span. The span is deliberately
small (global plus a handful of streamed zones), so this is acceptable and is not
disguised as `O(B + C)`; if profiling ever shows the resolution term, the fix is
an indexed lookup, not a redesign. Beyond that: no entity scans, no repeated
registration lookups, no backend creation during steady following, no schema
interpretation or string lookup in the step. Topology work is structural-version
gated; target refresh and telemetry are contiguous walks over dense records. Any
bounded behavior (expiry, pending diagnostics) logs what it dropped rather than
truncating silently.

---

## 18. Acceptance criteria

1. A dynamic body preserves a complete relative pose to a translating and rotating
   target that can be any entity with a valid world transform.
2. The follower collides normally; the target receives no solver feedback.
3. Locked and spring responses use one mechanism; break conditions report error,
   force, torque, and terminal reason.
4. Endpoints work across active registries through `EntityRef`; every unload,
   dormancy, and destruction path ends deterministically with the documented
   reason and no dangling backend object.
5. Physics bindings live in `Registry::Resources`; no game, genre, or project term
   appears in the engine API; no Jolt type crosses the firewall.
6. No steady-state structural scan or allocation.
7. A game can implement suspended actors and forgiving pickups without modifying
   the physics backend.

---

## 19. Deferred

Bilateral joints, distance and rope limits, hinges, cone and twist limits,
ragdolls, breakable structural assemblies, networked prediction, persistent saved
constraints, editor-authored mechanical assemblies, residency pins, cross-registry
entity migration, on-demand target-chain propagation, camera spring arms. Each may
reuse the handle, lifecycle, tagging, and event substrate; none expands this
mechanism now. Competing multi-driver poses are the recorded trigger for a future
relationship-entity mechanism (Section 4).
