# Driven Pose Constraints and Backend Residency

Status: approved plan (2026-07-20), third revision. The first revision modeled
constraints as free-standing entities; the second corrected ownership but patched
around three substrate gaps with local mechanisms. This revision closes the gaps at
the layer that owns each one, because the same gaps were generating defects and
design conundrums independently of this feature. Scope is deliberately larger than
the feature: the goal is boundaries that are still correct a year from now.

Audience: whoever implements any track here (human or agent), and reviewers. Every
claim about the current tree in Section 2 was verified against source at the time
of writing; re-verify before relying on one, per the repository rule that plans are
not proof.

Scope: ECS lifecycle contract, registry lifecycle observability, event-channel
primitive, physics binding ownership, the shared-simulation residency contract, the
driven-pose constraint mechanism, diagnostics, and tests. Game integration
(abilities, input, cameras, pickup rules) stays out of scope; games consume the
mechanism through components and events.

---

## 0. Decisions up front

Substrate (Track A):

1. **`DestroyEntity` fires `OnRemove` hooks.** The `ComponentTraits` contract is
   completed: a component leaving the world fires its hook on every path — remove,
   destroy, command buffer, and world teardown. Today destruction fires nothing,
   which makes the documented retain/release pattern leak (Section 2.1).
2. **`World` teardown drains entity hooks before resources die.** Hooks reach
   their release targets through `World` resources; the current teardown order
   destroys resources first, so unload-path releases are unreachable.
3. **Registry lifecycle becomes observable as frame data.** `BuildFrameView`
   additionally computes transitions — per-domain entered and left spans, plus
   destroyed registry ids — from drain-point-stable state. No observer callbacks,
   no bus: transitions are data delivered at one phase point.
4. **`FrameRegistryView` owns cross-registry resolution.** `Find(RegistryId)`
   resolves participating registries for simulation code; `FindAttached` resolves
   any attached registry for engine orchestrators handling lifecycle edges;
   `IsAlive(EntityRef)` composes resolution with the generational check.
   `ZoneRuntime::FindRegistry` remains the tools-and-lifecycle lookup.
5. **`TickEventChannel<T>` is a named primitive.** Two-phase publication over
   `EventBuffer<T>`: stage from outside the step, fold-and-clear at tick entry,
   publish during the step. Events that must outlive their publishing registry get
   one mechanism instead of per-subsystem staging.

Residency (Track B):

6. **Dormant means absent from the shared simulation.** `ZoneParticipation`
   already promises it ("cannot affect simulation or presentation"); physics
   currently violates it — a dormant zone's bodies keep simulating and colliding.
   On leaving the physics span a registry's bindings evict their backend objects
   (records and links retained); on re-entry they restore from
   component-authoritative state; destruction still cleans up via destructors.
7. **Bindings live in `Registry::Resources` and are named for their mechanism.**
   `PhysicsScene` becomes `RigidBodyBinding`; `CharacterMoverPool` moves beside
   it; `DrivenPoseBinding` joins them. The binding criterion: retained backend
   objects derived from this registry's entities, reconciled against entity
   lifecycle, destroyed with the registry. Nothing else earns a binding.
8. **A shared residency-conformance harness tests every binding** through the
   same gauntlet: populate, go dormant, return, destroy — asserting zero backend
   residue at each edge. The reconcile protocol shared by the three bindings is
   extracted only after all three exist in code, and only the genuinely shared
   part; storage semantics stay per-binding.

Feature (Track C):

9. **The solver is global, bindings are registry-local, and a driven constraint
   belongs to the follower.** The definition is a component on the driven body's
   entity; the follower's registry owns the backend constraint.
10. **Terminal completion consumes the definition.** Ending a constraint removes
    `DrivenPoseConstraint` and its runtime components in one flush. Rearming is
    structural; chunk-conservative `Changed` marks cannot resurrect anything.
11. **The binding owns frame and velocity composition.** The backend receives a
    resolved world-space driven frame with velocities evaluated at the attachment
    point; it never sees the target's local attachment frame.
