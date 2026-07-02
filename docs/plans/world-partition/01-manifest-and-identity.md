# Phase 1: Manifest and Identity

Status: execution spec (2026-07-02). Implements Phase 1 of
`docs/plans/world-partition-authoring.md` (Sections 3 and 9 there; read them first,
then `00-execution-overview.md` in this directory, before writing code).

Scope: the partition id vocabulary, the manifest records, JSON round-trip, the
adjacency index, and manifest-level validation. Pure data and pure functions.

Non-goals (do not touch): `ZoneRuntime`, `AsyncZoneLoader`, `ZoneParticipation`,
anything in `editor/`, any cook code, any loading policy. If a change seems to need
one of these, stop (overview Section 4).

Stages S1 through S4, in order, each a separate commit with the suite green.

---

## S1. `ZoneId` migration

### What changes

Replace the body of `engine/include/zone/ZoneId.h` (currently a hand-rolled
`uint32_t` struct with `Invalid()`, `IsValid()`, and a `std::hash` specialization)
with:

```cpp
#pragma once

#include <core/identity/StrongId.h>

// The persistent zone identity: minted by the editor when a zone is created,
// serialized in the world partition manifest, and used verbatim as the runtime
// residency key in ZoneRuntime. Zero is invalid (StrongId convention).
using ZoneId = StrongId<struct ZoneIdTag, uint64_t>;
```

Everything else (equality, ordering, hashing, binary Serialize/Deserialize) comes from
`StrongId`. Delete the hand-rolled `std::hash<ZoneId>`; do not redefine it.

### Call-site checklist (exhaustive at time of writing; re-grep to confirm)

`grep -rn "ZoneId::Invalid" engine editor app example template test`

1. `engine/include/world/registry/Registry.h:88` (`MakeGlobalRegistry`):
   `registry.Zone = ZoneId::Invalid();` becomes `registry.Zone = ZoneId{};`
2. `editor/kyusu/src/document/EditorDocument.cpp:80`:
   `Registry_.Zone = ZoneId::Invalid();` becomes `Registry_.Zone = ZoneId{};`
3. Any test call sites the grep finds: same replacement.

Unchanged by design (verify they still compile, do not edit them):

- `ZoneId{ 1 }`-style aggregate literals (tests, `example/CubeDemo`,
  `template/src/TemplateGame.cpp`). Aggregate init of the StrongId works unchanged.
- `example/JobSystemBenchmark/JobSystemBenchmark.cpp:120` casts an index to build an
  id; the widened underlying type accepts it.
- `Registry.h` asserts on `Zone.IsValid()`: `StrongId::IsValid()` has the same
  meaning.

### What does NOT change in S1

No text form, no minting, no new headers. S1 is the type swap alone, so that if
anything breaks, the diff is minimal and obvious.

### Gate S1

Full suite green with no test edits beyond the mechanical `Invalid()` replacements.
Grep audit: `grep -rn "ZoneId::Invalid" .` returns nothing;
`grep -rn "struct std::hash<ZoneId>" .` returns nothing.

---

## S2. Partition ids and text forms

### New file: `engine/include/zone/WorldPartitionIds.h`

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <core/identity/StrongId.h>
#include <zone/ZoneId.h>

// Persistent partition identities beside ZoneId. Minted by the editor, serialized
// in the world partition manifest. Zero is invalid.
using RegionId     = StrongId<struct RegionIdTag,     uint64_t>;
using TransitionId = StrongId<struct TransitionIdTag, uint64_t>;

