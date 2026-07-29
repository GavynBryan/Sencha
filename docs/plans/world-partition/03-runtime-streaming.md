# Phase R: Runtime Streaming

Status: execution spec (2026-07-03). Implements Phase R of
`docs/plans/world-partition-authoring.md` (Sections 4, 6.5, 6.6, and the runtime half
of Section 10; read them first, then `00-execution-overview.md`, especially D7, D10,
and D14, before writing code).

HISTORICAL. The world graph contracts are `11-zone-runtime-model.md` and
`12-spatial-compilation.md`; where this file disagrees with them, they win.
`ZoneRuntime` is now `RuntimeWorld`, portals no longer exist, and the fixture below
carries a `transitions` array that `LoadManifest` refuses. Demand sources are the
reasons listed in Plan 11 section 4.

Prerequisite: Phase 1 complete (it is). Phase E1's world cook exists but is NOT a
dependency: per D7 this phase runs against hand-written cooked manifest fixtures and
must never block on editor work. Phase R and Phase E2 are parallel lanes.

Scope: the `WorldPartitionRuntime` policy layer, the pure demand computation, the
streaming tunables, the template game's world path, and PIE play-from-world.

Non-goals (do not build any of it): participation tiers (Track C item 4; the only
participations issued here are dormant and full), stateful detach (unload is
`DestroyZone` until Track C item 5 lands; this type is its first caller later),
`IZonePopulationStrategy` (deferred by the roadmap; the policy is one concrete
function), portal consumption (portals are editor-only, D13), transition timing
semantics (history reset, input/camera policy: Track C item 6), the telemetry JSON
writer (Track C item 1; `DemandRecords()` is the surface it will serialize, see the
coordination note in R3), anything in `editor/` except the R6 PIE launch line, and any
change to `ZoneRuntime`, `AsyncZoneLoader`, or `ZoneParticipation`.

Stages R1 through R6, in order, each a separate commit with the suite green.

---

## Standing decisions for every stage

- **The runtime is game-owned and game-pumped.** The game constructs
  `WorldPartitionRuntime` (the `AsyncZoneLoader` ownership precedent in
  `template/src/TemplateGame.h`) and calls `Update` from a game-registered frame-update
  system. Recorded deviation from the design doc Section 4 comment "called once per
  frame before `FramePhase::DrainAsyncTasks`": the call runs in the Update phase.
  Issue-to-commit latency of one drain is inherent to async builds; the drain-commit
  contract is unaffected, and the engine gains no new phase hook.
- **Loads are always issued dormant** (`ZoneParticipation{}`), the recipe never
  overrides that; participation flips through `ZoneRuntime::SetParticipation` after
  attach. The runtime never calls `MarkTemporalDiscontinuity`: a dormant attach plus a
  participation flip is not a camera cut.
- **The engine mints no ids** (overview rule 9). Every id this phase touches comes
  from a parsed manifest.
- **Determinism.** No unordered container iteration feeds any output or any
  loader/zone call sequence. Linger clocks advance by the `deltaSeconds` argument to
  `Update`, never by wall clock. The residency event sequence for a scripted focus
  path is identical at zero async task threads and at N (R4 asserts this).
- **The focus zone is never unloaded and never demoted below full participation**
  while it remains the focus. Everything else is dormant in v1.0; the visual pop of a
  neighbor flipping to visible on entry is accepted (recorded trigger to revisit:
  participation tiers, Track C item 4).
- **The player pawn and camera live in the global registry**
  (`ZoneRuntime::Global()`), spawned once at world load, so zone unloads never destroy
  them. Runtime cross-zone entity migration is Track C item 5 territory, not this
  phase.

---

## R1. Streaming tunables on `EngineRuntimeConfig`

### What changes

`engine/include/core/config/RuntimeConfig.h` gains three fields beside
`AsyncCommitBudgetMs`, exactly as pinned by D14:

```cpp
// World partition streaming policy (WorldPartitionRuntime). HopCount is the
// neighbor graph distance kept resident around the focus zone; LingerSeconds is
// how long an undemanded zone stays attached before DestroyZone; ResidentZoneCap
// bounds the demand set (focus and pins may exceed it; see the policy spec).
int    StreamingHopCount = 1;
double StreamingLingerSeconds = 3.0;
int    StreamingResidentZoneCap = 8;
```

