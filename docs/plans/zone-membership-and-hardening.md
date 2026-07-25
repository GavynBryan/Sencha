# Zone Membership and Runtime Hardening

Status: proposed, 2026-07-25. Not started.

Scope: give streamed entities a zone-membership mechanism so games do not hand-roll
migration, and close the runtime defects and leftovers found in the ECS/zone
architectural pass of the same date.

This plan depends on the zone ontology defined in
`docs/plans/world-partition/11-zone-runtime-model.md` and `12-spatial-compilation.md`.
Those two files exist only on `agent/world-graphs-and-docks`, not on this branch —
they arrive with Track B. Until then, read them with
`git show agent/world-graphs-and-docks:docs/plans/world-partition/11-zone-runtime-model.md`.

Nothing in Track C may be built before Track B lands: building membership on AABB
containment would violate ontology invariant 3 and would be rewritten.

## 1. Invariants this plan must not break

From the zone runtime contract:

1. Zone AABBs may overlap, need not touch, do not tile the world, and never imply
   topology.
2. **Ordinary movement changes an entity's zone only through an explicit Dock.**
   AABBs may be queried for initial placement, teleport, save restore, recovery,
   radius demand, diagnostics, and framing — never to decide that a moving entity
   has changed owner.
3. Gates control physical traversal. They do not remove topology or suppress
   residency demand.
4. The deleted spatial-ownership chain stays deleted: no polygonal or cell-based
   zone boundary, containment mesh, height-band copy, split-cell workflow, or
   cooked ownership artifact.

From the repository engineering constraints:

5. Participation leases are game-held. A component must not silently retain a zone
   as resident (`WorldPartitionRuntime.h`). Membership must not auto-lease.
6. Structural mutation happens through `CommandBuffer` or outside active
   iteration, never inside it.
7. Behavior enters through component values and authored data, not through a
   central branch that grows per entity kind.

## 2. Sequencing and the branch problem

`feature/unified-runtime-container` and `agent/world-graphs-and-docks` diverged at
`0cad5177`: 509 commits on the former, 13 on the latter. Both change
`engine/src/zone/`. Measured surface:

| | count |
|---|---|
| Files both branches changed | 26 (19 under `engine/` or `editor/`) |
| Files only the dock branch changed | 51 under `engine/`/`editor/` |
| Conflict hunks in a trial merge | 65 |

The contested engine files are only five: `WorldPartitionRuntime.h/.cpp`,
`CharacterMoverPool.h/.cpp`, and `world/ComponentManifest.h`. The remainder of the
contest is editor authoring surface (14 files).

The ontology core is model-agnostic. `ZoneDemand.cpp`, `DockCrossing.cpp`,
`WorldPartitionManifest.cpp`, `WorldPartitionValidation.cpp`, `WorldTopologyCook.cpp`,
and `WorldPartitionIndex.cpp` contain zero references to `Registry`, `ZoneRuntime`,
or `World&` — they are plain data and pure functions. They transplant at file
granularity. `WorldPartitionRuntime` is the single component needing real
reconciliation: the dock branch adds graph-driven demand and focus crossing to it,
while the unified branch retargets it from `ZoneRuntime` to `RuntimeWorld`.

Track A is independent of all of this and can land first.

## 3. Track A — runtime hardening

Independent of the zone ontology. Each item is one commit.

### A1. A failing zone load must stop retrying, and must say so

Reproduced 2026-07-25 with a temporary probe against
`WorldPartitionRuntimeTests`: a recipe whose `Finalize` returns false produced
**10 builds and 10 full hidden imports in 10 frames**. `AsyncZoneLoader::ImportAndFinalize`
returns on failure without recording anything, so `WorldPartitionRuntime::Update`
sees the zone demanded, not resident, and not loading, and re-issues it. Every
frame, forever, with no log line. Each retry allocates a worker task and creates
then destroys every entity of the zone in a hidden partition.

Compounding consequence: a game gating fast travel on `IsZoneResident` polls
forever while the loader churns, because no failure is observable.

Owner: the retry decision lives in `WorldPartitionRuntime`; the swallowed error
lives in `AsyncZoneLoader`.

Shape:

