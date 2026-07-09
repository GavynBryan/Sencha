# Zone Architecture Review: Containment, Demand Cost, Graph Authoring

Status: assessment and phase proposals (2026-07-09). Nothing here is implemented;
owner review decides which parts become numbered phases, and every code change
named below is a proposal until then. Read `00-execution-overview.md`,
`06-streaming-maturation.md`, `09-retire-portals-doors-as-world-content.md`, and
`10-per-region-streaming-and-topology-labels.md` first.

## Why

Authoring practice has surfaced four drawbacks against the partition model:

1. Overlapping zones feel discouraged, but containment patterns (a room inside a
   larger area, a corridor or garden wrapping around an interior zone) are
   legitimate shapes that subdividing the outer zone does not always express well.
2. Hand-authoring preload reach per transition (`PreloadDepth`) is tedious; a
   better way of computing the preload set beyond "neighbors within N hops" is
   wanted.
3. Preloading every neighbor is expensive for a large or high-fan-out zone.
4. The flat transition list stops answering adjacency questions on graph-shaped
   worlds, and there is no way to express where a doorway is, only that an edge
   exists.

This document grounds each drawback in the code as it stands, separates actual
defects from perceived limits, and proposes the smallest mechanisms that remove
the friction. Two of the four reduce to small pure-policy and validation fixes,
not model changes.

---

## Verdicts up front

**1. Containment is mechanically broken in two specific places, not
architecturally discouraged.** The model already handles overlap better than its
reputation: focus resolution picks the smallest containing zone
(`ZoneDemand.cpp:72-83`) and validation only warns. The "discouraged" experience
comes from two defects, both fixable in place:

- Focus hysteresis keeps the previous focus while its bounds contain the position
  (`ZoneDemand.cpp:66-70`). A zone fully contained inside the current focus can
  therefore never win focus by movement: walk from a garden zone into the house
  zone inside it and focus stays on the garden forever, so the house never gains
  Logic or Audio participation while the player stands in it. Nothing simulates
  or sounds inside the inner zone. Fix: containment-aware handoff with a
  switch-in margin (Section 2.2).
- `partition.bounds.overlap` treats full containment and partial interpenetration
  identically (`WorldPartitionValidation.cpp:285-319` over `BoundsInterpenetrate`
  at `:44-55`), so the intended containment pattern warns forever. Fix: split the
  rule (Section 2.3).

Nested runtime zones stay rejected (the design doc's Section 2.1 argument is
untouched: containment is resolved point-wise in the pure policy, never stored as
hierarchy). Multi-box derived bounds are deferred with a trigger (Section 2.4).

**2. Preload authoring tedium is structural: hops and authored depth are
topology proxies for a measurable quantity.** The question every knob
approximates is "will this zone be resident before the player can reach it, and
what does keeping it resident cost." The cook can measure the cost side (cooked
bytes per zone) for free. Proposal: per-zone cost records plus a cost budget in
the demand policy, riding the existing eviction order. `HopCount` becomes a
generous ceiling instead of a hand-tuned horizon; most `PreloadDepth` uses
dissolve; the count cap stops being blind to zone size. This is Track C item 1's
`ZoneBudgetRecord` gaining its first producer and consumer, not a parallel
scheme (Section 3).

**3. The expensive-neighbor case is the same fix plus one owner decision.** The
cost budget bounds the neighbor set by measured weight. Separately: Teleport
edges currently preload their targets with Visible and Physics participation like
every neighbor, but a Teleport implies no geometric relationship, so there is
nothing a dormant attach would visually pop. Giving Teleport dormant preload is
the one honest streaming behavior topology can carry; it reverses part of doc
10's S-D5 story ("topology never affects streaming"), so it is an explicit owner
decision with the UI labels updated in the same commit (Section 3.3).

