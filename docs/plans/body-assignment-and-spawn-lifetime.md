# Body assignment and spawn lifetime

Status: implemented 2026-10-14 on `claude/body-assignment-spawn-lifetime`.
Revises the shipped design in [body-spawns.md](body-spawns.md). Where the code
and tests differ from this document, they are the authority; the deviations
made while implementing are listed in section 9.

Removes two workarounds and three duplications behind `BodySpawns`:

- The adapter delivers an asynchronous result by re-entering a synchronous
  request (`Update -> RequestParticipantBody -> ProvideBody`), because the
  participant lifecycle cannot assign a body except by asking a policy for one.
- The adapter tracks every handed-off body (`root -> SceneSpawnId`) so that a
  dead root's children get cleaned up. That is a property of a spawned group,
  not of participants, and no other spawn can get it.
- "The group's single root" is written three times (`BodySpawns`, arena's
  `SpawnedGroupRoot`, `NetPrefabSpawner`). "Root records in scene contents"
  is written once, privately, in `NetPrefabSpawner`. "Destroy a scene
  instance's members" is written twice (`SceneSpawnService::Pump`,
  `NetPrefabSpawner::Despawn`).

The fix makes each lower contract complete and leaves a smaller adapter that
only ties them together.

## 1. Ownership

| Owner | Responsibility |
| --- | --- |
| Game | Which prefab, where, which partition, when content is ready, optional body preparation, reap veto. |
| `ParticipantLifecycle` | Participant identity; body assignment validation and binding; control; retirement and reap. |
| `SessionParticipantProjection` | Replication marker, peer ownership, and driven-subject facts for every assignment, whichever operation made it. |
| `SceneSpawnService` | Load, publish, cancel, destroy, and (when asked for) a group's lifetime tied to its root. |
| `world/scene` group functions | A scene instance's single root; destroying all of an instance's members; counting root records in scene contents. |
| `ParticipantBodySpawns` | Participant -> pending spawn associations only: start, withdraw, hand off. |

Rule F stays as it is: `participant/` never learns that a body was a spawn, and
`runtime/spawn/` never learns what a participant is.

## 2. Participant assignment

### 2.1 Public API

```cpp
// participant/ParticipantLifecycle.h
enum class ParticipantBodyStatus : std::uint8_t
{
    Assigned,
    AlreadyAssigned,
    Unavailable,
    InvalidParticipant,
    InvalidBody, // dead, invalid, or itself a participant
    BodyInUse,   // another participant's body
};

class ParticipantLifecycle
{
public:
    ParticipantBodyChange RequestBody(World&, EntityId participant);
    ParticipantBodyChange AssignBody(World&, EntityId participant, EntityId body);
    // ...unchanged
private:
    ParticipantBodyChange Bind(World&, EntityId participant, EntityId body);
};

// app/SessionParticipantProjection.h
ParticipantBodyChange AssignBody(World&, EntityId participant, EntityId body,
                                 bool sessionActive);

// app/Engine.h
ParticipantBodyChange AssignParticipantBody(EntityId participant, EntityId body);
```

### 2.2 Invariants

A1. `Bind` is the only code that writes `ParticipantControl::Body`. Both entry
points use it, so validation is the same for both.

A2. `Bind` checks, in order:
1. The participant is alive and has `ParticipantControl`. Otherwise
   `InvalidParticipant`.
2. The participant has no living body. Otherwise `AlreadyAssigned`, reporting
   the existing body.
3. The offered entity is alive and is not a participant. Otherwise
   `InvalidBody`.
4. No other participant holds it as `Body`. Otherwise `BodyInUse`.

It then binds control exactly as `RequestBody` does today, including
displacing another participant that merely controls the entity, and reports
that in `Control`.

A3. `AssignBody` never destroys the offered entity. Ownership moves to the
lifecycle only when the result is `Assigned`. On any other result, cleanup is
the caller's job.

A4. `RequestBody` keeps its current contract:
- It returns early (`InvalidParticipant`, `Unavailable`, `AlreadyAssigned`)
  without calling the policy.
