# Driven Pose Constraints and Backend Residency

Status: approved plan (2026-07-20), fourth revision. Revision one modeled
constraints as free-standing entities. Revision two corrected ownership but
patched around substrate gaps locally. Revision three closed the gaps but
derived registry lifecycle by diffing frame views, which forced carry-over
machinery and an orchestrator-only lookup. This revision records lifecycle
transitions at their mutation sites and processes them in a drain-time
residency phase, which deletes that machinery; it also adds composable
participation leases, whose justification is a verified defect in the current
pin mechanism. Scope is deliberately larger than the feature: the goal is
boundaries that are still correct a year from now.

Audience: whoever implements any track here (human or agent), and reviewers.
Every claim about the current tree in Section 2 was verified against source at
the time of writing; re-verify before relying on one, per the repository rule
that plans are not proof.

Scope: ECS lifecycle contract, registry residency transitions and phase,
participation leases, event-channel primitive, physics binding ownership, the
shared-simulation residency contract, the driven-pose constraint mechanism,
diagnostics, and tests. Game integration (abilities, input, cameras, pickup
rules) stays out of scope; games consume the mechanism through components,
events, and leases.

The contract in one sentence: **frame participation controls iteration;
residency transitions control retained backend state; active cross-registry
relationships retain the participation they require through leases; destruction
performs an explicit detach before RAII cleanup.**

---

## 0. Decisions up front

Substrate (Track A):

1. **`DestroyEntity` fires `OnRemove` hooks.** The `ComponentTraits` contract is
   completed: a component leaving the world fires its hook on every path —
   remove, destroy, command buffer, and world teardown. Today destruction fires
   nothing, which makes the documented retain/release pattern leak
   (Section 2.1).
2. **`World` teardown drains entity hooks before resources die.** Hooks reach
   their release targets through `World` resources; the current teardown order
   destroys resources first, so unload-path releases are unreachable.
3. **Registry lifecycle transitions are recorded where they happen.**
   `AttachZone`, `SetParticipation`, and `DestroyZone` produce explicit
   residency-change records (attached, participation changed, detaching)
   instead of forcing consumers to reverse-engineer deltas from frame views.
   All three already run only at drain points, so the seam exists; this makes
   it explicit.
4. **A `RegistryResidency` phase processes the change batch once per rendered
   frame**, after async commits and before the frame view is built — even on
   frames whose fixed-step accumulator produces zero ticks. Backend state is
   corrected before any frame span can observe it. Destruction is two-step:
   `Detaching` registries get a final visit while fully alive, then are
   destroyed.
5. **Destructors become verified fallback, not the teardown path.** Ordinary
   teardown happens in the `Detaching` visit with the registry readable and
   sibling resources alive, eliminating dependence on unspecified
   resource-destruction order and events-from-dying-state. Destructors still
   clean up anything left (abnormal shutdown, direct-test usage) and a
   diagnostics counter records fallback cleanups; the conformance harness — not
   a destructor assert — proves the detach path leaves zero residue.
6. **`FrameRegistryView` stays an iteration view** and gains only resolution:
   `Find(RegistryId)` over participating registries and `IsAlive(EntityRef)`.
   Residency changes carry live registry pointers, so no attached-scope lookup
   is needed by transition consumers; `ZoneRuntime::FindRegistry` remains the
   tools-and-lifecycle lookup.
7. **`TickEventChannel<T>` is a named primitive.** Two-phase publication over
   `EventBuffer<T>`: stage from outside the step (residency phase, teardown),
   fold-and-clear at tick entry, publish during the step.

Residency and streaming (Track B):

8. **Dormant means absent from the shared simulation.** `ZoneParticipation`
   already promises it ("cannot affect simulation or presentation"); physics
   currently violates it. Leaving the physics domain evicts backend objects
   (records and links retained); returning restores from
   component-authoritative state; detaching removes everything with full refs
   in hand.
