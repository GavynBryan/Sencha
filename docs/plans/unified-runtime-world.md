# Unified Runtime World and Zone-Partitioned ECS

Status: proposed execution plan (2026-07-21). This document becomes authoritative for
runtime ECS and zone ownership only after owner approval. Until then, current source and
shipped tests remain authoritative.

Audience: implementers and reviewers working on ECS storage, world partition, asynchronous
loading, frame scheduling, backend residency, serialization, editor/runtime boundaries, and
performance validation.

Related work:

- PR #123 establishes lifecycle correctness on the current per-registry architecture:
  complete removal hooks, explicit residency transitions, dormant backend eviction,
  participation leases, event staging, rigid-body state completion, and the low-level
  pose-servo prototype.
- PR #123 is a behavioral foundation and migration oracle. Its per-registry placement is
  transitional and must not be extended into new backend-family resources.
- `docs/plans/engine-roadmap.md` remains the standing product roadmap. This document owns
  the implementation program for replacing one-runtime-registry-per-zone with one runtime
  entity world partitioned by zone.

The decision in one sentence:

> A runtime simulation owns one ECS `World`; zones are independently streamed,
> independently participating chunk partitions inside that world; asynchronous workers
> build detached plain-data packages; backend scenes index retained objects by zone.

---

## 0. Why this program exists

The current runtime equates three different concepts:

1. a streaming and residency unit (`ZoneId`),
2. an entity identity universe (`Registry` plus `EntityId`),
3. a resource-lifetime container (`Registry::Resources`).

That equation initially made detached async construction and zone-level iteration easy.
It also made ordinary relationships across adjacent rooms cross-world relationships. The
result is increasing machinery for:

- `RegistryId + EntityId` references,
- special global-registry ownership,
- cross-registry resolution,
- per-registry backend object ownership,
- registry-specific lifecycle transitions,
- follower-registry versus target-registry rules,
- entity migration by reconstruction rather than movement,
- repeated pressure to add one registry resource per backend object family.

The repeated pressure is evidence that the registry boundary cuts through one simulation
world at the wrong level.

This program keeps the useful zone architecture:

- independently cooked zones,
- detached asynchronous preparation,
- owner-thread publication at one drain point,
- independent Visible/Physics/Logic/Audio participation,
- dormant resident zones,
- topology-driven demand,
- authored pins and composable runtime leases,
- budgeted import and teardown,
- future partition-level parallelism.

It removes only the artificial entity-universe split between adjacent streamed zones.

---

## 1. Program invariants

Every phase and patch in this program must preserve these invariants.

### 1.1 Runtime world ownership

- One game simulation instance owns one runtime ECS `World`.
- Multiple worlds remain valid for genuinely isolated simulations: editor documents,
  previews, tests, server instances, import staging, or separate causal spaces.
- Adjacent streamed zones in one play session are not separate worlds.
- `Engine` remains the explicit integration root. The unified `World` is not a service
  locator and does not absorb physics, audio, rendering, assets, jobs, or logging.

### 1.2 Zone semantics

- A zone remains the authored, cooked, streamed, resident, and participation atom.
- Zone participation is orthogonal to zone residency.
- Participation changes are nonstructural and cheap.
- Dormant means no presence in any domain whose participation flag is false.
- Forced teardown receives an explicit final residency visit before entities and backend
  objects disappear.

### 1.3 ECS hot-path semantics

- Zone ownership is a storage partition key, not a component checked per entity.
- Participation filtering happens at archetype-partition or chunk granularity.
- Existing archetype SoA storage and 16 KB chunk discipline remain.
- Existing cached-query behavior remains.
- `Changed<T>` remains chunk-conservative.
- Structural changes remain owner-thread operations or command-buffer commits.
- No worker mutates live ECS storage.

### 1.4 Asynchronous streaming

- Workers build detached plain CPU data only.
- Publication occurs at `FramePhase::DrainAsyncTasks` or its explicit successor.
- Import can be budgeted and resumed across drain points without exposing a partial zone.
- Cancellation before publication is cheap and complete.
- A representative room import must not miss a fixed tick under the declared target budget.

### 1.5 Backend ownership

- Physics, audio, navigation, and other retained backends own one scene per runtime
  simulation, not one service per zone and capability.
- A backend scene is a composition root over separate record-family modules.
- Record families remain separate files, storage, diagnostics, and tests.
- Transient operations such as forces and raycasts do not receive retained record families.
- Backend zone indices update correctly when an entity migrates.

### 1.6 Identity

- `EntityId` is the live runtime identity inside one simulation world.
- Runtime `EntityId` is never persisted.
- Detached packages use package-local entity identity.
- Authored and saved cross-load references use stable entity identity.
- Zone slots are dense runtime values and are never serialized.

### 1.7 Determinism

- Serial execution is the reference path.
- Partition iteration order is stable.
- Import, migration, hook dispatch, backend reconciliation, and teardown have explicit
  deterministic ordering.
- Parallel partition processing must match the serial result where the system contract
  requires equivalence.

---

## 2. Final ownership model

