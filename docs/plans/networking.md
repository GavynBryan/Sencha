# Networking: Sessions, Replication, and Zone-Scoped Authority

Status: ratified design, execution pending (reviewed 2026-07-10, ratified and
corrected 2026-08-07). This is roadmap Track G. The model, the module layout, the
protocol shape, and the security posture below are decided; the phase list in
Section 12 is the execution plan. No networking code exists yet.

Read `docs/plans/engine-roadmap.md` (tracks, gates),
`docs/plans/world-partition/00-execution-overview.md` (binding rules, D10, D14, D17,
D18), `docs/plans/world-partition/11-zone-runtime-model.md` (the canonical world
graph runtime contract), and `docs/plans/runtime-stable-identity.md` (the one entity
identity scheme this track consumes) first.

**What changed between review and ratification.** The original review listed five
constraints that had to bind the v1.0 tracks before they calcified (Section 3). Four
of them have since shipped on their own tracks: the paced fixed-tick scheduler, mover
state living in components, tick-stamped input action records, and one stable entity
identity. Section 3 now records each as a landed constraint rather than a request,
and names the single piece still outstanding (a headless frame loop). Section 1 has
been re-grounded against the current tree, which moved underneath the review in three
ways that matter: the frame is eleven phases rather than ten, zones are storage
partitions of one `World` rather than a registry each, and the stable id is
`PersistentEntityId` rather than the `SerializedEntityId` this document proposed.

The owner also decided, after review, that the debug console ships in every build
rather than being stripped from shipping binaries. That inverts this document's
original reasoning about cheat gating: see Sections 9 and 10.8.

---

## Why

The user-facing requirement: a small group of players can host and join a session of a
Sencha game (one of them hosting, or a headless host), traverse the same streamed
world together, and interact through the same abilities, physics, and world state the
single-player game has, without the game developer writing netcode. A designer marks
what replicates the same way they mark what serializes: on the component schema. A
player with a bad connection sees other players smoothly and their own character
responsively. A player cannot cheat another player by editing memory, packets, or
cvars.

The engineering translation, per directive 2: this is not a "multiplayer mode" bolted
beside the game. It is an authority boundary drawn through the existing substrate: one
machine's simulation is authoritative; other machines mirror its replicated state and
contribute only inputs and validated requests. Single-player is the degenerate case of
the same shape (a session with zero remote peers), not a separate code path.

---

## Verdicts up front

**1. Server-authoritative replication, not deterministic lockstep. Determinism still
pays, three ways.** Lockstep looks tempting because the engine is deterministic, but
it loses on this engine's own architecture, not on taste (Section 2.1): per-client
zone residency means no peer even holds the whole world; drop-in join needs state
transfer anyway, so snapshots get built regardless; and the engine's determinism
guarantee is serial-vs-parallel within one process, not bit-identical floats across
CPUs, compilers, and Jolt builds. What determinism actually buys networking:
prediction resimulation that converges exactly, loopback tests that assert
byte-identical replication streams, and cheap state-hash desync detection
(Section 2.2).

**2. Replication is the tractable third, as suspected, because four load-bearing
mechanisms already exist.** Components are trivially copyable PODs
(`World.h:95` static_assert) enumerated by `TypeSchema` fields; component identity is
already cross-build stable (`ComponentTypeId` is a constexpr FNV-1a of a declared
name, `ComponentTypeId.h:30-41`); change detection already narrows what to scan
(per-chunk per-column write counters, `Chunk.h:49`); and the world is already
partitioned into per-zone storage partitions that make natural replication scopes. What does
not exist and must be built: a stable per-entity identity (nothing survives the wire
today; `EntityId` is a per-World index plus generation, `EntityId.h:11-18`), a
delta/ack pipeline, quantization metadata, and remap tables for the ids that are
deliberately not stable across processes (gameplay tags, attribute ids). Sections 5
and 6.

**3. Ownership: authority is the server's, whole. Clients own exactly two things: an
input stream and validated requests.** Per-entity state is server-simulated; the
client's pawn is the one entity a client predicts locally, and prediction is a
client-local replay, never authority (Section 7). No client-authoritative transforms,
no shared-authority objects, no host migration in v1 (trigger recorded in
Section 13). The enforcement mechanism is composition, not branching: clients simply
do not run authority systems, and replicated zones on a client carry participation
without Logic, so gameplay systems never see them (Section 7.2).

**4. Security's spine is three cheap structural facts, then hardening.** (a) Clients
send inputs and requests, never state, so the class of "teleport/health/ammo hacks"
requires no anti-cheat code at all: the server simply never reads state from the
wire. (b) Validation of requests is the same data that drives gameplay (AbilityKit
tag/cost checks, `MovementProfile` limits), so server-side validation is the
simulation itself, not a parallel rule set (directive 3). (c) Interest is zone-scoped,
so a client is never told about state outside its granted zones, capping wallhack
class leaks at zone granularity. The trust boundary is symmetric: the client treats
authority bytes as hostile too, and the wire carries values, never code, content,
or commands, so a compromised server has no channel through which to run anything
on a client; server build identity is graded honestly (mutual verification always,
build signatures if server binaries stay first-party, platform attestation as the
only strong form) and client safety never depends on identity claims
(Section 10.4). On top of that: hardened pure decoders with fuzz tests in both
directions, a stateless handshake cookie, rate and byte budgets, and transport
encryption as an owner decision (Section 10).

**5. Cvars: one new enforced axis (who may write in a session), one existing axis
finished (Cheat), and the editor question dissolves.** The console already has the
metadata surface (`CVarFlags`, `ConsoleTypes.h:32-44`, with `Cheat`, `Developer`,
`Unsafe` defined but enforced nowhere). Proposal: a `Replicated` flag (authority
writes, clients mirror read-only), `Replicated|InitOnly` verified at handshake
(tick rate class), and `Cheat` finally enforced against a session gate. Rendering
cvars stay untouched local. Editor cvars need no new dichotomy: editors are separate
processes with their own `ConsoleService` instances and `Owner = "editor"`; a session
never sees them. Section 9.

**6. Streaming is not the hardest part; it is the part the zone work already shaped
correctly.** The demand policy is pure and deterministic, residency is per-zone
registries, participation is data, and the runtime is game-pumped. Multiplayer needs
exactly one policy generalization (N focus sources instead of one, merged by minimum
hop; Section 8.1) plus one genuinely new protocol (per-peer zone grant/ack so the
server never replicates entities into a zone the client has not finished loading;
Section 8.2). Late join, teleport travel, and mid-session zone entry all reduce to
that same grant path (Section 8.3). The zone review's cost budget becomes the server's
multi-player residency budget with no changes (Section 8.4). The actual hard parts of
this plan are elsewhere: the tick scheduler prerequisite (Section 3.1) and prediction
against Jolt-backed character movement (Section 7.4).

**7. The real reason "now is the time": five cheap constraints on other tracks.**
Most of the cost of retrofitting multiplayer into a shipped engine is not writing
netcode; it is that input, animation, AI, scripting, and saves baked in
single-player assumptions. Section 3 pins the assumptions to forbid now: a paced
fixed-tick scheduler; authoritative gameplay state lives in components, never in
system-local members (the character mover's vertical velocity is today's concrete
violation, `CharacterMover.h:11-18`); input actions are tick-stamped POD records;
one stable entity identity shared by save overlays and replication; scripts and AI
mutate authoritative state only inside fixed-tick systems.

---

## 1. Grounding

Verified against the tree at ratification (2026-08-07).

- **Frame and time.** `FrameDriver` runs a fixed eleven-phase frame
  (`FrameDriver.h:18-32`): PumpPlatform, ResolveLifecycle, RebuildGraphics,
  DrainAsyncTasks, ZoneResidency, ScheduleTicks, Simulate, Update,
  ExtractRenderPacket, Render, EndFrame. Simulation consumes `FixedSimTime` only
  (constant dt, monotonic `TickIndex`); wall time is presentation-only. The tick
  scheduler is a wall-time accumulator: `FixedStepScheduler::Advance` returns a
  `FixedStepPlan` of 0..N ticks for the frame, capped at `MaxTicksPerFrame` (4) with
  whole ticks beyond the cap dropped rather than deferred, and `Simulate` runs that
  many inner ticks (`RuntimeFrameLoop.cpp:126-155`). The presentation `Alpha` is a
  real sub-tick residual and `WorldTransformHistory` interpolates poses between
  ticks. Sim rate is decoupled from frame rate.

  The review found this locked 1:1 with alpha hardcoded to 1.0, and made fixing it
  a binding constraint (Section 3.1). It shipped in the interim, which is why
  prediction replay and interpolation below can assume both.
- **Scheduling.** `EngineSchedule` topologically sorts systems per phase method
  (FixedLogic, Physics, PostFixed, FrameUpdate, ExtractRender, Audio, EndFrame),
  ties broken by registration order; systems run serially in order
  (`EngineSchedule.h:144-204,241-289`). The deterministic reference is
  `JobWorkerCount == 0`; the parallel paths are asserted bit-identical
  (`TransformPropagation.h:63`, `test/runtime/ZoneParallelTests.cpp:166`).
- **ECS.** `EntityId` is `{uint32 Index, uint32 Generation}`, valid only within its
  owning `World` (`EntityId.h:11-18`). `ComponentId` is a per-World
  registration-order uint16 signature bit; the cross-module, cross-build identity is
  `ComponentTypeId`, a constexpr FNV-1a hash of a declared stable name
  (`ComponentTypeId.h:9-41`). Components must be trivially copyable
  (`World.h:95`). Change detection is per-chunk per-column last-written frame
  counters (`Chunk.h:43-51`), bumped whole-chunk by `Write<T>` (`Query.h:241-260`);
  queries filter against a caller-supplied reference frame (`Query.h:107-114`).
  `CommandBuffer` supports Add/Remove/Destroy/Create only; buffered `CreateEntity`
  cannot carry initial components (`CommandBuffer.h:116-133`). Scene loading creates
  entities directly, not through a CommandBuffer (`SceneSerializer.cpp:539`).