`engine/src/core/config/RuntimeConfig.cpp` parses them with the existing helpers
(`ReadIntEither`/`ReadDoubleEither`), JSON keys `streaming_hop_count`,
`streaming_linger_seconds`, `streaming_resident_zone_cap` (snake_case only is fine to
pass for the camelCase argument too; follow the file's existing dual-key call shape).
Validation clauses in the same style as the `AsyncCommitBudgetMs` clause:

- `StreamingHopCount >= 0` ("0 keeps only the focus zone resident");
- `StreamingLingerSeconds` finite and `>= 0.0`;
- `StreamingResidentZoneCap >= 1`.

### Gate R1

New tests in `test/core/RuntimeConfigTests.cpp` (the existing file):

- `StreamingFieldsParseAndDefault`: absent keys yield the defaults above; present
  keys parse.
- `StreamingFieldsRejectInvalid`: negative hop, negative or non-finite linger, and a
  zero cap each fail with a message naming the field.

Grep audit: `grep -n "Streaming" engine/include/core/config/RuntimeConfig.h` returns
exactly the three fields plus their comment.

---

## R2. Demand records and the pure policy

### New files: `engine/include/zone/ZoneDemand.h`, `engine/src/zone/ZoneDemand.cpp`

```cpp
#pragma once

#include <span>
#include <vector>

#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneParticipation.h>

// Why a zone is demanded. Flags, not a single enum: one zone can be demanded
// for several reasons at once (pinned and a neighbor, say), and the demand
// inspector wants all of them.
struct ZoneDemandSources
{
    bool Focus = false;
    bool Pinned = false;
    bool Neighbor = false;
    bool Lingering = false;
};

// One zone's desired residency this update. The data contract the kyusu demand
// inspector and the streaming telemetry read (design doc Section 6.6); records
// first, UI second.
struct ZoneDemandRecord
{
    ZoneId            Zone;
    ZoneParticipation Desired;
    ZoneDemandSources Sources;
};

// A script- or transition-driven residency demand beyond the policy. Data, not
// subclasses.
struct ZonePin
{
    ZoneId            Zone;
    ZoneParticipation Minimum;
};

// Mirrors the EngineRuntimeConfig streaming fields (R1); plain data so the
// policy stays pure and testable without config plumbing.
struct WorldPartitionStreamingConfig
{
    int32_t HopCount = 1;
    double  LingerSeconds = 3.0;
    int32_t ResidentZoneCap = 8;
};

// Pure. The demand set for one focus: the focus zone at full participation,
// its graph neighbors within HopCount hops dormant, plus pins at their minimum.
// Lingering is runtime state and is layered on by WorldPartitionRuntime::Update,
// never computed here. Deterministic: records ascend by zone id value.
[[nodiscard]] std::vector<ZoneDemandRecord>
ComputeZoneDemand(const WorldPartitionManifest& manifest,
                  const WorldPartitionIndex& index,
                  ZoneId focus,
                  std::span<const ZonePin> pins,
                  const WorldPartitionStreamingConfig& config);
```

Pinned semantics, exhaustive:

- **Invalid or unknown focus yields an empty vector.** The caller decides what "no
  focus yet" means; the policy does not guess.
- **Focus** gets `ZoneParticipation{ .Visible = true, .Physics = true, .Logic = true,
  .Audio = true }` and `Sources.Focus`.
- **Neighbors** are found by BFS over `WorldPartitionIndex::Outgoing` edges only, up
  to `HopCount` hops from the focus. Outgoing-only is deliberate: a two-way door is
  two edges (validation rule `partition.transition.unpaired` keeps it that way), so
  paired doors are symmetric by construction, and a OneWay edge INTO the focus does
  not preload its source. Neighbors get dormant participation and
  `Sources.Neighbor`. Each zone's hop distance is the minimum over paths.
- **Pins** get their `Minimum` participation (fielded OR with whatever the zone
  already earned) and `Sources.Pinned`. A pin on a zone the manifest does not contain
  is ignored (validation owns reporting broken content; the policy stays total).
- **Cap.** When demand exceeds `ResidentZoneCap`, non-focus non-pinned Zones
  are evicted by hop descending, runtime-derived spatial cost descending, then
  Zone ID descending. Connections author no eviction policy. Focus plus pins
  may exceed the cap.
- **Output order:** ascending zone id value, always.

### Gate R2

New tests in `test/runtime/ZoneDemandTests.cpp`, each over small hand-built manifests:

- `FocusAloneIsFullParticipation`
- `NeighborsWithinHopCountAreDormant` (hop 2 fixture; hop-3 zone absent)
- `OneWayInboundEdgeDoesNotPreloadSource`
- `PinnedZoneCarriesItsMinimum` (pin beyond hop range appears; pin on a neighbor
  ORs participation)
- `CapEvictsByHopThenDerivedCostThenId`
- `PinsAndFocusExceedCap`
- `RecordsAscendByZoneId`
- `InvalidFocusYieldsEmptyDemand` (invalid id and unknown-but-nonzero id)

Grep audit: `grep -n "unordered" engine/src/zone/ZoneDemand.cpp` feeds no returned
container (membership sets are fine; outputs iterate sorted data).

---

## R3. `WorldPartitionRuntime`

### New files: `engine/include/zone/WorldPartitionRuntime.h`, `engine/src/zone/WorldPartitionRuntime.cpp`

```cpp
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneDemand.h>
#include <zone/ZoneRuntime.h>

// How one zone's cooked refs become a registry. The game owns this knowledge
// (component registration, scene deserialization, collision restore); the
// policy layer only decides WHEN a zone loads (00-overview D10).
struct ZoneLoadRecipe
{
    AsyncZoneLoader::BuildFn      Build;
    AsyncZoneLoader::FinalizeFn   Finalize;
    std::shared_ptr<AssetPreload> Preload;   // null when the zone preloads nothing
};

using ZoneLoadRecipeFn = std::function<ZoneLoadRecipe(const ZoneHeader&)>;

// The metadata and policy layer over ZoneRuntime: owns the cooked manifest and
// decides desired residency; never deserializes scenes, never owns registries.
// Single-threaded by contract: every method runs on the owner (main) thread.
class WorldPartitionRuntime
{
public:
    WorldPartitionRuntime(ZoneLoadRecipeFn recipe, WorldPartitionStreamingConfig config);

    // Cooked manifest in; adjacency index built here. Refuses (false, message in
    // *error) when any zone lacks a CookedSceneRef or when
    // ValidateWorldPartitionManifest reports any Error-severity record: a broken
    // manifest fails at load time, not mid-traversal.
    bool LoadManifest(WorldPartitionManifest manifest, std::string* error);
    [[nodiscard]] bool HasManifest() const;
    [[nodiscard]] const WorldPartitionManifest& Manifest() const;   // asserts HasManifest

    // The one policy input. Position resolution: candidate zones are those whose
    // Bounds contain the position; the current focus wins if it is a candidate
    // (hysteresis at doorway thresholds); otherwise the smallest-volume candidate,
    // ties broken by ascending zone id; no candidate keeps the previous focus
    // (sticky: bounds gaps and overhangs are normal geometry, not focus changes).
    void SetFocus(Vec3d position);
    // For when position is not meaningful (menus, scripted warps). Asserts the
    // zone exists in the manifest.
    void SetFocus(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const;

    void PinZone(ZoneId zone, ZoneParticipation minimum);
    void UnpinZone(ZoneId zone);

    // Once per frame from the owning game system. Computes demand, layers linger
    // state, and diffs desired against resident plus in-flight: issues dormant
    // BeginLoad through the recipe, SetParticipation changes, CancelLoad for
    // undemanded in-flight loads, and DestroyZone for zones whose linger expired.
    // Never touches the focus zone's residency.
    void Update(double deltaSeconds, AsyncZoneLoader& loader, ZoneRuntime& zones);

    // Why is this zone resident: rebuilt every Update, includes Lingering
    // entries, ascending zone id. The surface Track C item 1's telemetry writer
    // serializes; coordinate there rather than duplicating a record type.
    [[nodiscard]] std::span<const ZoneDemandRecord> DemandRecords() const;
};
```

Pinned `Update` semantics, exhaustive:

1. Demand = `ComputeZoneDemand(...)` with the stored manifest, index, focus, pins,
   config. No manifest or no valid focus: demand is empty and only step 5 runs.
2. **Load.** Desired zones that are neither loaded (`ZoneRuntime::IsZoneLoaded`) nor
   in flight (`AsyncZoneLoader::IsLoading`) get `BeginLoad(zone, recipe.Build,
   recipe.Finalize, ZoneParticipation{}, recipe.Preload)`, in this order: hop
   ascending, runtime-derived cost ascending, Zone ID ascending.
3. **Participation.** Desired zones that are loaded and whose current participation
   differs from desired get `SetParticipation`. The focus zone is always desired
   full; the previous focus demotes to dormant in the same update its successor
   promotes.
4. **Cancel.** In-flight zones no longer desired get `CancelLoad`; a false return
   (work mid-flight) is retried on the next update, and the zone is reported
   `Lingering` meanwhile.
5. **Linger and destroy.** Loaded, undesired, non-focus zones accumulate linger time
   by `deltaSeconds`; while lingering they are demoted to dormant participation and
   reported with `Sources.Lingering`. At or past `LingerSeconds` they are
   `DestroyZone`d. Re-entering the desired set resets the zone's linger clock to
   zero.
6. Records rebuilt: the demand set plus lingering zones, ascending zone id.

### The canonical hand-written cooked fixture (D7)

Embedded in the test file as a raw string and pinned here as the shape every R test
shares. Three zones, one region, doorway pair Hub/Hallway, doorway pair
Hallway/Arena, with cooked fields filled by the fixture
paths (the tests' recipes never open them):

```json
{
  "format_version": 1,
  "name": "TraversalFixture",
  "start_zone": "00000000000000a1",
  "regions": [ { "id": "00000000000000b1", "name": "Fixture Region" } ],
  "zones": [
    { "id": "00000000000000a1", "name": "Hub", "region": "00000000000000b1",
      "scene": "levels/hub.level.json",
      "bounds": { "min": [-8, 0, -8], "max": [8, 4, 8] },
      "cooked_scene": "levels/hub.cooked.json",
      "cooked_collision": "levels/hub.collision.json",
      "content_hash": "00000000000000d1" },
    { "id": "00000000000000a2", "name": "Hallway", "region": "00000000000000b1",
      "scene": "levels/hallway.level.json",
      "bounds": { "min": [9, 0, -2], "max": [20, 4, 2] },
      "cooked_scene": "levels/hallway.cooked.json",
      "cooked_collision": "levels/hallway.collision.json",
      "content_hash": "00000000000000d2" },
    { "id": "00000000000000a3", "name": "Arena", "region": "00000000000000b1",
      "scene": "levels/arena.level.json",
      "bounds": { "min": [21, 0, -8], "max": [40, 8, 8] },
      "cooked_scene": "levels/arena.cooked.json",
      "cooked_collision": "levels/arena.collision.json",
      "content_hash": "00000000000000d3" }
  ],
  "transitions": [
    { "id": "00000000000000c1", "from": "00000000000000a1", "to": "00000000000000a2",
      "topology": "doorway" },
    { "id": "00000000000000c2", "from": "00000000000000a2", "to": "00000000000000a1",
      "topology": "doorway" },
    { "id": "00000000000000c3", "from": "00000000000000a2", "to": "00000000000000a3",
      "topology": "doorway" },
    { "id": "00000000000000c4", "from": "00000000000000a3", "to": "00000000000000a2",
      "topology": "doorway" }
  ]
}
```

### Gate R3

New tests in `test/runtime/WorldPartitionRuntimeTests.cpp`: headless, zero-thread
`AsyncTaskQueue` plus manual drain (the `AsyncZoneLoadTests.cpp` pattern); test
recipes build trivial registries and count invocations per zone.

- `LoadManifestRefusesUncookedZones` (empty `cooked_scene` on one zone)
- `LoadManifestRefusesErrorValidation` (dangling transition endpoint)
- `NeighborLoadsDormantOnUpdate` (participation observed dormant at finalize)
- `FocusZoneParticipationIsFull`
- `FocusChangeDemotesOldFocusToDormant`
- `LingerThenDestroy` (advance dt below, at, and past `LingerSeconds`)
- `LingerClockResetsOnRedemand`
- `PinKeepsZoneResidentPastLinger`
- `UndemandedInFlightLoadIsCancelled` (and retried when `CancelLoad` fails once)
- `FocusResolutionPrefersCurrentZoneOnOverlap`
- `PositionInNoZoneKeepsFocus`
- `SmallestVolumeWinsTiesById`
- `FocusZoneIsNeverUnloaded`
- `DemandRecordsAreDeterministicallyOrdered` (two identical updates, identical
  records)

Grep audits: `grep -rn "mutex\|std::thread\|std::async" engine/src/zone/` empty;
`grep -rn "mt19937\|random_device" engine/src/zone/` empty; no `#include` of any
editor header.

---

## R4. The traversal gate as headless tests

Track C item 2's traversal-hitch harness has not landed (verified: no such target in
the tree). The design doc gate ("zero missed fixed ticks under the traversal-hitch
harness") is expressed structurally now, as tests in the same file over the R3
fixture; when the harness lands, this scenario becomes one of its scripts.
Coordination, not duplication: do not build the harness here.

- `TraversalAttachesDormantAheadOfCrossing`: focus positions scripted from Hub center
  to Arena center in fixed steps; every zone attach is observed dormant (finalize
  reads `GetParticipation`), never visible-on-attach.
- `TraversalFlipsParticipationOnEntryAndUnloadsBehind`: Hallway flips to full when
  the position enters its bounds; Hub demotes to dormant, lingers, and is destroyed
  after `LingerSeconds` of scripted dt.
- `TraversalDemandRecordsExplainResidencyEveryStep`: at every scripted step, every
  resident zone has a record whose sources are non-empty ("why is Hallway resident"
  is answerable at each step, design doc Section 10).
- `TraversalIdenticalAcrossTaskThreadCounts`: the ordered sequence of (load issued,
  attach, participation change, destroy) events is byte-identical with the queue
  drained serially and with one task thread.
- `TraversalRunsFullTickBudget`: the scripted traversal executes through
  `RuntimeFrameLoop`; every frame runs exactly `TickBudget::TicksToRunThisFrame`
  fixed ticks (`RuntimeFrameSnapshot::FixedTicks` equals the scheduled budget), so
  streaming work never eats a scheduled tick. This is the "zero missed ticks"
  assertion available without the harness.

Gate R4: the five tests green with the rest of the suite.

---

## R5. Template game world path

### What changes (all in `template/`)

1. The build/finalize bodies inside `TemplateGame::LoadMap` split into free functions
   in the same file, shared by both paths:
   `BuildZoneRegistry(Registry&, /*scene path, caches*/)` (component registration plus
   `ParseSceneFile`) and `FinalizeZoneScene(Registry&, ...)` (`LoadSceneJson` plus
   `LoadZoneCollision`). Pawn and camera spawning move OUT of the shared finalize:
   they are world-lifetime concerns, not zone concerns.
2. `TemplateGame` gains `std::optional<WorldPartitionRuntime> Partition;` and two
   console commands registered at the same point the map handler binds (the startup
   script mechanism turns `+world x`/`+zone y` argv into `world x`/`zone y` commands
   at `ConsolePhase::GameLoaded`; verify and note the phase in the commit):
   - `world <name>`: reads `assets/.cooked/worlds/<name>.sworld.json`, parses with
     `ReadWorldPartitionManifest`, constructs `Partition` with a recipe closing over
     the shared build/finalize (per-zone cooked scene and collision refs resolved
     under the cooked scan root; nothing else), spawns the camera and pawn once into
     `ZoneRuntime::Global()`, seeds focus from the manifest `StartZone`.
   - `zone <hexid>`: `SetFocus(ZoneId)` override, usable before or after `world`
     takes effect (queued until the manifest is present).
3. A game-local frame-update system (`WorldPartitionUpdateSystem`, registered in
   `OnRegisterSystems`) reads the pawn's `WorldTransform` position each frame and
   calls `Partition->SetFocus(position)` then `Partition->Update(dt, loader, zones)`.
   The streaming config comes from the engine runtime config fields (R1).
4. The `map` path and `kPlayZone` stay byte-identical in behavior: `+map` still
   works for single-zone content, and `world` refuses when a `map` zone is loaded
   (one world at a time; the message says to restart).
5. Verification sub-task recorded in the spec: confirm the global registry
   participates in the frame spans the pawn's movement and camera systems consume
   (it hosts the pawn now). If it does not, stop; that is an engine gap to raise,
   not a template workaround.

### Gate R5

Suite green (template compiles into the module tests). Manual: `app --game <module>
+world <fixture> ` walks Hub to Arena with zones loading ahead and unloading behind;
`+map levels/<name>` unchanged. Grep audit: `grep -n "ZoneId{ 1 }"
template/src/TemplateGame.cpp` appears only in the map path.

---

## R6. PIE play-from-world

`PieDriver::Cook` in world mode already runs `CookWorld` (E1 W6). This stage changes
only the Play launch line in world mode: `+world <world file stem> +zone <focus zone
hex id>` instead of `+map <focus scene stem>`, against the cooked world manifest W6
writes. Cook-first gating unchanged. Single-zone (legacy) documents keep `+map`.

### Gate R6

Manual: editor Cook then Play on the three-zone fixture world starts in the focus
zone and streams neighbors during traversal. Suite green.

---

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The design doc Section 4 gate restated: game code calls `SetFocus` as the player
  moves through a three-zone cooked world; zones load dormant ahead of traversal,
  flip participation on entry, and unload behind; R4's tick-budget assertion holds;
  no game code names a `ZoneId` except `kPlayZone` in the legacy map path and the
  `zone` command's parsed argument.
- The Section 10 runtime slice reproducible end to end on the R3 fixture.
- Grep audits from R3 and R5 pass at phase end.
- Nothing under `editor/` changed except the R6 launch line in `PieDriver`.
- Delete the `WorldPartitionRuntime` row from the roadmap's Section 3
  vocabulary-versus-code table (design doc Section 13 item 5; the table's own rule).
