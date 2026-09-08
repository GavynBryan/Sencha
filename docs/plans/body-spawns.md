# Body spawns

Status: landed 2026-09-07. The design below is the one that shipped;
`engine/include/app/BodySpawns.h` and
`test/runtime/BodySpawnsTests.cpp` are the authority where they
and this document differ. The shutdown gate it names landed with it as
`AsyncTaskQueue::Stop` and `test/runtime/SceneSpawnShutdownTests.cpp`.

The change gives a prefab-backed body request one owner from submission through
assignment or cleanup. A game programmer chooses whether to request a prefab and
where to place it. The engine mechanism handles the lifetime of that request and
the resulting group.

The important distinction is between two lifetimes:

- Before assignment, the request belongs to the participant waiting for it.
- After handoff, the prefab group follows the lifetime of its body root.

The second rule matters when a game preserves a body after its participant
retires. Tracking every live group by participant would turn a legitimate reap
veto into delayed destruction by an orphan sweep.

**Evidence and ownership**

The current implementation is split between `OnStart` policy lambdas and
`PawnSpawn.*` in `templates/fps`, `templates/arena`, `templates/platformer`, and
`templates/horror`. Their pending records associate participants with
`SceneSpawnId`; their live records also use participants as keys. Completion
reenters `Engine::RequestParticipantBody`, and retirement queues group destruction
through `ReapBody`.

The relevant existing contracts are:

- [ParticipantLifecycle](../../engine/include/participant/ParticipantLifecycle.h)
  accepts synchronous body policies. An unavailable answer is not automatically
  retried. An existing living body makes another request a no-op.
- [SessionParticipantProjection](../../engine/src/app/SessionParticipantProjection.cpp)
  applies session facts to the outcome of that lifecycle. Completion must use
  this path through `Engine::RequestParticipantBody`.
- [SceneSpawnService](../../engine/include/runtime/spawn/SceneSpawnService.h)
  owns asynchronous scene loading and group destruction. `SceneInstanceIndex`
  owns group membership through component hooks.
- [FrameDriver](../../engine/include/runtime/FrameDriver.h) orders the async
  drain before networking, zone residency, simulation, and frame update.
- [Game-module architecture](../gameplay/game-module.md) leaves admission,
  content readiness, and gameplay policy with games. It permits mechanism
  promotion on independent justification; template duplication alone is
  insufficient, and FPS/arena count as one consumer.

The justification here is a concrete lifetime boundary. A synchronous
participant policy delegates production to an asynchronous scene service, and
somebody must own the outstanding request and its cleanup. `app/` is the layer
that can compose both mechanisms without either lower layer learning about the
other.

| Owner | Responsibility |
| --- | --- |
| Game | Admission, permission to create a body, prefab selection, placement, partition, content invalidation, and optional body decoration. |
| Proposed `BodySpawns` in `app/` | Pending participant/spawn associations, completion handoff, cancellation, and root/group lifetime associations. |
| Existing participant lifecycle and session projection | Participant identity, body assignment, control, retirement policy, and session facts. |
| Existing scene spawn service and instance index | Loading, publication, membership, and queued destruction of scene groups. |

No spawn ID, scene path, scene-service dependency, or prefab component enters
`participant/`. `runtime/spawn/` acquires no participant dependency. The new
mechanism needs no networking dependency of its own.

**Public shape**

Add one concrete class and its small request value in
`engine/include/app/BodySpawns.h`, with implementation in the
matching `engine/src/app/BodySpawns.cpp`.

The following is an API sketch. Includes, private storage, and ordinary special
member declarations are omitted.