// Text forms follow the AssetId precedent exactly: 16-digit lowercase hex, no
// prefix, because JSON numbers are doubles and cannot hold 64 bits. FromString is
// strict: exactly 16 hex digits and nonzero, anything else is nullopt so malformed
// content fails loudly at parse time.
[[nodiscard]] std::string ZoneIdToString(ZoneId id);
[[nodiscard]] std::optional<ZoneId> ZoneIdFromString(std::string_view text);
[[nodiscard]] std::string RegionIdToString(RegionId id);
[[nodiscard]] std::optional<RegionId> RegionIdFromString(std::string_view text);
[[nodiscard]] std::string TransitionIdToString(TransitionId id);
[[nodiscard]] std::optional<TransitionId> TransitionIdFromString(std::string_view text);
```

Implementation in `engine/src/zone/WorldPartitionIds.cpp`: one static hex
encode/decode pair shared by the six functions, written to the same strictness as
`AssetIdToString`/`AssetIdFromString` in `core/assets/AssetId.h` (mirror the pattern;
do not add a dependency from `zone/` to the asset front door for two ten-line
functions).

No `SpaceId`, no `WorldId`, no minting function in the engine (overview D4: minting
is editor-side, arrives in Phase E1).

### Gate S2

New tests in `test/runtime/WorldPartitionIdTests.cpp`:

- `ZoneIdHexRoundTrip`: nonzero id to string to id, equal; string is 16 lowercase
  hex digits.
- `IdFromStringRejectsMalformed`: wrong length, uppercase, stray characters, all-zero,
  and empty each return nullopt (one assertion per case, all three id types share the
  implementation so testing one type's rejects plus one round-trip per type is enough).

---

## S3. Manifest records and JSON round-trip

### New file: `engine/include/zone/WorldPartitionManifest.h`

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <math/geometry/3d/Aabb3d.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

class JsonValue;

enum class TransitionTopology : uint8_t
{
    Seam,       // contiguous geometry, no visual break
    Doorway,    // authored opening; expects a portal entity in From's content
    Teleport,   // discontinuous; no geometric relationship implied
};

struct TransitionFlags
{
    bool OneWay = false;
};

struct RegionRecord
{
    RegionId    Id;
    std::string Name;
};

struct ZoneHeader
{
    ZoneId      Id;
    std::string Name;
    RegionId    Region;                    // exactly one, validated
    std::string SceneRef;                  // project-relative authored scene path (overview D8)
    Aabb3d      Bounds;                    // world coordinates (single implicit space in v1.0)
    bool        BoundsOverridden = false;  // true: designer-set, cook must not recompute

    // Cooked-manifest-only fields; zero/empty in authored manifests. The world cook
    // (Phase E1 stage W6) fills them; Phase R consumes them.
    std::string CookedSceneRef;
    std::string CookedCollisionRef;
    uint64_t    CookedContentHash = 0;
};

struct TransitionRecord
{
    TransitionId       Id;
    ZoneId             From;
    ZoneId             To;
    TransitionTopology Topology = TransitionTopology::Doorway;
    TransitionFlags    Flags;
    int32_t            PreloadPriority = 0; // higher loads earlier within the neighbor set
    // No portal reference: linkage is content-side (overview D1).
};

struct WorldPartitionManifest
{
    std::string Name;
    ZoneId      StartZone;   // optional; invalid means "not designated" (validation warns)
    std::vector<RegionRecord>     Regions;
    std::vector<ZoneHeader>       Zones;
    std::vector<TransitionRecord> Transitions;
};

// Strict on identity and enums, tolerant on unknown keys (ignored, for forward
// compatibility). Returns nullopt with a message in *error on the first hard
// failure. Does not validate cross-references; that is WorldPartitionValidation.
[[nodiscard]] std::optional<WorldPartitionManifest>
ReadWorldPartitionManifest(const JsonValue& root, std::string* error);

// Writes every field, ids as hex strings, arrays in the order stored. Cooked-only
// fields are written only when nonzero/nonempty, so authored files stay clean.
[[nodiscard]] JsonValue WriteWorldPartitionManifest(const WorldPartitionManifest& manifest);
```

Implementation in `engine/src/zone/WorldPartitionManifest.cpp`. Follow the
`RuntimeConfig.cpp` error style (accumulate into `std::string* error`, return
nullopt). Keys are snake_case only (this is a new format; the dual camelCase reading
in `RuntimeConfig` is legacy tolerance this format does not inherit).

### The JSON schema (authored `.sworld`)

The canonical fixture; tests embed exactly this shape.

```json
{
  "format_version": 1,
  "name": "TestWorld",
  "start_zone": "00000000000000a1",
  "regions": [
    { "id": "00000000000000r1-not-real-see-note", "name": "Chozo Ruins" }
  ],
  "zones": [
    {
      "id": "00000000000000a1",
      "name": "Hub Room",
      "region": "00000000000000b1",
      "scene": "levels/hub_room.level.json",
      "bounds": { "min": [-8.0, 0.0, -8.0], "max": [8.0, 4.0, 8.0] },
      "bounds_overridden": false
    }
  ],
  "transitions": [
    {
      "id": "00000000000000c1",
      "from": "00000000000000a1",
      "to": "00000000000000a2",
      "topology": "doorway",
      "one_way": false,
      "preload_priority": 0
    }
  ]
}
```

