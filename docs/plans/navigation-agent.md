# Navigation agent: requests and path following (SEN-F012)

Status: **approved, revision 3** (2026-10-15); stages 0-6 implemented on
`claude/navigation-agent-plan`. The current architecture is described in
`docs/navigation/runtime.md`, "Agents"; this document remains the record of
the decisions. Ticket: GitHub #100. Base: `origin/main` at `8f13cb5e`.

**Where the implementation departs from this plan:**

- **Stage boundaries.** The repath reactions (validation, refresh, deviation,
  stuck) landed with Stage 2's system, because they live in the same passes.
  Stage 3 added the fixtures that exercise them.
- **The `Planning` phase.** It never shows between ticks for a new order: pass 2
  plans in the same tick pass 1 takes the order up, as D11 intends. It shows
  only while an order waits in a full queue.
- **Authored queries.** These are hand-declared engine queries
  (`navigation.phase`, `navigation.failure`, `navigation.remaining`), as
  perception's are, not `SENCHA_QUERY` members of `NavAgentProgress`. The engine
  binds them in `EngineVocabularyBindings::BindNavigation`.
- **Console.** `nav.agent.goto <agent> me` replaces `... aim`, which needs the
  physics world and is deferred (`docs/deferred.md`).
- **Allocation.** With workers, a planning tick allocates exactly once:
  `JobSystem::ParallelFor`'s own per-batch control block, which every
  `ParallelFor` user pays. The follower itself allocates nothing in steady state
  (`test/core/NavigationAllocationTests.cpp`).
- **Request API.** `NavDestination::AtPoint` and `AtEntity` construct orders.
- **Registration.** `RegisterNavigationAgents` takes the runtime world and the
  data-asset cache too.

Revision 3 makes five local corrections from the second review:

- **Self-contained planning input.** `NavPlanInput` holds arrays and counts.
  `PlanRoute` builds the span-bearing request itself (section 3, Stage 2's
  relocation test).
- **Failure cause versus last search.** `TraversalFailed` is kept apart from
  `LastQuery` (D7, test E).
- **Route proximity for corners.** Corner overshoot advances only near the
  route (D4, test K).
- **No capability truncation.** Capability storage covers the zone's own
  64-kind cap (D7).
- **Queue age.** A new request does not inherit a superseded request's queue
  age (D5).

Revision 2 answers the first review:

- **Planning.** Route planning runs as parallel fork-join batches from the
  start (D11).
- **Budget.** The fixed four-plans-per-tick budget is replaced by a measured,
  worker-independent budget with priority classes (D5, D11).
- **Invalidated routes.** An invalidated route is no longer followed while its
  replan waits (D5).
- **Determinism test.** It now claims only what `EntityId`-keyed ordering
  guarantees (G).
- **No binding-cache extraction.** Stage 0's shared binding-cache extraction is
  dropped (D12).

Navigation already answers where an agent can travel (`docs/navigation/runtime.md`).
This plan adds the missing middle: an entity is given a destination, follows a
route by producing `MovementIntent`, hands link crossings to gameplay, and
reports how it went. Deciding where to go stays with whoever asks.

The design is deliberately small:

- three components;
- one fixed-tick system;
- a system-owned pool of route slots;
- a registered table of traversal executors keyed by traversal tag;
- a handful of cvars.

It reuses the existing query, policy, link-state, movement, `JobSystem` and
debug-line mechanisms unchanged. It changes three existing contracts, each in a
small way (section 12).

## 1. Verified current architecture

Everything below was read in source on `origin/main`.

### Navigation (reused unchanged)

- **Queries.** `NavFindRoute`, `NavProjectPoint`, `NavRaycast` and
  `NavValidateRoute` are in `engine/include/navigation/NavigationQuery.h`.
  - They are reached through `NavigationQuery`, which
    `NavigationSystem::Queries()` returns.
  - Each takes a caller-owned `NavQueryContext` and allocates nothing after
    warm-up (`test/core/NavigationAllocationTests.cpp`).
  - Distinct contexts may query one zone concurrently.
- **Routes.** `NavRouteBuffer` (`NavigationTypes.h`) holds:
  - `Walk` steps (corner ranges) and `Traverse` steps (`Link`, `Traversal` tag,
    `Entry`, `Exit`, `EntryRadius`, `Reversed`, `Cost`);
  - corners;
  - deduplicated tile and link dependencies with their revisions.

  A walk leg that precedes a link ends exactly at the link's entry
  (`RouteAssembler::CrossLink`, `NavigationQuery.cpp`).
- **Validation.** `NavValidateRoute` runs in O(dependencies). It reports
  `ZoneReloaded`, `TileChanged` or `LinkChanged` only for what the route
  depends on.
- **Statuses.** `NavStatus` declares `Partial`, but **no operation returns
  it.**
  - A search that runs out of nodes returns `SearchLimitReached`.
  - `NavFindRoute` then clears the route (`NavSearch.cpp`,
    `NavigationQuery.cpp`).
  - There is no partial-route following to build on (section 8.4).
- **Capabilities.** `NavQueryRequest::Capabilities` lists the traversal kinds
  the agent can cross. A link is usable when its kind equals a capability or
  descends from one (`NavQueryPlan.cpp`, `Grants`).
- **Avoidance.** `AvoidLinks` and `LinkCostOverrides` are the agent's own
  memory, held by the caller.
- **Policy data.**
  - `navigation.policy` data assets compile through `CompileNavigationPolicy`
    and bind through `BindNavigationPolicy`.
  - **No runtime code binds a policy today.** The only callers are tests and
    candidate evaluation, which receive a ready `NavQueryRequest` from their
    caller.
- **`NavigationSystem`** is engine-registered for every host (`Engine.cpp`).
  - It runs `PostFixed`: detect geometry, rebuild tiles, apply `NavLinkState`.
  - A change made in tick T is therefore visible to queries in T+1.
  - It processes every partition, dormant ones included: `NavLinkStateSync` uses
    `ForEachChunk`. `docs/plans/navigation-core.md` §5.3 says "Logic plus
    persistent", which is wrong; `docs/navigation/runtime.md` is right.
- **Zone resource lifetime.**
  - `ZoneNavigation` is a zone resource and dies with the zone.
  - `FindZoneNavigation` answers for dormant resident zones.
  - The build profile's parameters, including radius, are reachable as
    `zone.Mesh(i).Profile()`.
- **Traversal tags.** No engine or template code registers any
  `navigation.traversal.*` tag. Only tests do (`NavigationFixture.h`).
- **Queries are pure reads.**
  - A query reads its `ZoneNavigation`, the request's policy and spans, and
    `GameplayTagRegistry::IsDescendantOf`, which is a const parent walk with no
    caches.
  - Everything it writes is in its `NavQueryContext` and output buffer.
  - `ContextsDoNotContaminateEachOther` and
    `ParallelQueriesMatchTheSerialReference` pin this. The batch probe confirms
    it at 1–14 workers (section 9).

### When a `ZoneNavigation` can change

Every mutation or lifetime event happens outside the FixedLogic hook:

| Event | Where | Phase |
| --- | --- | --- |
| Created and registered | `AttachZoneNavigation`, from `LoadedLevel::AttachSceneContent` | async commit (`DrainAsyncTasks`) |
| Tile replaced (salt bump) and links re-projected | `NavTileMesh::SetTile` through `NavTileRebuilder` | `PostFixed` |
| Link enabled or re-costed | `ZoneNavigation::SetLinkState` through `NavLinkStateSync` | `PostFixed` |
| Destroyed with its zone | `FinalizeResidencyProcessing` | `ZoneResidency` |

During FixedLogic, then, every resident zone's navigation is alive and
unchanging. The one exception would be gameplay calling the public non-const
`FindZoneNavigation` and then `SetLinkState`. That is documented as
"owner thread, between queries", and no system runs while another system's
fork-join is in flight: the schedule runs systems one at a time.

### `JobSystem` (reused unchanged)

- `ParallelFor(count, fn)` is a blocking fork-join. The caller participates.
  - Jobs pull indices from one atomic counter.
  - `CurrentWorkerIndex()` (0 = caller) indexes per-worker scratch sized
    `WorkerCount() + 1`.
  - `JobSystem(0)` runs every index inline in order: the serial reference.
- Contract (`jobs/JobSystem.h`):
  - no `ParallelFor` from inside a job;
  - at most one active per pool;
  - jobs must not throw;
  - jobs touch no ambient mutable engine state.
- Callers in the runtime:
  - `NavTileRebuilder` and `AnimPoseSystem`, both in PostFixed;
  - cook code (editor-only).

  No FixedLogic system uses it today.
- Worker count: `RuntimeConfig.JobWorkerCount`, or
  `DefaultWorkerCount() = hardware_concurrency - 2` (14 on the 16-thread
  measurement machine).
- Fixed tick: 60 Hz by default (`RuntimeConfig::FixedTickRate`).

### Movement (reused; one fact added, section 12)

- **The component.** `MovementIntent { Vec3d WishDir; bool Jump; }`
  (`engine/include/movement/MovementIntent.h`).
  - `WishDir` is world-space and planar. Its magnitude in [0, 1] scales
    `MaxSpeed` (`FreeLocomotionSystem.cpp:68-84`).
  - It is a derived component of `CharacterMovement`.
  - **Nothing resets it per tick.** It holds the last value written.
- **Writers.**
  - The engine has no writer.
  - Every template's steering system writes it only for entities carrying the
    `movement.controlled` tag (`templates/*/src/*SteeringSystem.cpp`).
  - Each is ordered with `OrderMovementAfterInput<T>`, which puts the writer
    before `AbilitySystem`, `FreeLocomotionSystem` and
    `ModeRequestCollectionSystem` (`MovementRegistration.h:91-97`).
  - **No arbitration mechanism exists beyond that tag filter.**
