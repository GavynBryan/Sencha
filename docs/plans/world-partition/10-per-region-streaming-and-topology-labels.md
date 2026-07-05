# Phase: Per-Region Streaming Shape; Honest Topology Labels

Status: IMPLEMENTED 2026-07-05 (all four stages; see "Implementation notes" at the
end for deviations and owed manual gates). Independent of Phase G (07) and of the
portal retirement (09). Read `00-execution-overview.md` and
`06-streaming-maturation.md` first.

## Why

The demand policy already runs two OR'd sources: `Neighbor` (graph BFS over
transitions, hop count) and `Spatial` (`Radius`, point-to-box proximity). Those are
exactly the two world topologies:

- a **graph** world (rooms connected by authored doorways, no diagonal loading)
  wants `Radius = 0`, `HopCount >= 1`: reachability is the authored graph;
- a **grid** world (cells tiled across a town, diagonals load) wants `Radius > 0`:
  proximity loads the surrounding ring, corners included, with zero authored edges.

But `Radius`, `HopCount`, and `ResidentZoneCap` are a single **global**
`EngineRuntimeConfig`, so a world cannot be part graph and part grid, and there is no
authoring surface for the choice at all. A designer opens the partition panel and
sees only the transition `Topology` enum (Doorway/Seam/Teleport) with no explanation,
which does not control streaming and, post-09, barely controls anything (only Teleport
differs, and only in the unpaired-pairing exemption). This phase adds the streaming
surface the two topologies actually need, and relabels topology honestly so it stops
reading as the streaming control it is not.

## The model

- **Per-region override of the demand shape.** `RegionRecord` gains an optional
  `RegionStreamingConfig` (each field optional; absent inherits the world/global
  base). The three fields that define the shape: `HopCount`, `Radius`,
  `ResidentZoneCap`. Linger and the neighbor-participation flags stay global (uniform
  quality-of-life, not topology; revisit trigger recorded below).
- **Selected by the focus zone's region.** Focus is always exactly one zone; its
  region picks the config in force. Crossing a region boundary switches the shape
  naturally as focus moves. A pure resolver merges the region override over the base
  and both the runtime and the editor preview call it (D18: preview consumes only the
  pure policy).
- **Both sources still run and OR.** Nothing about the two demand sources changes; the
  per-region config only sets their parameters. A grid region near a graph region still
  preloads the graph region's entrance across the boundary (a cross-region edge, or
  proximity), then flips to graph shape on entry.
- **No `StreamingMode` enum.** "Graph" versus "radius" is read off the values
  (`Radius == 0` versus `Radius > 0`), never stored as an enum and never branched on in
  the runtime (directive 1/3). The panel derives a label for the designer; the type
  system and the policy stay value-driven.

---

## Standing decisions

- **S-D1. The override lives on `RegionRecord`, optional, absent = inherit.**
  `format_version` stays 1; the reader is tolerant of an absent `streaming` key (the E1
  / 07 precedent). No world re-saves to gain the field.
- **S-D2. Three per-region fields only: `HopCount`, `Radius`, `ResidentZoneCap`.**
  They are the demand shape. `LingerSeconds`, `NeighborVisible`, `NeighborPhysics` stay
  global. Revisit trigger: a region demonstrably needs different linger or
  participation than its neighbors.
- **S-D3. Resolution is a pure function beside the demand policy.**
  `ResolveRegionStreamingConfig(manifest, focus, base)` merges field by field; the
  runtime passes its `EngineRuntimeConfig`-derived base, the preview passes its own.
  The policy signature does not change (it still takes one resolved config).
- **S-D4. Region streaming edits are non-undoable `WorldDocument` verbs** (the D11
  `AddZone` / transition-verb pattern): mint, `MarkManifestEdited`, `RunValidation`.
- **S-D5. Topology is relabeled, not changed.** No behavior added or removed in this
  phase (that is transition timing, Track C, out of scope). The enum stays; the UI
  stops implying it controls streaming and states what each value actually does today.

---

## Stages (one commit each, suite green, layering script green)

### S1. Manifest field, serialization, validation, resolver

- `engine/include/zone/WorldPartitionManifest.h`: `RegionRecord` gains
  `RegionStreamingConfig Streaming;` where

```cpp
// Per-region overrides of the streaming demand shape. Each field optional:
// absent inherits the world/global base (EngineRuntimeConfig). Graph versus
// radius character is derived from the values: Radius == 0 is graph-only.
struct RegionStreamingConfig
{
    std::optional<int32_t> HopCount;        // >= 0
    std::optional<double>  Radius;          // finite, >= 0
    std::optional<int32_t> ResidentZoneCap; // >= 1

    friend bool operator==(const RegionStreamingConfig&,
                           const RegionStreamingConfig&) = default;
};
```