(Note: id strings above are illustrative shape only; real fixtures use valid 16-hex
values.)

Parse rules, pinned:

- `format_version` is required and must equal 1; anything else is a hard error
  naming the value ("unsupported format_version 2").
- Every id field parses through the strict FromString helpers; a malformed or zero
  id is a hard error naming the array and index ("zones[3].id is malformed").
- `topology` maps `"seam"`, `"doorway"`, `"teleport"` to the enum; anything else is
  a hard error naming the value. Write emits the same strings.
- `start_zone` is optional; absent means invalid `ZoneId{}`.
- `bounds` is required per zone: objects with `min`/`max` arrays of exactly 3
  numbers each.
- `one_way` optional, default false. `preload_priority` optional, default 0, must
  fit int32.
- Cooked fields (`cooked_scene`, `cooked_collision`, `content_hash` as 16-hex
  string) are optional on read.
- Unknown keys anywhere are ignored without warning.
- Duplicate ids, dangling references, and semantic problems are NOT parse errors;
  they parse fine and validation reports them (S4). Parse rejects only what cannot
  be represented.

### Gate S3

Tests in `test/runtime/WorldPartitionManifestTests.cpp`:

- `ManifestJsonRoundTrip`: parse the canonical fixture, write it, parse again,
  compare all fields of both results for equality (add `operator==` defaults to the
  record structs for this).
- `ReadRejectsUnsupportedFormatVersion`
- `ReadRejectsMalformedZoneId` (and, via shared code, that the error message names
  the location)
- `ReadRejectsUnknownTopology`
- `ReadRejectsMissingBounds`
- `ReadIgnoresUnknownKeys` (fixture with extra keys parses identically)
- `ReadAcceptsMissingOptionals` (no start_zone, no one_way, no preload_priority)
- `WriteOmitsEmptyCookedFields` (authored manifest writes no `cooked_*`/`content_hash`
  keys)
- `ReadParsesButDoesNotValidate` (a fixture with a dangling transition endpoint
  parses successfully; the point is the parse/validate split)

---

## S4. Adjacency index and validation

### New file: `engine/include/zone/WorldPartitionIndex.h`

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <zone/WorldPartitionManifest.h>

// Derived adjacency over a parsed manifest: per-zone outgoing and incoming
// transition lists. Built once after parse; the manifest stores each edge exactly
// once and this is the only adjacency representation (the design doc rejects
// per-header edge lists). Deterministic: zones and edge lists are ordered by
// ascending id value, never by container iteration order.
class WorldPartitionIndex
{
public:
    static WorldPartitionIndex Build(const WorldPartitionManifest& manifest);

    // Indices into manifest.Transitions, sorted by TransitionId value.
    // Empty span for an unknown zone.
    [[nodiscard]] std::span<const uint32_t> Outgoing(ZoneId zone) const;
    [[nodiscard]] std::span<const uint32_t> Incoming(ZoneId zone) const;

    [[nodiscard]] bool ContainsZone(ZoneId zone) const;

private:
    // Sorted zone id array plus offset tables into one shared index buffer;
    // O(zones + transitions) memory, binary-search lookup.
    // (Exact layout is the implementer's, the contract above is not.)
};
```

Implementation in `engine/src/zone/WorldPartitionIndex.cpp`. Edges whose endpoints
are unknown zones are skipped by Build (validation reports them; the index stays
usable on broken input).

### New file: `engine/include/zone/ContentRiskRecord.h` (overview D6)

```cpp
#pragma once

#include <cstdint>
#include <string>

// The structured content warning record (roadmap Track C item 1 names it; this is
// its first landing, minimal on purpose). Streaming records will extend the family;
// coordinate rather than duplicate if Track C item 1 has landed by the time you
// read this.
enum class ContentRiskSeverity : uint8_t
{
    Warning,
    Error,
};

enum class ContentRiskSourceKind : uint8_t
{
    World,
    Region,
    Zone,
    Transition,
};