- An entity the policy returns belongs to the lifecycle, and is destroyed if it
  ends up unbound.

The two new refusals are the exception: an entity refused as `InvalidBody` or
`BodyInUse` is not destroyed, because a policy cannot give away a participant
or another participant's body. Today the lifecycle silently rebinds such an
entity. That only happens when a policy has a bug, and no shipped template
does it (section 6).

A5. The projection applies one private `ProjectBody` step to every `Assigned`
result from either entry point: the replication marker when a session is
active, `NetSetOwner` for peer participants, and `ProjectControl`. A refused
assignment projects nothing.

A6. Peer admission keeps asking for a body synchronously: `AdmitPeer` and the
admission paths call `RequestBody` before they return. The policy hook stays
in place for this reason. `AssignBody` adds a way to deliver a body later; it
replaces nothing.

A7. Reap is unchanged. `ReapBody` absent means the body is reaped. Returning
`false` keeps the body alive after the participant retires.

The new `BodyInUse` check scans participants, which is O(participants), the
same cost as the `SetControlSubject` scan the same call already does.

## 3. Root-owned spawn lifetime

### 3.1 Public API

```cpp
// runtime/spawn/SceneSpawnService.h
enum class SceneSpawnLifetime : std::uint8_t
{
    Independent, // members live until destroyed or the group is despawned
    OwnedByRoot, // the root's death despawns the rest of the group
};

[[nodiscard]] SceneSpawnId RequestSpawn(
    std::string_view sceneAssetPath,
    const Transform3f& root,
    StoragePartitionId partition = StoragePartitionId::Default(),
    SceneSpawnLifetime lifetime = SceneSpawnLifetime::Independent);

// The single parentless member recorded at publication. Invalid while
// pending, after failure, for a group published with several roots, and once
// the root has died.
[[nodiscard]] EntityId Root(SceneSpawnId id) const;
```

### 3.2 Invariants

S1. The default is `Independent`, and an independent spawn behaves exactly as
it does today. The console spawn command (`LevelCommands`) and the turret
sample both keep it.

S2. An `OwnedByRoot` spawn has exactly one instantiated root, and that root is
a member of its own published group. This rests on a chain the builder and
importer already guarantee for a spawn:

- `BuildEntityPackageFromSmap` (`SmapFormat.cpp`) creates exactly one package entity
  per scene record. It calls `SetParent` for every record whose `Parent` is not
  `UINT32_MAX`, and fails the whole build when a relation is invalid. So
  package roots are exactly the root records.
- `Parent` is never serialized as a component (`TransformComponents.h`), so no
  record can bring a parent in through its component list.
- A spawn imports with no zone scope (`ZoneId{}`), so `ZoneStateStore`
  suppression never applies. Every package entity is created, and every
  package relation becomes a `Parent` component (`ZonePackageImporter.cpp`).
  The importer is all-or-nothing.
- Every package entity is stamped with the spawn's `SceneInstance` before
  import. The group is therefore exactly the imported entities.

The service checks the scene **before publication**. When the build settles
(owner thread, at the drain), `CountRootRecords(*build->Contents())` must be
exactly 1. Otherwise the request ends as `Failed` with the count in its error,
and nothing is created.

At publication, `SingleRoot` is evaluated over the group's own index members.
By the chain above it cannot fail. If it does anyway, that is an internal
contract violation: it asserts in debug builds, and in release builds the
imported members are destroyed before the request is reported `Live` and the
request ends as `Failed`. The same pattern is in `NetPrefabSpawner::Instantiate`
today.

S3. Publication records the root's generational `EntityId` on the request
record, for every spawn whose group has exactly one root. Because the record is
keyed by `SceneSpawnId` and lives for the session, ownership metadata outlives
the root. A member that is later unparented does not change the recorded root.