```cpp
struct BodySpawnRequest
{
    std::string ScenePath;
    Transform3f Root = Transform3f::Identity();
    StoragePartitionId Partition = PersistentStoragePartition;
};

class BodySpawns
{
public:
    using SelectSpawn = std::function<std::optional<BodySpawnRequest>(
        const World&, EntityId participant)>;
    using PrepareBody = std::function<void(
        World&, EntityId participant, EntityId body)>;
    using RequestBody = std::function<ParticipantBodyChange(EntityId participant)>;

    BodySpawns(World& world, SceneSpawnService& spawns, Logger& log,
                            SelectSpawn select, RequestBody request,
                            PrepareBody prepare = {});

    EntityId ProvideBody(EntityId participant);
    void Update();
    bool CancelPending(EntityId participant);
    void CancelAllPending();
    bool RequestDespawnBody(EntityId body);
    void Close();
};
```

The object borrows one world, its scene spawn service, and a logger. It is neither
copyable nor movable. The two required operations are installed once;
`PrepareBody` is optional. Ordinary `std::function` follows the existing module
policy convention and requires the same matching compiler/standard-library ABI.

`SelectSpawn` is the game decision. It is read-only and runs once when a new
attempt starts, never on each pending frame or again merely because a spawn
finishes. Returning `nullopt` creates no attempt and no future work. The value
provides a complete transform and partition, rather than embedding a player
start convention in the engine.

`PrepareBody` runs once for a successfully produced candidate, before the root
is returned for participant assignment. Arena uses it for `StampNetPrefab` and
its success message; other games may use it for their success message. It is
notified before assignment, so it must not announce that control was assigned.
It does not decide replication ownership or bind control. Arena still stamps
the actual spawned source identity even when no session is active yet.

`RequestBody` is one application operation, bound to
`Engine::RequestParticipantBody`. It exists because the concrete mechanism must
be testable with the real lifecycle and scene service without booting graphics,
networking, input, or an entire `Engine`. Production supplies a one-line lambda;
a focused fixture supplies the equivalent lifecycle or projection call. There
is no interface or registry around this operation.

The object does not install or replace `ParticipantPolicies` itself. The game
retains explicit ownership of those slots. In particular, `BuildParticipant`
and the ability to veto reaping retain their existing meaning.

One small addition is needed on the existing `SceneSpawnService`:

```cpp
bool IsDespawnRequested(SceneSpawnId id) const;
```

Its public `Status` currently reports a queued live despawn as `Live` and a
withdrawn pending request as `Pending`. A consumer cannot tell whether a group
is still available for handoff. The query exposes the existing withdrawal flag
and queued/completed despawn phases; it stores no new state. It is true after
an accepted despawn request, including while work is still running and after
destruction, and false for unknown IDs or attempts never accepted for despawn.
If a withdrawn build subsequently fails, the accepted withdrawal still answers
true. Existing status values and their meanings remain unchanged.

The provider checks this query while observing attempts and immediately before
handoff, including after preparation. A request ended directly through the
scene service therefore cannot be offered as a body just because the next drain
has not removed its entities yet. This is a missing observation on the owning
service, rather than another cancellation flag synchronized by the consumer.

**Request and completion contract**

`ProvideBody` first validates the generational participant identity and its
`ParticipantControl`. It processes an existing attempt before considering a new
selection. If the participant already has a living body, it ends any obsolete
pending attempt and returns no new candidate; normally the lifecycle's earlier
check already handles this case.

| Situation | Result |
| --- | --- |
| No attempt; selection returns none | Return no body. Store nothing and schedule no retry. |
| No attempt; selection provides a description | Submit exactly one scene request, retain its association, and return no body. |
| Existing request remains pending | Return no body without running selection or submitting another spawn. |
| Existing request is live and has one valid root | Prepare the root, revalidate identities, transfer tracking to the root, and return it. |
| Existing request failed or ended | Consume the attempt, report a failure where appropriate, and return no body. A later explicit request can try again. |
| Participant disappeared or acquired another living body | End the outstanding attempt and never assign its candidate. |