- **Readers.**
  - `FreeLocomotionSystem` reads it (`Without<ActorTimeDilated>`).
  - `JumpExecutionSystem` reads it.
  - `MovementTuningResolutionSystem` reads `Jump` for `when.jump` layers.
  - `DilatedCharacterStepSystem` reads it.
  - `PawnCommandCapture` reads it, for the predicted pawn only.
- **Facts are one tick old.** `KinematicState`, `SupportState` and
  `WorldTransform` describe tick N-1 when read in FixedLogic at tick N
  (`EngineFramePhases.cpp:761-819`).
- **Actions override locomotion inside composition.**
  - Bursts, tethers and root motion force the planar channel through
    `MotionAxisOverride`.
  - `ComposeMotion` resolves this, but `MotionRequest` does not record whether
    the planar velocity was locomotion's (`MotionChannels.h`,
    `MotionComposition.cpp`).
  - A dilated character composes inside `StepScaledCharacterTick` and never
    writes `MotionRequest` back (`CharacterTickStep.cpp:95`).
- **Jump is intent.** `docs/gameplay/movement.md` already states: "An AI or a
  navigation bridge jumps the same way a player does: by setting the intent."
- **No collision between characters.** Characters do not collide with each
  other (`docs/deferred.md`, "Dilated characters meeting characters").

### Scheduling, authority, lifetime (reused)

- **Ordering.**
  - `EngineSchedule` topologically sorts each hook by `After<>` edges.
  - Registration order only breaks ties.
  - A system with no edges runs in the first wave.
- **Fixed tick.** FixedLogic, then the authored-event drain, then Physics, then
  transform propagation, then PostFixed.
- **Authority.** `IsSimulationAuthority(world)` gates authority-only work.
  `PerceptionSystem` is the precedent: it registers opt-in and scans only on the
  authority.
- **Dormancy.** Dormant zones' entities stay alive but are absent from the
  FixedLogic `Partitions`.
- **Detach.**
  1. `ZoneResidency` systems see `Detaching` while the entities and zone
     resources are still alive.
  2. Then `DestroyPartition` runs each entity's `OnRemove`.
- **Pool reclaim.** `CharacterMoverPool` reclaims its slots by owner check:
  - each slot holds `EntityId Owner`;
  - when `World::StructuralVersion` changes, slots whose owner died or lost the
    component are freed (`CharacterMoverPool.cpp:189-210`).

  This is the reclaim pattern reused below.
- **Entity to zone.** `CandidateRun::ZoneOf` (`CandidateRun.cpp:177`) is the
  only entity-to-zone resolution in the tree.

### Authored surface and diagnostics (reused)

- **Verbs and queries.**
  - Verbs and queries are declared through `SENCHA_VERB` / `SENCHA_QUERY`.
  - Component members carrying `SENCHA_QUERY` read as `<component>.<name>`.
  - Engine vocabulary installs through `InstallEngineVocabulary`.
- **No shared request mechanism.** There is no shared "request with identity
  and outcome" mechanism.
  - `AnimRequestSet`, `ActiveAbilities` and `ModeTransitionRequest` each keep
    their own records.
  - `AbilityRequest` carries no payload.
  - `AbilityRecord` does not keep the request's cause.
- **Authored events.** `AuthoredEventDispatcher` drains after FixedLogic and is
  neither journaled nor replicated.
- **Debug lines and panels.**
  - `DebugLineChannels` is a World resource: a producer owns a named channel.
  - Perception draws through `perception.debug.draw`.
  - `IDebugPanel` panels, such as `PerceptionPanel`, own their own selection.
- **Entity text.**
  - `AppendEntityText` (`ecs/EntityText.h`) formats an entity.
  - The only parser is local to animation (`ParseAnimatedEntity`,
    `AnimationRegistration.cpp`).

## 2. Ownership

| Concern | Owner | Mechanism |
| --- | --- | --- |
| Where an entity can go, routes, validity | navigation queries | `NavigationQuery`, unchanged |
| The current destination and its identity | gameplay, through one door | `NavAgentRequest` component, written only by `RequestNavigation` / `CancelNavigation` and the verbs that call them |
| Route storage, cursor, repath and stuck bookkeeping | `NavAgentSystem` | system-owned route slots; the entity holds a slot index |
| Which plans run this tick, and in what order | `NavAgentSystem` | the planning queue: due tick, then `EntityId` (D5, D11) |
| Running plans concurrently | `JobSystem` | one `ParallelFor` per tick over gathered plain inputs; per-worker `NavQueryContext`; per-plan staging buffer (D11) |
| Status of the current request | `NavAgentSystem` | `NavAgentProgress` component, one writer |
| Movement intent of an agent being navigated | `NavAgentSystem` | the single producer for agents without `movement.controlled` (D3) |
| Physical execution | movement | locomotion → composition → motor, unchanged |
| Crossing a link | the executor registered for the traversal kind | `NavTraversalExecutors` World resource (D7) |
| Whether to go, retry or give up | gameplay or AI | reads `NavAgentProgress`; issues new requests |

Dependency direction:
- `navigation/agent/` depends on `navigation/` queries, `movement/` components,
  `gameplay_tags/`, `world/` and `jobs/` (`JobSystem` only).
- Movement does not learn about navigation.
- Navigation queries do not learn about agents.
- Physics, render and editor code are untouched. The panel and drawing go
  through the existing debug seams.

## 3. Proposed components, resources, systems, API

Illustrative C++. Names and field lists are proposals, not settled API.

```cpp
// navigation/agent/NavAgent.h -- authored, on an NPC prefab.
struct SENCHA_COMPONENT("sencha.navigation.agent") SENCHA_SCHEMA("Nav Agent")
       SENCHA_SCENE_CHUNK("NAGT")
NavAgent
{
    // Build profile tag name ("navigation.profile.humanoid"), resolved per plan.
    SENCHA_FIELD("profile") NavProfileName Profile;
    // Optional navigation.policy data asset; none means default costs.
    SENCHA_FIELD("policy") NavigationPolicyHandle Policy;
};
// Derived components of NavAgent (the DerivedComponents mechanism):
// NavAgentRequest and NavAgentProgress. Neither has a schema or scene chunk.

enum class NavDestinationKind : std::uint8_t { None, Point, Entity };

struct SENCHA_COMPONENT("sencha.navigation.agent_request") NavAgentRequest
{
    NavRequestId Id;                 // per-agent sequence, nonzero once used
    NavDestinationKind Kind = NavDestinationKind::None;
    bool Follow = false;             // Entity only: keep tracking after arrival
    Vec3d Point = Vec3d::Zero();
    EntityId Target;
    float ArriveRadius = 0.0f;       // 0: nav.agent.arrive_radius
    float SpeedScale = 1.0f;         // (0, 1], scales WishDir magnitude
};

enum class NavAgentPhase : std::uint8_t
{
    Idle, Planning, Moving, Traversing, Holding, Arrived, Failed, Cancelled
};

enum class NavAgentFailure : std::uint8_t
{
    None, NoZone, ZoneUnavailable, ProfileUnavailable, StartOffNavigation,
    DestinationOffNavigation, DestinationInOtherZone, NoPath, SearchLimitReached,
    RouteTooLong, TargetLost, Stuck, TraversalFailed, TraversalUnsupported,
    TakenOverByInput
};

enum class NavRepathReason : std::uint8_t
{
    None, ZoneReloaded, TileChanged, LinkChanged, ZoneChanged, TargetMoved,
    Deviated, Stuck, TraversalFailed
};

struct SENCHA_COMPONENT("sencha.navigation.agent_progress") NavAgentProgress
{
    SENCHA_QUERY("request")   NavRequestId Request;   // the request these fields describe
    SENCHA_QUERY("phase")     NavAgentPhase Phase = NavAgentPhase::Idle;
    SENCHA_QUERY("failure")   NavAgentFailure Failure = NavAgentFailure::None;
    SENCHA_QUERY("remaining") float Remaining = 0.0f;  // metres along the route
    SENCHA_QUERY("destination") Vec3d Destination = Vec3d::Zero(); // as last planned
    NavRepathReason LastRepath = NavRepathReason::None;
    NavStatus LastQuery = NavStatus::Success;
    std::uint16_t Repaths = 0;
    NavRouteSlot Slot;               // index into the system's slots; owner-checked
};
```

Request API (`navigation/agent/NavAgentRequests.h`), the only writers of
`NavAgentRequest`:

```cpp
struct NavDestination { NavDestinationKind Kind; Vec3d Point; EntityId Target;
                        bool Follow = false; float ArriveRadius = 0; float SpeedScale = 1; };

// Replaces any current request. Returns the new id; invalid if `agent` has no NavAgent.
NavRequestId RequestNavigation(World&, EntityId agent, const NavDestination&);
// Ends the current request; its progress reports Cancelled under the returned id.
NavRequestId CancelNavigation(World&, EntityId agent);

enum class NavRequestStanding : std::uint8_t { Current, Superseded, NoAgent };
struct NavRequestState { NavRequestStanding Standing; NavAgentPhase Phase; NavAgentFailure Failure; };
// What became of `request`: its phase if it is still the agent's current one.
NavRequestState ReadNavigation(const World&, EntityId agent, NavRequestId request);
```

Authored surface, installed with the engine vocabulary. These are instant
gameplay verbs, authority only:

| Verb or query | Arguments | Effect / answer |
| --- | --- | --- |
| `navigation.move_to` | target (NavAgent), point | `RequestNavigation` to a point |
| `navigation.approach` | target, subject (entity), follow=false | `RequestNavigation` to an entity |
| `navigation.stop` | target | `CancelNavigation` |
| `navigation.phase` / `navigation.failure` / `navigation.remaining` | agent | `NavAgentProgress`'s phase, failure (enum names) and remaining metres |