S4. Root death is noticed only in `Pump`, never in component hooks. No hook is
added. `ComponentTraits<SceneInstance>` stays the only maintainer of
`SceneInstanceIndex`. `Pump` runs in this order:
1. Publish ready requests in request order.
2. Walk the live `OwnedByRoot` spawns in spawn-ID order, and queue a despawn
   for each one whose root is dead.
3. Execute the queued despawns.

A root destroyed at any point in frame N is therefore cleaned up at frame N+1's
pump, the same latency the adapter's tracking gives today.

S5. A root-death despawn goes through the existing despawn state, so
`IsDespawnRequested` turns true and `Status` reaches `Despawned`. An explicit
`RequestDespawn` of an owned group removes it from the owned list, so it is
never despawned twice. A group whose members were all destroyed (partition
teardown, for example) settles as `Despawned` and logs no error.

S6. The shared group functions live in `world/scene/SceneInstanceGroup.h` and
`.cpp`, as one mechanism: "a scene instance as a unit". Counting root records
is a question about the format's records, so it is declared next to
`SmapContents` in `world/scene/SmapFormat.h`, which already defines what
`Parent == UINT32_MAX` means.

```cpp
// world/scene/SceneInstanceGroup.h
[[nodiscard]] EntityId SingleRoot(const World&, std::span<const EntityId> members);
void DestroySceneInstance(World&, SceneInstanceId);

// world/scene/SmapFormat.h
[[nodiscard]] std::size_t CountRootRecords(const SmapContents&);
```

- `SingleRoot` returns the only living parentless member, or nothing when there
  are zero or several.
- `DestroySceneInstance` destroys every member from a snapshot of the index,
  the pattern `NetPrefabSpawner::Despawn` uses today.

The callers that move onto these functions:
- `SceneSpawnService` (root check, root recording, despawn execution),
- `NetPrefabSpawner` (`Prepare` count, post-import root, `Despawn`),
- arena's turret sample, whose `SpawnedGroupRoot` is deleted and which calls
  `SceneSpawnService::Root` instead.

The owned list costs O(R) per pump, where R is the number of live root-owned
groups. Settled groups cost nothing. Request records gain a `Lifetime` byte and
a root `EntityId`.

S7. Root-owned lifetime never overrides replication authority. A
`SceneSpawnService` acts only on the requests made in its own process.
Replicated entities reach a client through a different path:

- The snapshot spawns them through `INetPrefabSpawner`
  (`ReplicationSnapshot.cpp`), never through `SceneSpawnService`.
- The snapshot destroys them through `INetPrefabSpawner::Despawn` when the
  authority's destruction arrives.

A client's service therefore holds no record for any replicated group, and its
pump cannot destroy one. On the client, the adapter never starts a spawn
either:

- `AdmitLocalParticipant` refuses on a client (`Engine.cpp`).
- `ParticipantControl` is not replicated.

So `RequestBodiesForWaitingParticipants` finds no participants to request
bodies for. On the authority, a root-death despawn is an ordinary
authoritative destruction that replication carries to clients, the same as
today's reap through `RequestDespawnBody`.

## 4. The thinned adapter

It moves from `app/` to its own directory, `participant_spawn/`, next to the
two layers it joins. The class is renamed so "body" cannot be read as a
physics body.

```cpp
// participant_spawn/ParticipantBodySpawns.h
struct ParticipantBodySpawnRequest
{
    std::string ScenePath;
    Transform3f Root = Transform3f::Identity();
    StoragePartitionId Partition = StoragePartitionId::Default();
};

class ParticipantBodySpawns
{
public:
    using SelectSpawn = std::function<std::optional<ParticipantBodySpawnRequest>(
        const World&, EntityId participant)>;
    using PrepareBody = std::function<void(World&, EntityId participant, EntityId body)>;
    // Bind to Engine::AssignParticipantBody to keep session projection.
    using AssignBody = std::function<ParticipantBodyChange(
        EntityId participant, EntityId body)>;

    ParticipantBodySpawns(World&, SceneSpawnService&, Logger&,
                          SelectSpawn, AssignBody, PrepareBody = {});

    // Installed as the lifecycle's ProvideBody, wrapped to answer none.
    void StartSpawn(EntityId participant);
    void Update();
    bool CancelPending(EntityId participant);
    void CancelAllPending();
    void Close();
};
```