- `AsyncZoneLoader` records a per-zone failure (zone, stage, message) and exposes
  it. Stage distinguishes package build, import, finalize refusal, and publish.
- `WorldPartitionRuntime::Update` does not re-issue a zone with a recorded
  failure. The record clears when the zone's `CookedContentHash` changes or on an
  explicit reset, so a recook recovers without a restart.
- Failures surface as runtime records in the `ContentRiskRecord` family, whose
  header already reserves this: "streaming records will extend the family;
  coordinate rather than duplicate when they land."
- One log line at error severity on first failure, not per retry.

Tests:

- The reverted probe becomes a kept regression test: a finalize-refusing zone is
  built exactly once across many frames.
- Failure at each stage (build throw, import error, finalize false, publish
  false) records the correct stage and stops reissue.
- A recorded failure is cleared by a content-hash change and the zone loads.
- A zone that fails and is then undemanded does not leak its record.
- A failing zone does not block an unrelated zone's load in the same frame.
- The demand records still report the zone, so the inspector can show why it is
  absent rather than silently omitting it.

### A2. Retained render caches must release state for departed entities

`ZoneResidencyContext` exists so retained backend owners can react to detach.
`PhysicsStepSystem` is its only subscriber engine-wide. `ShadowResidency` holds
8 spot and 4 point slots keyed by `RenderEntityKey`; a slot whose owner entity was
destroyed by a zone detach stays `Live` with `EffectiveScore` 0, still holding its
atlas allocation. Reclamation happens only above budget, or through a steal that
requires `kStealOutscoredFrames` = 30 consecutive outscoring frames
(`ShadowResidency.cpp`). With four point slots, one lit room fills the pool, so
crossing a threshold leaves a new zone's lights waiting roughly half a second at
60 Hz for tiles held by entities that no longer exist.

Shape: a slot unmatched by any request for a small fixed number of consecutive
frames is definitionally dead — extraction no longer produces its owner — and
releases immediately without hysteresis. This needs no partition knowledge and no
new plumbing, which is why it is preferred over routing the residency batch into
the renderer.

Tests, at the layer owning the invariant (`ShadowResidency`, headless):

- A slot whose owner stops appearing in requests releases its allocation within
  the dead-slot window.
- A live slot that merely loses score for a few frames is not released (the
  hysteresis contract still holds).
- After releasing dead slots, a new request is granted immediately rather than
  waiting for a steal.
- Determinism: identical request/event sequences produce identical assignment,
  which is the existing class contract.
- Regression must fail before the fix: assert the new-light grant latency across
  a simulated owner disappearance.

### A3. Cuts

One commit, or one per item if any turns out to have live consumers.

- `engine/include/world/entity/EntityRegistry.h` plus its one-line
  `engine/src/world/entity/EntityRegistry.cpp`. This declares a second
  `class EntityRegistry` in the global namespace; the live one is
  `engine/include/ecs/EntityRegistry.h`, used by `World.h` and `Ecs.h`. Both are
  compiled into `sencha_engine` through `GLOB_RECURSE`, which is an ODR
  violation that only survives because nothing includes both. Delete the orphan.
- `MakeGlobalRegistry` and `MakeZoneRegistry`: no consumers outside their own
  header.
- `RegistryKind::Boundary`: no consumers anywhere.
- `RenderEntityKey`'s `Kind`, `Zone`, and `RuntimeRegistry` fields. Its own
  comment states they stay at their defaults for every runtime key, yet
  `operator<` pays two to four extra comparisons per call on light and
  shadow-caster ordering, and the header forces `render/` to include
  `world/registry/Registry.h` — pointing the renderer at the editor's registry
  vocabulary. The editor genuinely needs a wider key because its documents are
  separate worlds with colliding `EntityId`s, so the field to keep is one narrow
  document discriminator, not three fields plus a branch on `Kind`.

`Registry` and `RegistryEntityFacade` themselves are **not** cut here. The facade
describes itself as migration-only and its runtime migration is complete, but it
is now the editor document's storage container (`EditorDocument.cpp`). The honest
classification is misplaced rather than dead: an editor-shaped container living in
`engine/include/world/registry/`. Relocating it is editor work and out of scope.