Path, transform, and partition are chosen at submission. Later settings changes
do not retarget an in-flight request. A game revokes that request with
`CancelPending` or `CancelAllPending`, then issues a fresh body request if wanted.
The service owns the submitted placement data; the association retains only
the original path needed for diagnostics, not another canonical request image.

`Update` examines only owned attempts and body roots. It collects actionable
items before invoking any application operation. For a live pending attempt it
calls the supplied `RequestBody` once, which reenters `ProvideBody` through the
existing participant policy. Failed or ended requests can be consumed directly:
they need no body-assignment call and must not trigger a new spawn.

The work item carries both participant identity and the original `SceneSpawnId`.
Before acting, lookup must still match both. Cancellation or a new request for
the same participant cannot make an old completion consume the replacement.
New attempts created during callbacks wait for a subsequent update.

After reentry, inspect the structured outcome and the original attempt. If the
operation did not consume that attempt, end it. This covers a replaced policy,
an already-assigned body, and a participant that vanished. An ignored completion
must not become an endless per-frame re-ask. If assignment rejected a candidate,
queue cleanup of that candidate's group, including when the lifecycle already
destroyed its root. A final cleanup pass collects newly dead tracked roots after
callbacks, so a rejection during this update is cleaned up without searching
live records by participant or adding a second root index. Cleanup actions use
the same spawn-ID ordering and fresh lookups as the initial pass.

An explicit body request may also consume a ready attempt before `Update` runs.
Root/group tracking is established before returning the root, so this path is
covered by the same cleanup invariant. A later update observes a root destroyed
by rejected assignment and disposes of its remaining members.

No ECS row pointer, map iterator, or span of group members survives a game
callback or structural operation. Copy IDs, then look up again. Preparation may
add components; the participant or candidate disappearing during preparation
must cause cleanup instead of a stale return. The selector is read-only.
Callbacks must not throw or recursively request bodies from this provider;
`Update` and `Close` are not recursively callable. The intended
`Update -> RequestBody -> ProvideBody` reentry is supported explicitly.

**Root and group lifetime**

Require exactly one live parentless member when selecting the body root. Zero
roots or multiple roots refuse the candidate and queue destruction of the whole
group, with the original path and root count in the diagnostic. Keep this check
local to the body-spawn mechanism: general scene spawning can still
produce multiple roots.

This deliberately tightens the templates' current first-root behavior. It agrees
with [NetPrefabSpawner](../../engine/src/runtime/spawn/NetPrefabSpawner.cpp),
which already rejects prefab sources with other than one root. Inspection found
one root in every shipped pawn prefab: FPS/arena have two members each and
platformer/horror have one each. No content conversion is proposed.

After handoff, store `body root -> SceneSpawnId`, independent of participant
identity. Group membership remains exclusively in `SceneInstanceIndex`.

`RequestDespawnBody` recognizes an owned root, queues destruction through the
scene service, and removes the association. Recognition uses the full
generational ID and works even after that root has died. The return value says
whether this object recognized the body; it does not claim destruction has
already happened. Unknown or procedural bodies are unaffected by this method.

The templates' default reap policy calls this method and returns `true`, retaining
the current immediate root destruction plus queued child cleanup. A custom reap
policy can return `false` without requesting group destruction. Its living root
and children then survive participant retirement together. The provider keeps
tracking that root until it dies or an explicit despawn is requested.

On update, a tracked root that is no longer alive causes group cleanup even if
its participant has already disappeared or never reached retirement. An explicit
respawn after root death can therefore coexist briefly with queued destruction
of the old group without sharing a participant-keyed live record. Transferring
control to a vehicle has no effect on the body's group lifetime.

Changing `Parent` alone does not transfer a member out of its scene instance.
The existing scene-instance membership contract continues to determine which
members a group despawn destroys.

**Cancellation, content, and timing**

`CancelPending` ends only the named participant's outstanding attempt;
`CancelAllPending` ends all outstanding attempts owned by this object. Both also
cover published groups still awaiting assignment. They leave accepted bodies
alone and are idempotent.