```text
Runtime simulation instance
|
+-- Runtime ECS World
|   +-- Persistent partition
|   +-- Zone A partitions by archetype
|   +-- Zone B partitions by archetype
|   `-- Zone C partitions by archetype
|
+-- ZoneRuntime
|   +-- stable ZoneId records
|   +-- dense resident ZoneSlotId table
|   +-- participation state
|   +-- import and teardown transactions
|   `-- zone residency changes
|
+-- WorldPartitionRuntime
|   +-- topology and demand
|   +-- authored pins
|   `-- participation leases
|
+-- PhysicsScene
|   +-- RigidBodyRecords
|   +-- CharacterMoverRecords
|   +-- JointRecords
|   `-- PoseServoRecords
|
+-- AudioScene
+-- NavigationScene
`-- frame schedule and diagnostics
```

### 2.1 One runtime world

Introduce a runtime composition object only if current `Engine` ownership becomes unclear.
Do not create an interface or general container merely for symmetry.

Conceptual shape:

```cpp
struct RuntimeWorld
{
    World Entities;
    ResourceRegistry Resources; // world-scoped only
    ZoneTable Zones;
};
```

Whether this lands as a named `RuntimeWorld` or as explicit members owned by `Engine` is an
implementation decision made when the first phase reaches composition code. The ownership
contract is mandatory; the wrapper type is not.

### 2.2 Persistent partition

Reserve one runtime partition for entities that survive ordinary zone unloading:

```cpp
struct ZoneSlotId
{
    uint16_t Value = 0;
    friend bool operator==(ZoneSlotId, ZoneSlotId) = default;
};

constexpr ZoneSlotId PersistentZoneSlot{0};
```

Likely persistent entities include:

- player-controlled pawn,
- active camera and camera rig entity,
- world-lifetime game-state entities,
- cross-zone directors whose lifetime is the simulation rather than a room.

Persistence is storage ownership, not automatic domain participation. Systems still select
which persistent entities match their components and tags.

### 2.3 Stable zone identity and dense runtime slot

`ZoneId` remains authored and persisted identity. A resident zone receives a dense slot:

```cpp
struct ZoneRuntimeRecord
{
    ZoneId Id;
    ZoneSlotId Slot;
    ZoneLoadState LoadState;
    ZoneParticipation Participation;
    uint64_t StructuralVersion;
    ResourceRegistry Resources; // strictly zone-scoped resources only
};
```

Slot requirements:

- slot zero is persistent,
- reuse is generational or delayed until no stale frame/package reference can exist,
- slot lookup is O(1),
- the resident slot set is dense enough for bitsets and small arrays,
- no cooked or saved data contains a slot value.

### 2.4 Resource homes

Every current and future runtime resource must be classified into one of three homes.

#### World-scoped resources

Owned by the runtime simulation instance:

- active-camera selection,
- physics scene,
- audio scene,
- navigation scene,
- world-lifetime gameplay services,
- event channels whose publishers may unload.

`ActiveCameraService` is presumed world-scoped unless source review proves that it is
actually one-zone metadata.

#### Zone-scoped resources

Owned by `ZoneRuntimeRecord::Resources` only when all are true:

1. the object is not naturally entity-indexed,
2. it is not a retained backend object family,
3. its lifetime is exactly one zone's resident lifetime,
4. no other zone or world-lifetime entity owns or shares it.

Possible examples:

- imported zone metadata,
- zone environment configuration,
- zone-local immutable acceleration data,
- zone-local script state that is explicitly not represented as entities.

A zone resource must not be used merely because the old registry had a resource table.

#### Backend-scoped retained state

Owned by the relevant backend scene:

- body handles,
- character mover handles,
- joints,
- pose servos,
- audio voices and emitters,
- navigation agents and requests,
- future retained vehicle/controller objects.

These do not live in world resources or zone resources.

### 2.5 PhysicsScene file discipline

`PhysicsScene` is a composition root and ordering owner, not a record grab-bag.

```cpp
class PhysicsScene
{
public:
    void Reconcile(World&, const ZonePartitionSet&);
    void ApplyZoneResidency(World&, std::span<const ZoneResidencyChange>);
    void ApplyEntityZoneMoves(World&, std::span<const EntityZoneMove>);
    void Step(float dt);

private:
    RigidBodyRecords Bodies;
    CharacterMoverRecords Characters;
    JointRecords Joints;
    PoseServoRecords PoseServos;
};
```

Merge gate:

> Adding a retained physics family adds or extends a dedicated record-family module with
> its own tests. It must not primarily enlarge `PhysicsScene.cpp`.

Each record family owns:

- backend handles,
- dense storage or stable slots appropriate to its mechanism,
- entity reconciliation,
- zone secondary indices,
- migration handling,
- diagnostics,
- conformance tests.

Examples:

- force and impulse: transient commands, no record family,
- ragdoll: composition of body and joint records unless Jolt requires a retained ragdoll
  object with distinct lifecycle,
- vehicle: a dedicated record family only if a retained backend vehicle/controller object
  actually exists,
- pose servo: dedicated records because backend constraint handles persist across steps.

---

## 3. Unified component schema

The current `World` requires all component types to be registered before the first entity
is created and uses a fixed 256-component signature budget. The unified world makes this a
runtime-wide contract rather than a per-zone convenience.

### 3.1 Frozen runtime schema

Before the runtime world creates its first entity:

```cpp
RuntimeComponentSchema schema;
RegisterEngineComponents(schema);
game.RegisterComponents(schema);
RuntimeWorld runtime(schema);
```

The exact API may reuse existing registration functions; the required behavior is:

- engine and game module registrations complete before entity creation,
- registration order is deterministic,
- stable `ComponentTypeId` resolves to one runtime `ComponentId`,
- every streamable zone uses the same schema,
- zone loading never registers a component,
- unknown package component types fail before partial import.

### 3.2 Component-budget gate

Before Phase 1 implementation:

1. measure engine component count,
2. measure the current test game's count,
3. estimate Loss Function's near-term component count,
4. record remaining headroom,
5. add a warning threshold.

Default decision:

- retain the fixed 256-bit signature if projected v1.0 usage has comfortable headroom,
- warn at a chosen threshold such as 192 or 224,
- widen the fixed signature before migration if headroom is not comfortable,
- do not introduce heap-allocated dynamic signatures without measured need.

The budget is runtime-wide but is no longer multiplied conceptually by zone count.

### 3.3 Schema fingerprint

Add or reuse a deterministic schema fingerprint covering:

- stable component type identity,
- size,
- alignment,
- tag status,
- serialization contract version where available.

Cooked zone packages record the fingerprint or compatible schema version. Import rejects
incompatible packages with a useful diagnostic before allocating runtime entities.

---

## 4. Partition-capable ECS storage

Zone ownership is structural metadata in ECS storage. It is not an ordinary component.

### 4.1 Entity location

Extend entity location with a partition key:

```cpp
struct EntityLocation
{
    ArchetypeId Archetype;
    ZoneSlotId Zone;
    uint32_t PartitionIndex;
    uint32_t ChunkIndex;
    uint32_t RowIndex;
};
```

Names may be adjusted to current types. Raw indices remain internal; public APIs use strong
IDs.

### 4.2 Archetype partitions

Every archetype keeps chunks grouped by zone slot:

```cpp
struct ArchetypePartition
{
    ZoneSlotId Zone;
    std::vector<std::unique_ptr<Chunk>> Chunks;
};

