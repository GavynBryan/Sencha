# Phase N: Streaming Maturation (runtime demand-model extensions)

Status: execution spec (2026-07-05), NOT yet implemented. Owner review required
before any stage starts (`00-execution-overview.md` Section 5 discipline). Read the
design doc Sections 4 and 6.5, overview decisions D7, D10, D14, and D17, and
`03-runtime-streaming.md` first: this phase extends the mechanisms Phase R built and
changes none of their contracts.

Prerequisites: Phases 1, E1, E2, E3, R complete (they are), plus the V-series
feedback-loop work (named transitions, pair-aware portals, streaming preview): the
editor preview must exist so every knob added here is tunable without a cook.

Scope: four demand-model extensions, all entering through data and config
(directive 3): render-only neighbors, spatial-radius demand, tag-gated transitions,
per-edge preload depth.

Non-goals (do not build any of it): the elevator/airlock recipe (pins plus a
Teleport-topology focus switch already express it; document on request), see-through
portal rendering (v2.0, Track C item 7), runtime portal components (D13 stands),
participation tiers beyond the N1 mask (LOD-style graduated logic is Track C item 4's
full shape), transition timing semantics (history reset, input/camera policy: Track C
item 6), any scripting-language surface.

Stages N1 through N4, in order, each a separate commit with the suite green.
N2 through N4 are independent of each other but all build on N1's participation mask.

---

## Standing decisions for every stage

- **Data first.** Every extension is authorable in the manifest or config; no
  gameplay-named types, no special-case branches in the runtime (directive 1/3).
- **The pure policy stays pure.** All four extensions land inside `ComputeZoneDemand`
  / `ComputeZoneHopRanks` / `ResolveFocusZone` (plus plain-data inputs); the editor
  preview picks them up with zero extra wiring. Nothing in this phase touches
  `ZoneRuntime`, `AsyncZoneLoader`, or the attach path.
- **Attach stays dormant.** Zones still `BeginLoad` with `ZoneParticipation{}` and
  converge to their desired participation on the next Update: visible-on-attach
  remains forbidden (no discontinuity, R-spec standing decision).
- **Determinism.** Same inputs, same demand records, same order; the R4 traversal
  suite extends to cover each new input.

---

## N1. Render-only neighbors (D17)

The v1.0 "dormant neighbors, accepted pop" decision is retired: a doorway should
read as real space before the player crosses it.

### What changes

- `EngineRuntimeConfig` gains `bool StreamingNeighborVisible = true` and
  `bool StreamingNeighborPhysics = true` (keys `streaming_neighbor_visible`,
  `streaming_neighbor_physics`). Physics defaults on deliberately: the focus flip
  happens at the bounds threshold and colliders sync one step later, so a pawn
  crossing fast needs the neighbor's static collision already resident.
- `WorldPartitionStreamingConfig` mirrors both. `ComputeZoneDemand` gives neighbors
  `ZoneParticipation{ .Visible = NeighborVisible, .Physics = NeighborPhysics }`
  instead of dormant; pins still OR on top. Logic and Audio stay off for neighbors:
  nothing simulates or sounds until entry.
- The editor preview's demand list derives its participation string from
  `Desired` instead of assuming dormant (one line in `WorldPartitionPanel.cpp`).

### Pinned semantics

- The focus zone is unchanged (full). Lingering zones still demote to fully dormant.
- A neighbor that becomes the focus flips Logic/Audio on in the same Update that
  moves focus (existing convergence, no new path).

### Gate N1

Tests in `test/runtime/ZoneDemandTests.cpp` and `WorldPartitionRuntimeTests.cpp`:
`NeighborsPreloadVisibleByDefault`, `NeighborConfigOffKeepsDormant`,
`AttachIsStillDormantBeforeFirstConvergence`, and the R4 traversal update
`TraversalNeighborIsVisibleBeforeCrossing`. Manual: stand in Hub, see the Hallway
through the doorway; nothing moves or sounds in it until entry.

---

## N2. Spatial-radius demand (open-world fields)

Graph hops cannot describe a Hyrule-field grid of seam-connected cells; proximity
can.

### What changes

- `EngineRuntimeConfig` gains `double StreamingRadius = 0.0`
  (`streaming_radius`, finite, >= 0; 0 = off). `WorldPartitionStreamingConfig`
  mirrors it.
- `ZoneDemandSources` gains `bool Spatial`.
- `ComputeZoneDemand` gains the focus position:
  `std::optional<Vec3d> focusPosition` parameter (nullopt = graph-only, the
  editor-preview and menus case keeps working unchanged). When Radius > 0 and a
  position is present: every zone whose bounds' closest point lies within Radius of
  the position joins the demand set with the N1 neighbor participation and
  `Sources.Spatial` (OR-ed with Neighbor when both apply).
- `WorldPartitionRuntime` stores the last `SetFocus(Vec3d)` position;
  `SetFocus(ZoneId)` substitutes that zone's bounds center. The editor preview
  passes the preview camera position.