Traversal executors (D7):

```cpp
enum class NavTraversalResult : std::uint8_t { Running, Succeeded, Failed };

struct NavTraversalFrame
{
    World& Entities;
    EntityId Agent;
    const NavRouteStep& Step;     // Link, Traversal, Entry, Exit, EntryRadius
    std::uint32_t Ticks;          // ticks since the traversal began; 0 on the first call
    float DeltaSeconds;           // the agent's seconds this tick (ActorTime scale applied)
    MovementIntent& Intent;       // the follower's intent for this agent; write here only
};

struct NavTraversalExecutor
{
    NavTraversalResult (*Step)(NavTraversalFrame&) = nullptr;
    // Called when the request is replaced or cancelled mid-crossing.
    void (*Abort)(World&, EntityId agent, const NavRouteStep&) = nullptr;
};

// World resource. Resolution picks the nearest registered ancestor-or-self of a
// link's kind, so registering navigation.traversal.jump covers .jump.long.
class NavTraversalExecutors
{
public:
    bool Register(GameplayTagId kind, NavTraversalExecutor executor);
    [[nodiscard]] const NavTraversalExecutor* Resolve(const GameplayTagRegistry&,
                                                      GameplayTagId kind) const;
};

// Engine-provided executors, registered by a game against its own kinds:
NavTraversalResult NavTraverseByWalking(NavTraversalFrame&);  // drops, gaps a step crosses
NavTraversalResult NavTraverseByJumping(NavTraversalFrame&);  // MovementIntent.Jump at the entry
```

The planning batch (`engine/src/navigation/agent/`, private to the system):

```cpp
// Gathered on the owner thread from ECS, slot and policy state. Plain values
// and pointers to state that cannot change during FixedLogic (section 1). It
// holds no span or pointer into itself, so it may be copied or relocated freely;
// PlanRoute builds the span-bearing NavQueryRequest on its own stack.
struct NavPlanInput
{
    const ZoneNavigation* Zone;
    GameplayTagId Profile;
    const NavQueryPolicy* Policy;       // the system's bound-policy table; stable across the join
    Vec3d From;                         // the agent's WorldTransform position, N-1
    Vec3d To;                           // the point, or the target's position, N-1
    Vec3d Extents;                      // projection extents from the build profile
    // Usable kinds are a subset of the zone's distinct kinds, which the zone caps
    // at kNavMaxTraversalKinds, so this array cannot overflow.
    std::array<GameplayTagId, kNavMaxTraversalKinds> Capabilities;
    std::uint8_t CapabilityCount = 0;
    std::array<NavLinkId, kNavAgentAvoidLinks> AvoidLinks;
    std::uint8_t AvoidLinkCount = 0;
};

// Written by exactly one job; read on the owner thread at commit.
struct NavPlanOutput
{
    NavStatus Status;
    NavLocation Start, End;
    NavQueryDiagnostics Diagnostics;
    NavRouteBuffer* Route;              // a staging buffer, swapped into the slot on success
};

// One function for every path: inline below the parallel threshold or without
// workers, and as the ParallelFor body otherwise.
void PlanRoute(const NavPlanInput&, NavPlanOutput&, NavQueryContext&);
```

System and registration:

```cpp
// FixedLogic, authority only. Opt-in, after RegisterMovementSystems.
// `jobs` may be null or have zero workers: the planning batch then runs inline.
void RegisterNavigationAgents(EngineSchedule&, JobSystem* jobs, ConsoleRegistry*);
// Orders NavAgentSystem as a movement producer (OrderMovementAfterInput) and
// after the game's own decision system, which issues requests this tick.
template <typename TDecisionSystem> void OrderNavigationAfter(EngineSchedule&);
```

## 4. Lifecycle

```
            RequestNavigation (new Id)
 Idle/any ───────────────────────────► Planning ──plan ok──► Moving ◄──────────┐
                                          │                   │  │             │
                                     plan fails          entry of Traverse    exit ok
                                          ▼                   │  ▼             │
                                       Failed            Traversing ───────────┘
                                                              │ executor Failed → repath, link avoided
   CancelNavigation (new Id) ──► Cancelled            Moving ─┴─► Arrived  (Point, or Entity without Follow)
                                                      Moving ◄──► Holding  (Entity with Follow)
```

One `NavAgentSystem::FixedLogic` runs three passes.

**Pass 1: sweep (serial).** For each agent in the Logic partitions with a
request:

1. **Request change.** If `Request.Id != Progress.Request`, the old request
   ends silently:
   - its slot is reset;
   - a mid-crossing executor's `Abort` runs;
   - `Progress` adopts the new id.

   A `None` kind becomes `Cancelled` and writes zero intent once. Any other kind
   becomes `Planning` and is queued as **blocking**.
2. **Preemption.** If the agent carries `movement.controlled`, the request
   fails with `TakenOverByInput` and the follower stops writing intent.
3. **Validate.** Call `NavValidateRoute` and react to its answer (D5).
   - An invalid route is dropped at once: the agent stops and is queued
     blocking.
   - A route that is valid but stale for a moving target is queued as
     **refresh** and kept.

**Pass 2: plan (one fork-join).**

1. Pop the plans admitted this tick (D11).
2. Gather their inputs on the owner thread.
3. Run them through `JobSystem::ParallelFor`, or inline below the threshold.
4. Commit the results in queue order: swap the staging buffer into the slot,
   then update `Progress`.

**Pass 3: follow (serial).** For each agent with a usable route:

1. **Follow.**
   - Advance the corner cursor (D4).
   - Check deviation and stuck (D4, D5), which may queue a blocking replan for
     next tick and stop the agent.
   - Write `WishDir` toward the current corner, scaled by `SpeedScale` and by
     arrival braking.
2. **Traverse.** At a `Traverse` step's entry, run that step's executor each
   tick until it returns `Succeeded` or `Failed`, or until
   `nav.agent.traversal_timeout` passes (D7).
3. **Terminal.** On reaching a terminal phase:
   - write zero intent once;
   - release the slot;
   - keep the terminal phase until the next request.

   Nothing lingers or expires, so a terminal state cannot be missed.

An agent without a usable route writes zero intent: one that is waiting for its
first plan or a blocking replan. Planning in pass 2 lets an agent queued in
pass 1 move in the same tick when the budget admits it.

## 5. Scheduling and authority contract

- **Hook.** `NavAgentSystem` runs in FixedLogic on the authority only, like
  `PerceptionSystem`. A client's agents keep whatever was replicated; their
  motion arrives through ordinary transform replication. No route, request or
  progress component is replicated, and there is no navigation protocol.
- **Ordering.**
  - It runs as a movement producer: `OrderMovementAfterInput<NavAgentSystem>`.
  - It runs after the game's decision systems (`OrderNavigationAfter<T>`), so a
    request written at tick N plans at tick N, within budget.
- **Inputs at tick N.**
  - `WorldTransform`, `KinematicState`, `SupportState` and
    `MotionRequest.PlanarOverridden`, all as of N-1.
  - Navigation as published in PostFixed of N-1. Geometry and link changes are
    therefore seen one tick after they happen, the existing navigation contract.
- **Where order matters.** Only the planning queue's admission is
  order-sensitive.
  - Pass 1 collects the tick's queue changes, then sorts them by `EntityId`
    before inserting. The queue's order, due tick then `EntityId`, therefore
    never depends on chunk layout or iteration order.
  - Each agent's sweep and follow work reads that agent's own components and
    other entities' N-1 transforms, and writes only its own components. Their
    order does not matter.
- **Planning is a fork-join batch from v1** (D11):
  - gather on the owner thread;
  - one `ParallelFor` with per-worker `NavQueryContext`s and a staging buffer
    per plan;
  - commit on the owner thread in queue order.

  `JobSystem(0)`, a null `JobSystem`, and batches below
  `nav.agent.parallel_min_batch` run the same `PlanRoute` inline in index order.
  That is the serial reference, and it is the same code.
- **Admission does not depend on workers.** Which plans run on which tick
  depends only on the queue and the cvars, never on the worker count. Serial and
  parallel runs therefore plan the same requests on the same ticks and commit
  identical results.
- **Worker contract.**
  - A job reads only its `NavPlanInput`: plain values, a `ZoneNavigation`
    pointer, the bound policy, and the tag registry. Section 1 shows none of
    them can change during FixedLogic.
  - It writes only its `NavPlanOutput`, its staging buffer and its worker's
    context.
  - It reads no ECS state, logs nothing, and allocates nothing after warm-up.
  - The batch is the only `ParallelFor` in flight, and `NavAgentSystem` never
    runs inside a job.
- **Debug guard.** In debug builds, the system asserts after the join that every
  zone a gathered input points at is still resident, with its navigation
  generation unchanged.
- **Steering stays serial.** Section 9's estimate for 256 agents is
  0.1–0.25 ms, and a parallel follow pass would need per-agent output staging
  for intent and progress. It is deferred behind a measurement (section 11).
- **Headless.** A headless server runs all of it. Drawing and the panel are
  dev-only consumers of state the system already keeps.

## 6. Decision log

### D1. Where active request state lives

- **Existing:** `NavLinkState` is gameplay data on an entity that a system
  reads once per tick. `ModeTransitionRequest` is a one-slot request component
  written through `RequestLocomotionMode`. Movement's rule is one writer per
  component.
- **Alternatives:**
  - (a) a command queue resource drained by the system;
  - (b) one component that both gameplay and the follower write;
  - (c) separate request and progress components, each with one writer.
- **Recommendation: (c), always present.**
  - `NavAgentRequest` and `NavAgentProgress` are derived from `NavAgent`.
  - They are written through `RequestNavigation` and by the system
    respectively.