12. **An endpoint registry leaving the physics view is terminal.** With
    transitions observable, the departing side's binding is visited and publishes
    full-fidelity events. The backend's refresh-or-expire check demotes from
    load-bearing mechanism to debug invariant. Residency pins and cross-registry
    entity migration are future world-partition features, not constraint behavior.
13. **`PhysicsConstraintId` is generational**, and `RemoveConstraint` on a dead
    handle is a safe no-op — required by unspecified sibling-resource destruction
    order, and by eviction and teardown paths crossing.
14. **Three update categories, three mechanisms**: topology through
    structural-version gating, configuration edits through
    `Changed<DrivenPoseConstraint>`, per-tick target state unconditionally.

Rejected regardless of budget, because they are wrong rather than expensive:

- Registry lifecycle observer callbacks or an event bus. Ordering becomes
  implicit, ownership becomes spooky; transitions-as-frame-data delivers the same
  information at a defined phase point.
- Per-zone physics worlds. Cross-zone collision is a requirement; piecewise
  ownership of one shared solver is the correct price.
- A binding base-class framework. The three bindings share a protocol, not
  storage semantics (dense swap-remove vs stable-slot pool). Extract the protocol
  after the third instance exists; never force the storage.
- Suspending and resuming constraints across dormancy. A relationship with an
  absent endpoint is a ghost no system owns. Endpoint departure is terminal; a
  game that wants the relationship to survive pins the zone.
- Constraint entities with an arbitrary owning registry, and a
  `PhysicsConstraintScene` with global authority (rejected in revision two;
  arguments preserved in Section 5).

---

## 1. Goal

Add a reusable physics mechanism that drives one physics body toward a pose
defined relative to another entity while preserving collision response.

The invariant: a follower body attempts to maintain a configured position and
orientation relative to a moving target frame. The solver resolves the follower
against the world, and the constraint reports when the relationship can no longer
be maintained within configured limits.

This supports suspended actors, physics-object pickups, grabs, moving machinery
attachments, magnetic and tractor mechanics, breakable carries, and rigid or
compliant pose following — all as component data on one mechanism.

The larger goal of this revision: the feature must not be the last one to fight
the substrate. Every gap it exposed is closed where it lives, so the next
shared-backend mechanism (vehicles, bilateral joints, contact event streams)
arrives on rails instead of re-litigating ownership.

---

## 2. Current foundation (verified)

- `PhysicsStepSystem` owns the one shared `PhysicsWorld` and
  `CollisionShapeCache` by value and outlives every zone registry. Its
  `Physics(PhysicsContext&)` already iterates the physics participation span
  (`EngineFramePhases.cpp` sets `ActiveRegistries = ctx.Registries.Physics`).
- `PhysicsScene` is the per-registry ECS-to-body bridge: dense owned records,
  structural-version-gated reconciliation, `PhysicsBodyLink` for hash-free sync,
  destructor removes the registry's bodies. `CharacterMoverPool` repeats the
  pattern for character movers. Both self-describe as registry-local but live in
  the ECS `World`'s resource bag, while `Registry` carries a dedicated
  `ResourceRegistry Resources` member whose comment names "physics worlds" among
  intended tenants. `ActiveCameraService` already lives there.
- `EntityRef` exists with zero consumers. `RegistryId` allocation is monotonic
  (`ZoneRuntime::AllocateRegistryId`), generation currently fixed at 1.
- `FrameRegistryView` is spans only. The global registry participates in every
  span, ordered first. Zone lifecycle mutates only at drain points outside a live
  frame view (asserted), so registry pointers are frame-stable.
- Dormancy is routine: `WorldPartitionRuntime` demotes the previous focus zone as
  normal streaming behavior. `ZoneParticipation`'s comment defines dormant as
  unable to affect simulation or presentation.
- Transform propagation runs after physics inside Simulate;
  `CharacterControllerSystem` runs after `PhysicsStepSystem`. Both mean poses
  read during physics can be one tick stale (Section 13).