struct Archetype
{
    ArchetypeSignature Signature;
    std::vector<ArchetypePartition> Partitions;
};
```

Requirements:

- one chunk belongs to exactly one zone slot,
- row creation selects the destination zone partition,
- structural moves preserve the entity's zone unless explicitly migrating,
- empty zone partitions are reclaimed on zone teardown,
- partition lookup must not add a general unordered-map lookup to every row operation,
- expected resident zone count is small; use a small-vector, sorted vector, dense slot
  array, or measured alternative.

### 4.3 Fragmentation expectation

The current one-registry-per-zone design already produces a partial tail chunk per
archetype per zone. Grouping chunks by zone inside one world reproduces that boundary but
reduces duplicated world-level metadata.

Measure rather than assume:

- chunk slack by archetype and zone,
- empty partition overhead,
- archetype metadata reduction,
- query cache memory,
- entity registry capacity.

### 4.4 ECS API additions

```cpp
EntityId CreateEntity(ZoneSlotId zone);
EntityId CreateEntityWithSignature(
    ZoneSlotId zone,
    const ArchetypeSignature& signature);

ZoneSlotId GetEntityZone(EntityId entity) const;

void MoveEntityToZone(
    EntityId entity,
    ZoneSlotId destination);

ZoneDestroyProgress DestroyZonePartition(
    ZoneSlotId zone,
    StructuralBudget budget);
```

Compatibility overloads may initially default entity creation to the persistent slot.
They must be deleted or made explicit once all runtime creation sites are ported.

### 4.5 Partition structural versions

Maintain at least:

```cpp
uint64_t WorldStructuralVersion;
uint64_t ArchetypeGeneration;
uint64_t PartitionStructuralVersion[ZoneSlot];
```

Rules:

- any structural mutation increments the global structural version,
- a mutation increments every affected partition version,
- creating a previously unseen archetype signature increments archetype generation,
- entity migration increments source and destination partition versions,
- backend record families gate zone reconciliation on partition version,
- cached query archetype matching gates on archetype generation rather than unrelated row
  churn.

Do not let activity in Zone B force body reconciliation for Zone A.

---

## 5. Entity migration

`MoveEntityToZone` is a flagship operation and must be correct before backend consolidation.

### 5.1 Semantics

```text
MoveEntityToZone(entity, destination)
    validate source and destination
    preserve EntityId and generation
    preserve component signature
    allocate destination row in same-signature destination partition
    copy component columns
    update entity location
    swap-remove source row and repair moved-neighbor location
    bump source partition version
    bump destination partition version
    bump global structural version
    mark destination columns changed
    append EntityZoneMove