- **Why:**
  - A queue would duplicate the ECS as storage, and it needs its own lifetime
    handling for dead entities.
  - One shared component has two writers.
  - Presence-as-state, adding and removing a request component, is a structural
    change per request. It would need `CommandBuffer` and would cost a tick of
    latency.
- **Cost:** an idle agent pays the components' bytes (about 0.2 KB) and one
  branch per tick. Entities without `NavAgent` pay nothing.

### D2. Route buffers and query scratch

- **Existing:**
  - `NavRouteBuffer` uses heap-backed fixed capacity (`std::vector` members),
    so it cannot live in chunk storage.
  - `NavQueryContext` is single-thread scratch.
  - `CharacterMoverPool` owns slots with owner checks.
- **Recommendation:** `NavAgentSystem` owns a vector of route slots.
  - Each slot holds:
    - `EntityId Owner`;
    - a `NavRouteBuffer`;
    - the cursor (step, corner, traversal ticks);
    - stuck-window and repath bookkeeping;
    - a small fixed avoid-list (`NavLinkId`, expiry tick) of four entries.
  - `NavAgentProgress::Slot` is an index, honoured only when
    `slot.Owner == entity`. A copied or duplicated component therefore cannot
    alias another agent's route; it simply replans.
  - Slots are taken on planning and released on terminal phases.
  - When the world's structural version changes, slots whose owner died or lost
    `NavAgent` are freed. If the owner is alive but `NavAgent` was removed, its
    `MovementIntent` is zeroed first, because nothing else would clear it.
  - The pool grows to the peak number of concurrent active agents and is
    retained, so steady state allocates nothing.
- **Query scratch.**
  - The system owns `WorkerCount() + 1` `NavQueryContext`s, indexed by
    `CurrentWorkerIndex()`.
  - It also owns `nav.agent.plans_per_tick` staging `NavRouteBuffer`s.
  - Both are created on the owner thread before the first batch that needs
    them, so a registered system that never plans holds neither.
  - A successful plan's staging buffer is `swap`ped with the slot's route on
    commit (vector swaps, no allocation), and the old route becomes the staging
    buffer.

  A slot therefore keeps its old route until the new one commits. The follower
  uses an old route only for refresh replans; an invalid route is dropped at
  once (D5).
- **Memory.** Measured at about 0.26 MB per warmed 4096-node context: 16
  contexts on a 16-thread machine ≈ 4.3 MB.
- **Rejected:**
  - A `HandlePool` generational handle is not needed. One owner reads the slots,
    and the owner check already gives the safety a generation would.
  - Copying routes into components would cost ~7 KB per agent in chunks, idle
    agents included.
  - A slot per agent for life is the same cost.
  - Planning straight into the slot would make a refresh lose its still-valid
    route while the replan waits for the batch. It would also let a job write
    owner-held state.
- **Sizing:** from measurement (section 9), slots are built as
  `NavRouteBuffer(32 steps, 64 corners, 128 dependencies)`, about 7 KB each.
  The default 64 dependencies overflowed on a 96 m maze, whose largest route
  needed 96.

### D3. One effective `MovementIntent` writer

- **Existing:** template steering systems write only `movement.controlled`
  entities. Nothing else writes intent. Intent persists between ticks.
- **Recommendation:** make the existing implicit rule explicit and owned. A
  character's intent producer is chosen by its state:
  - `movement.controlled` means input steering writes it;
  - otherwise, an agent whose request is active (`Planning` with a route,
    `Moving`, `Traversing`, `Holding`) has it written by `NavAgentSystem`;
  - otherwise nothing writes it, and the follower leaves it at zero when it
    stops.
  - Traversal executors write only the `MovementIntent&` the follower lends
    them.

  The rule goes into `docs/gameplay/movement.md` beside the `MovementIntent`
  row.
- **Taking control:** gaining `movement.controlled` mid-route (possession)
  ends the request with `TakenOverByInput`. Nothing is suspended in hiding.
- **Rejected:**
  - A steering arbitration framework (priorities, blending) has no second
    competing producer in the tree.
  - Feeding the follower through an `InputActionSource`, as a bot participant
    does, would route navigation through game-specific action vocabulary and the
    game's yaw framing.
- **Hazard documented:** an NPC prefab copied from a pawn prefab keeps
  `movement.controlled`. Template steering then writes zero every tick, and
  every request fails as `TakenOverByInput`, which is visible rather than
  silent.

### D4. Progress measurement

All of it is pure functions over plain values, tested headless.

- **Position.** Progress uses the achieved position: `WorldTransform` as of
  N-1. It never assumes the requested motion happened.
- **Deviation is checked first.** Each tick the agent's planar distance to the
  current segment (previous corner → current corner) is measured. Past
  `nav.agent.deviation`, or with the vertical gap beyond the profile height, it
  is off-route (see below). The cursor does not move, so an off-route agent
  never skips corners.
- **Corner reached.** An on-route agent reaches a corner when either holds:
  - the planar distance is at most `nav.agent.corner_radius` and the vertical
    gap is within the profile height; or
  - it has crossed the plane through the corner that bisects the turn there
    (its normal is the sum of the incoming and outgoing directions, or the
    incoming direction for a U-turn), **and** its planar distance to the next
    segment is within `nav.agent.deviation`, with the vertical gap within the
    profile height. A plane perpendicular to the next segment alone would contain
    the incoming segment at a right-angle turn, so an agent short of the corner
    would already count as past it.

  The plane test handles overshoot, and speeds whose step exceeds the radius,
  without orbiting. The proximity condition stops a knockback or teleport that
  lands beyond the plane but away from the route from counting as progress.
  Such an agent fails the plane test's proximity check and is still far from
  its current segment, so it is `Deviated`.

  The cursor may advance several corners in one tick, re-applying both
  conditions at each corner.
- **Steering.** `WishDir` points planar toward the current corner.
  - String-pulled corners need no smoothing. Corners are already inset by the
    build radius, so cutting them slightly just slides along a wall.
  - The wish magnitude is `SpeedScale × clamp(d / max(stop, ArriveRadius), 0, 1)`
    on the final leg only. Here `stop` is the free-locomotion stopping distance,
    planar speed ÷ `Friction`, from `KinematicState` and
    `ResolvedMovementTuning`.

    Template tuning (4.5 m/s, friction 6) would otherwise overshoot about 0.75 m
    past an arrival radius. Intermediate corners and link entries are not braked
    for.
- **Remaining distance.** Distance to the current corner plus the suffix length
  of the rest. The suffix sums are computed once per plan.
- **Deviation.** When the planar distance from the current segment exceeds
  `nav.agent.deviation`, the agent stops and queues a blocking replan from where
  it is (`Deviated`).
  - This covers impulses, knockback and teleports.
  - No separate teleport rule is needed.
  - Steering back toward a corner from somewhere the route does not describe
    could walk the agent off a ledge it was knocked beside, so it does not keep
    going.
- **Stuck.** Measured over a window of the agent's own time, so a slowed agent
  is not called stuck:
  - expected progress = wish speed × window;
  - the window counts only ticks where `MotionRequest.PlanarOverridden` was
    false at N-1 (section 12). Root motion, bursts and tethers do not count
    against the agent;
  - a window with less than `nav.agent.stuck_progress` × expected triggers a
    blocking replan;
  - `nav.agent.stuck_repaths` consecutive stuck replans fail the request with
    `Stuck`.
- **Dormancy gaps.** A gap in processed ticks resets the window.

### D5. Repath triggers and bounds

Every replan has a class:

- **Blocking:** the agent has no route it may follow. It stops (zero intent)
  until the replan commits.
- **Refresh:** the route is still valid and the agent keeps following it until
  the replacement commits.

| Trigger | Detected by | Class | Reason |
| --- | --- | --- | --- |
| New request | id change | blocking | (planning) |
| Route dependency changed | `NavValidateRoute` each tick (measured cheap, section 9) | blocking | `TileChanged` / `LinkChanged` / `ZoneReloaded` |
| Agent changed zone | slot route zone ≠ agent zone | blocking | `ZoneChanged` |
| Deviation | D4 | blocking | `Deviated` |
| Stuck | D4 | blocking, then fail | `Stuck` |
| Traversal failed | executor or timeout | blocking; the link is avoided for `nav.agent.avoid_seconds`. A replan that finds no way around, or a second crossing failure, ends the request `TraversalFailed` (D7) | `TraversalFailed` |
| Moving target | target moved > max(`nav.agent.retarget_distance`, `nav.agent.retarget_fraction` × remaining) since planned | **refresh** | `TargetMoved` |

**Invalid routes are never followed.** `NavValidateRoute` reporting anything but
`Valid` means a tile the route crosses was rebuilt or a link it uses changed.

- The rebuilt tile may have lost the floor the route assumed: a bridge
  destroyed, a gap opened.
- `NavValidateRoute` does not say whether the change lies ahead of the agent or
  behind it.

So the route is dropped on the tick the change becomes visible (T+1 after the
gameplay change), and the agent stops. With the default budget, a replan in an
uncontended tick commits in that same tick's pass 2, so the agent loses no tick
of movement. In a storm it waits its turn at the head of the queue.

The one exception is mid-crossing. An agent in `Traversing` finishes its crossing
when the invalidation is about anything other than the link being crossed: a
jump in flight cannot stop. It replans on landing. If the crossed link itself
changed, the executor's `Abort` runs, and it is a traversal failure.

Telling "changed behind me" from "changed ahead of me" would let some
invalidated routes keep moving. It needs validation to report which dependency
changed and where it lies along the route, which is a navigation API change.
That is recorded for later (section 11).

**Ordering and admission.** The queue holds at most one entry per agent, keyed
by `(due tick, EntityId)`:

- A blocking entry is due on the tick it was queued.
- A refresh entry is due `nav.agent.refresh_defer_ticks` (default 8) later.

Blocking work therefore runs before refreshes queued up to eight ticks earlier.
A refresh's due tick is fixed when it is queued, and every later arrival's due
tick is at least its own queue tick, so arrivals can delay a refresh by at most
eight ticks of their own work but never starve it.

An entry remembers the request id it plans for. A further trigger for an agent
already queued updates its one entry:

- **A new request** (a different id) is new work. The old entry is removed, and
  the new request is queued blocking with the current tick as its due tick. It
  does not inherit the superseded request's age: otherwise an agent re-ordered
  every tick would jump the queue on the strength of its first, long-gone
  order.
- **A blocking trigger for the same request** keeps the entry's place and makes
  it blocking. Its due tick becomes the earlier of its own and the current tick,
  so waiting for that request's replan is never reset.
- **A refresh trigger for the same request** changes nothing.
- **A cancel** removes the entry.

Each tick admits entries in key order, up to `nav.agent.plans_per_tick` (D11).

How redundant work is bounded:

- **Per agent.**
  - `TargetMoved` (the only refresh) respects `nav.agent.repath_interval`.
  - Blocking replans never wait on an interval, since the agent is stopped until
    one commits. Each is bounded by its own trigger instead:
    - stuck by its window;
    - deviation by the distance the agent must stray again from a fresh route;
    - traversal failure by the two-failure limit.
  - Invalidation and new requests do not wait.
  - The retarget threshold grows with remaining distance: a far target's
    movement barely changes the route.
  - One queue entry per agent means a burst of triggers costs one plan.
- **Globally.**
  - The per-tick admission cap applies.
  - A storm, such as a door closing on 200 routes, spreads over ticks in
    deterministic key order. Jitter is unnecessary.
- **Scope.** Validation names only what a route depends on, so a change
  elsewhere in a zone triggers nothing.
  - Known over-trigger: dependencies include tiles already walked past, which
    stops the agent for a replan it did not strictly need. In an uncontended
    tick that costs one search and no movement. It is recorded with the "where
    along the route" item (section 11).
- **No silent retry.** The follower never retries a request that failed. A
  failure is reported, and retrying is gameplay's decision: issue the request
  again.

How the cases are distinguished:

| Situation | Reported as |
| --- | --- |
| No route exists | `Failed` / `NoPath` |
| A route became invalid | a repath with `LastRepath` = `TileChanged` / `LinkChanged` / `ZoneReloaded`. It fails only if the replan fails |
| A route was only partly found | does not occur: navigation produces no partial routes (8.4) |
| The search exceeded its budget | `SearchLimitReached`, distinct from `NoPath` |
| The route exceeds the corridor or buffer | `RouteTooLong` |
| The zone or destination is unavailable | `NoZone`, `ZoneUnavailable`, `DestinationOffNavigation`, `DestinationInOtherZone` |
| The route is valid but cannot be followed physically | `Stuck`, `TraversalFailed` (with `LastQuery` showing what the replan found) |

### D6. Dynamic destinations

- **Reading the target.** An `Entity` destination reads the target's
  `WorldTransform` (N-1):
  - at plan time, projected onto the agent's zone;
  - each tick, for the retarget test and for arrival. Arrival uses the target's
    live position, not the planned point.
- **Target ends.** If the target dies or streams out with its zone, the request
  fails with `TargetLost`.
  - The last planned point stays in `Progress.Destination`, so gameplay can
    issue a point request there if it wants "go to last known".
  - Navigation does not pick that policy.
- **Target teleports.** That is a large move, so it falls under the retarget
  rule.
- **Target in another zone.** The request fails with `DestinationInOtherZone`
  (no cross-zone planner).
- **`Follow = true`.** Arrival enters `Holding` (zero intent). The agent leaves
  `Holding` for `Moving` when the target is more than
  `ArriveRadius × nav.agent.follow_hysteresis` away. The request never ends on
  its own.
  - This is in scope because "keep up with that entity" is a representative
    request, and every game would otherwise write its own hysteresis.

### D7. Traversal handoff

- **Existing:**
  - Routes name each crossing (`Traverse` step, kind tag, entry, exit).
  - `JumpExecutionSystem` makes a jump from intent.
  - AbilityKit cannot take a position payload, and its request is not tied to
    the record it starts.
  - Movement leases (`movement.tether` to a point, `movement.burst`) exist, but
    their completion does not mean "arrived at the exit".
- **Recommendation:** a registered operation table keyed by traversal tag, with
  a three-state result.
  - Reaching the entry is not completion. The follower enters `Traversing` when
    the agent is within `EntryRadius` of the entry, then calls the resolved
    executor each tick.
  - The executor reports `Running`, `Succeeded` or `Failed`.
  - The follower enforces `nav.agent.traversal_timeout`, so an executor cannot
    hang a request.
  - On success the cursor moves to the walk leg after the link, which starts at
    the exit.
- **No unhandled crossings.** A character cannot walk off a ledge with no
  handler. The capabilities passed to each plan are computed as follows:
  1. Start from the zone's distinct link kinds.
  2. Keep a kind only if the agent's `GameplayTagContainer` holds that kind or
     an ancestor of it.
  3. Keep a kind only if an executor resolves for it.

  The search then never returns an unhandled crossing. If an executor is
  unregistered mid-route, the request fails with `TraversalUnsupported`.

  The result is a subset of the zone's distinct kinds, which `ZoneNavigation`
  already caps at 64 (one capability bit each in the query plan). Stage 0 names
  that cap `kNavMaxTraversalKinds`, used by the zone, the query plan and
  `NavPlanInput`. The input's array therefore holds every usable kind by
  construction: nothing is truncated, and no route is lost to an artificial
  `NoPath`.
- **Failure after a failed crossing.** `Failure` records why the request ended,
  and `LastQuery` records the last search's status. They are kept apart:
  - An executor reports `Failed` (or times out). The follower adds the link to
    the slot's avoid-list and queues a blocking replan.
  - If that replan finds another route, the agent continues.
  - If it returns `NoPath`, the request ends with
    `Failure = TraversalFailed` and `LastQuery = NoPath`. The cause is the
    crossing; the search only shows there was no way around it. Reporting
    `NoPath` would tell gameplay the destination is unreachable, when it was
    reachable until the crossing failed.
  - Any other search failure keeps its own failure value (`SearchLimitReached`,
    `RouteTooLong`, ...). Those describe the search, not the crossing.
  - A second crossing failure within one request ends it with
    `TraversalFailed`, whatever `LastQuery` says.

  Capabilities come from data (tags granted by prefabs or effects, so an injured
  NPC can lose `navigation.traversal.jump`), with no new schema.
- **Engine executors.** The engine ships two executors and registers neither. A
  game binds its own kinds:
  - `NavTraverseByWalking` steers to the exit and succeeds on stable support
    within the step's radius of it.
  - `NavTraverseByJumping` does the same, holding `Jump` from the entry until
    support is lost.

  A teleport, ladder or mantle is a game executor. It may run an ability, a
  lease or a relocation, and the follower never knows which.
- **Rejected:**
  - A `switch` over kinds in the follower.
  - AbilityKit as the v1 dispatch (two contract gaps, section 12).
  - Authored verb dispatch: verbs report admission, not arrival.

  The likely data-bound successor is a kind → lease-verb binding, whose
  `Completes` and lease-held probe already give "running / done". That needs a
  success/failure answer the lease contract does not have yet. It is deferred
  (section 11).

### D8. Status, cancellation, failure

- **Identity.** `NavRequestId` is minted per agent by `RequestNavigation`.
  `NavAgentProgress::Request` names the request its fields describe.
- **Standing.** A caller holding an id calls `ReadNavigation`:
  - `Superseded` means another request replaced it;
  - otherwise it gets that request's phase and failure.

  An old request's completion therefore can never be read as the new one's.
- **Cancellation.** Cancel is a request of kind `None`. The canceller sees
  `Cancelled` under its own id.
- **Authored callers.** They get admission only from verbs (`VerbAdmission`),
  not the id. They read `phase` and `failure` through queries, which describe
  the latest request. That is correct for the single decision-maker an agent
  normally has.
- **Rejected for v1:** authored events (`navigation.arrived` and so on).
  - They are not journaled or replicated, and they drain after FixedLogic.
  - Every consumer in the roadmap (utility tables, scripts) evaluates state each
    tick anyway.
  - Push delivery is deferred to its first consumer (section 11).

### D9. Zones and lifetime

- **The agent's zone** is its storage partition's zone, through
  `RuntimeWorld::ZoneOf`. This is extracted from `CandidateRun::ZoneOf` so
  there is one resolution (section 12).
- **Persistent partition.** An agent in the persistent partition fails with
  `NoZone`. No consumer keeps NPCs persistent, and choosing a zone by position
  is the cross-zone planner's problem.
- **Dormant zone.** Its agents are not processed. Their state freezes, and the
  route stays valid if nothing changed. On waking they validate and the stuck
  window resets.
- **Zone unload.** Agents are destroyed with it, and the reclaim sweep frees
  their slots. A request whose target was in that zone fails with `TargetLost`.
- **Agent moved between zones.** A `ZoneChanged` replan follows.
- **Shutdown.** Slots die with the system, which owns plain buffers and no
  external handles.

### D10. Place in the fixed tick

The recommended order:

```
… InputActionResolve → (game decision systems) → NavAgentSystem → [game steering]
   → AbilitySystem → ModeRequestCollection → … → FreeLocomotion → Jump → RootMotion
   → HeldMotion → MotionComposition → DilatedCharacterStep
Physics → transforms → PostFixed: NavigationSystem (geometry, tiles, link state)
```