Removed:
- `ProvideBody`'s delivery path: the adapter never hands a body back through
  the policy.
- `RequestDespawnBody`, the `Bodies` map, `CollectDeadBodies`, and the
  `Providing` guard.
- The "did the operation consume the attempt" check after each call.

The only state left is `participant -> {SceneSpawnId, path}`.

### 4.1 Invariants

B1. `StartSpawn` submits exactly one `OwnedByRoot` spawn, and only when all
three hold:
- the participant is alive,
- it has no living body,
- it has no pending attempt.

It runs `SelectSpawn` once per attempt. `nullopt` stores nothing and schedules
no retry.

B2. `Update` collects the pending attempts, sorts them by `SceneSpawnId`, and
acts on each one only if a fresh lookup still matches both the participant and
the spawn. A completion is never applied to a newer attempt.

For each matching attempt:

| Observation | Action |
| --- | --- |
| Participant dead, or has a living body | `RequestDespawn` the spawn; forget the attempt. |
| `IsDespawnRequested` (cancelled elsewhere, or root died) | Forget the attempt. |
| `Pending` | Keep the attempt. |
| `Failed`, `Despawned`, or `Unknown` | Log, then forget the attempt. No retry. |
| `Live`, root alive | `PrepareBody`. Look up again, check again. Forget the attempt, then `AssignBody`. If the result is not `Assigned`, `RequestDespawn` the spawn unless a participant holds the root as its body. |

B2a. The adapter never despawns a group whose root a participant holds as its
body, whoever assigned it. `PrepareBody` may assign the root itself, to its own
participant or to another; either way the attempt ends and the group follows
its root (S4). The holder is found with `ParticipantWithBody`, the same query
the lifecycle's `BodyInUse` check uses.

B3. After `Assigned`, the adapter keeps nothing. From then on the group's
lifetime is the root's (S4):
- reap destroys the root, and the next pump destroys the rest;
- a reap veto keeps the root, so the group stays;
- a later death of the root cleans up the group.

B4. Neither the lifecycle nor the projection calls back into the adapter while
`AssignBody` runs. The only callback that can reach the adapter is the game's
`PrepareBody`. It may call `CancelPending`, `CancelAllPending`, or
`RequestParticipantBody`; each of these works on the snapshot plus fresh
lookups. Calling `Update` or `Close` from inside it is a contract error, caught
by an assertion.

B5. `Close` withdraws pending attempts in spawn-ID order, clears the callbacks,
and makes later calls inert. Accepted bodies are not touched. Today's `Close`
also queues despawns for accepted bodies. Those despawns are never executed:
`OnShutdown` runs once, at `Engine::Run` teardown (`Engine.cpp:1469`), and the
scene pump runs only in the frame phases (`EngineFramePhases.cpp:709`).

### 4.2 Template wiring after the change

In each of fps, arena, platformer, and horror:
- `Bodies.emplace(..., AssignBody = engine.AssignParticipantBody)`.
- `ProvideBody = [this](World&, EntityId p) { Bodies->StartSpawn(p); return EntityId{}; }`.
- The `ReapBody` binding is deleted, because absent already means reap.
- `OnShutdown` clears `ProvideBody` and then calls `Close`.

`blank` still constructs nothing.

## 5. Layering, ABI, and documentation

L1. Rule F in `scripts/check_engine_layering.sh` becomes:
- `participant/` may not include `runtime/spawn`, `world/scene`, or
  `participant_spawn`.
- `runtime/spawn/` may not include `participant/`.
- `participant_spawn/` may include both, but may not include `net`, `camera`,
  `controller`, `input`, `zone`, `core/config`, or `app`.

Each rule is checked to fail on a planted violation.