Tests: existing suites plus the module ABI check, since `RenderEntityKey` layout
changes. `scripts/check_module_abi.sh`, and `sizeof`/`offsetof` coverage where the
key is reachable through installed headers.

### A4. `ZoneResidencyContext` widening — examined and rejected

Withdrawn after checking it, 2026-07-25. The finding was that the context hands
only a `World&`, so a subscriber cannot resolve a `ZoneId` to its partition and
must reach for a `RuntimeWorld` elsewhere. Three facts make the widening a net
loss:

- `ZoneResidencyChange` already carries `Partition`, so a subscriber can act on a
  departing zone's entities from the batch alone.
- `MoveEntityToPartition` is on plain `World` and `PersistentStoragePartition` is a
  compile-time constant, so rescuing an entity so it outlives its zone — the case
  Track C actually needs — already works.
- The physics residency tests boot no `RuntimeWorld` at all. Requiring one in the
  context would make every focused residency test start the zone runtime, which is
  the "tests requiring unrelated systems to boot" trigger, to buy a capability
  whose only plausible consumer gets it better from its own constructor: a
  membership system holds a `RuntimeWorld&` the way `PhysicsStepSystem` holds its
  `PhysicsWorld`.

The original framing was also unfair to the game module. A game system holding a
`RuntimeWorld*` for the partition runtime it drives is ownership, not reaching
across a boundary.

Kept instead: the context documents what is available and where zone resolution
belongs, so the apparent gap is not rediscovered.

## 4. Track B — integrate the dock ontology with the unified runtime world

Goal: `feature/unified-runtime-container` carries the zone ontology of plans 11
and 12, expressed over one `World` with storage partitions.

This is the prerequisite for Track C and it has to happen regardless, because the
two branches cannot both be main.

Order:

- **B1.** Transplant the model-agnostic ontology: manifest and endpoint records,
  `WorldPartitionIndex`, `ZoneDemand` (graph demand, `ResolveZoneAt`,
  `ZoneContainmentResult`, the reason vocabulary), `DockCrossing`,
  `WorldPartitionValidation`, `WorldTopologyCook`, and their tests. These files do
  not reference the world model, so this is a file-level port plus build wiring.
  Verification: the dock branch's own headless coverage passes unchanged —
  overlapping AABBs, deterministic ambiguous lookup, point-to-box radius demand,
  more than two resident zones, graph-only hop depth, cross-graph seeding, closed
  gate retaining topology, swept and exact-plane crossing, bounded rejection,
  diagonal and horizontal bases, jitter and reversal hysteresis, capsule-safe late
  clamps, reciprocal endpoints, parallel edges, traversal determinism across task
  counts.
- **B2.** Reconcile `WorldPartitionRuntime`: graph-driven demand, focus crossing
  through `AdvanceZoneFocus`, and `LateTraversalCount` telemetry, over
  `RuntimeWorld` partitions instead of zone registries. Preserve the unified
  branch's lease mechanism and linger behavior. Note the dock branch removes
  `ZoneParticipation::operator==`, which the unified branch's participation
  comparison uses; keep one spelling.
- **B3.** Reconcile the remaining four contested engine files and the editor
  authoring surface (14 files: manipulator session, inspector, zone bounds
  renderer, world cook, workspace, brush manipulation sink, render feature, view
  settings).
- **B4.** Re-run the streaming evidence from `docs/plans/unified-world-hardening.md`
  and confirm the criteria still hold under graph demand, since demand shape
  changes which zones are resident and therefore the chunk census and the
  propagation order rebuild counts. Regenerate
  `docs/plans/evidence/unified-world-streaming/`.

Escalation: B is a merge of two large in-flight efforts. It should not begin until
the dock branch is confirmed final, because reconciling it twice is the expensive
failure mode.

## 5. Track C — entity zone membership

The mechanism games should not have to write. Depends on B.

### The shape

`AdvanceZoneFocus` is already entity-agnostic: a pure function over
`ZoneFocusState { Current, Previous, SuppressedDock, PreviousPosition }`, the
partition index, a position, and options carrying capsule dimensions plus the
resident-physics zone set. It needs no `World` and no entity. Generalizing from
one focus actor to many tracked entities therefore adds no spatial math — the
per-actor state becomes a component.