- **Why here.** It is a producer like input steering, and it must precede
  everything that reads intent.
- **Rejected:**
  - PostFixed would steer with a tick of lag and could not hand jump to the same
    tick.
  - PreSimulate runs per frame, not per tick, and breaks determinism.

### D11. How route planning runs and how much runs per tick

**Existing.**

- **Concurrent queries are supported.** Navigation already supports them:
  distinct `NavQueryContext`s on one immutable zone.
- **A fork-join exists.** `JobSystem` is the engine's intra-frame fork-join, and
  `NavTileRebuilder` is the precedent for using it. It gathers serially, runs a
  `ParallelFor` with one output slot per item, then commits serially in
  canonical order.
- **Navigation is immutable in this hook.** Section 1 shows nothing changes a
  `ZoneNavigation` during FixedLogic.

**Shape: one fork-join batch per tick, from v1.**

1. **Admit and gather** (owner thread).
   - Pop up to `nav.agent.plans_per_tick` entries in key order.
   - For each, fill a `NavPlanInput` from the agent's components, its slot and
     the bound-policy table:
     - positions (N-1);
     - zone pointer;
     - profile tag;
     - policy pointer;
     - capabilities (D7);
     - the avoid-list.
2. **Plan.**
   - If the batch has fewer than `nav.agent.parallel_min_batch` (default 2)
     plans, or there are no workers, run `PlanRoute` inline in index order.
   - Otherwise run one `ParallelFor` over the batch, with
     `Contexts[CurrentWorkerIndex()]`.
   - `PlanRoute` projects both ends and calls `NavFindRoute` into the plan's
     staging buffer, with diagnostics.
3. **Commit** (owner thread, batch index order).
   - On success, swap the staging buffer into the agent's slot, reset the
     cursor, and update `Progress`.
   - On failure, write the failure (D5 table).

   Nothing else can have touched these agents during the join.

**This is not a second scheduler.**

- The queue is the system's own sorted vector of entity ids.
- The concurrency is one `ParallelFor`.
- The serial reference is the same function with no workers.

**Measured** (`docs/plans/evidence/navigation-route-cost/`, profile preset,
16-thread machine, 256 requests on 96 m scenes):

- **Scaling.** Parallel planning scales with workers.
  - A 256-plan maze batch: 19.6 ms serial; 6.2 / 4.0 / 3.0 ms with 3 / 7 / 14
    workers.
  - A 16-plan batch: 1.2 ms serial; 0.50 / 0.36 / 0.29 ms.
- **Dispatch is negligible.** An empty batch costs 0.1–4 µs against 25–170 µs
  per plan. A batch of two already gains: 199 → 133–157 µs in the maze. That
  sets the inline threshold at 2.
- **Identical output.** Every worker count produced bit-identical statuses, node
  counts and corners.

**The per-tick budget.**

| Admission | Storm drain (ticks / seconds at 60 Hz) | Heaviest storm, tick median / max µs, serial | 3 workers | 7 workers | 14 workers |
| --- | --- | --- | --- | --- | --- |
| 4 per tick (revision 1) | 64 / 1.07 s | 513 / 740 | 241 / 463 | 233 / 410 | 222 / 342 |
| **16 per tick** | **16 / 0.27 s** | 1693 / 2049 | 648 / 948 | 450 / 606 | 359 / 713 |
| 32 per tick | 8 / 0.13 s | 3349 / 3999 | 1082 / 1268 | 774 / 909 | 562 / 880 |
| 64 per tick | 4 / 0.07 s | 6444 / 7663 | 2416 / 2788 | 1509 / 1739 | 1127 / 1217 |
| node budget 8k, waves of 8 | 25 / 0.42 s | 1041 / 2037 | 401 / 890 | 337 / 709 | 289 / 593 |

The heaviest storm is "maze converging": 256 agents replanning to one point,
the repath-storm shape. The other workloads are cheaper; the evidence README
has all three.

- **Recommendation: `nav.agent.plans_per_tick` = 16.**
  - With 3 or more workers, the heaviest measured storm stays under 1 ms of
    tick wall time: median 0.36–0.65 ms. It drains 256 simultaneous requests in
    16 ticks (0.27 s).
  - Ordinary demand (a few requests a tick) commits in the tick it arrives,
    because pass 2 runs after pass 1 queues it.
  - Blocking work is served first, and nothing starves (D5).
- **Why the budget must not scale with workers.** A worker-dependent admission
  rule would plan different requests on different ticks on different machines.
  That is a serial/parallel divergence, which the constitution treats as a
  defect.
  - The cost is that a zero-worker host pays the serial column: 1.7–2.0 ms in
    the heaviest storm.
  - `DefaultWorkerCount()` is zero only on machines with two or fewer hardware
    threads.
  - A game or server profile that wants a different trade sets the cvar for
    that configuration. Its serial and parallel runs then still agree with
    each other.
- **CPU bound.** Each search stops at the context's `nav.agent.max_nodes`
  (4096). At the measured ~0.1 µs per node, that is about 0.45 ms per plan
  including projections, so one tick's planning is at most about 16 × 0.45 ≈
  7 ms of CPU. That is an estimate from the per-node rate. The worst measured
  tick was 2.0 ms serial.
- **Rejected: node-budget waves.** Admitting waves until the tick's visited
  nodes reach a budget is deterministic, since node counts do not depend on
  workers. But:
  - Visited nodes are known only after a wave, so the 8k budget overshot to
    16–18k.
  - Each wave adds a join, so it ran slower per node than one batch (0.42 vs
    0.31 ms for ~16k nodes at 14 workers).
  - It drained the maze storm no faster than "16 per tick".

  It helps only in cheap zones, where a count budget is already cheap.
- **Rejected: true background search.**
  - `AsyncTaskQueue` commits at the frame's `DrainAsyncTasks`, not per tick, so
    results would land on wall-clock boundaries.
  - A search would race `NavTileRebuilder` mutating Detour meshes in PostFixed.
- **Rejected: a multi-tick sliced search.** It needs search state persisted
  across ticks, and no measured query needs it. The worst observed is 0.8 ms, at
  a 192 m maze with a 16k node budget.

### D12. Authored data versus runtime state

| Authored (saved, inspectable) | Runtime only |
| --- | --- |
| `NavAgent` (profile name, policy asset) on prefabs | `NavAgentRequest`, `NavAgentProgress` |
| `navigation.policy` assets (unchanged) | route slots, per-worker query contexts, staging buffers, planning queue |
| traversal capabilities as `navigation.traversal.*` tags (prefab or effect-granted) | bound `NavQueryPolicy` cache entries |
| `nav.agent.*` cvars (tunables) | traversal executor table (registered by code) |

- **Request state is not saved.** There is no save system yet. When it lands,
  `NavAgentRequest` is the natural record to persist: a point or a
  `PersistentEntityId` target, never a route.
- **Bound policy: no shared cache.** Navigation does not need a shared binding
  abstraction.
  - **The mechanisms already exist.** Compiling an asset and knowing when it
    reloaded are `DataAssetCache`'s:
    - `TryGet<NavigationPolicyData>(handle, kNavigationPolicySubtype)`;
    - `GetReloadVersion(handle)`.

    Resolving names is `BindNavigationPolicy`'s.
  - **The system's own table is small.** It maps the handle to a bound
    `NavQueryPolicy`, rebinds when the reload version or the tag registry's
    size changes, and reports an unbound name once per change. It uses those
    calls and is about thirty lines.
  - **Gather points into it.** Entries are stable (one heap object per handle),
    so a `NavPlanInput` can point at its policy across the join. A rebind
    happens only in pass 1.
  - **The extraction is not justified.** `MovementProfileBindingCache` also
    tracks locomotion-mode generation and delivers errors to the tuning system,
    and `InputBindingCache` joins two assets. Their common core is the same two
    `DataAssetCache` calls this table makes, so extracting it would add an
    abstraction without removing a mechanism.

  Revision 1's Stage 0 extraction is withdrawn.

## 7. Path-following detail worth stating

- **Vertical separation.** Arrival and corner tests compare planar distance and
  require the vertical gap within the profile's height. Stairs and ramps work,
  and a floor above or below does not satisfy them.
- **Off the navmesh at plan time.** The agent is projected with extents
  (profile radius × 2, profile height). A failure is held for
  `nav.agent.offmesh_grace` (an airborne or knocked-back agent) before
  `StartOffNavigation`.
- **Width check.** A capsule wider than the profile's radius
  (`CharacterController.Radius` against `zone.Mesh(p).Profile().Radius`) is
  counted as a diagnostic when planning, not a failure. It explains "stuck in
  every doorway".
- **Not added:**
  - local avoidance, crowds, RVO, formations;
  - path smoothing beyond Detour's string pull;
  - an extra nav-raycast shortcut;
  - facing.

  Facing belongs to `LookOrientation` and `AimFacing`, or to a future
  "face travel direction" mechanism shared with third-person players
  (section 11).

## 8. Repath and failure policy summary

1. Every cause of a replan is in the D5 table, and each has a reason code.
2. Every terminal failure is a `NavAgentFailure` value, and each value is
   produced by exactly one condition.
3. The follower never retries a failed request, and gameplay owns retry policy.
4. **Partial routes: pushed back.** "Get as close as possible to an unreachable
   point" needs two things:
   - `NavFindRoute` to return a best partial route, a navigation contract
     change;
   - a policy for what "close" means.

   Gameplay can already pick a reachable stand-in with `CollectReachable` or
   candidate evaluation and request that. Not in v1.

## 9. Memory and performance

**Measured: one query at a time.** Method, probe sources and raw results are in
`docs/plans/evidence/navigation-route-cost/`:
- `profile` preset, pinned to one core;
- the templates' build profile;
- 512 seeded pairs per scene;
- one warm `NavQueryContext`.