- Resource destruction order is unspecified in both resource stores.
- `RigidBody` is linear-only with an unsynchronized `GravityScale` field.
  `PhysicsBodyId` is non-generational, safe for bodies only because records and
  links reconcile as a unit within one registry.

### 2.1 Live defects this revision fixes

- **Hook contract incomplete.** `World::DestroyEntity` removes the row and fires
  no `OnRemove` (`World.h`), and the traits documentation's fire table
  (`docs/ecs/component-traits.md`) lists add/remove paths only — destruction is
  absent. Yet the documented flagship use is refcounted handles, and
  `StaticMeshComponent` and `AudioSourceComponent` ship live `OnAdd` retains.
  Destroying such an entity leaks its retains. Implementation must first write
  the failing regression (destroy leaks a retain; unload leaks all retains) and
  confirm no compensating sweep exists elsewhere.
- **`World` teardown order.** The destructor clears resources before component
  storage, so even with destruction hooks, unload-path releases could not reach
  their targets. Teardown must drain hooks first.
- **Dormant zones keep physical presence.** Bodies of a registry that left the
  physics span remain active in the shared `PhysicsWorld`: they simulate,
  collide, and answer queries, violating the documented participation contract.
  No code path removes or parks them today.

---

## 3. Track A: substrate

### A1 — Complete the ECS lifecycle contract

`DestroyEntity` fires `OnRemove` for every non-empty hooked component of the
entity, before the row is removed, in column order (deterministic), under the
existing `ScopedLifecycleHook` guard — the no-structural-mutation rule already
covers reentrancy. The `CommandBuffer` destroy path fires the same hooks at
flush. Archetypes precompute their hooked-column list at creation, so hook-free
archetypes pay one branch.

`World` teardown (destructor and move-assignment) drains `OnRemove` for all live
hooked components before destroying component storage, and destroys resources
last, so hooks can reach release targets. Zone unload thereby releases every
retained handle.

The batching rule in `docs/ecs/decisions.md` D1.5 extends naturally: bulk
destroys of hooked archetypes run per-entity in record order; hook-free
archetypes keep the bulk path.

Division of labor, stated once and written into the traits doc: **hooks own
per-component resource correctness** (refcounts, external retains);
**binding records own per-mechanism backend residency** (backend objects that
need reconciliation, iteration, and eviction). Hooks do not replace records:
records are also the hot-path iteration structure and the destroy-detection
mechanism for state that is not a refcount.

Exit: the previously-failing leak regressions pass; hook order is deterministic
and tested; `ctest` ECS suite green; the traits doc's fire table includes
destruction and teardown rows.

### A2 — Registry lifecycle transitions and resolution

`ZoneRuntime::BuildFrameView` computes, against the previous frame's membership:

```cpp
struct RegistryTransitions
{
    // Newly participating this frame, per domain. Pointers frame-stable.
    std::span<Registry* const> EnteredPhysics;   // (and per other domains as needed)

    // Left the domain this frame but still attached (dormant). Pointers
    // frame-stable: lifecycle mutates only at drain points.
    std::span<Registry* const> LeftPhysics;

    // Detached or destroyed since the last frame view. Ids only; teardown
    // already ran via destructors.
    std::span<const RegistryId> Destroyed;
};
```

carried on `FrameRegistryView` beside the spans. Scratch-vector storage,
allocation-free steady state, same pattern as the existing span scratch. Only the
physics domain's transitions are consumed in this plan; other domains are
computed when a consumer exists (the audio sweep in `AudioSystem` is the known
candidate and can migrate later — noted, not required here).

Resolution moves onto the view:

```cpp
Registry* Find(RegistryId id) const;          // participating this frame
Registry* FindAttached(RegistryId id) const;  // any attached, incl. dormant
bool IsAlive(EntityRef ref) const;            // Find + generational check
```