- `WorldPartitionManifest.cpp`: read/write an optional `"streaming"` object on each
  region (`hop_count`, `radius`, `resident_zone_cap`; only present keys emitted, whole
  object omitted when all absent). Unknown/malformed values set the parse error the way
  the topology parser does.
- `WorldPartitionValidation.cpp`: new rule `partition.region.streaming_invalid`
  (Error, Region source) when a present `HopCount < 0`, `Radius` non-finite or `< 0`,
  or `ResidentZoneCap < 1`. Ascending region id.
- `WorldPartitionValidation.cpp`: `partition.graph.unreachable` becomes region-aware,
  or it warns on every edge-less cell in a grid region (the exact authoring pattern
  this phase teaches). Each region with an explicit `Radius > 0` override collapses to
  one reachability node: its zones are mutually reachable, and the region is reached
  through any cross-region edge or by containing the start zone. An island grid region
  with no edge into it still warns on every cell. Validation is pure over the
  manifest, so only explicit overrides participate (the inherited base lives in
  `EngineRuntimeConfig`); a grid region is authored with an explicit radius, so the
  real case is covered.
- New pure resolver in `engine/include/zone/ZoneDemand.h` /
  `engine/src/zone/ZoneDemand.cpp`:

```cpp
// The streaming config in force while `focus` is resident: the focus zone's
// region overrides applied over `base`, field by field. Pure; the runtime and
// the editor preview both resolve through it. Invalid or region-less focus
// returns base unchanged.
[[nodiscard]] WorldPartitionStreamingConfig
ResolveRegionStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                             const WorldPartitionStreamingConfig& base);
```

- Tests (`test/runtime/`): region streaming round-trips through JSON (present subset,
  all-absent omits the key, legacy manifest without the key still loads);
  `streaming_invalid` fires per bad field; `ResolveRegionStreamingConfig` overrides
  present fields and inherits absent ones, and returns base for an unknown focus;
  `unreachable` stays quiet for an edge-less cell in an explicit-radius region reached
  through one entrance edge, and still fires for every cell of an island radius region
  with no edge into it.

### S2. Runtime and editor preview consume the resolver

- `WorldPartitionRuntime`: keep `Config_` as the base. `Update` reads the config in
  two places one line apart: `ComputeZoneHopRanks(..., Config_.HopCount, ...)` (the
  load-issue ordering) and `ComputeZoneDemand(..., Config_, ...)`. Resolve
  `ResolveRegionStreamingConfig(Manifest_, Focus_, Config_)` once above both and pass
  the resolved config to both calls. Resolving only the demand call would demand zones
  past the base hop count that the ranks BFS never reaches; rank-less zones sort last
  in load ordering (hop = INT_MAX), so exactly the zones a region preloads deeper
  would load last and ignore `PreloadPriority`. The cap needs nothing extra: it is
  applied inside `ComputeZoneDemand`. No other runtime change.
- Editor preview: the two build sites (`WorldPartitionPanel.cpp` demand list,
  `ZoneBoundsRenderer.cpp` bounds tint) resolve through the same function against the
  preview focus and pass the whole resolved config. Today both construct
  `{ .HopCount, .Radius }` only, which leaves `ResidentZoneCap` at the struct default;
  a grid region's raised cap must preview with the real eviction, so the two-field
  construction goes. Default preview is the resolved per-region shape (a graph region
  reads as a diamond, a radius region as a disc). The manual sliders become explicit
  per-field preview overrides with a clear-to-inherited affordance, the manifest's own
  absent-inherits model: absent shows the resolved value, edited overrides that one
  field for the preview only. No seeding from the resolved config: a re-seed on region
  change clobbers the user's tweak, and a one-time seed decays into an absolute knob
  that hides the authored shape.
- Preview visualization, same stage (the shape must be obvious, not inferred from
  tinted boxes):
  - `ZoneBoundsRenderer.cpp`: when the resolved `Radius > 0`, draw the radius as a
    segment circle around the preview focus position, so which cells fall inside is
    read directly and a mistuned radius is visible at a glance.
  - `WorldPartitionPanel.cpp`: the preview section leads with the config in force:
    the focus zone's region name and the resolved values, inherited fields marked
    (`Shape from "Downtown": Hop 1 (inherited), Radius 250, Cap 12`). Focus selects
    the region config; this line is what makes that rule legible at boundaries.
- Tests (`test/runtime/WorldPartitionRuntimeTests.cpp`-style): a two-region fixture
  (graph region: Radius 0, Hop 1; grid region: Radius > 0) demands the diamond set from
  a focus in the graph region and the proximity disc from a focus in the grid region,
  with no manifest edge between the grid cells. Plus a load-ordering case: a region
  raising `HopCount` above the base issues loads for the deeper zones in hop order,
  not last (proves the ranks call resolved too).