L2. Fingerprint audit (`engine/CMakeLists.txt:322`). There is a gap today:
- `Engine` holds `SessionParticipantProjection` by value.
- That projection holds `ParticipantLifecycle` by value.
- `Engine::Participants()` is inline.

So the offset of `ParticipantPolicies` is compiled into each module, but
`participant/*.h` is not hashed. `runtime/spawn/*.h` puts `SceneSpawnStatus`
and `SceneSpawnLifetime` values across the boundary, and isn't hashed either.

The fix adds `participant/*.h`, `runtime/spawn/*.h`, `participant_spawn/*.h`,
and `world/scene/SceneInstanceGroup.h` to the hashed headers. The rule is that
every type these headers put across the boundary, by value or through member
access, is itself hashed:

| Type | Exposed by | Hashed through |
| --- | --- | --- |
| `World`, `EntityId`, `StoragePartitionId` | all | `ecs/*.h` |
| `SceneInstanceId` | `SceneInstanceGroup.h` | `world/scene/SceneInstance.h` (component header) |
| `ParticipantControl`, `InputActionSourceId` | `ParticipantLifecycle.h` | component header; `input/*.h` |
| `ParticipantBodyChange`, `ParticipantPolicies` | lifecycle, projection, adapter | `participant/*.h` (new) |
| `SceneSpawnId`, `SceneSpawnStatus`, `SceneSpawnLifetime` | spawn service, adapter | `runtime/spawn/*.h` (new); `StrongId.h` |
| `Transform3f` (by value in `RequestSpawn` and `ParticipantBodySpawnRequest`) | spawn service, adapter | `math/geometry/3d/Transform3d.h`, `math/Vec.h`, `math/Quat.h`, `math/Mat.h` (new) |
| `AssetId`, `INetPrefabSpawner` | `NetPrefabSpawner.h` | `core/assets/AssetId.h`; `net/*.h` |
| `std::function`, `std::string`, `std::span`, `std::optional` | adapter, lifecycle | standard library: same-toolchain requirement, as today |

`AssetLease` stays unhashed: it appears only inside a private map's value
type in `NetPrefabSpawner`, so it shifts no member offset that the inline
`ResolvedCount` or `RefusedCount` read. `SmapContents` stays unhashed. No module-facing API hands one to a module: its
only appearance in a hashed header is a private member function of
`LoadedLevel`. `CountRootRecords` is used only inside the engine.

L3. One deliberate ABI bump, `SENCHA_GAME_ABI_VERSION` 25 -> 26, with a v26
entry in `module-abi-history.md`. The entry covers:
- the `AssignParticipantBody` operation,
- the two new body statuses,
- the spawn lifetime parameter and `Root`,
- the renamed and moved adapter,
- the widened fingerprint.