`Find` is the simulation-facing default: a registry outside the frame is
unresolvable, which is the correct default semantics for gameplay systems.
`FindAttached` exists for engine orchestrators processing lifecycle edges
(Section 4.2's carried-over farewell case) and is documented as such.
Implementation starts as a linear scan; registry counts are a handful. The
indexed variant is a drop-in if profiling ever cares.

Exit: transition and resolution coverage in zone runtime tests, including
enter/leave/destroy across drain points, dormant-vs-destroyed distinction, and
pointer stability within a frame.

### A3 — TickEventChannel

```cpp
template <typename T>
class TickEventChannel
{
public:
    void Stage(const T& event);      // outside the step (teardown, drain points)
    void Publish(const T& event);    // during the step
    void BeginTick();                // fold staged into published, clear old
    std::span<const T> Items() const;
};
```

Built on `EventBuffer<T>`, placed beside it in `core/event`. Single-threaded by
contract (teardown at drain points and publication in the step share the
simulation thread). Visibility rules are the channel's documentation, not each
subsystem's: same-tick `PostFixed` consumers see the tick's events plus staged
events since the last executed tick; frame-lane consumers see only the last
executed tick; a zero-tick frame leaves staged events pending until the next
tick that runs.

Exit: focused tests including the zero-tick frame, stage-then-destroy, and
multi-tick frames.

---

## 4. Track B: physics residency

### B1 — Binding ownership refactor

Move `PhysicsScene` and `CharacterMoverPool` into `Registry::Resources`; rename
`PhysicsScene` to `RigidBodyBinding`:

```text
PhysicsStepSystem            owns PhysicsWorld, CollisionShapeCache,
                             step orchestration, TickEventChannel instances

Registry::Resources          RigidBodyBinding
                             CharacterMoverPool
                             DrivenPoseBinding     (Track C)

Registry::Components (World) authored components, runtime link components
```

Behavior-preserving; direct tests keep constructing bare `World`s and hold
bindings as locals. Every backend removal path is order-independent (decision
13), because sibling-resource destruction order is unspecified.

Exit: full suite green; a regression asserts bindings are reachable in
`Registry::Resources` and absent from the `World` bag.

### B2 — Shared reconcile protocol

`RigidBodyBinding`, `CharacterMoverPool`, and `DrivenPoseBinding` each carry the
same skeleton: dense records, `LastStructuralVersion` gate, create-and-sweep
reconcile, reconcile-count diagnostics, destructor teardown. After all three
exist in code (i.e., after C's P4), extract the genuinely shared part — the
version gate, the sweep protocol shape, the counters — as a small composition
utility. Storage stays per-binding: the pool's stable-slot free list and the
dense swap-remove vector are real semantic differences, not duplication. If the
diff shows the shared part is smaller than the ceremony of extracting it, record
that finding and keep the pattern; the conformance harness (B4), not code
sharing, is what actually protects future bindings.

### B3 — The dormancy contract: evict and restore

The governing sentence: **dormant means absent from the shared simulation.**

`PhysicsStepSystem`, at the start of its physics work each frame, consumes the
transition data:

- For each registry in `LeftPhysics` (and any carried-over leaver): a **farewell
  visit** — each binding evicts its backend objects. `RigidBodyBinding` parks or
  removes its bodies (mechanism is an implementation choice behind the binding
  and `PhysicsWorld`; the contract is: no contacts, no default query hits, no
  solver cost). `CharacterMoverPool` parks its movers under the same contract.
  `DrivenPoseBinding` terminates its constraints with full-fidelity events
  (decision 12). Records and link components are retained — eviction is not
  teardown.
- For each registry in `EnteredPhysics`: a **restore visit** — bindings
  reinstate backend state from component-authoritative data (the last active
  step synced transforms and velocities back to components, so restoration is
  faithful). Restore precedes the frame's normal reconcile pass.
- Farewell visits that cannot run because the frame executed zero physics ticks
  carry over by `RegistryId`; on the next executed tick, ids found in
  `Destroyed` are dropped (their destructors already tore down), the rest
  resolve via `FindAttached`.

This corrects the standing contract violation (Section 2.1) and is a behavior
change: dormant zone geometry stops being collidable and stops answering default
queries. That is what the participation contract always claimed; any content
relying on the violation was relying on a bug. Called out loudly in the change
notes.

Restoration cost is bounded by zone size (room-scale by the engine's stated
capacity assumptions); shapes stay in the cooked cache. If profiling shows
restore spikes, the park-don't-remove mechanism absorbs them; the contract does
not change either way.

Exit: fitness tests prove a dormant zone's simulation presence is zero (no
contacts against its geometry, no query hits, body/mover/constraint accounting
correct), a returning zone restores bit-faithful component state, and repeated
dormancy cycles allocate nothing in steady state.

### B4 — Residency conformance harness

A parameterized test gauntlet any binding can run: register components, populate
entities, tick; leave the span (evict), assert zero simulation presence; return
(restore), assert equivalence; destroy the registry, assert zero backend residue;
repeat under churn. `RigidBodyBinding`, `CharacterMoverPool`, and
`DrivenPoseBinding` all pass it in this ticket. The harness — not review vigilance
— is what holds the line when the fourth binding arrives.

The residency contract itself is written down in
`docs/architecture/backend-residency.md`: the binding criterion (decision 7),
the lifecycle table (participating / leaving / dormant / returning / destroyed —
who acts, through which mechanism), the hooks-vs-records division (A1), and the
two resolvers' semantics (A2). `ZoneParticipation`'s comment and the traits doc
are updated to point at it.

---

## 5. Track C: the driven pose constraint

The mechanism as corrected through prior revisions, on top of Tracks A and B.
Ownership argument, preserved: the follower is the one unambiguous owner. A
constraint entity would need an arbitrary owning registry, third-party lifecycle,
and placement guidance; a component on the follower makes discovery and body
binding own-registry concerns where structural-version gating works, and shrinks
cross-registry coupling to const reads of the target. Accepted limitation, still
deliberate: one pose driver per follower (one component per type per entity).
Competing drives are the recorded trigger for a future relationship-entity
mechanism; the backend API below is component-model-agnostic, so that migration
would not touch it.

### 5.1 Components

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

struct DrivenPoseLink
{
    PhysicsConstraintId Constraint;
};

enum class DrivenPoseState : uint8_t { Pending, Active, Broken, Invalid };

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

All three register in `RegisterPhysicsComponents` before entity creation.
`DrivenPoseLink` mirrors `PhysicsBodyLink`: hash-free steady state.
`DrivenPoseTelemetry` is a published copy, records are canonical; because
terminal completion removes all three components in one flush, the component
only ever shows `Pending` or `Active` — terminal outcomes arrive exclusively via
the event.

### 5.2 Drive and break settings

```cpp
enum class PoseDriveResponse : uint8_t { Locked, Spring };

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

Split linear/angular types so no field means force in one slot and torque in
another. `Locked` is rigid preservation; `Spring` lags and recovers under
frequency and damping. Error thresholds carry reset hysteresis so hovering
values do not accumulate-and-clear on alternating ticks; `RequiredDuration`
gives forgiving pickups without game policy in the engine. Break evaluation
lives in the binding.

### 5.3 PhysicsWorld constraint API

```cpp
struct PhysicsConstraintId
{
    uint32_t Index;
    uint32_t Generation;
};

struct DrivenPoseConstraintDesc
{
    PhysicsBodyId Follower;
    Transform3f FollowerLocalFrame;

    LinearPoseDriveSettings LinearDrive;
    AngularPoseDriveSettings AngularDrive;

    uint64_t UserData = 0;  // opaque; the binding packs the follower EntityId
};

struct DrivenPoseTarget
{
    Transform3f WorldFrame;  // already composed: targetWorld * TargetLocalFrame
    Vec3d LinearVelocity;    // evaluated at the frame origin (v + w x r)
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

PhysicsConstraintId AddDrivenPoseConstraint(const DrivenPoseConstraintDesc& desc);
void RemoveConstraint(PhysicsConstraintId id);          // dead handle: no-op
bool IsConstraintValid(PhysicsConstraintId id) const;

void SetDrivenPoseTarget(PhysicsConstraintId id, const DrivenPoseTarget& target);
PhysicsConstraintTelemetry GetConstraintTelemetry(PhysicsConstraintId id) const;
```

The backend's whole contract: drive this follower-local frame toward this
world-space frame, one-way, while the follower collides normally. It never sees
the target entity or its attachment frame. The hidden realization (kinematic
anchor, mass scaling, or another Jolt-side mechanism) never leaks.

Targets are refreshed every step by the binding. With Track A's transitions, the
orchestrator can always reach the binding that owes cleanup, so an unrefreshed
live constraint is no longer a semantic state — it is an orchestration bug.
Debug builds assert it (the former refresh-or-expire mechanism, demoted to
invariant); release builds skip driving the stale constraint and count it in
diagnostics.

`RemoveBody` invalidates dependent constraints before removing the body, on
every path including teardown, in either destruction order.

### 5.4 DrivenPoseBinding

Per-registry resource beside the other two bindings. Dense records are canonical:
follower id, handle, resolved target, previous target transform, failure
accumulation, state.

- **Reconcile** (structural-version gated): create backend constraints for
  followers whose body link exists — the registry's body pass has run first —
  remove constraints whose definition vanished, sweep dead followers.
  Statically invalid configurations (target ref equals follower, invalid ref, no
  dynamic `RigidBody`, nonfinite frames or drive values) consume the request
  immediately with a loud debug diagnostic. Pending (body not yet bound) is
  re-checked per step, surfaced by diagnostics, never silently timed out.
- **Configuration** (`Changed<DrivenPoseConstraint>`): push edits to the
  backend. Chunk-conservative re-pushes are cheap; resurrection is impossible
  because terminal constraints have no component left to mark.
- **PrepareStep** (every tick): resolve `Target` against the physics span,
  compose the driven world frame (`targetWorld * TargetLocalFrame`) and its
  velocities at the frame origin — `v + w x r` for off-center frames on rotating
  targets, not the target origin's velocity — classify teleports, refresh the
  backend. Composition is pure math with table-driven tests. Foreign-registry
  reads are const-only. Resolution failure is recorded for terminal handling.
- **CollectResults** (every tick): pull telemetry, evaluate breaks with
  hysteresis, publish `DrivenPoseEnded`, and for each terminal constraint remove
  the backend object plus all three components in one command-buffer flush.
- **Farewell visit** (B3): terminate all constraints with full refs and reason
  `OwnerInactive`; evict nothing else — constraints are relationships, and
  relationships end when an endpoint leaves the simulation.
- **Destructor**: remove backend constraints and stage `OwnerUnloaded` events
  into the channel.

Target velocity source: the target's `RigidBody` component values when present
(component reads only), otherwise fixed-step transform deltas with quaternion
delta for angular; teleports classify before derivation so a teleport never
manufactures a synthetic velocity.

### 5.5 Step orchestration

```text
0. Process transitions: farewell visits (leavers), restore visits (returners)
1. For each registry: rigid-body reconcile + kinematic push   (RigidBodyBinding)
2. For each registry: driven-pose reconcile                   (DrivenPoseBinding)
3. For each registry: resolve targets, refresh driven frames  (DrivenPoseBinding)
4. Step the shared PhysicsWorld once
5. For each registry: collect telemetry, evaluate breaks,
   publish terminal events, flush deferred ECS changes        (DrivenPoseBinding)
6. For each registry: pull dynamic body transforms/velocities (RigidBodyBinding)
```

`TickEventChannel<DrivenPoseEnded>::BeginTick` runs at step entry. Passes are
serial on the simulation thread in span order (global first, zones in attach
order): deterministic event and reconcile ordering. The pass-1/pass-2 barrier
guarantees every registry's bodies exist before any registry binds constraints.
Constraint passes are permanently excluded from any per-registry parallel axis —
they read foreign registries; the exclusion is by construction, not convention.

### 5.6 Lifecycle, termination, events

States: `Pending -> Active -> Broken | Invalid`. Terminal completion consumes
the request (decision 10); rearming is a fresh component.

```cpp
enum class DrivenPoseEndReason : uint8_t
{
    Removed,            // component removed by game code
    FollowerDestroyed,
    PositionError,
    AngularError,
    ForceLimit,
    TorqueLimit,
    TargetTeleported,
    TargetDestroyed,    // target entity dead in a resolvable registry
    TargetUnavailable,  // target registry unloaded or outside the physics view
    OwnerUnloaded,      // follower's registry destroyed
    OwnerInactive,      // follower's registry left the physics view
    InvalidConfiguration,
};

struct DrivenPoseEnded
{
    EntityRef Follower;
    EntityRef Target;
    PhysicsConstraintId Constraint;
    DrivenPoseEndReason Reason;

    float PositionError;
    float AngularError;
    float AppliedForce;
    float AppliedTorque;
};
```

All events carry full refs — the farewell visit and the destructor both run with
records intact, which the transitions work exists to guarantee. Events ride the
engine-owned `TickEventChannel<DrivenPoseEnded>` on `PhysicsStepSystem`;
bindings hold a non-owning channel pointer (same lifetime argument as their
`PhysicsWorld*`: `EngineSchedule` outlives `ZoneRuntime`).

The engine reports the physical result only; games decide what it means. The
integrator-facing consequence stays documented loudly: streaming demotes zones
routinely, so a constraint whose endpoint's zone demotes ends by design; the
remedies (pins, migration) are world-partition features.

### 5.7 Target pose staleness (documented v1 contract)

Pose source: `WorldTransform` when present, else `LocalTransform`. Propagation
runs after physics, so a parented target's world pose is one tick old — sockets
on moving parents lag one fixed step; v1 recommends top-level or physics-driven
targets, and on-demand parent-chain propagation is the recorded future option.
Character-driven targets are likewise one tick stale (`CharacterControllerSystem`
runs after the step); spring response masks it, locked response will trail.

### 5.8 Collision and diagnostics

The follower collides normally per collision filtering; the driven frame carries
no collision shape; follower-versus-source filtering is ordinary layer
configuration. Debug drawing: frame axes, desired frame, error vector and arc,
force and torque magnitudes, state coloring, thresholds. Console:

```text
physics.constraints.debug
physics.constraints.log_breaks
physics.constraints.count
physics.constraints.warn_pending_steps   (0 = off)
```

Counters: active, pending, created and removed per step, breaks by reason,
stale-refresh invariant hits, reconcile passes, resolution failures. No Jolt
type outside the physics implementation.

---

## 6. Execution order

```text
Track A (substrate)      A1 destroy hooks + teardown order
                         A2 view transitions + resolution
                         A3 TickEventChannel
Track B (residency)      B1 binding move + rename            (independent of A)
                         B3 evict/restore                     (needs A2)
                         B4 conformance harness + contract doc
                         B2 protocol extraction               (after C/P4)
Track C (feature)        P1 body completion                   (independent)
                         P2 backend constraint foundation     (independent)
                         P3 compliance and limits
                         P4 DrivenPoseBinding + orchestration (needs A2, A3, B1, B3)
                         P5 breaking and events
                         P6 diagnostics + example scene
                         P7 authoring                         (deferred as before)
```

A1, A2, A3, B1, P1, and P2 are mutually independent and can proceed in
parallel. Each lands as its own commit series with its own tests; no phase
waits on the whole.

Phase exit conditions:

- **A1/A2/A3**: as stated in Track A. A1's leak regressions must fail before the
  fix, for the intended reason.
- **B1**: suite green, bindings in `Registry::Resources`.
- **B3**: dormant zone has zero simulation presence; returning zone restores
  faithfully; cycles allocate nothing.
- **B4**: all three bindings pass the gauntlet; `backend-residency.md` merged;
  traits doc and participation comments updated.
- **P1**: dynamic bodies preserve and report full linear and angular state
  through ECS sync (including the currently-dead `GravityScale`).
- **P2**: a body follows a translating and rotating driven frame while
  colliding; source receives no feedback; stale-refresh asserts in debug;
  handles are generational and idempotent to remove.
- **P3**: rigid and forgiving following from one mechanism; limits enforced;
  deterministic repeated runs.
- **P4**: constraints bind and unbind across registries with no leaks and no
  steady-state topology scans; events flow through the channel; farewell and
  restore paths exercised.
- **P5**: obstruction, overload, teleports, and participation loss produce
  deterministic, inspectable terminal events; no auto-recreation.
- **P6**: tunable and inspectable without game code.

---

## 7. Required tests

Substrate:

- destroy fires `OnRemove` (direct and command buffer), in deterministic order;
  world teardown drains hooks before resources; the mesh/audio retain leaks
  regress before the fix and pass after
- transition spans across enter/leave/destroy at drain points; dormant vs
  destroyed distinguished; pointer stability within a frame
- channel: zero-tick frames, stage-then-destroy, multi-tick frames

Residency:

- conformance gauntlet for all three bindings (populate / dormant / return /
  destroy / churn), zero backend residue at each edge
- dormant zone: no contacts, no default query hits, zero solver cost; restore
  is component-faithful

PhysicsWorld (direct, headless, fixed stepping):

- locked and spring following, translation and rotation, off-center paths
- zero-gravity suspension; collision preserved; one-way isolation
- force and torque limits; teleport flag semantics
- generational handles: stale handle never resolves after slot reuse
- `RemoveBody` invalidates dependents in both destruction orders; dead-handle
  removal is a no-op; stale-refresh asserts in debug
- identical runs produce identical results

ECS integration:

- binds the step the follower's body binds; pending before
- every terminal reason reachable and correct, components consumed in one flush
- neighbor-chunk `Changed` marks cannot resurrect a broken follower
- off-center rotating target receives `v + w x r`, verified analytically
- `OwnerUnloaded` staged at teardown survives to the next executed tick's
  `PostFixed`, including across zero-tick frames
- global-registry follower against zone-registry target
- steady-state ticks: no topology reconciliation (per-tick refresh expected)

Stress and fitness:

- cost scales with active constraints; inactive entities free; no steady-state
  allocation; create/destroy churn leaks nothing; counts reach zero after
  registry destruction; invalid data loud in debug, safe in release

---

## 8. Performance expectations

Steady state: `O(active bodies + active constraints x physics registries)` —
the resolution term is a scan over a deliberately small span, stated honestly
rather than disguised; an indexed lookup is the recorded fix if it ever shows in
a profile. Topology work is version-gated; refresh and telemetry are contiguous
record walks. Transition processing is proportional to transitions, which are
rare. Eviction and restore are proportional to the leaving or returning zone's
contents and occur at streaming frequency, not tick frequency. Hook completion
adds cost only to hooked archetypes on structural paths. Anything bounded logs
what it dropped.

---

## 9. Acceptance criteria

1. A dynamic body preserves a complete relative pose to a translating and
   rotating target that can be any entity with a valid world transform; the
   follower collides normally; the target receives no feedback.
2. Locked and spring responses are one mechanism; breaks report error, force,
   torque, and terminal reason; endpoints work across registries via
   `EntityRef`.
3. Component lifecycle is complete: no path exists on which a hooked component
   leaves the world without its hook firing, including zone unload.
4. A dormant registry has zero presence in the shared simulation, and returning
   restores it faithfully.
5. Every unload, dormancy, and destruction path ends deterministically with the
   documented reason, full refs, and no dangling backend object — proven by the
   conformance harness for all three bindings.
6. Bindings live in `Registry::Resources`; the residency contract is a merged
   document; no game, genre, or project term in the engine API; no Jolt type
   across the firewall.
7. No steady-state structural scan or allocation; a game implements suspended
   actors and forgiving pickups without touching the backend.

---

## 10. Deferred

Bilateral joints, distance and rope limits, hinges, cone and twist limits,
ragdolls, breakable assemblies, networked prediction, persistent saved
constraints, editor-authored mechanical assemblies, residency pins,
cross-registry entity migration, on-demand target-chain propagation, camera
spring arms, audio-sweep migration onto A2 transitions, indexed registry
resolution, non-physics domain transition consumers. Each now has a substrate to
land on; none expands this ticket further. Competing multi-driver poses remain
the recorded trigger for a relationship-entity mechanism.