### Pinned semantics

- Eviction rank for spatial-only zones: hop = `HopCount + 1` (graph neighbors are
  always preferred), priority = `-(int32)std::lround(distance * 100.0)` (nearer
  survives longer; the quantization makes ties exact and deterministic), id
  descending as always. Focus and pins still exceed the cap; spatial zones never do.
- Distance is point-to-AABB (closest point), not center-to-center: a huge field
  cell whose edge is near the player is near.

### Gate N2

`RadiusZeroIsGraphOnly`, `ZonesWithinRadiusJoinDemand` (grid fixture, no
transitions at all), `SpatialUsesClosestPointNotCenter`,
`SpatialEvictsAfterGraphNeighbors`, `ZoneIdFocusFallsBackToBoundsCenter`. Manual:
a 3x3 seam-less grid world streams a moving 8-zone neighborhood in the preview.

---

## N3. Tag-gated transitions (condition-based zones)

A quest door that does not open yet should not preload what is behind it.

### What changes

- `TransitionRecord` gains `std::vector<std::string> RequiredTags;` (manifest key
  `required_tags`, array of dotted tag names, optional; empty = always open). ALL
  listed tags must be active for the edge to exist. All/Any/None query shapes are
  deliberately deferred until a real Any/None case appears (directive 4); the
  storage is names, never ids (tag ids are registration-order runtime values and
  are never serialized, `core/gameplay_tags` rule).
- `WorldPartitionRuntime` gains
  `void SetWorldTags(std::vector<std::string> activeTags);` (sorted-set member
  lookup at compute time; the game pushes its world-state tags whenever they
  change, e.g. from its quest/save system). `ComputeZoneHopRanks` skips edges whose
  RequiredTags are not all present in the supplied set (a new
  `std::span<const std::string>` input, empty span = no gating, so existing callers
  and the editor preview compile unchanged until they opt in).
- Editor: the transition inline editor gains a tags text field (comma-separated),
  routed through a new `SetTransitionRequiredTags` verb; the streaming preview
  panel gains a matching "world tags" scratch input so designers can toggle gates
  in the preview. Validation: no new rule (tag existence is game-registration
  state the editor cannot see; the preview's live reflow is the feedback).

### Pinned semantics

- Gating removes the EDGE, not the zone: a zone reachable another way stays
  demanded. The focus zone is never gated (focus comes from position, not edges).
- Tag comparison is exact string equality on dotted names; hierarchy semantics
  (IsDescendantOf) are deferred with the query shapes.

### Gate N3

`UngatedEdgesUnchanged`, `GatedEdgeInvisibleWithoutTag`,
`TagArrivalReflowsDemandNextUpdate`, `GatingNeverRemovesReachableZones`, manifest
round-trip with and without `required_tags`, verb + inline-editor coverage in the
editor suites. Manual: gate Hub->Arena on `quest.bridge_lowered` and watch the
preview reflow as the scratch tag toggles.

---

## N4. Per-edge preload depth (authored recursion)

A hub can demand that one critical corridor preloads several zones deep while the
global horizon stays at one hop.

### What changes

- `TransitionRecord` gains `int32_t PreloadDepth = 0` (manifest key
  `preload_depth`, >= 0, optional). `SetTransitionPreloadDepth` verb plus a field
  in the transition inline editor.
- `ComputeZoneHopRanks`: when the BFS crosses an edge, the remaining hop budget on
  the far side becomes `max(budgetAfterThisHop, edge.PreloadDepth)`. Depth extends
  REACH through that edge and whatever lies beyond it; it does not raise priority.

### Pinned semantics

- Depth propagates: an edge with depth 3 lets the BFS continue 3 hops past it even
  if the global HopCount was already spent; edges beyond it apply their own depth
  the same way. Hop values recorded in ranks stay true BFS distances, so cap
  eviction still trims the deepest zones first.
- The cap is the safety net: authored depth never exceeds `ResidentZoneCap`.

### Gate N4

`DepthExtendsThroughEdge` (chain fixture, HopCount 1, depth 3 reaches 3 deep),
`DepthDoesNotLeakSideways` (a sibling edge without depth stays at the global
horizon), `DepthChainsThroughFurtherEdges`, `CapStillBoundsAuthoredDepth`, manifest
round-trip, verb + editor coverage.

---

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The R4 traversal suite green with every new input at its default (defaults
  reproduce today's behavior except N1's visible neighbors, which is the point).
- Editor preview reflects all four knobs live with zero additional wiring beyond
  the pinned panel lines.
- Grep audits: no `unordered` iteration into any policy output; no tag ID
  serialized anywhere; `grep -rn "Metroidvania\|Hyrule\|quest\." engine/` returns
  nothing (world tags are game-authored strings, engine code never names one).
- `03-runtime-streaming.md` gains a pointer note that the demand model's extension
  surface lives here.