The v20 entry is left as written. There is no forwarding header (pre-1.0:
remove, don't deprecate).

L4. Documents updated:
- [game-module.md](../gameplay/game-module.md): "Bodies from prefabs", plus a
  short paragraph on spawn lifetime.
- [body-spawns.md](body-spawns.md): status line pointing here.
- `scripts/check_engine_layering.sh` comments.
- No entry is expected in `deferred.md`.

## 6. Intended behavior changes

Everything outside this list is meant to stay identical.

| Change | Why it is safe |
| --- | --- |
| A body prefab with several roots now ends `Failed` before publication, instead of being published and then destroyed. | No frame ever observed the doomed entities. Every shipped pawn prefab has one root. |
| The turret sample now requires exactly one root (`SingleRoot`) instead of taking the first. | `turret.sscene` has one entity. Replicated prefabs already require one root on clients (`NetPrefabSpawner::Prepare`). |
| A body whose spawn landed is assigned at the adapter's `Update`. Today an explicit `RequestParticipantBody` made between the async drain and `Update` could take it slightly earlier. | It is still the same frame, in the same frame phase as today's normal path. |
| A policy that returns a participant or another participant's body is refused instead of having it silently rebound. | This happens only when a policy has a bug. No shipped policy does it. |
| `Close` no longer queues despawns for accepted bodies. | Those despawns never executed (B5). |

## 7. Tests

The numbers are for review disposition. Each new regression is checked to fail
for the intended reason before the change it protects.

**Lifecycle** (`test/participant/ParticipantLifecycleTests.cpp`)
- T1. `AssignBody` binds a living entity: `Body`, `ControlSubject`, and the
  input reference are set; the result is `Assigned`.
- T2. Offering a body to a participant that already has a living one returns
  `AlreadyAssigned`. The offered entity stays alive and unbound, and the
  existing body is unchanged.
- T3. A dead or invalid entity returns `InvalidBody`; nothing changes.
- T4. Offering a participant entity returns `InvalidBody`, and that entity
  stays alive.
- T5. Offering another participant's body returns `BodyInUse`. Both
  participants are unchanged, and the entity stays alive.
- T6. An invalid participant returns `InvalidParticipant`, and the offered
  entity stays alive.
- T7. Offering an entity that another participant only controls returns
  `Assigned` and reports `DisplacedParticipant`, as `RequestBody` does.
- T8. Respawn: after the previous body dies, `AssignBody` returns `Assigned`.
- T9. Regression: a `RequestBody` policy that returns a fresh entity after
  retiring its own participant still has that entity destroyed (existing
  contract).
- T10. A `RequestBody` policy that returns another participant's body gets
  `BodyInUse`, and that body is not destroyed. This test fails before the
  change.

**Projection** (`test/net/SessionParticipantProjectionTests.cpp`)
- T11. Table-driven, over both entry points crossed with session on/off and
  peer vs local participant: an `Assigned` result produces identical
  replication marker, `NetOwner`, and `NetDrivenBy` facts.
- T12. A refused `AssignBody` stamps nothing on the offered entity.
- T13. `AdmitPeer` still assigns its body synchronously through
  `ProvideBody`.

**Spawn service** (`test/runtime/SceneSpawnServiceTests.cpp`)
- T14. With the default `Independent` lifetime, destroying the root leaves the
  other members alive across pumps. This pins today's behavior.
- T15. With `OwnedByRoot`, destroying the root destroys the rest at the next
  pump. `IsDespawnRequested` is true and the status is `Despawned`.
- T16. An `OwnedByRoot` spawn of a two-root scene ends `Failed`, with nothing
  published.
- T17. A root destroyed before the first pump after publication is cleaned up
  at the next pump.
- T18. `Root` is invalid while pending, after failure, for a multi-root
  independent spawn, and after the root dies. It is unchanged when a child is
  unparented.
- T19. An explicit `RequestDespawn` of an owned group despawns it exactly once,
  and the owned list is pruned.
- T20. An owned group whose members all disappeared in a partition teardown
  settles as `Despawned` with no error.
- T20a. Replication authority (S7). In one world, a group instantiated by
  `NetPrefabSpawner` (a client's path for replicated bodies) sits next to an
  `OwnedByRoot` spawn. Destroying the net group's root and pumping leaves its
  child alive; only `NetPrefabSpawner::Despawn` removes it. The owned spawn's
  root death still cleans up that group.
- T20b. A root-owned group published on the authority with a replicated root:
  reap destroys the root, the next pump destroys the children, and nothing in
  the client-side path is involved (asserted through the existing arena
  retirement test, T40).

**Group functions** (new `test/runtime/SceneInstanceGroupTests.cpp`)
- T21. `SingleRoot`, table-driven: zero roots, one, two, and dead members
  ignored.
- T22. `CountRootRecords`: zero, one, and two root records.
- T23. `DestroySceneInstance` destroys every member and empties the index
  entry, and tolerates members that are already dead.
- T24. `NetPrefabSpawner::Despawn` still removes the whole group.

**Adapter** (migrated from `test/runtime/BodySpawnsTests.cpp` to
`test/runtime/ParticipantBodySpawnsTests.cpp`)
- T25. Selecting nothing stores nothing and retries nothing.
- T26. A landed prefab is prepared once and assigned through `AssignBody`.
  Afterwards the adapter holds no record.
- T27. A failed spawn is consumed without a retry storm.
- T28. A two-root prefab is refused before publication. This assertion is
  stronger than the old test's "removed after publication".
- T29. A participant that disappears before the spawn lands has the spawn
  withdrawn.
- T30. Cancelling after publication destroys the group without assigning it.
- T31. An old completion never binds to a replacement attempt.
- T32. Retirement with the default reap destroys the whole group by the next
  pump.
- T33. A root destroyed before retirement still has its children cleaned up.
- T34. Reap veto: the root and children outlive retirement, and the root's
  later death cleans up the children.
- T35. An assignment refused after preparation (because `PrepareBody` assigned
  another body) despawns the group, and the adapter keeps nothing.
- T35c. `PrepareBody` assigning the root to its own participant keeps the
  body and its group.
- T35d. `PrepareBody` assigning the root to another participant keeps that
  participant's body and its group; the requesting participant stays without.
- T36. `PrepareBody` cancelling the attempt means no assignment, and the group
  is despawned.
- T37. A repeated `StartSpawn` while an attempt is pending submits no second
  spawn.
- T38. `Close` withdraws pending attempts, leaves assigned bodies alone, and
  makes later calls inert.
- T39. Several participants landing in the same frame are assigned in
  spawn-ID order.

**Template and layering**
- T40. The existing template suites (`test/templates`, including the arena
  turret paths) pass unchanged.
- T41. Each rule F rule fails on a planted include.

## 8. Order of work

One branch off fresh `origin/main`, landed as one change under one ABI bump.

1. Group functions (S6) and their migrations. Behavior-preserving apart from
   the turret tightening.
2. `OwnedByRoot` and `Root` on the spawn service (S1-S5).
3. `Bind` and `AssignBody` through the lifecycle, projection, and `Engine`
   (A1-A7).
4. Adapter thinned, renamed, moved, and rewired in the four templates (B1-B5,
   4.2).
5. Rule F, fingerprint, ABI bump, and documents (L1-L4).

Verification:
- `cmake --preset dev`, the build, and `ctest --preset dev`.
- The layering script, including the planted-violation checks.
- `git diff --check`.
- A live arena run: join, die, respawn, place a turret, and quit.

## 9. Deviations made while implementing

- The adapter and group-function tests live in `test/runtime/`, next to the
  spawn fixtures they share (`SmapSceneFixture.h`), instead of in two new
  single-file test binaries.
- T17 became "only the group whose root died is despawned". The case it
  described, a root dying before the next pump, is what T15 already exercises.
- T35 has two forms: preparation finding another body, and the assign
  operation refusing. Both despawn the group and keep nothing.
- T38 inverts the old `CloseEndsEveryAttemptAndAnswersNothingAfter`
  assertion that `Close` destroys assigned bodies. That is the contract change
  in section 6, row 5; the new test pins that assigned bodies survive `Close`.
- Each template's `OnShutdown` dropped its `ReapBody = {}` line along with the
  binding.

Review after implementation:

- A root claimed during `PrepareBody` was despawned: assigned to its own
  participant, the adapter saw "no longer needs a body"; assigned to another,
  `AssignBody` returned `BodyInUse`. Both destroyed a legitimately held body.
  B2a fixes it, T35c and T35d failed before the fix, and `ParticipantWithBody`
  replaced the lifecycle's private body scan rather than duplicating it.
- Network propagation (T20b). Pawn prefab children carry no replication
  component; only the root is marked, by the session projection. The wire
  therefore carries the root's destruction alone, as before this change, and
  the client sweeps its own group through `NetPrefabSpawner::Despawn`
  (`ReplicationVisibility.DestroyingAPrefabRootTakesItsGroup`, two worlds over
  bytes; `NetPrefabSpawnerTest.DespawningTheRootRemovesTheWholeGroup`). The
  three-process `HostClientProcess.ADedicatedHostServesTwoClientsAtOnce` runs
  a real peer departure through the authority's root-owned path.