```

No component add/remove hooks fire because the entity keeps the same component set and
component lifetime. A future explicit zone-transition trait is not added unless a concrete
component has a mechanical need not served by systems consuming the migration journal.

### 5.2 Migration journal

Produce generic structural facts:

```cpp
struct EntityZoneMove
{
    EntityId Entity;
    ZoneSlotId Previous;
    ZoneSlotId Current;
};
```

Ownership:

- the ECS `World` records moves,
- the schedule exposes a stable batch at a deterministic phase boundary,
- backend scenes and other consumers update secondary indices,
- the ECS does not know backend object types.

### 5.3 Phase placement

Initial fixed-tick ordering:

```text
Fixed logic
CommandBuffer flush
Apply requested entity-zone moves
Publish stable EntityZoneMove batch
Backend migration reconciliation
Physics push
Physics step
Physics pull
PostFixed
```

Frame-update or editor-driven migration outside fixed logic commits at the owner-thread
structural drain before the next frame view. There must be one documented legal mutation
window, not several ad-hoc paths.

### 5.4 Backend migration behavior

Each retained record stores current zone ownership:

```cpp
struct BodyRecord
{
    EntityId Entity;
    ZoneSlotId Zone;
    PhysicsBodyId Body;
};
```

On a move:

- remove the record from the source zone secondary index,
- add it to the destination zone secondary index,
- update the record's zone,
- evaluate source and destination participation,
- evict, retain, or restore backend presence before the next relevant backend step.

Cases:

| Source physics | Destination physics | Required result |
|---|---|---|
| active | active | keep body, update zone index |
| active | dormant | capture state and evict before solver |
| dormant | active | restore before solver |
| dormant | dormant | update ownership only |

The same contract applies mechanically to audio, navigation, and other retained scenes.

### 5.5 Cross-zone relationships during migration

A relationship does not automatically migrate because one endpoint moves. The owning
system decides whether to:

- continue with endpoints in different zones and retain participation through leases,
- migrate an associated entity,
- terminate the relationship,
- transfer ownership to a persistent entity.

The backend scene only updates the endpoint's zone index and applies domain participation.

---

## 6. `Changed<T>` and migration

Sencha's change tracking is chunk-conservative. Migration preserves that doctrine.

### 6.1 Required behavior

Moving a row writes every destination component column. Therefore:

- every destination column containing the migrated entity is marked changed for the
  current frame or structural epoch,
- neighboring rows in the destination chunk may conservatively match `Changed<T>`,
- the source partition's structural version changes,
- the swap-moved source row is not semantically marked changed merely because bytes moved
  to fill the hole,
- component values remain bit-identical unless a component's representation itself
  contains runtime location data, which is forbidden.

### 6.2 Required tests

- `Changed<T>` observes a migrated entity.
- Entity identity and every component value survive migration.
- Destination-chunk neighbors may conservatively match.
- Unrelated source chunks do not become changed.
- Multiple migrations in one flush are deterministic.
- Serial and partition-parallel paths agree.
- Migration followed by destruction fires each `OnRemove` exactly once.
- Migration does not fire `OnAdd` or `OnRemove`.

---

## 7. Partition-aware queries and frame view

### 7.1 Zone partition sets

Introduce a compact active-zone set suitable for the expected small resident count:

```cpp
class ZonePartitionSet
{
public:
    bool Contains(ZoneSlotId) const;
    std::span<const ZoneSlotId> OrderedSlots() const;
};
```

The implementation may use a dense bitset plus ordered slot vector. Requirements:

- O(1) membership,
- deterministic iteration,
- no allocation during query execution,
- persistent slot inclusion is explicit and testable.

### 7.2 Frame view

Replace `FrameRegistryView` with:

```cpp
struct FrameZoneView
{
    World* Entities;
    ZonePartitionSet Visible;
    ZonePartitionSet Physics;
    ZonePartitionSet Logic;
    ZonePartitionSet Audio;
    ZonePartitionSet Resident;
};
```

System contexts carry the full view and the domain-specific active set where useful. No
system receives a span of runtime registries.

### 7.3 Query execution

Partition-aware query traversal:

```cpp
query.ForEachChunk(activeZones, [&](auto& view)
{
    // view contains rows from exactly one zone partition
});
```

Traversal order is fixed:

1. query's stable matching-archetype order,
2. active zone slot order,
3. chunk order,
4. row order.

Alternative ordering may be chosen only if benchmarks and determinism requirements show a
better contract. The order must be documented before implementation.

### 7.4 No per-entity zone filtering

Rejected hot path:

```cpp
if (!activeZones.Contains(zoneOwner[i]))
    continue;
```

Zone is not an ordinary component and normal domain systems do not branch once per row.

### 7.5 Partition parallelism

Partition buckets are disjoint mutable storage. Preserve a future or existing helper:

```cpp
ForEachZoneParallel(activeZones, [&](ZoneSlotId zone)
{
    PropagateZoneTransforms(world, zone);
});
```

Rules remain:

- no worker structural mutation,
- no worker publication to owner-thread resources,
- local outputs merge deterministically,
- persistent/shared inputs are read-only,
- only parallelize after measured work crosses the established threshold.

---

## 8. Zone lifecycle and participation

### 8.1 Load state

```cpp
enum class ZoneLoadState : uint8_t
{
    Unloaded,
    Loading,
    Importing,
    Resident,
    Detaching,
};
```

Participation remains orthogonal:

```cpp
struct ZoneParticipation
{
    bool Visible;
    bool Physics;
    bool Logic;
    bool Audio;
};
```

### 8.2 Zone residency changes

Replace registry-specific changes with:

```cpp
enum class ZoneResidencyChangeKind : uint8_t
{
    Attached,
    ParticipationChanged,
    Detaching,
};