struct ContentRiskRecord
{
    ContentRiskSeverity   Severity = ContentRiskSeverity::Warning;
    ContentRiskSourceKind Kind = ContentRiskSourceKind::World;
    uint64_t              SourceId = 0;   // offending record's id value; 0 for world-level
    std::string           RuleId;         // stable, dotted, see the rule table
    std::string           Message;        // human-readable, names ids in hex
};
```

### New files: `engine/include/zone/WorldPartitionValidation.h` / `.cpp`

```cpp
// Pure. No logging, no file IO, no engine services. Emits records in deterministic
// order: rule table order, then ascending source id within a rule.
[[nodiscard]] std::vector<ContentRiskRecord>
ValidateWorldPartitionManifest(const WorldPartitionManifest& manifest,
                               const WorldPartitionIndex& index);
```

The v1.0 manifest-only rule table. Rule ids are stable API (the panel and tests key
on them); severities are pinned:

| RuleId | Severity | Fires when |
| --- | --- | --- |
| `partition.id.duplicate` | Error | Any id value appears twice within its record type. |
| `partition.zone.region_missing` | Error | A zone's `Region` names no region record. |
| `partition.transition.endpoint_missing` | Error | `From` or `To` names no zone. |
| `partition.transition.self_loop` | Error | `From == To`. |
| `partition.transition.unpaired` | Warning | A non-OneWay Seam or Doorway has no reverse edge (an edge with endpoints swapped). Teleports are exempt. |
| `partition.zone.scene_missing` | Error | `SceneRef` is empty. |
| `partition.zone.bounds_invalid` | Error | `Bounds` fails `Aabb3d::IsValid()`. |
| `partition.bounds.overlap` | Warning | Two zones' bounds intersect (`Aabb3d::Intersects`). One record per unordered pair, SourceId = lower zone id, message names both. |
| `partition.graph.unreachable` | Warning | With a valid `StartZone`: a zone not reachable from it following edges in either direction for paired edges and forward only for OneWay. |
| `partition.world.no_start_zone` | Warning | `StartZone` is invalid or names no zone. When it fires, `partition.graph.unreachable` is skipped entirely. |

Rules that need loaded zone content (portal linkage, portal normal, entity bounds
containment, player start) are NOT in this phase; they arrive with the phases that
load content (E1 onward). Do not stub them.

File-existence checking for `SceneRef` is also not here: validation is pure and
takes no filesystem. The editor layer (E1) checks resolvability against its content
roots and reports through the same record type.

### Gate S4

Tests in `test/runtime/WorldPartitionValidationTests.cpp`:

- `CleanFixtureEmitsNothing` (the canonical three-zone fixture from S3, made
  internally consistent, produces an empty vector; this test is the anchor, write
  it first).
- One test per rule, named after it (`DuplicateIdFires`, `RegionMissingFires`,
  `EndpointMissingFires`, `SelfLoopFires`, `UnpairedDoorwayFires`,
  `TeleportNeedsNoPair`, `SceneMissingFires`, `BoundsInvalidFires`,
  `BoundsOverlapFiresOncePerPair`, `UnreachableZoneFires`,
  `NoStartZoneFiresAndSuppressesReachability`): each takes the clean fixture,
  breaks exactly one thing, and asserts exactly the expected record (rule id,
  severity, kind, source id) plus nothing else.
- `RecordsAreDeterministicallyOrdered`: a fixture violating three rules asserts the
  exact output order twice.
- Index tests in `test/runtime/WorldPartitionIndexTests.cpp`:
  `OutgoingAndIncomingAreSortedByIdValue`, `UnknownZoneYieldsEmptySpans`,
  `DanglingEdgesAreSkippedByBuild`.

---

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The Phase 1 gate from the design doc: a hand-authored three-zone `.sworld` fixture
  round-trips, and every rule above fires on a deliberately broken fixture.
- Grep audits: no `ZoneId::Invalid`, no hand-rolled `std::hash` for partition ids,
  no `#include` from `engine/` into `editor/`, no `unordered_map`/`unordered_set`
  iteration feeding any returned container in the new files.
- `engine/CMakeLists.txt` gains the three new .cpp files beside the existing
  `src/zone/` entries; test CMake follows the `test/runtime/` siblings.
- Nothing in `editor/`, `app/`, `example/`, or `template/` changed except the one
  line in `EditorDocument.cpp` from S1.