An invalid participant is detected on the next provider update and its pending
attempt is ended. Explicit cancellation before scene publication withdraws the
spawn. If publication already happened, cancellation queues group destruction.
There is no claim that polling at frame update can prevent publication that
already occurred at the earlier async drain.

Keep the current settlement phase:

```text
DrainAsyncTasks: scene service publishes and executes queued despawns
PumpNet: participant joins and departures may be processed
ZoneResidency: game handles content readiness or invalidation
Simulate: existing fixed-step work
Update: game spawn system calls BodySpawns::Update
        -> existing participant operation assigns a completed body
        -> presentation consumers run after the spawn system
```

The scene's publication and participant assignment remain distinct operations.
The design guarantees no assignment to a dead participant and eventual group
cleanup; it does not make publication and assignment atomic. Once destruction
is queued, the next scene-service pump performs it. A root destroyed after the
provider's update is discovered on the following update unless its destruction
path called `RequestDespawnBody` explicitly. This latency must be represented in
tests rather than hidden behind sleeps or claims of same-call cleanup.

Keep zone handling in each game's spawn system. On play-content attachment, the
game publishes its content partition and explicitly admits/requests bodies as
today. On detachment, it clears that readiness and calls `CancelAllPending`.
This prevents a request made for departed content from surviving into the next
map merely because its entities use the persistent partition. Accepted body
survival across map changes remains the game's existing policy.

The default persistent partition is only a request-value convenience. A game
can select a zone partition. Its entities follow that partition's normal
teardown; this object does not reinterpret zones or choose a replacement
partition. A request whose group disappears is ended, not automatically retried.

**Game composition and teardown**

The game owns an `std::optional<BodySpawns>` alongside its existing
session state. Construct it in `OnStart`, when the world, spawn service, and
settings are available and before the startup script can request anything.
This avoids registering a scheduled system early or changing the hook sequence.

The essential wiring is explicit. This sketch omits the game's existing
component/input setup and uses proposed game-local function names:

```cpp
BodySpawns.emplace(
    engine.World().Entities(), engine.Spawns(), log,
    [this](const World& world, EntityId participant) {
        return ChoosePawnSpawn(world, participant);
    },
    [&engine](EntityId participant) {
        return engine.RequestParticipantBody(participant);
    },
    [this](World& world, EntityId participant, EntityId body) {
        PreparePawnBody(world, participant, body);
    });

engine.Participants().ProvideBody =
    [this](World&, EntityId participant) {
        return BodySpawns->ProvideBody(participant);
    };
engine.Participants().ReapBody =
    [this](World&, EntityId, EntityId body) {
        (void)BodySpawns->RequestDespawnBody(body);
        return true;
    };
```

The existing game spawn system remains useful: it owns the zone-readiness policy
and delegates its frame update to `BodySpawns->Update()`. Register it through
`OnRegisterSystems` as today, with explicit ordering before frame consumers of
the assigned body. No separate engine scheduler wrapper is needed.

Retain a short amount of this composition code in each template. Selection and
preparation use ordinary named game functions. No common template library is
introduced. `blank` constructs no object and registers no update.

At the start of game `OnShutdown`, clear the game's `ProvideBody` and `ReapBody`
bindings, then call `BodySpawns->Close()` before releasing settings or other game
state captured by its callbacks. `Close` ends all pending attempts, requests
cleanup for all tracked groups, clears callbacks and bookkeeping, and makes
further update/cancellation calls inert. Provision after close returns no body;
body-despawn requests recognize nothing. It does not call game callbacks or pump
the scene service.

Keep the closed optional object alive while the schedule still holds references
to it. This is necessary because `Engine::Run` invokes game `OnShutdown` before
`LoadedLevel::Unload` dispatches final zone-residency notifications. It is safe
to replace the closed object on the next `OnStart` or destroy it with the game
module after the schedule has gone. Destruction of a closed object touches no
engine service; an open object being destroyed is a lifecycle error worth a
debug assertion. This explicit rule avoids relying on a module-static destructor
that runs after the engine is gone.