**4. Build the graph panel; the recorded trigger has fired. Express doorway
locations as an optional anchor on the transition record, not as revived
portals.** The design doc deferred the node-link panel with the trigger "the
first world where the tree stops answering adjacency questions"; drawback 4 is
that trigger firing. The anchor is a world-owned point on an already-authored
edge: no geometry, no zone ownership, no derivation of connections (09's P-D2
intact). It ships with three same-phase consumers (edge rendering, load-order
refinement, validation) so it is never a dead field, and it is exactly the datum
the recorded future consumers (transition timing volumes, audio propagation, the
greenlit cross-zone planner, minimap markers) will attach to (Section 4).

---

## 1. Grounding

Verified against the tree at review time.

- Demand pipeline: `ComputeZoneHopRanks` (BFS over outgoing edges, remaining-hop
  budget, `PreloadDepth` relaxation), `ComputeZoneDemand` (focus full, neighbors
  `{NeighborVisible, NeighborPhysics}`, spatial radius at hop `HopCount + 1`,
  pins, cap eviction by hop descending then priority ascending then id
  descending), `ResolveFocusZone`, `ResolveRegionStreamingConfig`. All pure, all
  deterministic (`engine/src/zone/ZoneDemand.cpp`).
- Runtime: `WorldPartitionRuntime::Update` issues dormant loads in hop-ascending,
  priority-descending order (`WorldPartitionRuntime.cpp:155-190`), converges
  participation, cancels undemanded in-flight loads, lingers then destroys.
- Editor: `WorldPartitionPanel` is the only world-scoped authoring surface.
  Connections are authored from a zone row's Connect To submenu through a
  deferred `ConnectZones` call (`WorldPartitionPanel.cpp:49-60`,
  `TransitionConnect.cpp`), displayed as undirected pair rows, edited through a
  right-click inline editor. The streaming preview computes the same pure policy
  (D18) and the viewport already draws the transition graph as center-to-center
  lines (`ZoneBoundsRenderer.cpp:135-156`). All viewport tools bind the single
  focus zone document; no viewport gesture is world-scoped today.
- Bounds: derived in the editor from brush vertices, unioned per zone at save and
  revalidate, skipped when `BoundsOverridden` (which no UI can set; only hand
  edits of the `.sworld`). The cook never touches bounds.
- Consumers today: the streaming policy itself, plus render, physics, logic, and
  audio transitively through `ZoneParticipation` spans. Navigation (including the
  greenlit hierarchical cross-zone planner), audio spatialization, runtime
  occlusion, and minimaps are roadmap items with zero code. Claims below about
  "future consumers" are recorded direction, never justification for present
  fields.

---

## 2. Containment and wrap-around zones

### 2.1 The failure, traced

Fixture: zone Outer (a garden, a looping corridor, a large chamber) whose derived
bounds fully contain zone Inner (the house, the wrapped room). A doorway pair
connects them. This is the exact shape drawback 1 describes, and note that a
wrap-around zone produces it unavoidably: the AABB of a ring contains the hole.

- Authoring: works today. Each zone owns its entities; bounds derive per zone.
- Demand: works today. Inner is a hop-1 neighbor of Outer and vice versa;
  radius demand inside the courtyard correctly holds Outer.
- Validation: `partition.bounds.overlap` warns on the pair forever. This is where
  "overlapping zones are discouraged" comes from, and for this pattern the
  warning is wrong: the author did nothing suspicious.
- Focus: broken. `ResolveFocusZone` returns `previous` whenever its bounds still
  contain the position (`ZoneDemand.cpp:66-70`). Every position inside Inner is
  also inside Outer, so once focus is Outer it stays Outer. Consequences while
  the player is inside Inner: participation stays at the neighbor preload mask
  (Visible, Physics), Logic and Audio never activate, and the region config in
  force stays Outer's. The pattern only appears to work because neighbors render
  and collide by default.
- The failure is asymmetric: if focus is Inner (a scripted `SetFocus(ZoneId)`, a
  save restore), walking out hands off to Outer correctly; walking back in
  sticks to Outer again.