struct ZoneResidencyChange
{
    ZoneResidencyChangeKind Kind;
    ZoneId Zone;
    ZoneSlotId Slot;
    ZoneParticipation Previous;
    ZoneParticipation Current;
};
```

The batch is stable and read-only while handlers run.

### 8.3 Scheduling

```text
Pump platform
Resolve host lifecycle
Drain async task completions
Advance zone import and teardown transactions
Apply queued zone participation changes
Build stable ZoneResidencyChange batch
Run ZoneResidency consumers
Finalize completed detaches
Build FrameZoneView
Schedule fixed ticks
Run simulation and presentation phases
End frame view
```

### 8.4 Mid-frame participation requests

A gameplay system may request a participation change while a frame view is live. The
request is queued and becomes visible only through the next legal residency phase. It must
not mutate the current frame view or backend state immediately.

This preserves seamless dormant preload activation without allowing current-frame spans to
dangle or become semantically inconsistent.

### 8.5 Dormancy

Dormant resident data stays in ECS memory but does not appear in inactive domain queries.
Retained backends remove domain presence:

- physics: no bodies, movers, constraints, contacts, queries, or solver work,
- audio: no voices or emitter updates,
- render: no extraction,
- logic: no ordinary fixed or frame logic,
- navigation: no active agents or requests unless explicitly retained.

---

## 9. Participation leases

Settled ownership sentence:

> Participation leases are engine-owned and explicitly caller-held.

### 9.1 Mechanism ownership

`WorldPartitionRuntime` owns:

- lease tokens,
- generation validation,
- composition of lease floors with streaming demand and authored pins,
- forced invalidation during teardown.

### 9.2 Holder ownership

The system requiring residency holds the token:

- Vector Tether gameplay system,
- projectile system,
- cinematic system,
- importer or snapshot operation,
- engine operation with an explicit documented need.

A component does not silently acquire a lease merely by existing. A backend scene does not
make streaming-policy decisions.

### 9.3 Effective participation

```text
streaming demand
OR authored pin floors
OR active lease floors
```

### 9.4 Forced teardown

Forced teardown:

1. invalidates affected zone lease tokens,
2. stages terminal relationship events where applicable,
3. publishes `Detaching`,
4. removes backend presence,
5. destroys zone entities and resources.

Stale tokens cannot release a reused slot.

---

## 10. Detached zone packages

### 10.1 Worker output

Replace detached runtime-registry construction with detached plain-data packages:

```cpp
struct ZoneLoadPackage
{
    ZoneId Zone;
    ComponentSchemaFingerprint Schema;
    std::vector<ZoneArchetypeBatch> Archetypes;
    std::vector<EntityFixup> Fixups;
    ZoneLoadDiagnostics Diagnostics;
};
```

```cpp
struct ZoneArchetypeBatch
{
    std::vector<ComponentTypeId> Signature;
    uint32_t EntityCount;
    std::vector<LocalEntityId> Entities;
    std::vector<ComponentColumnBlob> Columns;
};
```

The package is:

- immutable after build,
- independent of runtime component IDs,
- independent of runtime entity IDs,
- independent of live chunk pointers,
- cancellable and discardable,
- suitable for future save-state overlay.

### 10.2 Identity categories

```cpp
struct LocalEntityId
{
    uint32_t Value;
};

struct StableEntityRef
{
    ZoneId Zone;
    PersistentEntityId Entity;
};
```

- `EntityId`: live runtime reference,
- `LocalEntityId`: package-local reference,
- `StableEntityRef`: durable authored/save reference.

Runtime IDs are assigned during import.

### 10.3 Bulk import

Do not deserialize by repeatedly creating an empty entity and adding components one by one.
Import directly into final archetype partitions:

1. validate package schema,
2. allocate or reserve zone slot,
3. resolve stable component types to runtime component IDs,
4. allocate runtime entity IDs in a batch,
5. allocate final destination chunks,
6. copy columns contiguously,
7. populate entity locations,
8. build local-to-runtime mapping,
9. resolve entity fixups,
10. fire required `OnAdd` hooks in deterministic order,
11. finalize zone resources,
12. publish `Attached`.

### 10.4 Transactional import

```cpp
ZoneImportHandle BeginZoneImport(ZoneLoadPackage&& package);
ZoneImportProgress ContinueZoneImport(
    ZoneImportHandle,
    AsyncDrainBudget);
ZoneImportResult CommitZoneImport(ZoneImportHandle);
```

While importing:

- partial partitions are hidden,
- no normal query includes the zone,
- backend reconciliation does not see the zone,
- graph-dependent hooks may be deferred until fixups complete,
- cancellation destroys all partial state.

Representative rooms should normally finish in one drain. The transaction prevents a
future large zone from forcing a rewrite.

### 10.5 Zero-copy adoption

Do not implement worker-built live chunk adoption in the first program.

Revisit only if measured bulk column import remains a material streaming bottleneck after:

- parse and decompression optimization,
- contiguous column copy,
- incremental import,
- lifecycle-hook batching,
- asset preload staging.

Worker-built live chunks would require runtime entity assignment, component-ID agreement,
fixup-safe layouts, changed-version initialization, lifecycle correctness, and cancellation.
Do not buy that complexity without evidence.

---

## 11. Zone teardown and persistence

### 11.1 Teardown sequence

```text
Unload requested
Participation request becomes empty
ZoneResidency Detaching visit
Backend scenes remove retained objects
Optional save snapshot package is captured
Zone partition rows are destroyed
Zone-scoped resources are destroyed
Zone slot is released
Snapshot serialization continues asynchronously
```

### 11.2 Budgeted partition destruction

Destruction is linear in entities and hooked components regardless of whether they live in
one `World` or a separate registry. Make it budgetable:

```cpp
ZoneDestroyProgress ContinueDestroyZonePartition(
    ZoneDestroyHandle,
    StructuralBudget);