### S3. Editor verbs and panel UI

- `WorldDocument` verbs (S-D4): `SetRegionHopCount(region, std::optional<int32_t>)`,
  `SetRegionRadius(region, std::optional<double>)`,
  `SetRegionResidentCap(region, std::optional<int32_t>)`; passing `std::nullopt` clears
  the field back to inherited.
- `WorldPartitionPanel`: each region row grows a streaming subsection: a derived badge
  (`Radius > 0` -> "Radius", else "Graph"; "(inherited)" when the region has no
  override) and inline editors for hop count, radius, and cap, each with an
  inherit/clear affordance. The badge is presentation only, computed from the resolved
  values; nothing stores a mode.
- Tests: verbs rewrite and revalidate (present, then cleared); the derived-label helper
  (pure, if factored out) maps radius/override state to the right badge. Manual gate:
  on a two-region world, set one region's radius and watch the streaming preview switch
  that region from a diamond to a disc, with the radius circle drawn and the
  config-in-force line naming the region whose shape is active.

### S4. Honest topology labels

- `TransitionInlineEditor.cpp` (and the panel badge legend): the topology combo gains a
  per-value help line stating what each actually does today, not aspirationally:
  - Teleport: no geometric relationship; no reverse edge required.
  - Seam / Doorway: identical today; reserved for seamless-versus-threshold transition
    timing (not yet implemented), and never affect streaming.
- No enum change, no behavior change. This is the S-D5 relabel: the surface stops
  implying topology is the streaming control.
- Gate: suite green (UI text; no logic). The manual read-through confirms the combo no
  longer reads as a streaming knob.

## Non-goals

- No `StreamingMode` enum, no per-connection streaming fields, no genre-named types.
- No change to transition topology behavior (transition timing is Track C).
- No per-zone (as opposed to per-region) streaming override: the region is the unit a
  designer reasons about for streaming shape. Revisit trigger: a single zone inside a
  region needs a different shape than its region.
- Linger and neighbor-participation stay global (S-D2).

## Open questions

- **Boundary hysteresis between shapes.** Crossing from a grid region (large resident
  set) into a graph region (small) evicts the grid ring quickly; crossing back reloads
  it. The existing linger covers short round-trips; if boundary flapping shows up in
  practice, per-region linger (deferred in S-D2) is the lever. Not a v1 blocker.
- **Graph-into-grid boundary preload.** Focus-selected config is asymmetric at region
  boundaries. Grid into graph preloads the graph entrance across the boundary (edge or
  proximity). Graph into grid preloads only the edge-connected entrance cell: the
  surrounding ring loads after focus crosses and the config flips, so a seamless exit
  has the entrance cell resident and seconds of ring load around it. Entrance-cell
  residency plus linger makes this a soft failure; not a v1 blocker. Revisit trigger:
  a visible ring pop when exiting a graph region into a grid region. Recorded escape
  hatch: per-zone-region demand semantics (a zone's own region governs how that zone
  is demanded, regardless of the focus region). That fixes the asymmetry but breaks
  S-D3's single-resolved-config policy signature, so it is a deliberate later trade,
  never a quiet v1 tweak.
- **Resident cap interaction: confirmed against the code (2026-07-05).** Spatial
  entries rank `HopCount + 1` with nearer-survives-longer priority; cap eviction is
  hop descending, then priority ascending, with focus and pins exempt. Under a tight
  cap the far spatial ring evicts first, then nearer spatial, then the outermost graph
  hops; the focus and its immediate graph ring survive. S-D2 makes the cap per-region
  so a grid region can raise it.

## Implementation notes (2026-07-05)

- The preview's shared resolution lives in `ResolvePreviewStreamingConfig`
  (`viewport/WorldViewSettings.{h,cpp}`): per-region shape resolved over engine
  defaults, then the per-field preview overrides. Both preview consumers call it.
- The user sidecar's `preview_hop_count` follows the absent-inherits model: written
  only when the preview override is set, so a fresh session previews the authored
  shape. A legacy sidecar carrying the key loads as an explicit override (clearable
  in the panel).
- The panel's inline region editors clamp to the validation bounds (hop >= 0,
  radius >= 0, cap >= 1); `streaming_invalid` still guards hand-edited manifests.
- The derived badge reads "Graph (inherited)" when a region authors no override;
  any authored field drops the marker ("Radius" when the authored radius is
  positive, else "Graph").
- Owed manual GUI gates: the S3 two-region diamond-to-disc preview walkthrough
  (radius circle drawn, config-in-force line naming the active region) and the S4
  combo read-through.