Subdividing Outer into non-containing pieces is today's workaround, which is
exactly the cumbersome authoring the drawback names. After the two fixes below,
subdivision goes back to being purely a streaming-granularity choice, never a
correctness requirement.

### 2.2 F1: containment-aware focus handoff

Change `ResolveFocusZone` (pure, no new state, editor preview picks it up with
zero wiring per D18):

- Candidates are zones whose bounds contain the position, unchanged.
- If `previous` is a candidate: it wins, unless some candidate is strictly
  contained within `previous`'s bounds and contains the position by a margin `m`
  on every axis. Then the smallest-volume such candidate wins (ties by ascending
  id). Strict containment: inside on all axes within the validation epsilon
  (1e-4), and strictly smaller volume, so identical boxes never qualify.
- If `previous` is not a candidate: unchanged (smallest volume, then nearest by
  closest point).

The margin only gates the switch-in; the switch back out happens when the
position leaves the inner box, at which point the outer zone is the only
candidate. Pacing across the inner boundary therefore flips focus at most once
per full crossing of the margin band, the same character as the existing doorway
hysteresis, and partial-overlap slivers between side-by-side rooms behave
exactly as today (neither strictly contains the other). Nested containment
resolves stepwise, or directly to the innermost margin-satisfying candidate when
several qualify. For inner boxes thinner than `2m` on an axis, the margin clamps
to a quarter of that axis extent so paper-thin zones stay reachable.

`m` is a new global tunable: `StreamingFocusMargin` on `EngineRuntimeConfig`
(double, world units, default 0.5, key `streaming_focus_margin`, finite and
`>= 0`), mirrored onto `WorldPartitionStreamingConfig`. Global, not per-region:
it is a quality-of-life constant like linger, and S-D2's three-field pin on
`RegionStreamingConfig` stands.

Tests (`ZoneDemandTests`): `ContainedZoneWinsFocusFromWrapper`,
`ContainmentHandoffRequiresMargin`, `WrapperRegainsFocusOnExit`,
`NestedContainmentResolvesToInnermost`, `PartialOverlapHysteresisUnchanged`
(and `ResolveFocusZonePrefersContainmentWithHysteresis` re-scoped to the
partial-overlap fixture it is actually about).

### 2.3 F2: validation distinguishes containment from bleed

Split `partition.bounds.overlap`:

- Partial interpenetration (interpenetrating, neither box containing the other):
  keep the existing Warning unchanged. This is the accidental-bleed and
  vertical-layering signal it was designed to be.
- Full containment (inner inside outer within epsilon, strictly smaller volume):
  no record when at least one transition connects the pair in either direction,
  because a connected contained zone is the working pattern F1 makes legal.
  When no edge connects them, emit `partition.bounds.contained_unconnected`
  (Warning, Zone source, lower id): an enclosed but unlinked zone is almost
  always a missed connection. Zones whose region carries an explicit
  `Radius > 0` override are exempt, mirroring the radius-region carve-out the
  unreachable rule already has (proximity streams them without edges).
- Two zones with equal bounds fail the strictness test and stay in the
  partial-interpenetration bucket: identical boxes still warn, which is right
  (that shape is usually misassigned content, not design).

Tests (`WorldPartitionValidationTests`): `ContainedConnectedPairIsClean`,
`ContainedUnconnectedPairWarns`, `PartialOverlapStillWarns`,
`IdenticalBoundsStillWarn`, `RadiusRegionContainmentExempt`.

### 2.4 Deferred: multi-box derived bounds