Because "focus" then means two different things (the streaming centre versus any
entity's current zone), the generalized state and function want mechanism-neutral
names. Proposed: `ZoneTraversalState` and `AdvanceZoneTraversal`, with focus
resolution becoming one caller. Streaming focus stays singular and stays driven by
the focus actor; an enemy crossing a dock changes its own membership and must not
move the streaming focus.

Authored intent and derived runtime state are separate components, following the
`LocalTransform`/`WorldTransform` precedent:

```cpp
// Authored, serialized. Absent means Traverse.
enum class ZoneMembershipRule : std::uint8_t
{
    Traverse,    // changes zone by crossing docks
    Anchored,    // never changes zone; dies with its owning zone
    Persistent,  // migrates to the persistent partition when its zone detaches
};
struct ZoneMembership { ZoneMembershipRule Rule = ZoneMembershipRule::Traverse; };

// Runtime-only, never serialized. Added by the system for Traverse entities.
struct ZoneTraversalState { /* the generalized ZoneFocusState */ };
```

`ZoneMembershipSystem`, two hooks:

- **`PostFixed`**, after movement and before extraction: for entities carrying
  `ZoneTraversalState` whose `WorldTransform` changed, call
  `AdvanceZoneTraversal`. On `Crossed`, record a migration. On
  `BlockedDestinationNotReady`, clamp to `SafeSourcePosition` — and for a
  character, also move the underlying `CharacterMover`, per the late-destination
  contract in plan 11 section 6, or the next physics tick restores the invalid
  position. Migrations apply after iteration.
- **`ZoneResidency`** on `Detaching`: `Persistent` entities migrate to the
  persistent partition. Everything else dies with its zone, which is already the
  behavior.

Where AABBs are still correct, and used: **spawn placement** for an entity with no
inherited zone, **teleport**, and **recovery** when membership and storage
disagree — exactly the uses invariant 3 permits, through `ResolveZoneAt`. A
spawned projectile normally inherits its spawner's zone rather than resolving
anything.

Destination not resident: leave membership unchanged and clamp. The entity keeps
simulating in its still-resident source zone and crosses when the destination
arrives. It must **not** acquire a participation lease, per invariant 5 — stray
projectiles pinning the world resident is the failure that rule exists to prevent.

### Cost

Gate the sweep on `Changed<WorldTransform>`. Propagation only rewrites entities
whose local transform or parent chain changed, so static zone content is never
visited and a zone of static meshes costs nothing. Per moved entity, the work is
a crossing evaluation against its own zone's endpoint list — a handful of planes,
no manifest scan and no broad phase, matching the existing focus path which
"performs no extra broad phase."

State the expected complexity in the commit and measure it: extend the streaming
traversal bench with a moving-population scenario rather than asserting the cost
is small.

### Invariant to assert everywhere

`ZoneTraversalState.Current` must always name the zone whose partition holds the
entity. Divergence is the defect class this mechanism can introduce, so it gets an
explicit checker used by the tests after every operation, including failed
crossings, blocked clamps, detach, and recovery.

### Edge cases, each a test

Membership and identity:

1. An entity crosses a dock: partition changes, generational `EntityId` is
   preserved, component data survives, exactly one migration journal record is
   published.
2. Its Jolt body survives an active-to-active crossing; crossing into a dormant
   zone evicts and restores.
3. Crossing back is suppressed until the hysteresis band clears, then succeeds.
4. **An entity standing in an AABB overlap of two zones does not change
   membership.** This is the regression that protects invariant 3 against a
   future reintroduction of spatial ownership.
5. An `Anchored` entity moving across a dock plane does not migrate.
6. A high-speed projectile whose swept segment spans a whole zone crosses
   correctly; one whose path leaves through no dock keeps its zone.
7. A one-way dock rejects the reverse direction for entities, not only for focus.
8. A closed gate blocks passage without changing membership or topology.

Residency interaction:

9. A `Traverse` entity inside a detaching zone is destroyed.
10. A `Persistent` entity inside a detaching zone migrates to the persistent
    partition, keeps identity, and keeps simulating.
11. An entity must not migrate into a partition that is detaching in the same
    frame.
12. A blocked entity whose source zone then detaches is handled without stranding
    a clamp.
13. Destination not resident: blocked, clamped, membership unchanged, and the
    crossing retries and succeeds once the zone becomes physics-ready.

Structure and determinism:

14. Many entities crossing one dock in a single frame all migrate, in a
    deterministic order.
15. Migration requested during iteration is deferred, and the
    `MoveEntityToPartition` assertion is not tripped.
16. Serial and parallel equivalence for the sweep if it is ever parallelized;
    until then, assert the serial reference explicitly.
17. Cross-partition parenting: a child whose `Parent` lives in another zone's
    partition. Define and test what happens when the parent's zone detaches —
    today the child holds a stale `EntityId`. This is an existing hole that
    membership makes reachable, so it is closed here.

Cost:

18. A population of static entities in resident zones produces zero crossing
    evaluations.
19. Crossing evaluations scale with moved entities, not resident entities.

### What Track C does not fix

Membership keeps entities alive across boundaries **within a session**. It does not
make anything remember across an unload: an enemy that follows the player into a
zone which later unloads still dies, and a pushed crate still resets. That is
Track D, and membership is necessary but not sufficient for a backtracking loop.

## 6. Track D — durable identity and persistence

Design first, build after review. This is the gap that decides whether the engine
can host the stated target games, and it is a persisted-format change, so it needs
an explicit decision rather than initiative.

The finding: identity is positional from authoring to runtime. An authored level
document stores entities as an array of component bags with no id
(`assets/levels/Z-DataGarden-Ext.json`: entity keys are `components` alone); the
cooked scene is the same; `ZoneLocalEntityId` is the array index; the local-to-live
map is discarded at import. No component carries an authored name or GUID. Save
vocabulary does not exist anywhere in the engine — zero matches. And
`StreamingTraversalTests.RestreamedZoneMatchesAFreshAttach` asserts that a
re-streamed zone is identical to a fresh attach, which encodes "the world forgets"
as a tested property.

Blocked by this: save games for streamed content, backtracking state (opened
doors, taken items, defeated enemies, moved objects), durable cross-zone links
such as a switch in one zone opening a door in another, and state-preserving hot
reload.

Sketch to evaluate, not to build yet: a stable authored id per entity, assigned in
the document and carried through cook into a runtime component; then a per-zone
delta store keyed on `(ZoneId, authored id)` consulted at import and written at
detach. Consequences to work through before committing to it: id assignment and
stability across authoring edits, save compatibility across content patches,
delta size and write cost at detach, what happens to entities the current content
no longer contains, and interaction with `RestreamedZoneMatchesAFreshAttach`,
which becomes conditional rather than absolute.

## 7. Verification protocol

Per commit:

- Focused tests for the invariant being changed, written to fail before the fix
  and for the intended reason.
- `cmake --preset dev && cmake --build --preset dev --parallel && ctest --preset dev`,
  serial.
- `git diff --check`.

Additionally, by change kind:

- Zone lifecycle or async work: `AsyncTaskQueue(0)` deterministic path plus the
  threaded path; `tsan` preset for anything touching the async lane.
- `RenderEntityKey` or any installed header: `scripts/check_module_abi.sh` and the
  module isolation coverage.
- Track C and B4: the streaming traversal bounds plus the wall-clock bench, with
  results recorded under `docs/plans/evidence/`. Report scenario, baseline,
  result, and method; do not claim an improvement without a measurement.
- Anything touching entity destruction or partition lifetime: ASan, since the
  earlier hardening work found a real use-after-free in this area.

## 8. Open decisions

1. **Is `agent/world-graphs-and-docks` final?** Track B should not start against a
   moving target, and Track C cannot start before B.
2. **Default membership rule.** Recommended: absent `ZoneMembership` means
   `Traverse`, so authors write nothing for the common case and one component for
   the exception. The alternative — membership only for annotated entities — is
   more predictable for existing content but reintroduces "remember to annotate
   every enemy."
3. **Whether to add `CommandBuffer::MoveToPartition`**, so game code can request a
   migration from inside a query the way it requests any other structural change.
   Recommended: yes; its absence is what made per-entity migration awkward to
   write by hand.
4. **Track D scope**, per section 6.