9. **Bindings live in `Registry::Resources`, named for their mechanism, one per
   backend object family.** `PhysicsScene` becomes `RigidBodyBinding`;
   `CharacterMoverPool` moves beside it; `DrivenPoseBinding` joins them. The
   binding criterion: retained backend objects derived from this registry's
   entities, reconciled against entity lifecycle, removed at detach. Nothing
   else earns a binding. No shared interface: `PhysicsStepSystem` knows the
   finite set of physics families and calls them in explicit order; they share
   a contract, not a base class.
10. **Participation leases replace reliance on the non-composable pin.**
    `PinZone` is last-writer-wins per zone and `UnpinZone` erases
    unconditionally (verified, Section 2.1), so independent holders cannot
    coexist. Leases are generational participation floors owned by
    `ZoneRuntime`; streaming demand, authored pins, and leases union into
    effective participation, and unload selection honors floors. Leases are
    **game-held**: the engine never acquires them implicitly — a physics
    component silently pinning zones against the streaming budget is a hidden
    side effect, and a layering violation besides. Forced teardown overrides
    leases; holders learn through terminal events.
11. **A shared residency-conformance harness gates every binding** through the
    same gauntlet. The reconcile protocol shared by the three bindings is
    extracted only after all three exist, and only the genuinely shared part.

Feature (Track C):

12. **The solver is global, bindings are registry-local, and a driven
    constraint belongs to the follower.** The definition is a component on the
    driven body's entity; the follower's registry owns the backend constraint.
13. **Terminal completion consumes the definition.** Ending a constraint
    removes `DrivenPoseConstraint` and its runtime components in one flush.
    Rearming is structural; chunk-conservative `Changed` marks cannot resurrect
    anything.
14. **The binding owns frame and velocity composition.** The backend receives a
    resolved world-space driven frame with velocities evaluated at the
    attachment point; it never sees the target's local attachment frame.
15. **An endpoint registry leaving the physics domain is terminal.** Leases are
    the mechanism that prevents routine streaming from causing it; when it
    happens anyway, the departing side is visited during the residency phase
    and publishes full-fidelity events. The backend's refresh check demotes to
    a debug invariant.
16. **`PhysicsConstraintId` is generational**, and `RemoveConstraint` on a dead
    handle is a safe no-op — defense in depth for crossing cleanup paths.
17. **Three update categories, three mechanisms**: topology through
    structural-version gating, configuration edits through
    `Changed<DrivenPoseConstraint>`, per-tick target state unconditionally.

Rejected regardless of budget, because they are wrong rather than expensive:

- Registry lifecycle observer callbacks or an event bus. The residency phase
  delivers the same information as batched data at one deterministic phase
  point, with explicit consumer ordering.