There is a separate existing shutdown concern that this extraction must not
conceal. `SceneSpawnService::RequestDespawn` withdraws publication but lets work
finish. `ConnectAssets(nullptr, nullptr)` disconnects pointers without joining
outstanding work. Meanwhile `Engine::Run` tears down content before
`Engine::Shutdown` destroys the task queue. `ScenePackageBuild` can retain asset
references and access the asset system during work.

Therefore a deterministic shutdown regression with an outstanding scene build
is a prerequisite verification gate. Closing this provider is not evidence that
the service's borrowed dependencies are safe to destroy. If that regression
confirms the lifetime defect, correct teardown at the engine/content owner:
quiesce producers and finish or discard task work while its loaders, assets,
serializers, and world are alive, then release those owners. Design and review
that ordering correction separately before implementation of it; do not hide a
global task drain, wait loop, or service shutdown inside this optional class.
This proposal specifies the provider's lifetime completely but does not claim
to have resolved the process-wide teardown concern.

**Storage, threading, and diagnostics**

Use two straightforward associative containers keyed by generational entity ID:

- Pending attempts: participant -> spawn ID and original diagnostic path.
- Handed-off bodies: root -> spawn ID.

Use the existing `EntityIdHash`. Retain reusable scratch for actionable work.
Collect first, sort actions by `SceneSpawnId`, then execute with fresh lookups.
Hash iteration must never choose the order of callbacks, new requests, or queued
destruction. Ordinary pending requests and living roots produce no work item.
No second member index or participant-body state is stored.

For P pending attempts, B tracked bodies, and K actionable items, an update costs
expected O(P + B + K log K), plus root inspection of completed groups and the
existing participant operations. Root inspection is O(group members) per
handoff. Individual lookups are expected O(1). Memory tracks outstanding
attempts, living body groups, and retained container/scratch capacity, rather
than spawn history. The scene service retains its existing historical status
records.

All operations and callbacks run on the world owner thread, outside active ECS
queries and lifecycle hooks. The new object schedules no worker tasks, creates
no new concurrency lane, and captures nothing into the service's asynchronous
closures. Loading still uses `SceneSpawnService` and the existing cooked scene
pipeline. Work order is deliberately specified; serial/parallel equivalence
still requires the tests below before any implementation claims it.

Use the supplied logger category instead of a configurable prefix callback.
The selector diagnoses missing game settings or starts; the scene service
diagnoses load/import failures; this object adds participant/request context
once when an attempt ends unexpectedly. Expected cancellation is not a repeated
error. Pending frames emit nothing. Include the original requested path rather
than reading potentially changed settings while reporting a failure.

**Change scope and compatibility**

Implementation adds the concrete header/source and a focused test file. It
also adds the read-only `SceneSpawnService::IsDespawnRequested` query and its
focused service tests, using existing request state. It removes the template
`PendingSceneSpawns` resources, participant-keyed live
lists, pending-spawn switches, and per-frame re-ask loops. Game start selection,
content readiness, presentation, and arena's stamp remain game code. The
turret sample retains its independent possession/placement behavior; its small
root query is not a reason to generalize this class.

Update the game-module documentation's body-request description to distinguish
game-initiated requests from completion of an explicitly delegated request.
Update the four template READMEs and directly affected comments to match the
new composition. Record the intentional changes: root validation, cancellation
on content invalidation, and cleanup after independent root destruction.

Add a boundary check forbidding spawn-service, scene-identity, and `app/`
dependencies in `participant/`, and participant dependencies in `runtime/spawn/`.
The new source should itself contain no game settings, play-zone lookup, player
start types, or networking policy.