```

A detaching zone is absent from every domain while teardown advances. Ordinary room-sized
zones should complete in one drain.

### 11.3 Snapshot package

Define a plain-data `ZoneSnapshotPackage` symmetric with load packages when save work begins.
At the owner-thread drain:

- copy saveable component state,
- translate runtime references to stable references,
- release live backend and ECS state,
- serialize the detached package asynchronously.

Do not move live ECS chunks to worker threads in the first implementation.

---

## 12. Relationships, hierarchy, and references

### 12.1 Live relationships

Loaded entities reference each other with ordinary `EntityId`.

A relationship crossing zone participation boundaries must either:

- hold explicit participation leases,
- tolerate the target becoming unavailable,
- terminate through a staged event.

### 12.2 Durable references

Cooked and saved references use `StableEntityRef`. Resolution occurs when the target zone is
resident. Resolved caches include an epoch or validity check and never persist runtime IDs.

### 12.3 Transform hierarchy

Initial rule:

> A transform parent may be in the same zone or in the persistent partition.

Zone-to-zone transform parenting is rejected during validation until a concrete consumer
earns a more complex ownership rule.

This preserves:

- independent unload,
- deterministic propagation,
- partition-parallel transforms,
- understandable migration.

### 12.4 Migrating hierarchy members

`MoveEntityToZone` validation must address hierarchy explicitly:

- moving a parent with zone-local children either migrates the owned subtree in a stable
  order or rejects the operation,
- moving a child away from a zone-local parent rejects unless the parent is persistent or
  the caller first reparents,
- persistent parents remain valid across zone migration.

The first implementation should prefer rejection over implicit broad subtree migration.
Add subtree migration only when a concrete game operation requires it.

---

## 13. Editor and cook boundary

The runtime unification does not require editor document unification.

Kyusu may continue using one editor registry per document or zone for:

- undo isolation,
- document lifetime,
- multi-document workflows,
- loaded reference zones,
- preview isolation.

Cooked output remains one package per zone.

At runtime or PIE, cooked documents import into one runtime world as zone partitions. The
editor representation is an authoring boundary, not proof of runtime ownership.

Cook-only package builders remain dev-only and do not link into the shipping runtime beyond
shared cooked-format readers.

---

## 14. Networking, navigation, and future systems

### 14.1 Networking

Zone membership remains the coarse interest-management filter:

```text
client relevance
    -> relevant ZoneIds
    -> relevant partition chunks
    -> entities