- Deriving transitions by diffing frame views (revision three's design).
  Mutation sites already know previous and current state; diffing forced
  carry-over lists across zero-tick frames and an orchestrator-only lookup,
  all deleted by recording at the source.
- Per-zone physics worlds. Cross-zone collision is a requirement.
- A binding base-class framework. Storage semantics genuinely differ
  (stable-slot pool vs dense swap-remove); extract the shared protocol after
  the third instance exists, never the storage.
- Engine-acquired participation leases. Residency retention is streaming
  policy; it belongs to the game, with the terminal event naming the remedy.
- Suspending and resuming constraints across dormancy. A relationship with an
  absent endpoint is a ghost no system owns; leases keep endpoints present,
  departure is terminal.
- Constraint entities with an arbitrary owning registry, and a
  `PhysicsConstraintScene` with global authority (rejected in revision two).

---

## 1. Goal

Add a reusable physics mechanism that drives one physics body toward a pose
defined relative to another entity while preserving collision response.

The invariant: a follower body attempts to maintain a configured position and
orientation relative to a moving target frame. The physics solver resolves the
follower against the world, and the constraint reports when the relationship
can no longer be maintained within configured limits.

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
  structural-version-gated reconciliation, `PhysicsBodyLink` for hash-free
  sync, destructor removes the registry's bodies. `CharacterMoverPool` repeats
  the pattern for character movers. Both self-describe as registry-local but
  live in the ECS `World`'s resource bag, while `Registry` carries a dedicated
  `ResourceRegistry Resources` member whose comment names "physics worlds"
  among intended tenants. `ActiveCameraService` already lives there.
- Zone lifecycle mutates only at drain points outside a live frame view
  (asserted in `ZoneRuntime`): `DestroyZone` runs from async-task completion
  (`AsyncZoneLoader.cpp`), `SetParticipation` from `WorldPartitionRuntime`
  updates and zone builders. `SetParticipation` is a direct field assignment
  and `DestroyZone` erases the owning pointer immediately — the two places the
  transition seam goes. The same zone's participation can change more than once
  in one drain window, so the change batch coalesces.
- `FramePhase` is a runtime-only enum (`DrainAsyncTasks = 3`,
  `ScheduleTicks = 4`); inserting a phase renumbers nothing persisted.
- `EntityRef` exists with zero consumers. `RegistryId` allocation is monotonic,
  generation currently fixed at 1. `FrameRegistryView` is spans only; the
  global registry participates in every span, ordered first.
- Dormancy is routine: `WorldPartitionRuntime` demotes the previous focus zone
  as normal streaming behavior.
- Transform propagation runs after physics inside Simulate;
  `CharacterControllerSystem` runs after `PhysicsStepSystem`. Poses read during
  physics can be one tick stale (Section 5.7).
- Resource destruction order is unspecified in both resource stores.
- `RigidBody` is linear-only with an unsynchronized `GravityScale` field.
  `PhysicsBodyId` is non-generational, safe for bodies only because records and
  links reconcile as a unit within one registry.

### 2.1 Live defects this revision fixes

- **Hook contract incomplete.** `World::DestroyEntity` removes the row and
  fires no `OnRemove` (`World.h`), and the traits documentation's fire table
  (`docs/ecs/component-traits.md`) lists add/remove paths only — destruction is
  absent. Yet the documented flagship use is refcounted handles, and
  `StaticMeshComponent` and `AudioSourceComponent` ship live `OnAdd` retains.
  Destroying such an entity leaks its retains. Implementation must first write
  the failing regression (destroy leaks a retain; unload leaks all retains)
  and confirm no compensating sweep exists elsewhere.
- **`World` teardown order.** The destructor clears resources before component
  storage, so even with destruction hooks, unload-path releases could not
  reach their targets.
- **Dormant zones keep physical presence.** Bodies of a registry that left the
  physics span remain active in the shared `PhysicsWorld`: they simulate,
  collide, and answer queries, violating the documented participation
  contract. No code path removes or parks them today.
- **`PinZone` is not composable.** One `ZonePin` per zone: a second
  `PinZone` call overwrites the existing minimum
  (`WorldPartitionRuntime.cpp:100-110`) and `UnpinZone` erases every pin for
  the zone (`:112-115`). Two independent holders cannot coexist and the first
  release destroys the other's hold. Any system built on pins for relationship
  retention inherits this defect.

---

## 3. Track A: substrate

### A1 — Complete the ECS lifecycle contract

`DestroyEntity` fires `OnRemove` for every non-empty hooked component of the
entity, before the row is removed, in column order (deterministic), under the
existing `ScopedLifecycleHook` guard — the no-structural-mutation rule already
covers reentrancy. The `CommandBuffer` destroy path fires the same hooks at
flush. Archetypes precompute their hooked-column list at creation, so hook-free
archetypes pay one branch.

`World` teardown (destructor and move-assignment) drains `OnRemove` for all
live hooked components before destroying component storage, and destroys
resources last, so hooks can reach release targets. Zone unload thereby
releases every retained handle.

The batching rule in `docs/ecs/decisions.md` D1.5 extends naturally: bulk
destroys of hooked archetypes run per-entity in record order; hook-free
archetypes keep the bulk path.

Division of labor, stated once and written into the traits doc: **hooks own
per-component resource correctness** (refcounts, external retains);
**binding records own per-mechanism backend residency** (backend objects that
need reconciliation, iteration, and eviction). Hooks do not replace records,
and generic ECS destruction is never taught to remove Jolt objects or audio
voices — each binding owns its projection.

Exit: the previously-failing leak regressions pass; hook order is
deterministic and tested; ECS suite green; the traits doc's fire table
includes destruction and teardown rows.

### A2 — Residency transitions and the RegistryResidency phase

`ZoneRuntime` records a change at each mutation site instead of mutating
silently:

```cpp
enum class RegistryResidencyChangeKind : uint8_t
{
    Attached,
    ParticipationChanged,
    Detaching,
};

struct RegistryResidencyChange
{
    RegistryResidencyChangeKind Kind;

    RegistryId Registry;
    Registry* Instance;          // valid while the batch is processed

    ZoneParticipation Previous;
    ZoneParticipation Current;
};
```

- `AttachZone` / `CreateZone` produce `Attached` (with the initial
  participation as `Current`).
- `SetParticipation` produces `ParticipationChanged`. Multiple changes to one
  registry within a drain window coalesce to first-observed `Previous` and
  final `Current`; intermediate states were never observable to any frame. An
  attach followed by participation changes coalesces into one `Attached` with
  the final participation.
- `DestroyZone` marks the zone `Detaching` and produces the change; the owning
  pointer is erased only after the batch is processed. The registry and all of
  its resources are alive and readable during the visit.

A new frame phase processes the batch:

```text
DrainAsyncTasks       async commits land; attach / participation / destroy
                      requests apply; ZoneRuntime coalesces the change batch
RegistryResidency     systems process the batch (new phase)
                      ZoneRuntime destroys registries marked Detaching
ScheduleTicks         BuildFrameView (never sees unprocessed residency state)
Simulate ...          unchanged
```

The phase runs once per rendered frame, including frames with zero fixed
ticks, which is what deletes revision three's carry-over machinery: there is
no "transition waiting for a physics tick" state. Systems opt in via
`RegistryResidency(RegistryResidencyContext&)` on the engine schedule, ordered
explicitly like every other phase. Backend mutation here is legal and safe:
same owner thread, between steps, deterministic batch order (request order
after coalescing).

`FrameRegistryView` stays an iteration view and gains resolution only:

```cpp
Registry* Find(RegistryId id) const;   // participating this frame
bool IsAlive(EntityRef ref) const;     // Find + generational check
```

Simulation code resolves through the view — outside the frame means
unresolvable, the correct default. Residency consumers use the live pointer in
the change record. Tools and lifecycle orchestration keep
`ZoneRuntime::FindRegistry`.

Exit: transition coverage in zone runtime tests — attach, participation
changes (including coalescing), destroy, batch ordering, pointer validity
through the visit, destruction deferred until after processing; view
resolution coverage; a zero-tick frame still processes residency.

### A3 — TickEventChannel

```cpp
template <typename T>
class TickEventChannel
{
public:
    void Stage(const T& event);      // outside the step: residency phase, teardown
    void Publish(const T& event);    // during the step
    void BeginTick();                // fold staged into published, clear old
    std::span<const T> Items() const;
};
```

Built on `EventBuffer<T>`, placed beside it in `core/event`. Single-threaded
by contract. Visibility rules are the channel's documentation, not each
subsystem's: same-tick `PostFixed` consumers see the tick's events plus
anything staged since the last executed tick; frame-lane consumers see only
the last executed tick; a zero-tick frame leaves staged events pending until
the next tick that runs.

Exit: focused tests including zero-tick frames, stage-from-residency-phase,
and multi-tick frames.

---

## 4. Track B: physics residency and leases

### B1 — Binding ownership refactor

Move `PhysicsScene` and `CharacterMoverPool` into `Registry::Resources`;
rename `PhysicsScene` to `RigidBodyBinding`:

```text
PhysicsStepSystem            owns PhysicsWorld, CollisionShapeCache,
                             step orchestration, TickEventChannel instances,
                             physics residency processing

Registry::Resources          RigidBodyBinding
                             CharacterMoverPool
                             DrivenPoseBinding     (Track C)

Registry::Components (World) authored components, runtime link components
```

Behavior-preserving; direct tests keep constructing bare `World`s and hold
bindings as locals. Every backend removal path is order-independent
(decision 16).

Exit: full suite green; a regression asserts bindings are reachable in
`Registry::Resources` and absent from the `World` bag.

### B2 — Shared reconcile protocol

After all three bindings exist in code (post C/P4), extract the genuinely
shared part of the skeleton — the version gate, the sweep protocol shape, the
counters — as a small composition utility. Storage stays per-binding: the
pool's stable-slot free list and the dense swap-remove vector are semantic
differences, not duplication. If the diff shows the shared part is smaller
than the ceremony of extracting it, record that finding and keep the pattern;
the conformance harness, not code sharing, is what protects future bindings.

### B3 — The residency contract: evict, restore, detach

The governing rule, per binding lifecycle method, driven by
`PhysicsStepSystem`'s `RegistryResidency` handler consuming the change batch:

```text
Participating          backend objects exist and are synchronized (normal passes)
Leaving physics        LeavePhysics: evict — no contacts, no default query hits,
                       no solver cost; records and links retained
Dormant                zero backend presence; binding idle
Returning              EnterPhysics: restore from component-authoritative state,
                       before the frame's normal reconcile
Detaching              Detach: remove backend objects and relationships with the
                       registry fully readable; stage terminal events
Destroyed              destructor is verified fallback: cleans anything left,
                       increments a diagnostics counter when it had to
```

Concretely, no shared interface (decision 9):

```cpp
class RigidBodyBinding
{
    void EnterPhysics(World&, PhysicsWorld&);
    void LeavePhysics(World&, PhysicsWorld&);
    void Detach(World&, PhysicsWorld&);
    // + existing sync passes
};
// CharacterMoverPool and DrivenPoseBinding: same shape, own semantics.
```

For rigid bodies, leave captures body state, removes or parks the body
(implementation choice behind the binding and `PhysicsWorld`; the observable
contract is zero simulation presence), and keeps the record; enter
reconstructs or reinserts and restores transform and velocity before normal
reconciliation resumes. Movers park under the same contract. Driven
constraints terminate on leave and detach (Section 5.6) — relationships end
when an endpoint leaves the simulation.

This corrects the standing contract violation and is a behavior change:
dormant zone geometry stops being collidable and stops answering default
queries. That is what the participation contract always claimed; content
relying on the violation was relying on a bug. Called out loudly in change
notes.

Exit: fitness tests prove a dormant zone's simulation presence is zero, a
returning zone restores component-faithful state, detach leaves zero backend
residue with full-fidelity events, repeated cycles allocate nothing in steady
state, and the fallback counter stays at zero through the whole suite.

### B4 — Conformance harness and the contract document

A parameterized gauntlet any binding runs; the harness supplies the sequence
and assertions, each binding supplies adapter operations and backend counts:

```text
Populate -> Participate -> Leave (verify zero presence) -> Return (verify
deterministic restoration) -> Destroy entities (verify reconciliation) ->
Detach registry (verify zero residue, events) -> churn and repeat
```

All three bindings pass it in this ticket; the fourth binding a year from now
gets a merge gate instead of an architecture argument.

`docs/architecture/backend-residency.md` lands with this phase and states: the
binding criterion, the lifecycle table above, the hooks-vs-records division,
lease semantics, the resolution split (view / change record / `FindRegistry`),
and the one-sentence contract from this plan's header. `ZoneParticipation`'s
comment and the traits doc are updated to point at it.

### B5 — Participation leases

Generational floors owned by `ZoneRuntime`:

```cpp
struct ParticipationLeaseId
{
    uint32_t Index;
    uint32_t Generation;
};

ParticipationLeaseId AcquireParticipationLease(ZoneId zone,
                                               ZoneParticipation minimum);
void ReleaseParticipationLease(ParticipationLeaseId lease);
```

- Effective participation is the union of streaming demand, authored pins, and
  active lease floors. `ZoneDemand` already unions pin minimums; leases join
  that computation, and unload selection skips zones with active floors the
  way it skips pinned zones today.
- Leases are acquired and released by game code (and future engine features
  that own a policy decision, e.g. save snapshots). The engine's constraint
  mechanism never acquires them implicitly (decision 10). The documented
  pickup recipe: acquire physics leases on both endpoint zones when creating
  the constraint, release them on `DrivenPoseEnded`.
- Leases prevent routine demotion, not immortality: forced teardown (world
  shutdown, explicit destruction) overrides floors, and holders observe the
  outcome through terminal events plus lease invalidation
  (`IsLeaseValid` answers false after override).
- The global registry needs no lease (never dormant); acquiring against it
  succeeds as a no-op. Acquiring against a detaching zone fails visibly.
- `PinZone`/`UnpinZone` remain for authored single-owner pins; their
  non-composability is documented at the declaration. Whether they migrate
  onto leases later is a world-partition decision outside this ticket.

Exit: lease tests — independent holders compose, release order is irrelevant,
floors hold participation and block unload selection, forced teardown
overrides and invalidates, generational ids reject stale releases.

---

## 5. Track C: the driven pose constraint

The mechanism as corrected through prior revisions, on top of Tracks A and B.
Ownership argument, preserved: the follower is the one unambiguous owner. A
constraint entity would need an arbitrary owning registry, third-party
lifecycle, and placement guidance; a component on the follower makes discovery
and body binding own-registry concerns where structural-version gating works,
and shrinks cross-registry coupling to const reads of the target. Accepted
limitation, still deliberate: one pose driver per follower (one component per
type per entity). Competing drives are the recorded trigger for a future
relationship-entity mechanism; the backend API is component-model-agnostic, so
that migration would not touch it.

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
only ever shows `Pending` or `Active` — terminal outcomes arrive exclusively
via the event.

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
frequency and damping. Error thresholds carry reset hysteresis; break
evaluation lives in the binding; `RequiredDuration` gives forgiving pickups
without game policy in the engine.

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
world-space frame, one-way, while the follower collides normally. It never
sees the target entity or its attachment frame. The hidden realization
(kinematic anchor, mass scaling, or another Jolt-side mechanism) never leaks.

Targets are refreshed every step by the binding. With residency transitions
explicit, an unrefreshed live constraint is an orchestration bug, not a
semantic state: debug builds assert it; release builds skip driving the stale
constraint and count it in diagnostics.

`RemoveBody` invalidates dependent constraints before removing the body, on
every path, in either cleanup order.

### 5.4 DrivenPoseBinding

Per-registry resource beside the other two bindings. Dense records are
canonical: follower id, handle, resolved target, previous target transform,
failure accumulation, state.

- **Reconcile** (structural-version gated): create backend constraints for
  followers whose body link exists — the registry's body pass runs first —
  remove constraints whose definition vanished, sweep dead followers.
  Statically invalid configurations (target ref equals follower, invalid ref,
  no dynamic `RigidBody`, nonfinite frames or drive values) consume the
  request immediately with a loud debug diagnostic. Pending (body not yet
  bound) is re-checked per step, surfaced by diagnostics, never silently
  timed out.
- **Configuration** (`Changed<DrivenPoseConstraint>`): push edits to the
  backend. Chunk-conservative re-pushes are cheap; resurrection is impossible
  because terminal constraints have no component left to mark.
- **PrepareStep** (every tick): resolve `Target` through the frame view,
  compose the driven world frame (`targetWorld * TargetLocalFrame`) and its
  velocities at the frame origin — `v + w x r` for off-center frames on
  rotating targets, not the target origin's velocity — classify teleports,
  refresh the backend. Composition is pure math with table-driven tests.
  Foreign-registry reads are const-only. Resolution failure is recorded for
  terminal handling.
- **CollectResults** (every tick): pull telemetry, evaluate breaks with
  hysteresis, publish `DrivenPoseEnded`, and for each terminal constraint
  remove the backend object plus all three components in one command-buffer
  flush.
- **LeavePhysics / Detach** (residency phase): terminate all constraints with
  full refs — reasons `OwnerInactive` and `OwnerUnloaded` respectively —
  staging events into the channel. Detach runs with the registry fully
  readable; the destructor is fallback only.

Target velocity source: the target's `RigidBody` component values when
present (component reads only), otherwise fixed-step transform deltas with
quaternion delta for angular; teleports classify before derivation so a
teleport never manufactures a synthetic velocity.

### 5.5 Step orchestration

Residency processing happens in the `RegistryResidency` phase before the frame
view exists, so the fixed-step sequence needs no transition handling:

```text
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
guarantees every registry's bodies exist before any registry binds
constraints. Constraint passes are permanently excluded from any per-registry
parallel axis — they read foreign registries; the exclusion is by
construction, not convention.

### 5.6 Lifecycle, termination, events

States: `Pending -> Active -> Broken | Invalid`. Terminal completion consumes
the request (decision 13); rearming is a fresh component.

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
    OwnerUnloaded,      // follower's registry detached
    OwnerInactive,      // follower's registry left the physics domain
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

All events carry full refs — leave and detach both run with records intact,
which the residency phase exists to guarantee. Events ride the engine-owned
`TickEventChannel<DrivenPoseEnded>` on `PhysicsStepSystem`; bindings hold a
non-owning channel pointer (`EngineSchedule` outlives `ZoneRuntime`).

The engine reports the physical result only; games decide what it means. The
integrator-facing guidance is now concrete instead of aspirational: streaming
demotes zones routinely, so a game that wants a cross-zone relationship to
survive acquires participation leases on both endpoint zones when creating the
constraint and releases them on `DrivenPoseEnded` (B5). A game that does not
lease gets a deterministic terminal event naming the reason.

### 5.7 Target pose staleness (documented v1 contract)

Pose source: `WorldTransform` when present, else `LocalTransform`.
Propagation runs after physics, so a parented target's world pose is one tick
old — sockets on moving parents lag one fixed step; v1 recommends top-level or
physics-driven targets, and on-demand parent-chain propagation is the recorded
future option. Character-driven targets are likewise one tick stale
(`CharacterControllerSystem` runs after the step); spring response masks it,
locked response will trail.

### 5.8 Collision and diagnostics

The follower collides normally per collision filtering; the driven frame
carries no collision shape; follower-versus-source filtering is ordinary layer
configuration. Debug drawing: frame axes, desired frame, error vector and arc,
force and torque magnitudes, state coloring, thresholds. Console:

```text
physics.constraints.debug
physics.constraints.log_breaks
physics.constraints.count
physics.constraints.warn_pending_steps   (0 = off)
```

Counters: active, pending, created and removed per step, breaks by reason,
stale-refresh invariant hits, fallback-destructor cleanups, reconcile passes,
resolution failures. No Jolt type outside the physics implementation.

---

## 6. Execution order

```text
Track A (substrate)      A1 destroy hooks + teardown order
                         A2 residency transitions + RegistryResidency phase
                         A3 TickEventChannel
Track B (residency)      B1 binding move + rename            (independent of A)
                         B3 evict / restore / detach          (needs A2)
                         B5 participation leases              (needs A2)
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
parallel. Each lands as its own commit series with its own tests.

Phase exit conditions are stated inline in Tracks A and B. For Track C:

- **P1**: dynamic bodies preserve and report full linear and angular state
  through ECS sync (including the currently-dead `GravityScale`).
- **P2**: a body follows a translating and rotating driven frame while
  colliding; source receives no feedback; stale-refresh asserts in debug;
  handles are generational and idempotent to remove.
- **P3**: rigid and forgiving following from one mechanism; limits enforced;
  deterministic repeated runs.
- **P4**: constraints bind and unbind across registries with no leaks and no
  steady-state topology scans; events flow through the channel; leave,
  detach, and restore paths exercised through the residency phase.
- **P5**: obstruction, overload, teleports, and participation loss produce
  deterministic, inspectable terminal events; no auto-recreation.
- **P6**: tunable and inspectable without game code.

---

## 7. Required tests

Substrate:

- destroy fires `OnRemove` (direct and command buffer) in deterministic
  order; world teardown drains hooks before resources; the mesh/audio retain
  leaks regress before the fix and pass after
- residency: attach / participation / detach change records, coalescing,
  batch order, pointer validity through the visit, deferred destruction,
  zero-tick frames still process residency
- view resolution: participating-only `Find`, generational `IsAlive`
- channel: zero-tick frames, stage-from-residency, multi-tick frames

Residency and leases:

- conformance gauntlet for all three bindings, zero backend residue at each
  edge, fallback counter zero throughout
- dormant zone: no contacts, no default query hits, zero solver cost; restore
  is component-faithful
- leases: independent holders compose, release order irrelevant, floors hold
  participation and block unload selection, forced teardown overrides and
  invalidates, stale lease ids rejected

PhysicsWorld (direct, headless, fixed stepping):

- locked and spring following, translation and rotation, off-center paths
- zero-gravity suspension; collision preserved; one-way isolation
- force and torque limits; teleport flag semantics
- generational handles: stale handle never resolves after slot reuse
- `RemoveBody` invalidates dependents in both cleanup orders; dead-handle
  removal is a no-op; stale-refresh asserts in debug
- identical runs produce identical results

ECS integration:

- binds the step the follower's body binds; pending before
- every terminal reason reachable and correct, components consumed in one
  flush
- neighbor-chunk `Changed` marks cannot resurrect a broken follower
- off-center rotating target receives `v + w x r`, verified analytically
- `OwnerUnloaded` staged at detach survives to the next executed tick's
  `PostFixed`, including across zero-tick frames
- a leased endpoint zone is not demoted by streaming while the lease is held;
  releasing the lease permits demotion and the constraint then ends with
  `TargetUnavailable` or `OwnerInactive` as appropriate
- global-registry follower against zone-registry target
- steady-state ticks: no topology reconciliation (per-tick refresh expected)

Stress and fitness:

- cost scales with active constraints; inactive entities free; no
  steady-state allocation; create/destroy churn leaks nothing; counts reach
  zero after registry detach; invalid data loud in debug, safe in release

---

## 8. Performance expectations

Steady state: `O(active bodies + active constraints x physics registries)` —
the resolution term is a scan over a deliberately small span, stated honestly;
an indexed lookup is the recorded fix if it ever profiles. Topology work is
version-gated; refresh and telemetry are contiguous record walks. Residency
processing is proportional to the change batch, which is empty on almost every
frame and bounded by streaming activity otherwise. Eviction and restore are
proportional to the transitioning zone's contents at streaming frequency, not
tick frequency. Lease bookkeeping is O(active leases) at demand-computation
time. Hook completion adds cost only to hooked archetypes on structural paths.
Anything bounded logs what it dropped.

---

## 9. Acceptance criteria

1. A dynamic body preserves a complete relative pose to a translating and
   rotating target that can be any entity with a valid world transform; the
   follower collides normally; the target receives no feedback.
2. Locked and spring responses are one mechanism; breaks report error, force,
   torque, and terminal reason; endpoints work across registries via
   `EntityRef`.
3. Component lifecycle is complete: no path exists on which a hooked
   component leaves the world without its hook firing, including zone unload.
4. A dormant registry has zero presence in the shared simulation; returning
   restores it faithfully; detaching removes everything with full-fidelity
   events while the registry is still readable; destructors are verified
   fallback with a zero counter across the suite.
5. Independent systems can hold participation concurrently through leases,
   and a leased cross-zone relationship survives routine streaming.
6. Every unload, dormancy, and destruction path ends deterministically with
   the documented reason and no dangling backend object — proven by the
   conformance harness for all three bindings.
7. Bindings live in `Registry::Resources`; the residency contract is a merged
   document; no game, genre, or project term in the engine API; no Jolt type
   across the firewall.
8. No steady-state structural scan or allocation; a game implements suspended
   actors and forgiving pickups without touching the backend.

---

## 10. Deferred

Bilateral joints, distance and rope limits, hinges, cone and twist limits,
ragdolls, breakable assemblies, networked prediction, persistent saved
constraints, editor-authored mechanical assemblies, cross-registry entity
migration, on-demand target-chain propagation, camera spring arms, migrating
the audio sweep onto residency transitions, migrating authored pins onto
leases, indexed registry resolution, non-physics residency consumers. Each now
has a substrate to land on; none expands this ticket further. Competing
multi-driver poses remain the recorded trigger for a relationship-entity
mechanism.