No existing participant struct, callback signature, `Engine` layout, virtual
table, component schema, replication format, or cooked format changes in this
design. A new installed `app/` header participates in the current semantic ABI
fingerprint, so the SDK and game modules must be rebuilt together. The explicit
ABI version remains unchanged for this additive API under its documented
deliberate-break rule. Any prerequisite that changes an existing ABI must be
assessed separately, not folded into that statement.

The asset loader, cook path, cache lifetime contract, scene instance hooks,
physics firewall, render extraction, and backend remain their existing owners.
There is no new authored setting, ECS component, editor interaction, or render
path. Those change-path checklists are therefore limited to preserving the
existing vertical path and checking lifecycle integration.

**Acceptance evidence**

Use a headless fixture with the real `World`, `ParticipantLifecycle`,
`SceneSpawnService`, scene serialization, and `AsyncTaskQueue(0)`. Bind the
request operation to the fixture's lifecycle, and add projection-specific
coverage with `SessionParticipantProjection`. These are normal operations on
the concrete mechanism; no test-only constructor or mock spawn-service
interface is needed.

Cover these externally meaningful behaviors:

1. Selection returning none remains idle across updates. A later explicit
   request can begin work. Repeated pending requests submit once; repeated
   requests for an assigned living body do nothing.
2. A two-member prefab returns its unique root. Preparation happens once before
   assignment, and assignment uses the existing session projection where
   composed. Arena stamps the actual prefab identity before hosting.
3. Failure creates no body and causes no retry storm. Changing settings during
   an attempt changes neither its placement nor its failure diagnostic.
4. Zero-root and multiple-root candidates are refused and their entire groups
   are eventually removed. A disappeared group is never approximated.
5. Explicit cancellation before publication prevents publication. Cancellation
   after publication prevents assignment and destroys the group at the next
   pump. Participant departure is tested on both sides of the update boundary.
   Direct service withdrawal and a queued live despawn are observable before
   the pump; neither may produce a body handoff. The service query remains true
   when an accepted withdrawal is followed by build failure.
6. Entity-slot reuse and cancel/re-request for the same participant cannot bind
   an old completion to a new attempt. A different body winning assignment ends
   the abandoned prefab request. Preparation removing the participant or root
   leaves no group behind.
7. Default retirement reaps the body and its children. Destroying the root first,
   then retiring or explicitly respawning, also cleans the old group. The
   regression must reproduce the current skipped-`ReapBody` path before the fix.
8. A reap veto preserves a living root and its group after participant retirement.
   Destroying that root later cleans its children. Possessing a different entity
   never reaps that control subject as part of body cleanup.
9. Content detach cancels pending work and reattachment permits a fresh explicit
   request. Accepted persistent bodies and zone-partitioned bodies retain their
   respective game/service lifetime behavior.
10. `Close` with pending and live work ends ownership, is idempotent, and invokes
    no game callback afterward. Final zone notifications may reach the closed
    object safely. The separate service/content shutdown gate above also passes.

For ordering, exercise the same logical scenario with zero and multiple async
workers, including delayed earlier work, and compare publication, body-assignment,
and cleanup order at matched explicit completion boundaries. Use deterministic
gates/events, not timing sleeps. Exercise zero, one, and many requests; record
representative update cost with idle roots and a completion burst so the
additional root-lifetime observation has a measured cost.

Run all four template checks plus blank, the participant and scene-spawn suites,
session projection, and module ABI/layout/isolation checks. Finish with the
canonical `cmake --preset dev`, `cmake --build --preset dev --parallel`, serial
`ctest --preset dev`, and `git diff --check`. Verify installed-template builds
against the matching SDK because the new header is a module-facing dependency.

The design is acceptable only if changing a spawn location remains a local game
function edit, and investigating abandoned prefab members leads to one concrete
lifetime owner. Implementation should preserve that reading path even when a
few lines of composition remain repeated across templates.