| Scene | Nodes (cap) | FindRoute median / p90 / max µs | max nodes used | max corners / tile deps | Validate median / max µs |
| --- | --- | --- | --- | --- | --- |
| 48 m, pillars every 4 m | 2048 | 6.4 / 16.8 / 28.9 | 259 | 11 / 17 | 0.03 / 0.44 |
| 96 m, pillars | 2048 | 19.0 / 58.6 / 105 | 898 | 21 / 35 | 0.07 / 0.38 |
| 96 m, pillars, ends ≥ 60 m apart | 2048 | 45.0 / 76.6 / 119 | 914 | 21 / 36 | 0.10 / 0.63 |
| 96 m maze (staggered walls) | 2048 | 82.6 / 155 / 261 | 1841 | 54 / 96 | 0.16 / 0.70 |
| 192 m maze | 2048 | 182 / 203 / 313 | — | 369 of 512 `SearchLimitReached` | — |
| 192 m maze | 16384 | 354 / 612 / 799 | 7554 | 239 of 512 overflowed the 512-polygon corridor | 0.25 / 0.80 |

`ProjectPoint` takes 0.5–0.6 µs at the median and `NavRaycast` 0.2–0.5 µs.

Consequences:

- **Node budget.** The agent context uses `nav.agent.max_nodes` (default 4096,
  read at registration). 2048, the context default, is under the 96 m maze's
  worst case with little margin.
  - Zones near 200 m of maze-like layout need more nodes and more corridor.
  - They report `SearchLimitReached` or `RouteTooLong` explicitly rather than
    failing quietly.
- **Validation every tick.** For 256 agents in the maze it is at most ~180 µs
  (~41 µs at the median). Checking every tick costs less than adding a
  zone-change counter to `ZoneNavigation`. That is decided by measurement.

**Measured: planning batches and storms.** Each batch also includes the two
endpoint projections. The full tables are in the evidence README; D11 has the
admission comparison. At the recommended 16 plans per tick:

| Workload (256 requests at once) | Ticks to drain | Tick median / max µs: serial | 3 workers | 7 workers | 14 workers |
| --- | --- | --- | --- | --- | --- |
| 96 m pillars, random | 16 | 422 / 579 | 161 / 281 | 133 / 207 | 122 / 205 |
| 96 m maze, random | 16 | 1247 / 1595 | 446 / 645 | 342 / 460 | 309 / 415 |
| 96 m maze, all to one point | 16 | 1693 / 2049 | 648 / 948 | 450 / 606 | 359 / 713 |

Serial and every parallel configuration produced identical routes.

**Measured in Stage 6: the follower at scale.** The run used the 96 m maze,
storms of simultaneous orders, and the profile preset. The full table is in the
evidence README.

| Agents | Planning tick median: serial / 14 workers | Follow-only tick median: serial / 14 workers | Ticks to plan a storm |
| --- | --- | --- | --- |
| 32 | 1.36 / 0.44 ms | 12 / 11 µs | 2 |
| 128 | 1.30 / 0.37 ms | 49 / 50 µs | 8 |
| 256 | 1.29 / 0.42 ms | 97 / 102 µs | 16 |

- **Following** costs about 0.4 µs per agent per tick. That is inside the
  revision-1 estimate of 0.1–0.25 ms for 256 agents, so steering stays serial.
- **The first planning tick** with workers reaches about 1.0–1.3 ms, because it
  makes the per-worker contexts.
- Idle agents cost one branch each.

Stage 6 measures 1, 32, 128 and 256 agents in the harness:
- planning and following separately;
- simultaneous requests and a geometry-change storm;
- serial and default workers;
- with `frame.trace.output`;
- recording scenario, baseline, result and method.

Steering goes parallel only if the follow pass measures over ~1 ms.

**Memory.**
- Idle agent: `NavAgent` (~80 B, mostly the inline profile name) plus request
  (~40 B) plus progress (~40 B), all in chunks.
- Active agent: plus one ~7 KB route slot.
- System:
  - `WorkerCount() + 1` contexts at a measured ~0.26 MB each when warmed (4096
    nodes): about 4.3 MB with 14 workers;
  - 16 staging buffers (~7 KB each);
  - the queue.
- At steady state, nothing allocates after the slot pool reaches peak
  concurrency.

**A simple game pays nothing.**
- `RegisterNavigationAgents` is opt-in.
- A registered system with no `NavAgent` entities finds an empty cached query
  and returns.

## 10. Authoring and diagnostics

- **Drawing.** `navigation.debug.draw` (cvar, off) fills a
  `DebugLineChannels` channel `navigation.agents`:
  - each active agent's remaining route, depth-tested;
  - its current corner and destination markers;
  - traversal links in a distinct colour.

  The selected agent is drawn on top, with the line to its current corner and
  its wish direction.
- **Panel.** `NavAgentPanel` is an `IDebugPanel` beside `NavigationPanel`.
  - It lists agents with phase and failure.
  - Selecting one (`NavAgentSystem::SelectAgent`, the `PerceptionPanel` shape)
    shows:
    - request id, kind, target, follow, arrive radius, speed scale;
    - phase, failure, remaining distance;
    - current step, corner index and count;
    - active traversal (link, kind, ticks);
    - last query status, nodes visited and cost (`NavQueryDiagnostics`, kept in
      the slot);
    - last repath reason and count;
    - the stuck window;
    - the current `MovementIntent`.
- **Counters.** `NavAgentSystem::LastStats()`: active, planned, queued,
  repaths by reason, failures by kind, traversals.
- **Console**, for manual testing:
  - `nav.agent.list`
  - `nav.agent.goto <entity> <x> <y> <z>`
  - `nav.agent.goto <entity> aim`: the local pawn's aim ray hit, through
    `AimOf` and `PhysicsQueries::Raycast`
  - `nav.agent.follow <entity> <entity|me>`
  - `nav.agent.stop <entity>`

  Entities parse through a shared `ParseEntityText` beside `AppendEntityText`,
  extracted from animation's local parser (section 12).
- **What this proves.** That gives "spawn an NPC with `NavAgent`, aim, `goto`,
  watch it". Every failure state above is visible in the panel and counters. No
  editor work is in this ticket.

## 11. Deferred work (entries added to `docs/deferred.md` when the stage lands)

| What | Trigger |
| --- | --- |
| Ability- or lease-backed traversal executor; needs an activation payload or query of the step, request→record correlation, and a success/failure answer from a lease | the first traversal that needs animation or an ability (ladder, mantle, door) |
| Data binding of traversal kinds to executors (no module code) | first content that binds a kind without a game module, likely the roadmap's data-authored enemy |
| `navigation.arrived` / `navigation.failed` authored events | the first push consumer (script runtime or LevelFlow) |
| Facing the direction of travel | first NPC content whose body must face its motion, or a third-person template |
| Validation that says which dependency changed and where it lies along the route, so a change behind the agent neither stops it nor costs a replan | a measured stop or repath rate from changes behind agents in real content |
| Parallel follow pass (steering) | the follow pass measured over ~1 ms in a real encounter (estimated 0.1–0.25 ms for 256 agents) |
| Cross-zone requests | the cross-zone planner (roadmap Track A item 6) |
| Navigation for a client-predicted pawn (click-to-move) | a game that navigates the local pawn; the authority re-derives intent from peer actions, so the request itself would have to travel |
| Partial routes | a consumer that cannot use a reachable stand-in |

## 12. Contracts that change, and contracts that do not

**Changed** (each in the stage that needs it):

1. **`MotionRequest` gains `bool PlanarOverridden`.**
   - `ComposeMotion` sets it, so the bulk and dilated paths agree.
   - `StepScaledCharacterTick` writes the last step's request back to the
     component, so dilated agents carry the fact too.
   - It is a public component's layout, so the module ABI version is bumped,
     with `sizeof`/`offsetof` coverage.
   - Without it, an ability that roots an NPC would be read as the NPC being
     stuck.
   - The alternative is re-deriving "was locomotion overridden" in the follower
     from `LocomotionOutput` and `MotionRequest`. That duplicates composition's
     rule, so it is rejected.
2. **`RuntimeWorld::ZoneOf(EntityId) -> std::optional<ZoneId>`**, moved from
   `CandidateRun::ZoneOf`. Candidate evaluation calls it.
3. **`ParseEntityText`** in `ecs/EntityText.h`, moved from
   `ParseAnimatedEntity`. `anim.trace` calls it.
4. **`kNavMaxTraversalKinds` (64)** is named in `NavigationTypes.h`. It
   replaces the literal 64 in `ZoneNavigation`'s kind table and `NavQueryPlan`'s
   arrays. Same value and same layouts; it is a new public constant in a
   fingerprinted header.
5. **Engine vocabulary** gains the `navigation.*` verbs and the
   `NavAgentProgress` queries.
6. **Documentation:**
   - `docs/gameplay/movement.md` gains the intent-producer rule (D3).
   - `docs/navigation/runtime.md` gains an "Agents" section.
   - The `navigation-core.md` §5.3 partition sentence is corrected.

**Untouched:**
- `NavigationQuery` and its operations;
- `NavRouteBuffer`, `NavQueryContext`, `ZoneNavigation`, `NavigationSystem`,
  `NavLinkState`;
- the `.snav` format and the cook;
- `navigation.policy`'s schema;
- `MovementIntent`, the locomotion systems and the motor;
- `JobSystem`, `DataAssetCache`, and the movement and input binding caches;
- AbilityKit;
- replication and prediction;
- the template steering systems.

## 13. Stages

Each stage builds, passes `ctest --preset dev`, and is reviewable alone.