A single AABB overstates wrap-around and L-shaped zones (the box includes the
hole). F1 makes the common containment patterns correct anyway, because the
inner zone wins by strictness wherever it exists, and a position in the wrapper's
hole with no inner zone belongs to the wrapper by any reasonable reading.
Deferred: deriving per-zone bounds as a small capped set of boxes (the level
cook's cell clusters are the natural source) so containment and spatial-radius
tests stop seeing the hole at all. Trigger: a real world where focus mispicks or
spatial over-demand survive F1 and F2, observed in the streaming preview. The
manifest stays O(zones): a capped box count per zone, never per-entity data.

Adjacent gap, owner call on bundling: `BoundsOverridden` exists in the manifest
and is respected by every recompute site, but no editor surface can set it; the
only path is hand-editing the `.sworld`. If wrap-around authors want hand-set
bounds before multi-box lands, that is one non-undoable verb plus an inspector
checkbox and a bounds field, in the S-D4 pattern.

---

## 3. Demand: from authored horizons to measured cost

### 3.1 What the knobs approximate

`HopCount` approximates time-to-reach (badly, across mixed zone sizes: one hop is
a closet or a cathedral). `PreloadDepth` patches the corridors where that
approximation fails, one edge at a time (the reported tedium). `ResidentZoneCap`
approximates memory in units of "zones", blind to zone weight (the reported
expensive-neighbor problem). `PreloadPriority` orders load issue and eviction but
cannot express "this neighbor is heavy". Each knob is reasonable; together they
ask the author to hand-solve a cost model the build system can measure.

### 3.2 Cook-measured zone cost, region cost budget

Data: the world cook already writes each zone's cooked scene and collision and
hashes the content (`WorldCook.cpp`); measuring the byte sizes there is free.
Emit them per zone. The home for the datum is Track C item 1's
`ZoneBudgetRecord` (keyed by `ZoneId`, JSON beside the cooked manifest), which is
planned, unbuilt, and pinned by the design doc as the place budget numbers live
instead of `ZoneHeader` (Section 3.2 deliberate omission: no inline budget
numbers, no second source of truth). This proposal is that record's first real
producer (the world cook) and first consumer (the demand policy). Coordinate
with the Track C spec's record shape; do not mint a parallel record. Initial
fields: cooked scene bytes, cooked collision bytes, preload manifest bytes.
Measured load milliseconds join later from the traversal harness (Track C item
2), same record, new field.

Policy (pure): `ComputeZoneDemand` gains an optional span of per-zone costs
(sorted by zone id) and the resolved config gains `CostBudget` (uint64 bytes,
0 = off), with a matching optional override on `RegionStreamingConfig` and an
`EngineRuntimeConfig` base (`streaming_cost_budget`). Enforcement rides the
existing eviction stage (`ZoneDemand.cpp:336-370`): the same evictable set
(never focus, never pins), the same comparator (hop descending, priority
ascending, id descending), evicting until both the count cap and the cost budget
hold. Zones without a cost record count as zero cost and are reported as such in
the demand records so the preview can flag them; when no cost data exists at all
the budget is inert and behavior is byte-identical to today. Determinism:
integer byte sums, total-order comparator, no float accumulation.

What this changes for the author: `HopCount` stops being the binding constraint
and becomes a generous ceiling (set 4 to 6 once per world or region); the budget
decides how deep preload actually reaches, so cheap room chains preload deep
automatically and heavy zones preload shallow. That is the requested "better way
of calculating which zones to preload": measured weight, not authored recursion.
`PreloadDepth` survives as the targeted exception (a critical corridor that must
win regardless of cost), which most worlds should rarely need; if authored uses
drop to zero after budgets prove out, retiring it is a later, separate decision.
Deliberately not proposed: auto-raising the hop horizon when cost data appears.
A hidden mode flip keyed on data presence is exactly the kind of implicit
behavior the config surface avoids; raising `HopCount` stays an explicit
authoring act.

`ResidentZoneCap` stays as the absolute count guard (S-D2's per-region override
of it is unchanged). Adding `CostBudget` to `RegionStreamingConfig` widens
S-D2's "three fields only" pin by one field of the same demand-shape family;
that is a deliberate amendment for the owner to ratify, not a quiet drift.

Editor: the streaming preview's config-in-force line gains the budget; demand
rows gain per-zone cost and a running total; zones dropped by budget read as
such. Costs come from the last cook's records when present, labeled stale by
content hash mismatch. All read-side, no new wiring beyond the records reader.

Tests (`ZoneDemandTests`, `WorldPartitionRuntimeTests`, cook tests):
`BudgetOffIsByteIdentical`, `BudgetEvictsHeaviestLastRankFirst`,
`FocusAndPinsExceedBudget`, `MissingCostCountsZeroAndFlags`,
`RegionBudgetOverrideResolves`, `WorldCookEmitsZoneCostRecords`, plus a
round-trip for the record JSON.

### 3.3 Teleport preloads dormant (owner decision)

Today every neighbor preloads with `{NeighborVisible, NeighborPhysics}`
regardless of edge topology (`ZoneDemand.cpp:237-258`). For Seam and Doorway
that is the point (D17: the far side must read as real space before crossing).
For Teleport it protects nothing: the enum's own definition is "discontinuous;
no geometric relationship implied", so there is no sightline or threshold a
dormant attach could pop. A hub with a dozen teleport destinations currently
pays render and physics residency for all of them.

Proposal: neighbors discovered across a Teleport edge demand dormant
participation (still loaded, still instant to flip on focus switch, which is the
original dormant-attach recipe and is invisible behind a discontinuous
transition by definition). A zone reachable through both a Teleport and a
geometric edge takes the stronger mask. Seam and Doorway stay identical to each
other (their split remains Track C item 6's timing work, untouched here).

This is an explicit reversal of part of doc 10's S-D5 surface story ("topology
never affects streaming"), which is why it is fenced as an owner decision: the
S4 help text was written to be honest, and it stays honest only if the labels
change in the same commit ("Teleport: no geometric relationship; preloads
dormant; no reverse edge required"). The alternative (a per-edge participation
override field) is rejected: it adds a hand-authored knob per connection, which
is the tedium this review is trying to remove, and doc 10's no-per-connection-
streaming-fields non-goal argues the same way.

Tests: `TeleportNeighborsPreloadDormant`, `StrongerEdgeMaskWins`,
`TeleportFocusSwitchConvergesFull`, plus the S4 label read-through.

### 3.4 Deferred demand ideas, recorded so they are not re-litigated silently

- Time-horizon demand (estimated seconds-to-reach versus measured seconds-to-
  load per zone): strictly better than hops plus budget, but needs anchors
  (Section 4.3) for distance and Track C item 2 telemetry for load rates.
  Trigger: traversal-harness misses that budget, priority, and anchor ordering
  cannot fix on a real world.
- Velocity-aware focus (`SetFocus` gains a velocity; spatial demand and edge
  priorities bias toward the heading): cheap but tuning-sensitive, and linger
  already absorbs reversal churn. Same trigger as above.
- Learned or history-based prediction: rejected. Nondeterministic inputs,
  invisible policy, no authoring story.

---

## 4. The graph surface and doorway anchors

### 4.1 Trigger state

The design doc deferred the node-link panel with the trigger "the first world
where the tree stops answering adjacency questions" (Section 11), and the demand
inspector with "a designer asks the question interactively". Drawback 4 is both
triggers firing. What follows honors the deferral discipline rather than
overriding it.

### 4.2 Graph panel v1 (read, select, connect)

A dockable panel beside the partition panel (`IEditorPanel`, registered like the
existing panels). It draws from `WorldDocument`'s manifest, the
`WorldPartitionIndex`, and the pure demand policy only (D18: no runtime, no
loader). ImGui draw-list canvas; no new UI framework.

- Layout is derived, not stored: nodes at zone bounds centers projected onto the
  world horizontal plane, with a deterministic, id-ordered minimum-separation
  relaxation for coincident or stacked zones. v1 stores nothing; manual node
  pinning is deferred (trigger: a world whose spatial layout is illegible, for
  example teleport-heavy or vertically stacked; the storage decision then is a
  shared sidecar beside the `.sworld`, never the manifest, never the cook).
- Nodes: zone name, region tint, focus ring, demand tint when the streaming
  preview is on (same palette as `ZoneBoundsRenderer` so the two views read as
  one model).
- Edges: pair-collapsed like the list rows, styled by data already on the record:
  topology (Seam solid, Doorway solid with a threshold tick, Teleport dashed),
  arrowhead for one-way, lock glyph for `RequiredTags`, small badges for nonzero
  `PreloadPriority` and `PreloadDepth`.
- Interactions: click selects (selection shared with the connections list);
  right-click opens the existing context menu embedding
  `DrawTransitionInlineEditor` unchanged; dragging node to node mints a
  connection through the existing deferred `ConnectZones` pattern
  (`WorldPartitionPanel.cpp:49-60`); hovering an edge or node tints the matching
  bounds in the viewport.

Independent cheap list fixes that should not wait for the panel: a filter box
over connection rows, grouping by region, sort by name or topology, and
hover-to-highlight of the two endpoint bounds in the viewport.

The viewport already draws the transition graph as center-to-center lines in
preview (`ZoneBoundsRenderer.cpp:135-156`); with anchors (below) those lines
route through real doorway positions, which makes the viewport itself a
readable graph for seam-and-doorway worlds. The panel earns its keep on
teleport-heavy and high-fan-out worlds where world-space layout is the problem,
and as the demand inspector's natural canvas. Both surfaces share every
mechanism; neither replaces the list.

### 4.3 Anchors: doorway locations as world data

`TransitionRecord` gains `std::optional<Vec3d> Anchor` (manifest key `anchor`,
array of three numbers, omitted when absent; `format_version` stays 1, absent
inherits nothing because there is nothing to inherit). The anchor is where the
crossing physically happens, in world coordinates (one implicit space in v1.0,
same caveat as `Bounds`; when spaces land, an anchor is in the source zone's
space, which is one more reason the field is optional).

Ownership answers the drawback's requirement directly: the anchor is a field on
a world-owned edge record. It belongs to no zone, lives in no zone's content,
and is authored from the world-scoped surface. It is explicitly not the portal
returning, on every axis that killed the portal (09): no entity, no brush, no
geometry to cook around, no arbitrary owner, and above all no derivation:
`ConnectZones` remains the only way an edge exists, and an anchor never creates,
implies, or verifies a connection (P-D2 intact). If a door mesh ever arrives it
is world-scene content bound to a transition (09's recorded direction), and its
natural default placement is the edge's anchor.

Pair semantics: a symmetric pair shares one location; the setter writes both
directions, and validation warns on divergence (hand edits):
`partition.transition.anchor_mismatch`.

Authoring, in the world-scoped surface so no zone document is involved:

- Select the edge (list or graph), Place Anchor, then one viewport click
  ray-picks the point (context and header-only zones are pickable for snap
  already; the gesture rides the panel's deferred-action pattern rather than a
  new `ITool`, because `ToolContext` binds the focus zone document and this
  gesture must not). This is the one small editor-infrastructure piece: a
  world-scoped one-shot viewport pick, which the anchor shares with any future
  world-level placement need.
- Numeric fields and a Clear in the transition inline editor.

Same-phase consumers, so the field is never parsed-but-unread (overview rule
12):

1. Rendering: the viewport transition lines and the graph panel route through
   anchors when present (center-to-center remains the fallback). Wrapped and
   contained zones stop drawing edges through walls; a zone with three doors
   shows three distinct edge roots.
2. Load ordering: among same-hop neighbors, distance from the focus position to
   the discovering edge's anchor breaks `PreloadPriority` ties (quantized like
   N2's spatial priority, deterministic). Standing near the north door loads the
   north neighbor before the far south one, with zero authoring beyond placing
   the anchor. This composes with Section 3's budget: rank order decides what
   the budget keeps.
3. Validation: `partition.transition.anchor_outside` (Warning) when the anchor
   lies outside the union of the two endpoint bounds inflated by epsilon;
   `anchor_mismatch` per above. Teleport edges are exempt from `anchor_outside`
   (either end is a legitimate location for a pad).

Recorded future consumers (direction, not justification, and none built now):
Track C item 6's timing model needs a crossing surface to hang threshold
semantics on, and today it would have nowhere to stand; audio propagation
through openings wants the aperture position; the greenlit hierarchical
cross-zone planner refines zone-graph edges into navmesh queries at exactly
these points; a minimap marks doors here. Each lands with its feature.

Deferred: anchor extent (an opening's half-size). A point serves every
same-phase consumer; the trigger is the first consumer that needs aperture size
(timing volumes or audio occlusion), and the field grows beside `Anchor` then.

Tests: manifest round-trip with and without `anchor`; pair-setter symmetry;
`anchor_outside` including the Teleport exemption; `anchor_mismatch`;
`AnchorDistanceBreaksSameHopTies`; editor verb and inline-editor coverage; a
graph-panel smoke test at whatever depth the editor harness supports.

---

## 5. Suggested sequencing

Independent phases; each becomes its own numbered execution spec if accepted.

- **Phase A (smallest, highest value): containment fixes.** Section 2.2 focus
  handoff, Section 2.3 validation split, optionally the `BoundsOverridden`
  affordance. Pure policy plus validation plus tests; no format change. This
  alone retires drawback 1.
- **Phase B: graph legibility.** Section 4.2 list fixes, then the panel.
  Editor-only, no format change.
- **Phase C: anchors.** Section 4.3 manifest field, verbs, world-scoped pick,
  and the three consumers.
- **Phase D: cost records and budget.** Section 3.2, coordinated with Track C
  item 1's record shape.
- **Phase E (one decision, one commit): Teleport dormant preload.** Section 3.3
  with the label updates.

A and B have no coupling; C strengthens B's rendering and D's ordering but
neither depends on it; E rides any. If effort must be cut, A alone is still
worth shipping.

## 6. Non-goals

- Nested runtime zones. The Section 2.1 rejection stands; containment lives in
  the pure policy as point tests, never as stored hierarchy, parent registries,
  or hierarchical participation.
- Portals, or any geometry-derived connection authoring (P-D2 stands).
- `IZonePopulationStrategy` or any second policy seam: everything here
  parameterizes the one concrete policy with data.
- Per-edge participation or streaming-shape fields beyond what exists
  (Section 3.3's rejection).
- Stored graph layout in v1; predictive or learned demand; multi-box bounds
  (2.4 trigger recorded).

## 7. Open questions for the owner

1. **Teleport participation (Section 3.3).** Accept giving topology one real
   streaming behavior, reversing that part of S-D5's surface story? The
   mechanical case is strong; the cost is re-teaching a label that was
   deliberately taught once already.
2. **Cost unit and record shape (Section 3.2).** Bytes now with milliseconds
   joining after the harness lands, on Track C item 1's record: confirm that
   sequencing with the Track C spec owner, or land the record family's first
   slice as part of Phase D.
3. **`StreamingFocusMargin` default (Section 2.2).** 0.5 world units is a
   guess; the preview makes it tunable, but the default should come from the
   template game's pawn dimensions.
4. **Panel versus anchors first (Sections 4.2, 4.3).** Recommendation: list
   fixes, then anchors, then the panel, because anchors make the existing
   viewport graph legible immediately and feed load ordering; the panel is the
   larger build. Reverse if the reading problem hurts more than the streaming
   one today.
5. **`BoundsOverridden` affordance (Section 2.4).** Bundle the small verb and
   inspector field into Phase A, or leave hand-edit-only until multi-box bounds
   are evaluated?
6. **`PreloadDepth` retirement watch (Section 3.2).** After budgets prove out
   on a real world, is authored depth still carrying weight? Zero remaining
   uses would make it a candidate for removal; no action now.