- **Serialization.** Schema-driven: `TypeSchema<T>` fields visited by
  `ComponentSerializer<T>`, special field types through `SceneFieldCodec`
  (asset handles serialize as paths, cooked form as `{id, path}` with id-first
  resolution, `SceneFieldCodec.cpp:84-166`). The production cooked format is JSON;
  binary exists but asset handles reject it and dynamic arrays do not round-trip
  (`SceneFieldCodec.cpp:26-38`, `Archive.h:226`; Track F item 3 owns finishing it).
  Cooked scenes now carry a stable per-entity id (`PersistentIdComponent`, chunk
  `'PSID'`), so the file-order remap the review found is no longer the only handle
  on an authored entity. See Section 3.4.
- **Identity that is already wire-safe.** `AssetId` is cook-minted, persisted in
  `asset_ids.json`, deterministic and rename-stable (`AssetIdMap.h:53-65`). Zone,
  region, and transition ids are editor-minted persisted 64-bit `StrongId`s carried
  verbatim in the cooked manifest (`ZoneId.h:5-8`). `PersistentEntityId` is an
  editor-minted 64-bit `StrongId` carried verbatim through the cook and resolved to
  a live `EntityId` at load, with bit 63 reserved for runtime minting
  (`core/identity/Id.h:37-47`). `ComponentTypeId` per above.
- **Identity that is deliberately not.** Gameplay tag ids are per-World
  registration-order values; names are the stable form
  (`GameplayTagSerialization.h:6-10`); the registry is a per-World resource
  populated imperatively (`MovementRegistration.cpp:29-35`). Attribute, ability, and
  effect ids follow the same pattern (`AttributeId.h:9`, `abilitykit.md` D-H).
- **Zones.** One `World` whose zones are storage partitions of it, not a registry
  each: `RuntimeWorld` maps `ZoneId` to `StoragePartitionId`, and partition zero is
  persistent, participates in every span, and hosts the pawn and camera. Chunks never
  mix partitions, so a `StoragePartitionSet` filters iteration at one branch per
  chunk — which is what makes a per-peer replication scope cheap (Section 8).
  Ownership is structural: an entity lives in exactly one partition; nothing assigns
  membership by position. Demand is a pure
  deterministic policy over a single focus zone id plus optional focus position,
  pins, config, and world tags (`ZoneDemand.h:100-117`); `WorldPartitionRuntime` is
  game-owned and game-pumped once per frame (`TemplateGame.cpp:259-271`), issues
  dormant loads, converges participation, lingers, destroys. Participation is four
  bits (Visible, Physics, Logic, Audio) compiled to the frame's
  `FrameZoneView` partition sets, which gate which partitions each phase sees
  (`ZoneParticipation.h`).
- **Physics.** Jolt behind a PIMPL, linked PRIVATE. One shared `PhysicsWorld` for
  all zones, stepped once per fixed tick, single-threaded
  (`PhysicsStepSystem.h:12-21`, `PhysicsWorldImpl.h:31`). Per-registry
  `PhysicsScene` bridges ECS to bodies and removes them on zone destruction.
  Character movement is a custom kinematic capsule (`CharacterMover`) over Jolt's
  CharacterVirtual; the mover owns vertical velocity as pool-internal state, outside
  any component (`CharacterMover.h:11-18`, `CharacterMoverPool`). No determinism
  claim or test exists for physics.
- **Input.** `InputFrame` held state plus edge lists, sampled in PumpPlatform,
  edges drained on the first fixed tick and preserved across zero-tick frames
  (`FrameDriver.cpp:52-57,116-118`). No action mapping layer exists (Track A item 1,
  unbuilt).
- **Console.** Per-Engine `ConsoleService` owning a `ConsoleRegistry`; dotted
  lowercase cvar names; flags per Verdict 5 with `Cheat`/`Developer`/`Unsafe`
  currently unenforced; `OnChange` callbacks and per-set source provenance exist
  (`ConsoleTypes.h:141-170`, `ConsoleRegistry.cpp:496-537`). Sim-affecting cvars
  exist today: `time.timescale` and `time.fixed_tick_rate`. The `movement.*` tuning
  cvars the review found here are gone: movement tuning is now authored in
  `movement.profile` data assets resolved through `MovementProfileBindingCache`, so
  it is content covered by the world-identity gate rather than cvar state.
- **Boot and boundary.** `Engine` is the concrete composition root owning services
  as members (`Engine.h:125-156`); the game binary boundary is the `Game` module ABI
  (one C factory plus an ABI descriptor, version 5, `GameModuleAbi.h:129-134`) with
  a SHA-256 fingerprint over module-facing headers (`engine/CMakeLists.txt:82-128`).
  Headless (`GraphicsApi::None`) boots services, task queue, and job pool, then
  returns before creating Platform, Graphics, or FrameDriver, so a headless engine
  cannot tick a frame today (`Engine.cpp:101-105`, `Engine.cpp:336-341`); the
  FrameDriver itself is renderer-agnostic and is stepped headless in tests.
- **Cook identity.** The manifest's per-zone `content_hash` is a brush-geometry
  freshness hash, not a full content identity: non-geometry component edits do not
  change it (`DocumentCook.cpp:38-52`). A join handshake cannot rely on it alone.
- **Tests.** GoogleTest, eleven suite targets plus fitness ctests
  (`test/CMakeLists.txt`); most tests construct `World`/`RuntimeWorld` directly. Exactly one engine-wide mutable process global exists:
  `DefaultComponentSerializerRegistry()` (`SceneSerializer.cpp:289-293`).
- **Precision note.** `Vec3d` is float-backed (`math/Vec.h:382`); all positions on
  the wire are floats, and quantization decisions start from that.

---

## 2. The model: authoritative sessions, not lockstep

### 2.1 Why lockstep loses here

Deterministic lockstep (send only inputs; every peer simulates everything) is the
right call for engines whose whole world is resident on every machine and whose
sessions start together. Three properties of Sencha, each deliberate, disqualify it:

1. **Nobody holds the whole world.** Residency is per-focus zone demand with a cap
   and a linger; a client near the Hub has the Arena unloaded. Lockstep requires
   every peer to simulate every zone that any peer can affect, which means resident
   worlds union-of-everyone, destroying the streaming model the engine is built
   around (and the zone review's cost budgeting with it).
2. **Drop-in join requires state transfer regardless.** The target games are
   action-adventures with co-op shapes, not synchronized-start matches. Joining a
   running session means serializing authoritative state to the joiner, which is
   exactly the snapshot machinery lockstep exists to avoid building. Once snapshots
   exist, input-sync buys only bandwidth, and Section 6's interest-scoped deltas
   already bound that.
3. **The determinism guarantee is the wrong shape.** Sencha's determinism is
   serial-equals-parallel within one process and one binary (Section 1). Lockstep
   needs bit-identical float behavior across different CPUs, OSes, and a Jolt build
   that makes no such promise here (no determinism claim exists in the physics
   layer). One divergent low bit desyncs the session unrecoverably without, again,
   snapshot transfer.

Rollback netcode (predict everything, rewind everything) fails on the same residency
grounds plus whole-world state capture per tick. It stays a non-goal (Section 13).

### 2.2 What determinism actually buys

- **Exact prediction replay.** Client prediction (Section 7.4) replays the same
  fixed-tick systems over stored inputs after a correction. Deterministic systems
  make the replay converge to exactly the server's result when inputs match, so
  corrections are rare and small instead of endemic.
- **Byte-identical test assertions.** The replication writer is a pure function of
  (registry state, ack state, budget). Tests assert identical snapshot byte streams
  across worker counts and across runs, the same way `ZoneDemandTests` asserts
  identical demand records. Netcode inherits the engine's strongest testing idiom.
- **Cheap desync detection.** Authority hashes replicated component state per zone
  at intervals; clients hash their applied view and compare (Section 10.7). Any
  mismatch is a replication defect caught in development, not a mystery.

### 2.3 Session roles and topologies

One session model, three configurations, one binary:

- **Host**: authority plus a local player. The listen-server case and, with zero
  remote peers, exactly single-player. No session constructed means no networking
  code in the frame at all.
- **Client**: mirrors replicated state, sends inputs and requests, predicts its own
  pawn.
- **Headless host**: authority with no local player, no platform, no graphics; the
  dedicated server. Requires the headless frame loop (Section 3.1); participation
  bits it never consumes (Visible, Audio) are inert because the consuming systems do
  not run, so no per-role participation mechanism is needed (directive 4: no seam
  without a measured need; revisit trigger: profiling shows meaningful cost from
  Visible/Audio bookkeeping on a headless host).

Roles are composition, not branches: the game's `OnRegisterSystems` receives the
session role and registers the appropriate system set (Section 7.2). The engine does
not scatter `if (IsServer)` through systems; a system either runs in a role or is not
registered in it.

---

## 3. What must bind now (constraints on the v1.0 tracks)

These were the retrofit hazards: each cheap to hold at review time and expensive
after the dependent track shipped. They were the concrete content of "if we want
multiplayer, now is the time." None of them builds netcode.

Four of the five have since landed on their own tracks. They are kept here, marked,
because the rest of this document depends on them and because the reasoning explains
why the shape is what it is. One item remains outstanding.

### 3.1 A paced fixed-tick scheduler, and a headless frame loop

**Scheduler: SHIPPED.** The review found the tick scheduler locked one-per-frame,
coupling simulation rate to display rate. Networking cannot exist on that: server and
client must tick at the same configured rate regardless of their frame rates, and the
client must be able to run 0, 1, or 2 ticks in a frame to converge on the authority's
clock.

`FixedStepScheduler` is now a pure wall-time accumulator returning a `FixedStepPlan`
of 0..N ticks with a `MaxTicksPerFrame` cap, dropping whole ticks beyond the cap
rather than deferring them so a stall falls behind instead of spiralling.
`BuildPresentationTime` carries a real fractional alpha and `WorldTransformHistory`
interpolates render poses between ticks. Rate and clamp are `EngineRuntimeConfig`
fields. The 0..2-tick elasticity Section 7.3 uses as its clock-convergence actuator
is therefore already available.

**Headless frame loop: OUTSTANDING. This is the one piece of Section 3 still owed,
and it is Section 12's first execution phase.** A headless engine still cannot tick:
`Engine::Initialize` returns early on `WindowGraphicsApi::None` (`Engine.cpp:119-123`)
before `FrameDriverInstance` is constructed, and `RegisterFramePhases` is entirely
inside `#ifdef SENCHA_ENABLE_VULKAN` (`EngineFramePhases.cpp`). Headless boot must
construct the FrameDriver and register a simulation-only phase set (DrainAsyncTasks,
ZoneResidency, ScheduleTicks, Simulate, Update, EndFrame; no platform, graphics, or
render phases). This is the dedicated host's skeleton and equally the CI
simulation-soak skeleton the roadmap's llvmpipe concern already wants.

Tests: the accumulator unit tests exist. Still owed: a headless `FrameDriver`
scenario test running fixed ticks with no graphics, and the traversal tick-budget
assertions re-run headless.

### 3.2 Authoritative gameplay state lives in components — SHIPPED

Replication, save overlays, and prediction rewind all serialize the same thing:
component data (plus explicitly registered resources). Any authoritative state that
lives in system members or pools is invisible to all three. The rule to adopt now,
enforced in review: **if losing a value would change gameplay, the value lives in a
component (or a registered serializable resource), not in a system object.** Systems
own caches and scratch, never truth.

The review's concrete violation was `CharacterMover` owning vertical velocity inside
the pooled mover object, so a character's airborne state could not be snapshotted,
replicated, or rewound from its components. That is fixed: the mover owns no
velocity, and gravity, jumps, and every other contribution are composed into the
request by the movement pipeline (`CharacterMover.h`). Movement's authoritative state
is components (`KinematicState`, `SupportState`, `CharacterMovement`,
`LocomotionOutput`, `MotionRequest`, and siblings in `MovementComponents.h`).

The rule stands for everything still to come. Animation (Track A item 2) and AI
(item 7) will make the same choice for graph state and perception facts, and they
should make it under this rule.

### 3.3 Input actions are tick-stamped POD records — SHIPPED

The review caught Track A item 1 (input action mapping) before it defined the shape of
player intent, because that shape is also the client-to-server wire format
(Section 7.3). It shipped with exactly the required shape:
`InputActionTickRecord { uint64 Tick; span<const InputActionValue> Values }`, where
`InputActionValue` is a 9-byte POD of `{float X, float Y, InputActionFlags}`
(`InputActionState.h`). Records are buffered in an 8-deep ring
(`kHistoryCapacity`), which is directly the redundancy window Section 7.3 sends.
`InputActionScope::Simulation` marks the actions a deterministic player command may
carry, which is the projection filter the command builder uses. Gameplay reads
actions, never `InputFrame` — enforced by `scripts/check_gameplay_input.sh`.

**The constraint that came with it:** `InputActionId` is a registration-order runtime
value and must never be serialized or sent on a wire; names are the stable identity.
The handshake syncs the ordered action name table and the wire speaks authority
indices (Section 5).

One seam this leaves for Section 7.3 to close: `InputActionResolveSystem` is today
the only producer calling `InputActionState::BeginTick`, and it holds its two-clock
latch privately. An authority consuming a remote peer's records needs a second
producer that does not go through that latch.

### 3.4 One stable entity identity, shared with the save overlay — SHIPPED, differently

Track C item 5 (stateful detach, `ZoneStateRecord`) needs stable entity identity for
its created/destroyed/changed overlay records. Replication needs the same thing for
baselines (Section 6.1). The binding rule was: **there will be exactly one stable
scene-entity identity scheme**, used by the overlay, the save system (Track A item 8),
and replication. Minting a second was the overview's stop-condition and would have
been this plan's first architectural defect.

The overlay track landed it first, and it is **not** the `SerializedEntityId` this
document proposed. The scheme is `PersistentEntityId`
(`docs/plans/runtime-stable-identity.md`): a 64-bit `StrongId` minted by the *editor*
when an entity is authored — not by the cook — serialized per entity in scene
documents, carried verbatim through the cook, and resolved at runtime through the
`PersistentEntityIndex` world resource. Runtime `EntityId` values are never
persisted. `PersistentIdComponent` is registered last in `ComponentManifest.h`
(manifest order is serialized state; append only). `SerializedEntityId` no longer
exists.

Two consequences for this track, both load-bearing:

- **Authored entities need no spawn messages.** Both ends share the same authored
  ids, so a zone grant plus a baseline delta fully describes authored content
  (Section 6.1).
- **Bit 63 is reserved, and this track does not get to use it.**
  `PersistentEntityIdRuntimeBit` is reserved for a future runtime *persistent* mint
  allocator (the save system's dropped-item case). `NetEntityId` is session-transient
  and separate; the wire id is a tagged pair of the two rather than an overload of
  the reserved namespace (Section 6.1).

### 3.5 Posture rules for scripting, AI, the HUD, and CI

- **Scripting (Track A item 3):** scripts that mutate authoritative state run inside
  fixed-tick systems on the authority; client-side scripts are presentation-only.
  The already-pinned constraints (no wall clock, no unseeded randomness, seams-only
  API) are exactly the networking constraints; add the role rule to the script API
  fitness test when the VM lands.
- **AI (Track A item 7):** perception facts and behavior selections are components
  and tags (already the plan), which makes AI state replicate and save for free.
  Nothing extra to build; just do not violate 3.2.
- **Game UI/HUD (Track A item 9):** the HUD binds component fields through
  reflection (its own plan), and in a session those fields are simply the client's
  replicated (or locally predicted) view, so the HUD gains no networking awareness
  at all: HUD rendering is always client-side, HUD truth is always authority-side,
  and there is no HUD protocol. Per-player private readouts (cooldowns, resource
  internals) ride the `.OwnerOnly()` field annotation (Section 6.2), which keeps
  them off other clients' wires. A headless host runs no HUD because nothing
  registers one there. The posture rule item 9 inherits: the HUD reads components,
  never system internals (rule 3.2 again).
- **CI and determinism gates (Track E items 1 and 2):** these stop being
  quality-of-life and become networking prerequisites. The serial-vs-parallel
  state-hash gate is the same machinery as desync detection (Section 10.7); build it
  once there.
- **Randomness:** gameplay currently uses none (runtime mints no random ids,
  overview rule 9). When seeded randomness arrives (scripting item), it must be a
  per-registry seeded stream keyed by tick, never `std::random_device`, or
  prediction replay (Section 7.4) breaks. Record this beside the script RNG design.

---

## 4. Where networking sits

### 4.1 Module layout and layering

New module `engine/include/net/` + `engine/src/net/`, below `app`, beside `zone`:

- `net/NetTransport.h`: the `INetTransport` seam (Section 4.3) and its
  implementations (`UdpTransport`, `LoopbackTransport`, `SimulatedTransport`).
- `net/NetProtocol.h/.cpp`: pure message encode/decode over spans. No sockets, no
  engine services; the fuzz target (Section 10.2).
- `net/NetSession.h/.cpp`: session and peer lifecycle, channels, handshake, clock
  sync. Owns per-peer state keyed by `PeerId` (`StrongId<PeerIdTag, uint32_t>`,
  session-transient, minted by the authority).
- `net/Replication*.h/.cpp`: the snapshot writer (authority) and applier (client),
  the identity tables, interest bookkeeping (Sections 6 and 8).

Dependency direction: `net` may include `ecs`, `world`, `zone`, `core`,
`gameplay_tags`; nothing below `app` includes `net`. The zone layer gains no
networking vocabulary: the multi-focus generalization (Section 8.1) is expressed in
zone terms (`FocusSourceId`), and the net layer maps peers onto it. Renderer, audio,
physics, and editor never reference `net`. Editor and cook code contain no
networking; PIE integration (Section 12, phase G7) only adds launch arguments.

`NetSession` is engine-owned in the `PlatformServices` pattern: an optional `Engine`
member, null unless the game hosts or joins, with `TryNet()` access. Replication
binding to game content (which components, via schema annotations; the zone recipe;
role system sets) stays game-side and data-side.

### 4.2 Frame integration: two new phases

The frame gains two phases (eleven becomes thirteen), registered like the existing
ones and in the headless set:

- **`PumpNet`**, between DrainAsyncTasks and ZoneResidency: drain the transport
  (nonblocking), decode, dispatch control messages, buffer inputs (authority) and
  snapshots (client), update clock-sync estimates. It must precede ScheduleTicks
  because the tick scheduler consumes the clock-sync estimate to run 0..2 ticks and
  converge on the authority's timeline (Section 7.3). It precedes ZoneResidency
  specifically so a zone grant arriving this frame reaches residency processing in
  the same frame rather than the next one (Section 8.2).
- **`FlushNet`**, between Simulate and Update: authority serializes snapshots from
  post-tick state and sends; clients send the tick's inputs and acks. State is
  complete for the tick and presentation has not consumed it yet.

Rationale for phases rather than scheduled systems: game systems cannot run before
ScheduleTicks (the earliest system phase is inside Simulate), and socket I/O timing
is frame structure, which is `FrameDriver`'s single responsibility. When no session
exists both phases are no-ops through the null service, the same shape as headless
platform phases. This is an engine-owned change and is called out for ratification
(the world-partition suite deliberately avoided adding phases; networking is the
boundary that finally justifies one).

Connection establishment (DNS, dialing) runs on `AsyncTaskQueue` and commits at
DrainAsyncTasks. No third concurrency lane: sockets are nonblocking and pumped on the
main thread; at 60 Hz that is at most one frame of added latency, which the jitter
buffer already absorbs. No dedicated socket thread unless profiling of a real session
shows PumpNet exceeding its budget (recorded trigger; the answer then is still the
async lane, not a bespoke thread).

### 4.3 The transport seam

`INetTransport` is a narrow datagram interface: bind/dial, send datagram to peer
address, drain received datagrams, local address introspection. Justified under
directive 4 three times over: a platform boundary (POSIX sockets now, winsock with
Track F item 1, a platform relay service later), a test boundary (`LoopbackTransport`
for in-process pairs, `SimulatedTransport` wrapping any transport with seeded,
deterministic loss/reorder/duplication/jitter schedules), and a real second
implementation on day one.

Above the seam, `NetSession` implements exactly two channel classes over datagrams:

- **Unreliable-sequenced** (newest wins, stale dropped): snapshots, inputs, cues.
- **Reliable-ordered** (windowed ack, resend, fragmentation for oversized payloads):
  handshake, tables, zone grants and baselines, cvar sync, disconnects.

Scope honesty: this is a small, well-understood reliability layer for 2..8 peers and
kilobyte-scale control traffic, not a general congestion-controlled stream; a
per-peer token-bucket send budget (Section 10.6) stands in for congestion control at
this scale. MTU is pinned conservatively (1200 bytes) with fragmentation only on the
reliable channel. If the owner prefers a proven dependency for this layer instead,
the seam is where it swaps in; the recommendation is to own it, matching the
engine's dependency posture (Jolt-style: one vetted dependency where the domain is
deep; this domain, at this scale, is not).

---

## 5. Session: handshake, identity, tables

Join flow, all messages length-prefixed and cap-checked (Section 10.2):

1. **Cookie challenge.** Client sends Hello; server replies with a stateless HMAC
   cookie of the source address and a rotating secret; client echoes it. No session
   state or allocation exists server-side until the echo verifies (SYN-flood and
   spoofed-source amplification are dead before they start).
2. **Compatibility gate.** The verified hello carries: protocol version (a constant
   this plan mints, bumped on any wire change; no cross-version sessions, Track F
   item 6's refuse-with-message posture), engine version, game module ABI
   fingerprint (already computed at build, `engine/CMakeLists.txt:82-128`), and a
   world identity. World identity is a hash the runtime computes at
   `LoadManifest` time over the cooked manifest bytes plus each zone's cooked scene
   and collision bytes as loaded; the existing `content_hash` is a cook-freshness
   signal, not a content identity (Section 1), so the session mints its own.
   Mismatch refuses with a reason string. The gate is mutual: the server's accept
   carries its own identity tuple and the client refuses a mismatch the same way,
   before applying any state (Section 10.4 grades what identity can and cannot
   promise; the optional build signature slots into this exchange when the hosting
   model justifies it, Open question 6).
3. **Table sync.** The wire speaks compact ids; the handshake makes them safe:
   - **Gameplay tags and attributes:** ids are per-World registration-order values
     (Section 1). The authority sends its ordered name list hash; matching binaries
     and modules produce matching orders, so the common case is verify-and-go. On
     mismatch the authority sends the full ordered name table and the client builds
     a remap array (authority id to local id) applied at the replication boundary.
     Wire format is authority ids either way. The same mechanism covers attribute,
     ability, and effect ids.
   - **Component types:** `ComponentTypeId` is content-addressed and module-stable;
     the fingerprint gate already guarantees agreement. The replication registry
     (Section 6.2) exchanges its list of replicated `ComponentTypeId`s plus a hash
     of each type's replication schema (field order, quantization params) so a
     stale-module mismatch fails loudly at join, not silently at apply.
   - **Assets:** `AssetId` is cook-stable and rides the verified world identity; no
     table needed. Paths never cross the wire in steady state.
   - **Zones:** manifest ids, identical by the world identity gate.
4. **Session admission.** `PeerId` assigned; replicated cvar values synced
   (Section 9); spawn and interest bootstrap proceed through the ordinary paths
   (Sections 7 and 8), so "late join" is not a special mode (Section 8.3).

Auth tokens (who is allowed to join) are a seam input: the handshake carries an
opaque token validated by a game-supplied callable (the `ZoneLoadRecipeFn`
precedent: a function, not an interface). LAN and direct-IP sessions validate
trivially; a platform service plugs in later without protocol changes.

Clock sync: ping/pong samples smoothed into an estimated authority-tick offset,
consumed by the tick scheduler (Section 3.1) to keep the client's predicted timeline
a configured lead ahead of the authority and the interpolation timeline a configured
lag behind (both cvars; Section 7.3).

---

## 6. Replication

### 6.1 Entity identity on the wire

`NetEntityId`: `StrongId<NetEntityIdTag, uint64_t>`, session-transient, minted by
the authority. Packing: 16 bits of scope (zone table index or the global-registry
sentinel) plus 32 bits of entity slot plus mint metadata; exact packing is an
implementation detail behind the strong id, never parsed by consumers. Each side
keeps per-registry maps between `NetEntityId` and runtime `EntityId`. Raw
`EntityId`s never cross the wire (generation and index are per-World, Section 1).

Identity assignment has two cases:

- **Scene-static entities** (the cooked scene's contents): both sides deserialize
  the identical cooked scene, so identity is the stable per-entity id from
  Section 3.4 — the editor-minted `PersistentEntityId` carried through the cook,
  resolved on each side through its own `PersistentEntityIndex`. No fallback scheme
  is needed; this shipped ahead of the track. No spawn messages for authored
  content, ever: a zone grant plus a
  baseline delta against authored state fully describes it. This is the single
  biggest bandwidth and simplicity win the architecture hands us, and it is also
  exactly the `ZoneStateRecord` overlay shape (created/destroyed/changed against
  authored), which is why Section 3.4 insists the two share one identity.
- **Dynamic entities** (pawns, projectiles, spawned pickups): authority mints a
  `NetEntityId` at spawn; creation replicates as (net id, archetype description as a
  list of `ComponentTypeId`s, initial component payloads). Client applies creations
  and destructions at the defined apply point (Section 6.5). When prefab assets land
  (Track D item 1), the archetype description collapses to an `AssetId` plus
  overrides; the protocol reserves that encoding now but ships the explicit list
  first (no dead field: the list form is the v1 wire format, the prefab form is a
  recorded successor).

Client-local entities (particles, presentation-only spawns) simply never enter the
map; nothing about them crosses the wire.

### 6.2 What replicates: schema annotations, not a parallel registry

A component opts into replication on its existing `TypeSchema`, the way it opts into
scene serialization (`SceneChunkId`) and asset fields (`.AsAsset(...)`):

- A `ReplicationId` on the schema (mirroring `SceneChunkId`) marks the component
  replicated; its `ComponentTypeId` is the wire type key.
- Per-field annotations extend `Field` the way `.AsAsset()` did:
  `.Quantize(min, max, bits)` for floats, `.WireDelta()` markers where field-level
  delta beats whole-component compare, `.OwnerOnly()` for fields replicated only to
  the owning peer (ability cooldown internals), `.Presentation()` for fields
  excluded from the wire entirely.
- Special field codecs mirror the scene codec split: asset handles encode as
  `AssetId`, `GameplayTagContainer` encodes as (count, authority tag ids) through
  the Section 5 remap, entity references encode as `NetEntityId` through the map
  (unmapped references refuse at the writer with a validation error, not silently).

One registration fold, `EngineReplicatedComponents`, mirrors
`EngineSceneComponents` (`world/ComponentManifest.h`), and game modules append their
own in `OnRegisterComponents`, so the replication registry cannot drift from storage
registration (the manifest-fold precedent). The writer and applier are schema-driven;
adding a replicated component to a game is a schema edit plus a manifest entry, zero
netcode. This satisfies OCP through data and the existing registration seam rather
than a new interface.

The wire codec is its own small pure layer over `VisitSchema` (bit-packing and
quantization have no business in the scene archives), but it shares the schema, the
field metadata, and eventually the binary asset-handle codec Track F item 3 finishes.
Wire byte order is pinned little-endian, which is native on every current and
plausible target; the codec asserts rather than swaps.

### 6.3 Deltas, baselines, acks

Per replication scope (each granted zone, plus the global scope), per tick, on the
authority:

1. **Dirty narrowing.** The writer keeps the last-serialized tick per scope and
   sweeps only chunks whose replicated columns changed since then, using the
   existing per-chunk column counters with a caller-supplied reference frame
   (`Query.h:107-114`). Chunk-conservative false positives cost a compare, not a
   send. Structural changes (new chunks after archetype moves) are caught by the
   identity map diff in the same pass.
2. **Snapshot ring.** Serialized (quantized) state per entity is retained in a ring
   of the last N ticks per scope (N a cvar; memory is bounded per scope, not per
   client).
3. **Per-client delta.** Each client's last-acked tick per scope selects the
   baseline from the ring; the delta is (spawned, destroyed, changed) entities with
   per-component field masks against that baseline. A client acked too far back
   (beyond the ring) gets a fresh baseline against authored state, the same encoding
   as a zone grant (Section 8.2); nothing special-cases "fell behind."
4. **Budget and priority.** Each client has a per-tick byte budget (cvar). Scopes
   drain in interest order: global scope first, then granted zones by that client's
   hop rank (focus zone first, then neighbors), anchor-distance tie-break once the
   zone review's Phase C lands. Within a zone, entities not sent for the longest go
   first (send-age accumulation), so saturation degrades to lower frequency, never
   starvation. Unsent scopes carry to the next tick.

Quantization is applied on the authority before both send and hash/desync purposes,
and the predicted client applies the same quantization to its own predicted values
before comparison (Section 7.4), so corrections fire on real divergence, not on
representation error.

Snapshot cadence is a cvar (`net.snapshot_interval_ticks`, default 2: 30 Hz state on
a 60 Hz sim); inputs and acks ride every tick.

### 6.4 Events map onto the existing event taxonomy

AbilityKit's D-F taxonomy already separates state from occurrences, which is exactly
the replication split:

- **Conditions and durable facts** (tags, attribute values, `Dead` components) are
  component state: they replicate through Section 6.3 and need no event channel.
- **Transient notifications and payload events** (cues: impact flashes, stingers,
  camera shakes) replicate on the unreliable channel as tick-stamped event records
  (event type id from a table synced like tags; POD payload via the same field
  codec). Late or lost cues are dropped by design; anything a client must not miss
  is, by definition, state.

`ICueSink` stays the game-facing surface; the net layer is one more producer on the
client and one consumer on the authority. AbilityKit itself remains
replication-free, honoring its own exclusion (`abilitykit.md:71`): the framework's
components replicate generically like any other data.

### 6.5 Client apply and interpolation

All structural application (spawn, destroy, component add/remove) happens at one
defined point: the start of the client's fixed tick, before any system runs, through
a `CommandBuffer` flush plus direct creation for spawns (the scene-load precedent:
`LoadSceneJson` creates directly under the same no-active-query conditions,
`SceneSerializer.cpp:539`; buffered `CreateEntity` cannot carry components,
`CommandBuffer.h:124-133`). Value application for non-predicted entities writes
component data from the interpolation buffer: the client holds the last few
snapshots per scope and applies state at (estimated authority tick minus
`net.interp_delay_ticks`), interpolating linearly between bracketing snapshots for
annotated continuous fields (position, orientation) and stepping everything else.
Per-tick application keeps remote motion smooth at snapshot cadence; sub-tick
presentation smoothing arrives free once Section 3.1's alpha is real and transforms
interpolate at extraction.

A snapshot that references a zone the client has not finished loading cannot occur
by protocol (grants gate replication, Section 8.2); the applier treats it as a
protocol violation, not a queue-and-hope case.

---

## 7. Ownership and input

### 7.1 Authority data

One replicated component: `NetOwner { PeerId Owner; }` (absent or invalid means
authority-owned). It is data like any other component: queries filter on it, the
input router consumes it, and nothing else in the engine interprets it. v1 semantics:
the authority simulates everything; ownership grants exactly (a) the right for that
peer's inputs to drive this entity's intent components and (b) client-side
prediction eligibility. Ownership transfer is a component write on the authority
(possession of a turret, a vehicle later); no protocol machinery beyond replicating
the component.

### 7.2 Role composition without branches

- **Authority-only systems** (AI, ability activation, effect lifetime, spawning,
  scoring): registered only when the session role is Host or HeadlessHost, in the
  game's `OnRegisterSystems` (which receives the role). One decision at the
  composition root, per SOLID-as-applied; no `IsServer` branches inside systems.
- **Client-side gating of zone content** rides participation, which already exists:
  on a client, granted zones converge to participation without Logic
  (Visible, Physics, Audio per the normal demand policy), so fixed-logic systems
  never see replicated zone partitions (`EngineFramePhases.cpp` binds FixedLogic
  to the Logic span). Zero new mechanism; the demand policy's participation
  assignment consults the session role's participation ceiling, a config value, not
  a code path.
- **The persistent-partition wrinkle, named honestly:** partition zero participates
  in every span unconditionally, and remote pawns live there
  (Section 8.5). Client-side systems that tick Global entities (movement, character
  controller) must run for the predicted local pawn but not for remote pawns. That
  selection is per-entity, so it is component data: movement systems already key on
  intent components; remote pawns on a client simply have no intent-producing
  system feeding them (their state arrives by replication), and the character
  controller system skips entities whose `NetOwner` is neither local nor
  authority-simulated-here. This is the one place a per-entity ownership check
  enters an engine system, it is a data filter in an existing query, and it is the
  honest minimum.

### 7.3 The input channel

The client sends, every tick, on the unreliable channel: (client tick index, the
resolved action records for that tick per Section 3.3, a redundancy window of the
previous K ticks' records so loss costs nothing until K consecutive drops). The
authority buffers per peer, consumes the record matching each authority tick,
substitutes "held state persists, edges empty" for gaps beyond the window, and
tracks buffer depth. Clock sync (Section 5) steers the client to keep its send lead
minimal but nonzero; the tick scheduler's 0..2-tick elasticity is the convergence
actuator.

Two operating modes, both shipped, selected by cvar:

- **Input-delay mode** (v1 default): the client renders its own pawn from
  replicated state like any remote entity, with the interp delay. Zero prediction
  machinery, perfectly consistent, right for LAN and couch-adjacent latency. This
  mode is also the permanent fallback and the reference implementation prediction is
  tested against.
- **Prediction mode** (Section 7.4).

What latency does and does not affect, stated plainly, because "clients send only
inputs" is a statement about wire authority, not about what the client computes.
Remote entities render from the interpolation buffer, so their motion is smooth at
any RTT; latency only ages the view (other players appear interp-delay plus
half-RTT in the past), and it is jitter beyond the buffer, not latency itself, that
stutters. The local pawn in prediction mode simulates on the client's own inputs
the tick they are pressed, so own-movement feel carries zero added latency at any
RTT; latency's remaining cost is confined to corrections when the authority
disagrees (something pushed the pawn that the client could not yet know about) and
to the round trip before the player's actions affect shared state (a door opens
when the authority says so). Input-delay mode is the one configuration where the
player's own movement feels the wire, which is why it is the LAN and couch default
and prediction is the internet answer.

Requests that are not per-tick movement (interact, purchase, drop) travel reliable as
intent records; the authority validates them with the same data gameplay already
uses: AbilityKit activation (tag query, cost) is the validator for ability requests,
range checks come from the ability definition's own data via `PhysicsQueries`.
Validation code paths that exist only for the server are a smell; the simulation is
the validator (directive 3).

### 7.4 Prediction and reconciliation (the honest hard part)

Scope: the local player's pawn and its directly-driven state (movement stack,
character controller, ability cooldown and cost bookkeeping needed for responsive
activation). Not rigid bodies, not other entities, not zone content. Mechanism:

- The client runs the predicted subset of fixed systems (a registered list, the
  composition root again) for its pawn each tick using its own input, tagging
  results per tick in a small history ring (inputs, predicted component state,
  post-quantization).
- Authoritative pawn state arrives tick-stamped. On receipt, compare against the
  history entry for that tick (both sides post-quantization, Section 6.3). Within
  epsilon: drop history through that tick. Divergent: rewind pawn components to the
  authoritative values, replay stored inputs through the predicted systems to the
  present, and smooth the visual delta over a few presentation frames (error decay
  on the presentation transform only; simulation state snaps).
- Replay is exact because the predicted systems are deterministic fixed-tick systems
  over component state (Sections 2.2, 3.2). The character controller resim needs
  `CharacterMover` stepping against the physics world outside the normal Physics
  phase, N times in one frame: the mover is a kinematic capsule doing move-and-slide
  queries (`CharacterMover.h:11-18`), so replay is repeated shape-casts against
  static geometry plus kinematic snapshots of dynamic bodies, not a Jolt world
  rewind. This is the single riskiest integration in the plan and is why 3.2's
  mover-state-into-components fix is a prerequisite, why prediction is its own
  phase, and why input-delay mode ships first and stays.
- Remote interactions during replay use latest replicated state (standard
  approximation); mispredictions from it are bounded by the correction path.

Deferred with recorded triggers: server-side lag compensation for hit validation
(trigger: a target game ships hitscan combat at internet latency), projectile
forward-prediction (trigger: the same), ability full-prediction with rollback of
effects (trigger: measured activation-feel complaints in the co-op slice; costs and
cooldowns are predicted-display-only until then).

---

## 8. Streaming and interest: the multiplayer zone story

### 8.1 Multi-focus demand: one pure-policy generalization

Today the policy takes one focus (`ZoneDemand.h:109-117`); the authority needs one
focus per connected player (its own pawn plus each client's pawn, all authoritative
positions on the server). Proposal, in the N1..N4 tradition of pure-policy
extensions:

- New input record: `ZoneFocusSource { FocusSourceId Source; ZoneId Focus;
  std::optional<Vec3d> Position; }`, with `FocusSourceId` a zone-layer strong id
  (the net layer maps `PeerId` onto it; the zone module stays networking-free).
- `ComputeZoneDemand` (and `ComputeZoneHopRanks`) accept
  `std::span<const ZoneFocusSource>` sorted by source id. Semantics: per-zone hop
  rank is the minimum over sources; every source's focus zone gets full
  participation and focus-eviction immunity; spatial radius applies per source with
  a position; the eviction comparator is unchanged over merged ranks (ties broken as
  today, deterministic). Cap and (zone review Phase D) cost budget apply to the
  merged set: focus zones and pins may exceed them, exactly as focus does today.
- `ResolveFocusZone` hysteresis (including the review's containment handoff) runs
  per source with per-source previous-focus state.
- `WorldPartitionRuntime` grows `SetFocus(FocusSourceId, Vec3d)` /
  `SetFocus(FocusSourceId, ZoneId)` / `RemoveFocusSource(FocusSourceId)`; the
  existing single-focus calls become the single-source case (source id 1), so the
  template game, the editor preview (D18: the preview passes one source, zero new
  wiring), and every existing test are unchanged in behavior.
- `RegionStreamingConfig` resolution picks per-zone config by that zone's region as
  today; the config in force for eviction is the base config (per-region caps bound
  regions, the global cap and budget bound the union; same resolution the review's
  budget already defines).

Tests: `TwoSourcesMergeByMinimumHop`, `EachSourceFocusIsFullAndUnevictable`,
`SourceRemovalReleasesItsDemand`, `SingleSourceIsByteIdenticalToToday`,
`MergedEvictionIsDeterministic`, plus the R4 traversal suite run with two scripted
sources crossing paths.

The server's residency is then automatically the union of every player's
neighborhood, with linger absorbing crossings, exactly as it absorbs one player
today. Server memory is governed by the same knobs (cap, and cost budget once the
review's Phase D lands), sized against the ratified 8-peer envelope (Section 14).

### 8.2 Per-peer interest and the grant/ack residency protocol

Interest is not computed twice: the authority already computed each peer's demand
(that peer's focus source, evaluated alone against the same policy) and uses it as
the replication scope set for that peer. The protocol makes residency safe:

- **Grant.** When a zone enters a peer's interest set, the authority sends
  `ZoneGrant(zone)` (reliable), followed by the zone baseline: the overlay-shaped
  delta against authored state (Section 6.1) plus current dynamic entities. The
  client's partition runtime treats grants as pins onto its own demand (its local
  policy still runs for presentation-side prefetch like neighbor visibility;
  authoritative interest and local demand agree in steady state because they are
  the same pure function of the same replicated pawn position).
- **Ack.** The client loads through the ordinary dormant-attach path and sends
  `ZoneAck(zone)` when the registry is attached and finalized. Only after the ack
  does the zone enter the peer's live delta scopes (Section 6.3). Until then the
  authority accumulates nothing per-client: the snapshot ring plus authored
  baseline reconstruct any join point, so a slow loader costs a stale baseline
  send, not queued memory.
- **Revoke.** When a zone leaves interest (plus linger), `ZoneRevoke(zone)`;
  the client unpins, normal linger-and-destroy follows, and the peer's scope state
  drops. In-flight snapshots for revoked zones are discarded by scope id.
- **Flow control invariant:** the authority never sends entity state for a zone the
  peer has not acked. The applier's Section 6.5 violation rule enforces it from the
  other side.

Movement across a boundary needs no special case: the pawn's authoritative position
moves, both sides' demand shifts, the neighbor was already granted and acked
(neighbor preload is the same mechanism that hides load latency in single-player,
D17), and participation flips on the client through its own policy.

### 8.3 Late join and travel are the same path

- **Late join:** admission (Section 5) creates the peer's focus source at the
  authority-chosen spawn zone; grants and baselines flow for its neighborhood; the
  pawn spawns (Section 7); done. There is no join-specific state transfer code:
  joining is "every zone is newly granted."
- **Teleport travel** (fast travel, respawn, scripted warps): authority moves the
  focus source (`SetFocus(source, zone)`), grants flow, and the pawn relocation
  replicates as ordinary state after the destination acks: a spawn barrier by
  protocol order, not by timer. The zone review's Teleport-dormant-preload decision
  composes cleanly: teleport-linked zones sit dormant on both sides until the
  crossing actually happens, and the grant/ack path covers the flip. Track C item
  6's transition timing model, when it lands, hangs client-side presentation
  (fades, input suppression) on the same grant/ack sequence; the discontinuity bus
  already carries the teleport reason.

### 8.4 What the zone review's phases contribute here

- **Phase A (containment focus fix):** per-source correctness; without it a player
  standing in a contained inner zone never gets Logic participation, which in
  multiplayer means the authority does not simulate around that player. It moves
  from quality-of-life to prerequisite.
- **Phase C (anchors):** per-peer load-order tie-breaks (near-door neighbors first
  per player) and, later, the crossing point Track C item 6 needs.
- **Phase D (cost records and budget):** the server residency budget for the union
  set; the demand records' per-source attribution extends the streaming preview and
  telemetry to answer "why is this zone resident and for whom" (`DemandRecords()`
  gains source flags per zone; the debug UI reads it).
- **Phase E (teleport dormant):** fewer resident zones per hub-shaped world,
  multiplied by player count on the server.

### 8.5 Two consequences stated plainly

- **Zones without players do not simulate.** Participation-gated Logic is the
  engine's model (dormant means absent from spans); multiplayer widens residency to
  the union of players but does not change the rule. Co-op gameplay that expects a
  vacated zone to keep simulating uses the existing answer (pins) or the coming one
  (`ZoneStateRecord` overlays restoring divergence on reload). This is the
  single-player model generalized, stated so nobody discovers it as a surprise.
- **Global-registry entities need per-peer relevance.** Pawns live in Global
  (Section 1), which every client always has. v1 replicates the global scope to all
  peers unconditionally: pawn count is player count, and the payload is small.
  The recorded trigger for position-based relevance of global entities (replicate a
  pawn only to peers whose granted set contains its containing zone): a measured
  bandwidth or information-leak concern in a real world. The containment test is
  the pure policy's existing point-in-bounds machinery; nothing new would need
  inventing.

---

## 9. Cvars and the console in sessions

The question "client vs server cvars" resolves into write authority plus mirroring,
on the existing metadata surface (Section 1: flags, OnChange, provenance already
exist; three flags enforced today, `ConsoleRegistry.cpp:496-519`).

- **`CVarFlags::Replicated` (new).** Writable only where authority resides. On the
  authority, `set` works normally and the new value syncs to clients (reliable
  channel), committing on the tick boundary with the ordinary `OnChange` firing
  locally on each client. On a connected client, `set` refuses with the reason and
  the authority's value ("server-owned: movement.ground_accel = 12.0"). With no
  session, `Replicated` is inert: single-player keeps today's behavior exactly.
- **`Replicated | InitOnly`: handshake-verified.** Values that cannot change live
  (`time.fixed_tick_rate`) are compared at join; mismatch refuses admission with the
  pair of values in the message. No live sync machinery for things that cannot
  legally change.
- **`Cheat`: finally enforced, and it is now shipping security.** In a session with
  remote peers, `Cheat`-flagged cvars and commands refuse unless `net.cheats`
  (itself `Replicated`, default false) is true. Outside a session, unrestricted,
  preserving every current dev workflow.

  The review wrote this gate off as "about honest multiplayer dev sessions, not
  shipping security," on the premise that shipping builds strip the console. **That
  premise is dead.** The owner decided (2026-08-07) that the debug console ships in
  every build, because it is a player-facing feature: players tinkering and modding
  are a supported use, not a leak. Every preset now builds it, and a host opts out
  per process via `EngineConfig.Console.UiEnabled`.

  So the console is a real surface a real player holds in a real session, and
  `Cheat` and `Replicated` enforcement is the thing standing between that surface
  and other players' game state. This does not weaken the security model — Section
  10.3 already holds, because the authority accepts no state from a client and a
  client's local cvar cannot move another player's character. What it does mean is
  that the enforcement in this section is load-bearing rather than hygienic, it
  ships rather than being compiled out, and it needs tests that run in the shipping
  configuration. Treat a missing `Cheat` flag on a new gameplay cvar as a defect,
  not an oversight.
- **What gets which flag (initial sweep, done in the phase that lands enforcement).**
  `time.timescale` becomes `Replicated` (a paused authority pauses the session;
  per-client timescale in a session is a desync by construction).
  `time.fixed_tick_rate` becomes `Replicated | InitOnly`. Note the F1 pause path
  writes the timescale directly rather than through the console
  (`EngineFramePhases.cpp`), so it must be routed through the console set or it
  bypasses enforcement.

  The review also listed six `movement.*` tunables. **Those cvars no longer exist**:
  movement tuning moved into `movement.profile` data assets resolved through
  `MovementProfileBindingCache`. Authored tuning is content, so it is covered by the
  world-identity gate at join (Section 5) rather than by cvar sync — which is the
  better answer anyway, since it makes the whole tuning document agree rather than
  six values.

  Rendering and frame-pacing cvars (`r.*`, `console.*`) stay untouched local, which
  is the user's observation confirmed: most existing cvars are client-side and
  correctly so. Streaming `EngineRuntimeConfig` fields stay per-machine (they are
  capacity, not simulation; the demand they produce is already made consistent by
  Section 8's protocol, not by matching knobs).
- **Editor cvars: no new dichotomy needed.** Editors are separate processes with
  their own `ConsoleService` registries; `Owner = "editor"` and the `editor.*`
  namespace already partition them, and no editor process ever joins a session.
  The one real editor-side gap in this area is unrelated to networking and already
  known: `Archive` has no write-back path anywhere (Section 1), which is why theme
  choice does not persist. Recorded as an observation for the editor track, not
  networking work.

Game code reading a `Replicated` cvar per fixed tick (the
`MovementTuningResolutionSystem` pattern) needs no change: mirrored commits land at tick boundaries, so both sides
read identical values for any given tick, preserving prediction replay.

---

## 10. Security

### 10.1 Threat model

In scope: a malicious or compromised client (forged, malformed, replayed, or flooded
packets; impossible inputs; request spam; information harvesting), an on-path
observer (LAN or internet), a griefing peer (DoS attempts against host bandwidth or
CPU), and a malicious or compromised authority attacking client integrity: crafted
messages aimed at client code execution, crashes, or resource exhaustion
(Section 10.4). Out of scope for the engine: an authority lying about gameplay
(deciding game state is what an authority is; dishonest hosting is bounded to the
session, and the trust signals in Section 10.4 plus the player's choice of whom to
join are the mitigations), platform-level bans, and anti-tamper of the client binary
(an engine cannot promise it; the design instead makes client tampering unable to
affect other players' state).

### 10.2 The decode boundary

Every byte from the wire crosses exactly one boundary: `NetProtocol`'s pure
decoders. The boundary is symmetric: the client decodes authority bytes under the
same rules, and the client-facing surface is the larger one (baselines, deltas, and
table syncs are the most complex messages in the protocol), so the fuzz corpus
weights authority-to-client messages accordingly. Strikes on a client disconnect it
from the offending session. Rules, enforced by review and tests:

- Length-prefixed everything; every count validated against remaining bytes and
  against a per-message-type cap table (max peers, max zones per grant, max
  entities per delta, max component payload = schema-computed size, max string =
  reason-text cap). No allocation sized by wire data beyond the caps.
- Decoders are pure functions over `std::span<const std::byte>` returning value
  types or a typed error; no engine services, no logging, no side effects, so they
  fuzz in isolation.
- Unknown message types, unknown `ComponentTypeId`s, out-of-range table indices,
  and violations of protocol state (Section 6.5's un-acked-zone rule) are typed
  errors that increment per-peer strike counters; strikes disconnect.
- Fuzzing ships with the phase, not after: a deterministic corpus of
  mutation-fuzzed valid messages runs in `net_tests` on every suite run
  (seeded generator, reproducible), and a libFuzzer harness over the decoders
  exists as a dev-only target for deeper runs.

### 10.3 Validation is the simulation

The server accepts state from no one. Inputs are intents; the simulation decides
outcomes. Consequences, enumerated because they are the anti-cheat surface:

- Speed and teleport hacks: impossible by construction; position is never read from
  clients. Movement outcomes come from the server's own `CharacterMover` driven by
  intent clamped to `MovementProfile` limits (the same data that drives legitimate
  movement; an out-of-range axis value clamps, logs, and strikes).
- Ability and economy hacks: `AbilityActivationSystem`'s own tag/cost/cooldown
  checks are the validation. There is no second rulebook to drift out of sync.
- Rate abuse: per-peer input records per tick are capped at exactly one (plus the
  redundancy window); request-class messages get token buckets; violations strike.
- Information: interest scoping (Section 8) means the wire simply never carries
  state the client should not know at zone granularity; finer-grained occlusion
  culling of replication is a recorded non-goal until a target game demonstrates
  the need.

### 10.4 Client integrity against a hostile authority

Joining a session must never hand the server the client machine. The defense is
structural first, then the Section 10.2 hardening discipline pointed the other way:

- **The wire carries values, never code and never content.** No message transfers a
  file, an asset, a script, a shader, or a console command. Both sides already own
  identical cooked content (the world identity gate, Section 5); replication
  payloads are field values for schema-known component types; table syncs are
  grammar-validated dotted names; cvar syncs are values for cvars the client itself
  flagged `Replicated`, passed through the client's own validator and range checks
  before commit. A compromised server that wants to run something on a client has
  no channel to send it through. If server-provided content (community maps, server
  mods) ever becomes a product decision, it arrives as its own reviewed design with
  signing and sandboxing requirements, never as a relaxation of this invariant
  (recorded trigger; see the matching non-goal).
- **No remote execution surface.** The protocol defines no "run command", "set
  arbitrary cvar", or "load path" message. Cvar sync rejects names the client has
  not flagged `Replicated` (strike); reason and display strings are length-capped
  and rendered as text, never interpreted.
- **Apply-layer validation.** Decoded values are validated again where they touch
  the world: unquantized continuous floats must be finite (quantized fields are
  bounded by construction, since dequantization cannot produce NaN or out-of-range
  values, and encodings derive from the client's local schema, never from the
  wire); enum payloads must be in range; entity references must resolve in the
  identity map, with spawns applied before references within a message; replicated
  parent links are cycle-checked before the transform system sees them. A malformed
  message is never partially applied; violations strike and disconnect.
- **Bounded client resources.** Grant counts, per-zone entity caps, snapshot ring
  depth, reliable-window sizes, and event rates bound what a hostile authority can
  make a client allocate; exceeding a cap is a protocol violation, not a growth
  path.
- **Session identity is mutual, and it is an assertion, not a proof.** The
  handshake's compatibility gate runs both directions: the client refuses an
  authority whose reported protocol, engine, fingerprint, or world identity differs
  from its own (Section 5). That stops accidents and downgrade confusion. It cannot
  prove the server runs unmodified code: any self-reported identity, hashed or
  signed by a key the attacker also possesses, can be forged by a machine the
  attacker controls. Client safety therefore never depends on identity claims; the
  mechanisms above hold against arbitrary bytes from a fully hostile peer.
- **Server build identity, graded honestly.** Three grades, increasing strength and
  cost. (1) The mutual identity gate above: compatibility and accident prevention,
  ships in G1 regardless. (2) A build signature: the authority signs its identity
  tuple plus the handshake challenge; the client verifies against a public key
  shipped with the game and surfaces an "official build" trust signal. Meaningful
  when dedicated server binaries stay in the developer's hands, because the private
  key never leaves the developer's infrastructure; only a speed bump once server
  binaries are publicly distributed, because a distributed binary's embedded key
  can be extracted. Dedicated hosting is now a ratified product deliverable, so this
  grade is a live decision rather than a hypothetical (Section 14, Open question 6).
  (3) Remote attestation (platform-run servers, or hardware-backed
  attestation where a platform provides it) is the only strong form and is a
  platform-service feature behind the existing auth-token seam (recorded trigger: a
  shipping target on a platform that offers it). At every grade, the trust signal
  gates UI and matchmaking presentation, never the hardening: the client stays safe
  against a server that fails or fakes every identity check.

### 10.5 Transport privacy and authenticity (owner decision)

Recommendation: encrypt and authenticate all post-handshake traffic with libsodium
(XChaCha20-Poly1305 AEAD; keys from an X25519 exchange folded into the Section 5
handshake; per-packet nonces from the sequence space, replay-rejected by the
sliding ack window). Rationale: the primitives are small, audited, allocation-free,
and the alternative (a platform relay's transport security) is exactly one
`INetTransport` implementation away whenever it arrives, at which point the crypto
layer disables itself per-transport. Cost: one vetted dependency, PRIVATE-linked
behind `net/` the way Jolt hides behind physics. The genuinely open question is
dependency appetite, not design — answered: libsodium (Section 14). Plaintext LAN mode remains a
cvar for development capture and CI determinism diffs.

### 10.6 Availability

The cookie handshake (Section 5) kills spoofed-source floods and amplification
(responses before verification are never larger than requests). Post-admission:
per-peer receive budgets (packets and bytes per second, cvars) with
strike-then-disconnect, connection caps (`net.max_peers`), oversized-datagram
rejection at the transport, and no per-packet heap allocation in steady state
(fixed rings for channels and snapshots). The headless host runs the same budgets;
a dedicated box's only special posture is that it starts sessions rather than
joining them.

### 10.7 Desync detection

Dev-mode (cvar-gated) state hashing: the authority folds each zone scope's
replicated, quantized component state into a 64-bit hash every K ticks and
broadcasts (zone, tick, hash) on the unreliable channel; clients compare against
their applied view and log-and-dump on mismatch (the dump names zone, tick,
first-divergent entity and component, both value sets). This is the Track E
determinism gate's hashing pointed across the wire instead of across worker counts;
build the folding once. It exists to catch replication defects and
nondeterminism regressions in development and soak, not to police cheaters
(Section 10.3 already did).

### 10.8 Shipping posture

Shipping builds keep: the decode boundary, budgets, crypto, interest scoping (all
of it is the protocol, not dev tooling). Shipping builds drop: desync hashing
default-off, plaintext mode, the fuzz harness, and `SimulatedTransport` (dev-only
target). Packaging (Track F item 2) adds the headless host binary configuration;
dedicated hosting is a product deliverable (Section 12, G7).

**The console ships.** The review assumed shipping builds strip it; the owner decided
otherwise on 2026-08-07, and every preset now builds the debug UI. The console is a
player-facing capability — tinkering and modding are supported uses — and the
multiplayer answer to it is authority, not absence: `Cheat` and `Replicated`
enforcement (Section 9) gates what a player's own console can touch in a session,
exactly as `sv_cheats` does in the engines Sencha is modelled on. Nothing in Section
10.3 depends on the console being gone: the authority accepts no state from clients,
so a client console cannot move another player's character no matter what it sets.
The consequence is that Section 9's enforcement is a shipping requirement with
shipping-configuration tests, not a development convenience.

---

## 11. Testing

The strategy leans on the engine's strongest property: everything below the socket
is deterministic and constructible headless.

- **`test/net/` suite (new `net_tests` target, gtest like the rest):**
  - `NetCodecTests`: round-trip every message type; quantization bounds; cap-table
    enforcement; the seeded fuzz corpus (every mutation either decodes to a value
    or a typed error, never UB; run under ASAN in CI like the rest of the suite).
  - `NetChannelTests`: reliability over `SimulatedTransport` schedules (loss,
    reorder, duplication, jitter; seeded and reproducible): delivery, ordering,
    fragmentation, ack-window replay rejection.
  - `NetSessionTests`: handshake matrix (cookie replay, version skew, fingerprint
    skew, world-identity skew, tag-table mismatch remap, admission caps).
  - `ReplicationTests`: authority and client `Registry` pairs in one process over
    `LoopbackTransport`, hand-pumped (the `WorldPartitionRuntimeTests` pattern:
    no Engine, zero-thread task queue, manual drain): baseline plus deltas
    reproduce state within quantization; spawn/destroy lifecycles; owner-only and
    presentation field exclusions; byte-identical writer output across
    `JobWorkerCount` 0 and N and across identical reruns.
  - `InterestTests`: two scripted focus sources; grant/ack/revoke sequences;
    the never-send-before-ack invariant; late join equals fresh grants; budget
    starvation fairness.
  - `PredictionTests`: scripted input tapes; correction convergence
    (post-correction replay equals authority within epsilon, exact for pure
    component systems); rewind depth bounds.
  - `NetCvarTests`, `DesyncHashTests` per Sections 9 and 10.6.
- **Engine-level:** the Section 3.1 scheduler and headless frame-loop tests; the
  Section 8.1 multi-source demand tests beside the existing `ZoneDemandTests`.
- **Integration and soak:** a two-process harness (headless host plus scripted
  client over loopback UDP) driven by the traversal-script machinery, asserting
  zero missed ticks under streaming plus replication and clean join/leave/rejoin
  cycles; wired into CI when Track E item 1 lands. In-process dual-`Engine` tests
  are possible but not the plan of record: the one process-global
  (`DefaultComponentSerializerRegistry`) is idempotent for identical registration
  but its unregister path makes engine teardown order-sensitive; headless
  session-over-registries covers the same surface without fighting it.
- **Suite discipline:** every phase lands green with its tests, one mechanism per
  test file, per the overview rules.

---

## 12. Sequencing (roadmap Track G)

Each phase is independently landable, ships its tests, and leaves no half-wired
mechanism (overview rule 12). Dependencies name existing roadmap items.

The track gate is the two-instance deliverable at the end of G4: two app instances on
one machine over loopback UDP, one hosting, each seeing the other's pawn move under
server-authoritative simulation. Zone interest (G3) is not required to reach it — the
`+map` path loads a single zone and pawns live in the persistent partition — but it
is required before any multi-zone (`+world`) session ships, and the three-zone
scripted co-op traversal is *its* gate rather than G4's.

"Seeing" in that gate means observed motion, asserted as close to the rendered
frame as the harness can reach — not World-level state and not log lines. Recorded
after the 2026-08-07 playtest: every World-level assertion passed while replicated
motion never reached the screen, because the tests stopped one derivation short of
what extraction actually reads.

- **G0. Foundations (no netcode). MOSTLY LANDED.** Section 3.1's paced tick scheduler,
  3.2's mover-state fix, 3.3's tick-stamped input records, and 3.4's stable identity
  all shipped on their own tracks. What remains: **the headless frame loop**
  (Section 3.1), plus ratifying this document and its Track G row into the roadmap.
  The headless loop unblocks CI simulation soak regardless of networking's fate and
  is the dedicated host's skeleton. No dependencies.
- **G1. Transport, protocol, session.** `INetTransport` plus Udp/Loopback/Simulated;
  channels; `NetProtocol` with cap tables and the fuzz corpus; handshake with
  cookie, compatibility gates, table sync; clock sync; `PumpNet`/`FlushNet` phases;
  `host`/`connect`/`disconnect` console commands establishing an empty session with
  a status readout. Depends on G0.
- **G2. Replication core.** Schema annotations and the replicated-component
  manifest; `WorldComponentSchema` gains a type-erased `SetComponentBytes` (only
  add/initialize exists today, and snapshot apply must overwrite without re-firing
  `OnAdd`); identity maps keyed by `PersistentEntityId` for authored entities and
  `NetEntityId` for dynamic ones; snapshot ring, deltas, acks, budgets; client apply
  and interpolation buffer; persistent-partition replication (pawns mirror across two
  machines standing still, then moving under authority control). Depends on G1.

  The manifest fold must refuse any component whose `ComponentTraits` carries
  lifecycle hooks unless its hook-relevant fields are wire-excluded —
  `SetComponentBytes` bypasses `OnAdd`, and a handle-retaining component
  (`StaticMeshComponent`) replicated in steady state would leak or dangle. Spawn
  payloads keep going through `ImportComponent`, which fires the hooks once.

  This phase's visible deliverable needs the player pawn to have a body, which is
  the player-representation work landing separately on its own branch.
- **G3. Zone interest.** Multi-source demand (Section 8.1); per-peer grant/ack/
  revoke; zone baselines against authored state; late join; teleport travel flow.
  Depends on G2; wants zone review Phase A (containment focus) and composes with
  Phases C, D, E as they land. **Not required for the G4 gate** (see above), but
  required before a multi-zone session ships; the three-zone traversal is its gate.
- **G4. Ownership, input, prediction. The track gate lands here.** `NetOwner`;
  session role reaching `OnRegisterSystems` so role is composition rather than
  branches; the input channel over the action stream plus a per-tick
  `LookOrientation` sample (look integrates on the presentation clock, so a
  replayable command must carry the orientation sampled for its tick rather than
  re-reading the component); the second `InputActionState` producer Section 3.3
  names, one per admitted peer, feeding that peer's pawn; per-peer pawn spawn
  (`FindPlayerStart` generalizes past its single authored start); authority
  consumption and validation; input-delay mode end-to-end, which is the two-instance
  deliverable. Prediction mode with reconciliation and error smoothing follows as its
  own phase rather than riding this one. Depends on G2 and Track A item 1.

  *Status 2026-08-07: landed in a simplified form that met the session gate and
  then failed the first live playtest in two instructive ways.* The command
  carries a client-framed wish vector rather than the action columns above, so
  the authority cannot re-interpret or validate at the action level and
  non-movement actions (jump, abilities) have no wire form at all; and
  possession is three overlapping marks (`Controlled` tag, `LocalLookControl`,
  `NetOwner`) plus a skip rule inside the input system, which produced the
  crossed-keys defect (a host's input steering every pawn). Both simplifications
  are retired by G4a below, which now precedes prediction.
- **G4a. Possession and spawn recipes. Added 2026-08-07; precedes prediction.**
  The first live playtest found both of its defects in the same missing
  ownership: nothing owns what a replicated entity *is* on the receiving
  machine. Two mechanisms close it.

  *Possession.* The input layer gains `InputActionSourceId` (slot zero is the
  local device), a world resource `InputActionSourceTable`, and a pawn component
  `InputActionSourceRef` (absent means local). `CharacterInputSystem` resolves
  its action view per entity through the table; the session registers one source
  per admitted peer and feeds it from the Section 7.3 command stream (action
  columns through the synced name table, the aim sample, the redundancy window,
  the gap policy). One possession mechanism replaces the mark constellation, the
  authority interprets remote input with the same game code it runs for local
  input, and abilities cross the wire without bespoke messages.

  *Spawn recipes.* A snapshot-spawned entity currently carries only its
  replicated components: no derived columns, no local scaffolding, with each
  game hand-rolling the difference. The playtest's frozen-motion defect was
  exactly this — render extraction and history recording both require
  `WorldTransform`, which is derived, so nothing on the client ever gave the
  mirrored pawns one and replicated motion never reached the screen. The spawn
  payload gains a recipe identity the receiver instantiates its full local
  shape from, the interim form of the prefab encoding Section 6.1 already
  reserves for Track D item 1. Prediction depends on this phase: reconciliation
  built over hand-rolled adoption compounds the debt.
- **G5. Session semantics.** Cvar flag enforcement and sync (Section 9); event/cue
  replication (Section 6.4); desync hashing (Section 10.7); a net stats debug
  panel (rates, RTT, budget occupancy, per-zone scope sizes) on the existing
  `IDebugPanel` seam. Depends on G2.
- **G6. Hardening.** Crypto and auth token seam (owner decision executed); rate
  budgets and strike enforcement; malformed-traffic soak in both directions
  (hostile client against an authority, hostile authority against a live client,
  per Section 10.4); build-signature verification if Open question 6 selects it;
  interest-leak audit
  (assert no un-granted state ever serialized per peer); shipping-config posture
  checks. Depends on G1..G5 surface existing.
- **G7. Dedicated host and tooling.** Packaged headless host configuration;
  PIE "host plus join" launch convenience (spawn a second app process with
  `+connect`, the out-of-process PIE precedent); the two-process soak in CI;
  networked variant of the traversal-hitch harness (Track C item 2's script
  driving two sessions). Depends on G1; packaging parts ride Track F item 2.

Gate for the track (the roadmap owns final wording): two players, one headless or
listen host, complete a scripted co-op traversal of the three-zone fixture world
(join mid-session, cross zones together and apart, teleport, disconnect and
rejoin) with zero missed fixed ticks on the host, no desync-hash mismatches, and
the full suite green including fuzz and channel tests.

What this plan deliberately does not schedule: which engine version Track G targets.
That is the roadmap's call (Open question 1); the phase structure works as a
parallel lane precisely because G0 is engine hygiene and G1..G7 touch no existing
system's semantics until composition wires them in.

---

## 13. Non-goals

- **Deterministic lockstep and whole-world rollback** (Section 2.1). Recorded as
  rejected, not deferred.
- **Host migration.** v1 sessions end when the authority ends. Trigger to revisit:
  a shipping target whose sessions are long-lived enough that host loss is a
  top-line complaint.
- **Client authority over any gameplay state.** Inputs and requests only. No
  trigger; this is a security invariant, not a feature gap.
- **MMO-scale anything** (sharding, seamless server handoff, hundreds of peers).
  The envelope is small-group co-op: 8 peers validated, 4 tuned.
- **Cross-version and cross-build sessions.** Protocol version plus fingerprints
  gate hard (Section 5). Content versioning across saves is Track F item 6's
  domain, not the wire's.
- **Voice, matchmaking, NAT traversal services.** Direct address and LAN in v1;
  the auth-token callable and the transport seam are where platform services
  attach later without protocol change.
- **Sub-zone interest granularity and per-entity visibility culling.** Zone scope
  is the v1 answer (Section 8.5's trigger records the revisit).
- **General-purpose stream compression.** Delta plus quantization first; trigger:
  measured bandwidth exceeding the envelope on a real world (then LZ4-class on the
  reliable channel only, still one codec path).
- **Script-defined wire messages.** Scripts act through components, intents, and
  cues, which already replicate; a script-visible message API is a second behavior
  channel (directive 3) until a concrete need survives review.
- **Server-distributed content or code.** Nothing loadable or executable ever
  arrives over the wire (Section 10.4's invariant): no map downloads, no server
  mods, no script push. Trigger to revisit: community-server content distribution
  as a product decision, which then arrives with its own signing and sandboxing
  design, not as a protocol relaxation.
- **A second demand policy or an `IZonePopulationStrategy` revival.** Multi-source
  demand parameterizes the one concrete policy with data, per the standing
  deferral.

---

## 14. Open questions

Five are answered (2026-08-07) and recorded as decisions. The rest stay open and are
owed before the phase that depends on them.

### Answered

- **Player envelope (was 1).** Design-validate at 8 peers, tune defaults for 4. G1
  pins cap tables at 8; `net.max_peers` defaults to 4.
- **Transport ownership (was implicit in Section 4.3).** Sencha owns the reliability
  layer: two channel classes over UDP behind `INetTransport`, with Udp, Loopback, and
  Simulated implementations. Not ENet, not GameNetworkingSockets. The domain at this
  scale is shallow enough to own, and owning it keeps the deterministic
  `SimulatedTransport` and the fuzzable pure decoders that the test strategy depends
  on.
- **Crypto (was 3).** libsodium, PRIVATE-linked behind `net/` the way Jolt hides
  behind physics. It lands in G6, but the packet header and handshake carry the AEAD
  framing from G1 — retrofitting reshapes the header, and that is cheap now and a
  protocol bump later.
- **Dedicated hosting (was 4).** Yes, a product deliverable. Listen server and a
  packaged headless dedicated configuration both ship (G7). The headless frame loop
  is G0 either way.
- **Shipping console posture (not asked at review; decided anyway).** The console
  ships in every build. See Sections 9 and 10.8 — this inverts the review's stated
  reasoning about what cheat gating is for.

### Still open

1. **Track G's version placement.** G0 is engine hygiene worth doing regardless;
   G1..G4 are a parallel lane that could run beside v1.0 without touching its gate,
   making co-op a v1.x headline; alternatively the v1.0 gate grows a co-op clause
   and accepts the schedule risk. Recommendation: G0 into v1.0 now, Track G gated
   as its own arc, revisited when the v1.0 slice is demonstrably on rails.
2. **Prediction default for the target games.** Input-delay mode is shippable for
   LAN/couch co-op; internet co-op wants G4's prediction. Which is the default
   posture for the two internal targets, and does either need lag compensation
   (currently deferred with a trigger)?
3. **Tick and snapshot rates.** 60 Hz sim with 30 Hz snapshots is the working
   default. If the 3rd-person target wants 30 Hz sim on modest hardware, the
   handshake-pinned tick-rate class makes that a per-project config, but the
   prediction window math should be validated at both rates before G4 hardens.
4. **The initial `Replicated` cvar sweep.** Section 9's corrected list is
   `time.timescale` and `time.fixed_tick_rate` (pinned); the `movement.*` cvars it
   originally named no longer exist. Ratify that, and decide whether `net.cheats`
   should also gate `Developer`-flagged cvars in sessions. This matters more than it
   did at review: the console now ships to players (Section 10.8), so this list is
   the shipping gate rather than a dev convenience.
5. **World-identity strictness at join.** Exact cooked-content hash match (
   recommended: it is the only defensible line while cooked scenes are JSON and
   mods are not a feature) versus a looser manifest-only match to ease dev
   iteration, with the strict mode as the shipping default.
6. **Server build identity grade (Section 10.4).** Mutual identity verification
   ships in G1 regardless. Decide whether the build-signature grade is wanted: it is
   meaningful if dedicated server binaries stay first-party and near-worthless once
   they are publicly distributed, which now couples to the answered decision that
   dedicated hosting is a product. Decide before G1 lands so the handshake either
   carries the signature exchange from day one or its absence is an explicit choice
   (adding it later is a protocol version bump, cheap before any release, not free
   after). Platform attestation stays a recorded trigger either way.