0. **Prerequisite extractions** (behaviour-preserving):
   - `RuntimeWorld::ZoneOf`, `ParseEntityText`, `kNavMaxTraversalKinds`;
   - `MotionRequest::PlanarOverridden` with the dilated write-back, plus the ABI
     bump.
   - Tests:
     - existing candidate, anim and movement suites unchanged;
     - a composition test that a burst sets the flag and plain locomotion does
       not, on both paths.
1. **Vocabulary and pure kernels:**
   - `NavAgent`, `NavAgentRequest`, `NavAgentProgress` with registration and the
     freeze table;
   - `RequestNavigation`, `CancelNavigation`, `ReadNavigation`;
   - the follow kernels: corner advance, arrival scale, deviation, stuck window,
     remaining distance.
   - Table-driven headless tests for each kernel, including:
     - overshoot at high speed;
     - corners on stairs, and floors above;
     - a position beyond a corner's plane but outside the route corridor (a
       knockback or teleport), which must not advance and must report
       deviation.
2. **`NavAgentSystem`, walking only:**
   - route slots with owner checks and the reclaim sweep;
   - the planning queue (due tick, blocking and refresh classes);
   - the fork-join planning batch: gather, `PlanRoute` inline or under
     `ParallelFor`, per-worker contexts, staging buffers, ordered commit;
   - the policy binding table;
   - follow and intent writing;
   - terminal handling and `TakenOverByInput`;
   - the `NavAgentHarness` (below).
   - Tests:
     - A, D, F, H (walking);
     - the planning-batch equivalence test (`JobSystem(0)` against
       `JobSystem(4)` on one gathered batch);
     - a relocation test: gather inputs, copy them into a vector that is then
       grown and reallocated, overwrite the originals with garbage, then plan
       from the copies. A jump route that needs the capability must still be
       found, and a forbidden link must still be avoided. A request that kept
       spans into the original storage would read the garbage;
     - queue fairness:
       - an agent re-ordered with a new request is queued behind entries
         already due;
       - an agent whose same request is re-triggered keeps its place;
     - a `tsan` preset run of H with workers.
3. **Repath policy:** validation reactions, the invalid-route stop, moving
   targets and `Follow`, deviation, stuck. Tests: B, C, I, J, plus stuck and
   repath interval.
4. **Traversal:**
   - the `NavTraversalExecutors` resource;
   - capability filtering;
   - `NavTraverseByWalking` / `NavTraverseByJumping`;
   - avoid-list and timeout.
   - Test E, including the unsupported and failing cases.
5. **Surfaces:** verbs and queries, console commands, debug channel,
   `NavAgentPanel`, counters. Tests: verb admission and query values; console
   parsing through `ParseEntityText`.
6. **Determinism, scale, measurement, docs:**
   - Test G.
   - Scale and storm runs at 1, 32, 128 and 256 agents, serial and at default
     workers, with recorded numbers.
   - `plans_per_tick` and the other defaults are re-tuned from them.
   - Documentation and `deferred.md` entries.

**`NavAgentHarness`** (`test/navigation/`):
- A `RuntimeWorld` with a zone attached through the real residency path.
- Navigation cooked from a `NavTestGeometry`.
- Static physics bodies built from the same boxes, so collision and the navmesh
  agree.
- An `EngineSchedule` with:
  - `RegisterAbilityKitSystems`;
  - `RegisterMovementSystems`;
  - `RegisterPhysics` (character controller);
  - `NavigationSystem`;
  - `RegisterNavigationAgents`.
- Ticked through the real FixedLogic → Physics → propagation → PostFixed order.
- `SpawnAgent(position, capabilities)` builds:
  - `CharacterController`, `CharacterMovement`, `AttributeSet` (MoveSpeed);
  - `NavAgent`;
  - capability tags.

No existing test runs `CharacterControllerSystem` against a navigated zone, so
this harness is new. It is assembled only from existing pieces.

## 14. Acceptance tests

| | Scenario | Pass condition |
| --- | --- | --- |
| A | `WallRoom`: an agent goes from one side of the wall to the other | `Arrived` within N ticks. The achieved path passes the gap (z > 7), never crosses the wall, and ends within the arrive radius |
| B | The agent approaches a target entity that walks along a line | Arrives. Repaths ≤ distance moved ÷ `retarget_distance` + 1. No two repaths closer than `repath_interval`. Intent direction never reverses between consecutive ticks except at corners |
| C | A `NavigationGeometry` box drops into the corridor the route uses | The next tick, `LastRepath == TileChanged`, and the agent arrives by the other way. With every way blocked: `Failed/NoPath`, and zero intent from that tick |
| D | A request is replaced mid-route, and in a second run cancelled | `ReadNavigation(old) == Superseded` from that tick on. The new id reaches `Arrived`. No tick reports `Arrived` under the old id. Cancel gives `Cancelled` under the cancel's id and zero intent the same tick |
| E | `TwoIslands` plus a jump link, with `navigation.traversal.jump` registered to `NavTraverseByJumping` | (1) Phases go `Moving → Traversing → Moving → Arrived`. `Traversing` begins only within `EntryRadius` of the entry, and walking resumes only after the executor succeeds. (2) Without the tag on the agent, or without a registered executor: `Failed/NoPath`, and the agent never leaves the first island. (3) An executor that always fails, with this link the only way across: `Failure = TraversalFailed`, `LastQuery = NoPath`, and the failed link in the avoid-list. (4) The same failing executor with a second, walkable way round: the agent replans avoiding the link and arrives |
| F | Lifecycle | (1) Destroying the agent frees its slot on the next tick. (2) Removing `NavAgent` from a live agent zeroes its intent. (3) Detaching the zone frees every slot. (4) A dormant zone freezes progress and resumes valid. (5) A target in a detached zone gives `TargetLost`. (6) Granting `movement.controlled` mid-route gives `TakenOverByInput` |
| G | 64 agents, all requesting on tick 0 (the queue saturates: 64 > 16), then a geometry change mid-run that invalidates about half the routes (a second saturated burst) | See "What G claims" below |
| H | 256 agents on 96 m pillars, all requesting on tick 0 | All reach terminal phases. Every request plans within `ceil(256 / plans_per_tick)` ticks (16 at the default). No slot is shared (owner checks hold every tick). No tick plans more than `plans_per_tick`. Zero allocations after warm-up (the allocation-test hook) |
| I | An agent mid-route; a box is added on a tile ahead of it. The queue is held saturated by other agents' blocking work for several ticks | From the tick the change is visible: `LastRepath == TileChanged`, zero intent and no corner advance on every tick until its replan commits. The agent never moves along the invalidated route |
| J | An agent follows a moving target while the queue is saturated with blocking work | It keeps moving along its still-valid route while its refresh waits. Blocking entries queued after its refresh came due do not overtake it, so its refresh commits within `refresh_defer_ticks + ceil(blocking ahead / plans_per_tick)` ticks |
| K | An agent walking an L-shaped route is teleported across the corner's plane, 4 m from both segments | That tick: no corner advance, `LastRepath == Deviated`, zero intent until the replan commits. It then arrives by a route from where it landed |

**What G claims.** The constitution asks for evidence from both paths, and G
claims only what `EntityId`-keyed ordering guarantees.

1. **Independent of worker count.** The scenario runs with `JobSystem(0)`,
   `JobSystem(1)`, `JobSystem(4)` and the default count. The per-tick record
   is bit-identical across all runs:
   - every agent's position;
   - phase and failure;
   - repath reason;
   - the tick each plan committed.

   Saturation is the point: admission is the order-sensitive decision. The test
   also runs under the `tsan` preset.
2. **Repeatable.** Two runs of one configuration are identical.
3. **Independent of chunk layout.** The same creation sequence (so the same
   `EntityId`s) is repeated with an unrelated extra component on every third
   agent, which puts those agents in another archetype and changes chunk
   iteration. The outcome is identical. This is what sorting queue changes by
   `EntityId` buys.
4. **Not independent of creation order when saturated, and not claimed.**
   - Shuffling creation changes which `EntityId` each agent gets. Under
     saturation that changes which agents plan first, and so the outcomes.
   - That is the intended rule (lower id first), not a defect, and G does not
     assert it either way.
   - A separate unsaturated case is asserted: with requests per tick at or below
     the budget, each agent's outcome, matched by a spawn label component, is
     identical under shuffled creation. In that case no agent's admission
     depends on another's id.

Plus the kernel tables (Stage 1), the stuck case (an agent walled in by a
contributor: replan, then `Stuck` with no unbounded repaths), and a dilated
agent (slowed 0.5) that is not reported stuck.

## 15. Risks

- **Character collision.** Characters do not collide with each other, so H
  proves independence, not crowd behaviour. Agents will overlap until character
  collision lands.
- **Free-mode only.** Arrival braking reads free-locomotion friction. A
  game-defined mode with different stopping behaviour still arrives within the
  radius plus its own overshoot. Arrival tolerance is the guard, and the kernel
  is one function to revisit.
- **Shared tag container.** `GameplayTagContainer` holds 32 tags. Capability
  tags share that budget.
- **Zero-worker hosts pay serial cost.** A host with no workers pays the serial
  planning cost: up to ~2 ms in the heaviest measured storm at 16 plans per tick.
  Such hosts set a lower `plans_per_tick`, trading storm latency for that
  configuration only.
- **A future in-hook mutation.** Something added later that mutates a
  `ZoneNavigation` inside FixedLogic would race the batch. The debug guard
  catches a zone detaching, and the `tsan` run of G and H catches the rest.
  `SetLinkState`'s comment already says "owner thread, between queries";
  Stage 2 adds "never during FixedLogic".
- **Content hazard.** An NPC prefab copied from a pawn prefab keeps
  `movement.controlled`. Every request then fails `TakenOverByInput`, which is
  loud but would surprise the first time; the panel shows it.