```

Cross-zone replication does not route through registry identities.

### 14.2 Navigation

Navigation data may remain spatially partitioned by zone or navigation cell. Agents and
targets use one live entity identity space. Navigation scene records index their owning
zone for dormancy and teardown.

### 14.3 Rendering and audio

Render extraction traverses visible partitions only. Audio traverses audio-active partitions
and owns retained voice state in one audio scene indexed by zone.

### 14.4 Portals and spaces

Portals remain topology and spatial-transition mechanisms. Non-Euclidean presentation does
not imply another ECS world. Introduce another runtime world only for an actually isolated
causal simulation.

---

## 15. Performance and memory gates

No phase merges based only on correctness tests. The program must demonstrate that the
unified world preserves or improves the relevant costs.

### 15.1 Baseline capture before Phase 1

Capture current main or PR #123 baseline for:

- one active zone,
- four active zones,
- four resident with one active,
- representative room load,
- representative unload,
- rigid-body reconciliation,
- transform propagation,
- render extraction,
- memory by world, archetype, and chunk.

Record hardware, build type, compiler, worker count, and fixture content.

### 15.2 Query benchmark fixtures

- one zone, all active,
- four resident zones, one active,
- four resident zones, all active,
- eight resident zones, two active,
- persistent entities plus active zones,
- many archetypes with sparse zone population,
- few archetypes with dense zone population.

Gates:

- all-active traversal within approximately 5% of the current single-world baseline,
- dormant entity count adds no per-entity branch work to active-domain traversal,
- no allocation during query execution,
- stable deterministic order,
- partition lookup overhead is measured and bounded.

The 5% figure is an initial review threshold, not a permanent product law. A larger change
requires an explained tradeoff and owner approval.

### 15.3 Streaming benchmark fixtures

- 250 entities,
- 1,000 entities,
- 4,000 entities,
- many small archetypes,
- few dense archetypes,
- asset-heavy but entity-light zone,
- cross-reference-heavy package.

Measure:

- worker package-build time,
- package memory,
- schema resolution,
- runtime entity allocation,
- column copy,
- fixup time,
- hook dispatch,
- backend restoration,
- teardown,
- peak package-plus-live-world memory.

Gate:

- representative room import normally completes inside configured commit budget,
- larger imports yield without exposing partial state or missing fixed ticks.

### 15.4 Fragmentation and memory gates

Measure:

- tail-chunk slack per archetype per zone,
- empty partition metadata,
- query cache duplication removed,
- component registration metadata removed,
- entity registry growth,
- package peak memory,
- post-unload retained capacity.

A destroyed zone must leave no live partition bucket. Reusable capacity may remain only if
bounded by an explicit allocator policy and visible in diagnostics.

### 15.5 Backend gates

Physics tests prove:

- dormant zones have zero bodies and constraints in backend queries,
- restoration preserves transform and velocity,
- migration updates zone indices,
- only affected zones reconcile,
- cross-zone collision works while both zones participate,
- detach leaves zero residue,
- serial and parallel orchestration agree where required.

Equivalent conformance suites are added as retained audio and navigation state exists.

---

## 16. Diagnostics and tooling

Add diagnostics as the mechanisms land, not after the migration is complete.

Required counters and views:

- resident zone count,
- zone slot map,
- participation by domain,
- entities and chunks per zone,
- partition structural versions,
- pending entity migrations,
- package import progress and bytes,
- teardown progress,
- backend records per zone,
- participation leases and holders,
- stale stable-reference resolutions,
- chunk slack by zone and archetype.

The timing panel should distinguish:

- package build,
- async wait,
- import,
- residency processing,
- backend restore/evict,
- partition query traversal,
- teardown.

Do not make architectural performance claims without these counters or benchmark output.

---

## 17. Migration program

### Phase 0: freeze and establish the baseline

Goals:

- merge the verified behavioral foundation from PR #123 after correcting obsolete
  per-registry resource doctrine,
- stop extending the per-registry ownership model,
- capture performance and memory baselines,
- freeze the runtime component schema decision.

Required work:

1. Update PR #123 description and architecture prose:
   - call per-registry backend placement transitional,
   - retain behavioral contracts,
   - point to this plan,
   - remove the doctrine that each retained family belongs in `Registry::Resources`.
2. Do not implement per-registry `DrivenPoseBinding`.
3. Do not add more `RegistryId`-based runtime relationships.
4. Do not add new backend-family resources to zone registries.
5. Count components and decide whether 256 fixed bits has sufficient v1.0 headroom.
6. Capture baseline benchmarks and diagnostics.

Exit gate:

- PR #123 green and merged,
- this plan approved,
- component budget decision recorded,
- baseline artifacts committed or attached to a tracking issue.

### Phase 1: partition-capable ECS storage

Goals:

- add zone partitioning without changing runtime world composition yet,
- preserve all unpartitioned behavior through the persistent default partition.

Required work:

- `ZoneSlotId`,
- partition key in entity location,
- archetype partition buckets,
- partition-aware row allocation and structural moves,
- partition-local structural versions,
- partition destruction,
- `GetEntityZone`,
- `MoveEntityToZone`,
- stable migration journal,
- `Changed<T>` migration semantics,
- hierarchy validation,
- tests and microbenchmarks.

Compatibility:

- existing `World::CreateEntity()` defaults to persistent slot temporarily,
- current registries still contain separate worlds during this phase,
- no production runtime behavior changes yet.

Exit gate:

- ECS suite green,
- migration and change-tracking tests green,
- query-independent storage benchmarks acceptable,
- no significant unbounded fragmentation.

### Phase 2: partition-aware queries and scheduling

Goals:

- make one `World` able to expose domain-specific zone partitions efficiently.

Required work:

- `ZonePartitionSet`,
- partition-aware cached queries,
- stable iteration order,
- `FrameZoneView`,
- partition-aware transform propagation,
- partition-aware logic, render, audio, and physics contexts,
- serial/parallel determinism tests,
- query benchmarks.

Compatibility:

- add adapters from one-registry current contexts where needed,
- do not maintain two independent implementations longer than one phase.

Exit gate:

- no hot system requires per-entity zone filtering,
- all-active and mostly-dormant query gates pass,
- serial and parallel tests pass.

### Phase 3: unified ZoneRuntime

Goals:

- replace runtime ownership of one registry per zone with one world plus zone records.

Required work:

- runtime world ownership in `Engine` or a concrete composition object,
- persistent partition,
- `ZoneTable`,
- dense resident slots,
- queued participation requests,
- `ZoneResidencyChange`,
- `FrameZoneView` construction,
- world-, zone-, and backend-resource classification,
- port `ActiveCameraService` and every current resource deliberately,
- compatibility adapters for editor and tests where necessary.

Exit gate:

- several zones coexist in one world,
- activate and sleep independently,
- player and camera survive unload without a special registry,
- no runtime system requires `ActiveRegistries`.

### Phase 4: package-based asynchronous loading

Goals:

- replace detached runtime-registry construction with detached package production and
  budgeted import.

Required work:

- package schema,
- package-local entity IDs,
- stable-reference encoding,
- serializer package path,
- bulk final-archetype import,
- local-to-runtime mapping,
- fixups,
- hook ordering,
- incremental transaction,
- cancellation,
- preload-gated commit,
- import diagnostics and benchmarks.

Exit gate:

- seamless dormant preload works,
- activation uses queued participation,
- representative room import meets budget,
- no worker mutates live world,
- no detached runtime registry is built for shipping zone loads.

### Phase 5: backend scene consolidation

Goals:

- move retained backend ownership from per-registry resources into one scene per backend.

Physics order:

1. `RigidBodyRecords`,
2. `CharacterMoverRecords`,
3. existing constraints and pose servos,
4. future joints or vehicles only when concrete.

Required work:

- one `PhysicsScene`,
- separate record-family modules,
- zone secondary indices,
- partition-version reconciliation,
- entity migration batch handling,
- zone evict/restore/detach,
- conformance suites,
- remove physics resources from zone registries.

Then apply the same model to retained audio and navigation state when those systems exist.

Exit gate:

- adding another retained physics family does not require registering a zone resource,
- backend conformance tests pass,
- dormant and migration behavior is correct,
- old binding resources are removed.

### Phase 6: remove runtime registry identity

Goals:

- delete obsolete runtime concepts after all consumers are ported.

Remove or restrict to editor/isolated-world usage:

- runtime global registry,
- runtime zone registries,
- `FrameRegistryView`,
- `RegistryResidency`,
- runtime `RegistryId` routing,
- `EntityRef { RegistryId, EntityId }`,
- per-registry backend resources,
- registry-span system contexts.

Introduce or complete:

- ordinary runtime `EntityId`,
- `StableEntityRef`,
- persistent partition,
- explicit world-level resources,
- explicit zone-level resources.

Exit gate:

- shipping runtime uses no registry identity for zone ownership,
- editor document registries remain isolated and supported,
- compatibility adapters are deleted.

### Phase 7: persistence hardening

Goals:

- prove unload/reload identity and state continuity.

Required work:

- stable persistent entity identity,
- `ZoneSnapshotPackage`,
- state overlay on package import,
- budgeted snapshot capture,
- async serialization,
- relationship termination and restoration rules,
- save/load tests across zone unload.

This phase may align with the save-game roadmap item, but identity contracts must not be
left ambiguous until then.

---

## 18. Compatibility strategy

This migration must not create a permanent dual architecture.

Rules:

- adapters are phase-local and carry deletion comments naming the exit gate,
- no new game-facing API is added on the old registry model after Phase 0,
- tests are ported to the new mechanism rather than duplicated indefinitely,
- current editor registries are not forced through runtime adapters,
- old runtime types are deleted as soon as their last shipping consumer leaves,
- no generic `IWorld`, `IRegistry`, `IPartition`, or strategy interface is introduced merely
  to host both shapes.

A compatibility adapter may be a concrete free function or small concrete wrapper. It is
not a new permanent abstraction boundary.

---

## 19. Rejected designs

### Zone ownership component

Rejected because it adds per-row hot-path filtering and allows ownership metadata to drift
from storage.

### Zone encoded in archetype signature

Rejected because it duplicates signatures per zone and makes participation pressure look
structural.

### Dormant archetypes

Rejected because participation changes would move every entity structurally.

### One world with unsorted mixed-zone chunks

Rejected because it preserves per-entity filtering and makes partition teardown expensive.

### One backend resource per zone and capability

Rejected because the backend orchestrator already receives zone lifecycle and capability
growth would recreate resource proliferation.

### One giant backend implementation file

Rejected. Backend scenes are composition roots over separate record-family modules.

### Worker mutation of live ECS

Rejected. Workers produce detached packages; publication is owner-thread only.

### Immediate zero-copy live chunk adoption

Rejected until measured bulk import proves insufficient.

### Persisted runtime EntityId

Rejected because unload, reload, generation reuse, and package import make it unsuitable for
durable identity.

### Runtime/editor world unification

Rejected. Editor documents are genuinely isolated ownership domains and may keep registries.

### Generic partition abstraction before implementation

Rejected. Implement zone-partitioned archetype storage concretely. Extract only proven
mechanical commonality later.

---

## 20. Risks and containment

### Risk: chunk partition lookup slows structural operations

Containment:

- expected resident slot count is small,
- benchmark dense array, sorted small vector, and measured alternatives,
- no unordered lookup is accepted by habit.

### Risk: package import duplicates memory

Containment:

- measure package-plus-live peak,
- release column blobs incrementally after import,
- budget import,
- revisit ownership transfer only with evidence.

### Risk: migration journals become a second event bus

Containment:

- journal contains structural facts only,
- fixed phase ownership,
- no arbitrary subscription system,
- known engine consumers called explicitly by schedule/composition roots.

### Risk: zone resources become the new registry-resource dumping ground

Containment:

- three-home admission rule,
- backend retained objects forbidden,
- world-scoped services forbidden,
- every new zone resource names why its lifetime is exactly zone residency.

### Risk: PhysicsScene becomes a grab-bag

Containment:

- explicit file and test gate,
- record families retain storage and lifecycle,
- composition root owns ordering only.

### Risk: component schema blocks streamed game modules

Containment:

- game module registration completes before runtime world creation,
- hot module reload remains a separate roadmap problem,
- packages validate schema fingerprints.

### Risk: plan overbuilds for hypothetical scale

Containment:

- room-scale fixtures are the primary benchmark,
- incremental import and partition APIs are justified by current streaming contract,
- zero-copy adoption, generalized spaces, subtree migration, and dynamic signatures remain
  trigger-driven.

---

## 21. Immediate next actions

Before implementation begins:

1. Approve or amend this document.
2. Amend PR #123 documentation so its behavioral contracts survive but per-registry
   resource placement is explicitly transitional.
3. Merge PR #123 after its verified suite result is recorded.
4. Create a tracking issue for this program with one checklist item per phase and links to
   benchmark artifacts.
5. Count registered components and decide the fixed signature budget.
6. Add baseline ECS, query, streaming, teardown, and physics benchmarks on current main.
7. Start Phase 1 in a dedicated implementation branch.

No P4 per-registry driven-pose binding is built. P3 spring compliance and real
force/torque limits remain a separate pure-physics backend task and do not block the
unified-world program.

---

## 22. Completion criterion

The program is complete when:

- one shipping runtime simulation uses one ECS world,
- zones remain independent streaming and participation atoms,
- dormant zones add no per-entity hot-path work,
- workers build detached packages and owner-thread import is budgeted,
- entities migrate between zones without changing runtime identity,
- backend zone indices remain correct across migration,
- retained backends own one scene composed from separate record families,
- no new physics capability requires a zone resource,
- editor document isolation remains intact,
- durable references survive unload and reload without persisting runtime IDs,
- representative traversal meets the no-hitch gate with diagnostics proving where time and
  memory are spent.
