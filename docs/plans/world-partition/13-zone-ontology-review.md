# Zone Ontology Review: Places, Residency, and What Crossings Are Not

Status: architectural review of `12-spatial-field-and-compiled-crossings.md`
(2026-07-13, before any implementation). This document amends 12's ontology; 12
remains the reference for the machinery this review keeps (sampling passes,
determinism regime, crossing extraction mechanics, gate contract). Where the two
disagree, this document wins. Owner review before anything starts.

## The verdict, up front

Doc 12's compilation machinery is right. Its ontology carries three errors, all
of the same kind: it generalized from enclosed, bottlenecked space and treated
everything else as a variant of it.

- **E1. It preserved "one authored place = one zone = one residency unit."**
  Correct for a room, a cave chamber, a courtyard. Wrong for a large field,
  garden, or mountainside, where the authored place is one identity but
  residency must be many independently loadable pieces around one or more
  focuses.
- **E2. It made compiled crossings the universal source of zone adjacency.**
  Correct where space constricts (a door, a cave mouth, a gate, a stairwell).
  Meaningless across a hundred-meter open frontier, which would compile into
  one enormous, information-free "crossing" and then feed a hop-count policy
  that has nothing useful to say about it.
- **E3. It sampled the whole world envelope.** Shape information lives where
  geometry defines shape: inside and around bespoke architecture, and along
  the frontiers where places meet. Sampling the interior of an open field
  answers no question anyone asks.

The fix is not a new runtime concept, and it is explicitly not a
`StreamingCell` type or a second streaming world. **The separation the review
asks for already exists in Sencha's model: Region over Zone.** A zone is, and
remains, the residency atom (one `Registry`, one `ZoneParticipation`, one
streamed unit); a region is the semantic grouping with a name and a streaming
shape (doc 10). What is missing is purely compilational: a large authored
place must be able to compile into many flat zones under one region, and
adjacency must be compiled as two different facts (contiguity everywhere,
constricted crossings only where they exist) instead of one.

Grievance 8 asked whether the graph-demand contract is being preserved out of
convenience. Answer, after genuinely re-deriving it: the runtime demand model
survives because it is already the dual system the grievances call for, not
because it is incumbent. Spatial radius demand was built precisely for
"a Hyrule-field grid of seam-connected cells"
(`06-streaming-maturation.md:84`), it is per-region (doc 10), it ORs with
graph demand today, and the one known gap (a target zone's own region should
govern how that zone may be demanded) is already recorded in doc 10's open
questions as a deliberate later trade (`10-...md:221`). This review pulls
that recorded trade in. `ZoneRuntime`, participation spans, recipes, and
`FrameRegistryView` do not change at all. The substantial revisions land in
the cook (subdivision, scoped sampling, contact records), the manifest (one
optional authored field, compiled zone expansion), the demand policy's inputs
(contact data, per-target-region semantics), and focus resolution (layered
lookup). Answer to ask 12: no total refactor of the runtime streaming model
is required, and this review derives why rather than assuming it.

---

## 1. The ontology (asks 1, 3)

### 1.1 The roles a "zone" was carrying

Six distinct roles, currently all answered by one word:

| Role | Correct owner | Status |
| --- | --- | --- |
| Residency atom (load/unload, participation) | **Zone** (unchanged: one registry, one participation) | exists |
| Containment answer ("what is at this position") | **Zone**, via layered lookup (Section 4) | revised |
| Semantic place identity | **The finest named level: zone for small places, region for compound or large places** | exists, needs no new type |
| Demand shape (how residency is requested here) | **Region** streaming config (doc 10), extended with per-target semantics | exists + recorded extension |
| Topology node (graph edges) | **Zone**, but only where constricted or logical edges exist (Section 3) | narrowed |
| Gameplay scoping (music, weather, encounters, map discovery) | **Region id and zone id as inputs to gameplay data**; never partition machinery itself | future consumers |

### 1.2 Why not a third runtime tier

A residency unit below the zone would either duplicate the zone machinery one
level down (two streaming worlds, the exact "incompatible worlds" failure
Direction 3 fears) or make participation hierarchical, which the design doc
rejected for wrecking the flat span model
(`world-partition-authoring.md` Section 2.1), and that rejection is
load-bearing engine architecture, not taste. The conclusion is forced: if a
place needs N independently resident pieces, those pieces must BE zones, flat,
each with its own registry and participation, and the place's identity must
live one level up, on the region. Both levels already exist; no concept is
minted, one is compiled.

Equally rejected: authoring "modes" (an indoor/outdoor enum, a
topological/spatial zone kind). Doc 10 already established the pattern this
review extends: the graph-versus-radius character is read off values
(`Radius == 0` versus `> 0`), never stored as a mode, never branched on by
name. Whether a place subdivides is likewise a value (Section 2), and whether
a boundary yields a graph edge is a measured property of the boundary
(Section 3). Courtyards do not behave like rooms because someone classified
them as roofed; they behave like rooms because their data (one bounded shape,
few constricted openings) is room-shaped. That is directive 3 applied to the
ontology itself.

---

## 2. Residency compilation (asks 2, 3)

### 2.1 The authored surface

`ZoneHeader` gains one optional authored field:

```cpp
// Present: the cook expands this zone into independently resident child
// zones on a horizontal grid of this pitch (meters). Absent: the zone
// compiles as one residency unit, exactly as today.
std::optional<double> ResidencyCellSize;
```

That is the entire authoring change for large places. The designer authors
the garden as one document (one zone entry, one scene file, ownership as
always) and states only "this place streams in 64m pieces." Everything else
derives. The recommended (not enforced) pattern for compound places: the
large place is its own region, radius-shaped, so the region carries the
compound identity and the demand shape while the compiled pieces carry
residency. A courtyard simply omits the field and nothing about it changes.

Validation: `partition.zone.residency_cell_invalid` (Error) for non-finite,
non-positive, or absurd values (below the cook cell size; above the zone
extent).

### 2.2 What the cook does (and why Sencha is already most of the way there)

The level cook already partitions every zone document spatially: brushes
cluster into 16m cells, each emitting `cell_X_Y_Z.smesh` and `.scol`
(`BrushClustering.cpp`, `DocumentCook.cpp`), and props pass through into one
cooked scene. Subdivision compilation is a re-bucketing of that existing
output, not a new pipeline:

- `ResidencyCellSize` is constrained to a multiple of the cook cell size, so
  every cook cell maps wholly into one residency cell.
- The world cook groups the source zone's cook cells and passthrough entities
  (by position, the brush-center precedent) into per-child cooked scenes and
  collision sidecars: one `ZoneHeader` child per non-empty residency cell,
  with `Region` inherited, derived names ("Garden 2,1"), bounds = cell box
  intersected with content bounds (analytic), and cooked refs per child.
- Child ids are deterministic: `Hash64(sourceZoneId, cellCoord)` (the doc 12
  Section 7.4 minting regime, same collision rules). Stable across recooks
  while the subdivision pitch is unchanged.
- The source zone header does not appear in the cooked manifest as a loadable
  zone; the sidecar records the provenance (child -> source) for the editor,
  telemetry grouping, and future save-state migration.

The runtime never learns the word "subdivision." It sees zones in regions,
which is what it already streams. `ZoneLoadRecipeFn` receives child headers
like any header. Demand records list children; the preview groups them by
region so the panel does not drown (Section 6 of the stage list).

Oversized brushes (a terrain slab spanning many cells) violate the
whole-brush bucketing rule; the cook splits them along residency planes with
the existing pure clip verb (`BrushOps` Clip) before bucketing. True terrain
as a first-class asset, HLOD, and impostor proxies remain Track C item 9
(v3.0, `engine-roadmap.md:391`); this design gives that item its natural
landing slots (finer pitch, proxy participation tiers, per-child artifacts)
and deliberately builds none of it.

### 2.3 Consequences owned honestly

- **Zone counts grow** in the cooked manifest (a 512m field at 64m pitch is
  up to 64 children). The demand policy's linear scans are fine at hundreds;
  the recorded trigger for a spatial index over zone bounds is v3 scale, not
  now.
- **Roaming runtime entities** (a boulder rolling across children, a
  wandering NPC) have no cross-registry migration today; anchored content
  belongs to its child, the pawn already lives in `Global()`, and the policy
  for the few true roamers (live in `Global()`, or migrate via stateful
  detach when Track C item 5 lands) is an open question this review
  surfaces rather than hides (Section 9).
- **Save-state keys**: Track C item 5 will key state by zone id; child ids
  are stable unless the pitch changes, and a pitch change is a content
  migration (rebucket by position), recorded as a risk, acceptable for a
  cook-time decision that changes rarely.

---

## 3. Adjacency: contiguity, crossings, and links (asks 4, 5)

### 3.1 Three facts, not one

Doc 12 compiled one artifact (crossings) and made it the source of the graph.
This review splits adjacency into what it actually is:

1. **Contiguity** (compiled fact, universal): free space labeled A meets free
   space labeled B traversably, summarized per zone pair as frontier length,
   area, direction distribution, and a representative point set. Cheap,
   always produced where places touch. Consumers: reachability validation
   (extending the radius-region carve-out that already exists), the editor
   overlay, the map products later, nav later. **Never a graph edge by
   itself.**
2. **Crossings** (compiled subset): contiguity components that constrict.
   Doorways, cave mouths, gates, stair throats, canyon necks. Detected by the
   measured property, not by roofs: a component whose traversable width is
   small against the free-space bodies it joins (constriction ratio and an
   absolute width band, both bake config). These compile into
   `TransitionRecord`s exactly as doc 12 specified (deterministic ids,
   directions, one-way drops, gate binding, metrics) and feed graph demand,
   prefetch anchoring, and future transition timing.
3. **Logical links** (authored): teleports, elevators, and any connection
   geometry cannot witness. Authored via `ConnectZones`, unchanged.

The answers the review demanded, plainly: compiled crossings are **not**
universal topology; they are the constricted subset of universal contiguity.
A hundred-meter frontier compiles into one contact record with summary
statistics, never thousands of facets pretending to be a door, and never a
graph edge. Broad outdoor neighbors are **not** connected through the graph
at all; their adjacency is contiguity plus proximity.

### 3.2 How demand consumes the three

The two demand sources already OR (doc 10); their inputs sharpen:

- **Spatial demand** (radius from the focus position) is the carrier for open
  space, exactly as built (`ZoneDemand.cpp:274-307`): it demands any zone
  whose bounds fall inside the radius, across region boundaries, with no
  edges authored or compiled. Subdivided children are its natural targets.
- **Graph demand** (hop ranks) consumes only transitions: compiled crossings
  plus logical links. Hop counts regain their honest meaning ("rooms away
  through real openings") because open frontiers no longer inject edges.
- **The recorded per-target-region semantics land now** (doc 10 open
  questions, `10-...md:218-224`): a region may declare that its zones join
  demand only through graph edges, pins, or focus (never spatial). This is
  the village problem's data answer: house interiors sit in a graph-only
  region, so street-radius demand does not pull ten interiors through their
  walls, while their doorway crossings still preload them at one hop when
  the player nears the right cell. It was already recorded as a deliberate
  later trade; the outdoor model is the trigger firing. The cost recorded
  there (the policy takes per-target region shapes, not one resolved config)
  is paid openly: `ComputeZoneDemand` gains the manifest's region table as
  policy input, and S-D3's single-config signature is amended on the record.
- **Contiguity records** feed demand only as metadata (approach distances
  toward frontiers and crossings, Section 8's facts stage), never as edges.

Pins, linger, caps, budgets (doc 11), tags: unchanged. Direction 2's
producer list maps almost one-to-one onto existing mechanisms: radius
(exists), topological proximity (exists, now honest), script requests (pins,
exist), teleport preload (pins on the destination, exists), dependency
retention (linger, exists), visibility (v2.0 portals item, recorded),
multiple focuses (a mechanical extension of the pure policy to a focus span;
recorded, not built; no multiplayer exists in Sencha today).

---

## 4. Containment and focus: layered lookup (ask 7)

`LabelAt(position)` remains one query with one answer per role:

- **Sampled bricks first** (sparse, only where baked: bespoke envelopes and
  frontier bands, Section 6): exact shapes where geometry defines them.
- **Analytic grid second**: inside a subdivided place's envelope, the label
  is computed from the cell coordinate. Zero storage, zero boundary
  jaggies, exact by construction. This is why one world-level sampled field
  stops being the answer (ask 7): open places do not need sampling to know
  which cell you are in.
- **Recovery third**: outside both, the existing nearest-bounds rule (the
  current `ResolveFocusZone` fallback, retained verbatim as the recovery
  tier for falling, spawning, and out-of-envelope positions).

Focus resolution keeps label-depth hysteresis from doc 12 where bricks
exist; grid labels use a distance-to-cell-boundary margin (analytic,
equivalent role). The identity questions resolve without pretending one id
answers everything: `LabelAt` yields the zone (residency, containment); the
zone's region is a manifest lookup (semantic compound identity, demand
shape, map region); music, weather, and encounters are gameplay data keyed
by whichever of those two ids (or by ordinary gameplay volumes) the game
chooses. In a room the ids coincide; in a field they do not, and both
answers stay honest. One lookup, two identities, no third concept.

---

## 5. What ownership means now (ask 6)

- **Authored ownership is place ownership**: an entity or brush belongs to
  the place document the designer built it in. This is unchanged and remains
  the semantic truth the editor, validation, and labeling reason about.
- **Compiled ownership is residency assignment**: the cook partitions a
  subdivided place's content among its children by position (whole-brush
  center rule, oversized brushes split first). One authored object can
  therefore contribute content to multiple residency units only by being
  split into per-unit pieces at cook; at runtime, each piece belongs to
  exactly one registry, preserving the structural invariant.
- **Labeling influence is scoped to where labeling is real work.** Between
  bespoke places, ownership growth with watershed costs proceeds as doc 12
  designed. Within a subdivided place, labels are the grid partition;
  ownership growth does not run there at all (semantic labeling and
  streaming subdivision are, as the review suspected, different operations,
  and this design stops conflating them). Where a subdivided place meets a
  bespoke place, the sampled frontier band wins over the grid (the cave
  carved into the garden's hillside is the cave's shape, not a garden cell).
- **Wide-open boundaries between two open places are downgraded from
  "problem requiring hints" to "explicitly low-stakes."** The label boundary
  lands where ownership actually changes (whose terrain brush is underfoot),
  deterministically; nothing gameplay-critical hangs on its exact meter
  (demand there is proximity, focus has margin, compound identity is the
  region). Ambiguity flags and hint volumes remain for the designer who
  cares, but the architecture no longer pretends the exact watershed line in
  an open meadow was ever an important answer.

---

## 6. Scoped sampling: what remains of the field (asks 8, 9)

The bake keeps its pass family and its determinism regime (doc 12 Sections
4 and 5 stand), and shrinks its domain:

- **Sampled**: bespoke place envelopes (rooms, caves, courtyards, cliff
  paths: anywhere shape is geometry-defined), plus frontier bands where any
  two places meet (to measure contiguity and detect constriction), plus gate
  passage neighborhoods.
- **Not sampled**: the interiors of subdivided open places (analytic labels;
  occupancy there is baked only if and when a consumer needs it, and the
  navmesh back end is that consumer, at its own resolution, on its own
  roadmap item).

What each product actually shares (the Grievance 4 split, answered):

- **Shared evidence, built now**: occupancy, clearance, support, slope,
  per-profile traversability, geometry ownership, gate passages. Pure
  kernels beside the other cook kernels; consumed by zone products today and
  the navmesh back end when Track A item 5 lands. This remains genuinely
  valuable for navigation: it is the rasterized front half every
  navmesh bake needs, unchanged by this review.
- **Zone products**: labels (bespoke + frontier), contiguity records,
  crossings, the runtime containment artifact (bricks now covering only
  bespoke space and frontiers, plus grid parameters per subdivided place),
  aggregate bounds.
- **Streaming facts**: crossing metrics, approach distances (scoped to
  crossings and region exits, not every frontier meter), cost records
  (`ZoneBudgetRecord` coordination unchanged from doc 11/12).
- **Nav, query, and map products**: separate artifacts, separate stages,
  consuming the shared evidence; none built here. Doc 12's interior region
  graph is demoted to the planner's roadmap item (its consumer) rather than
  a streaming-metadata stage.

The runtime artifact shrinks accordingly: grid parameters are bytes, bricks
exist only near architecture and frontiers, and the fixture-suite size gate
moves from "single-digit MB expected" to "hundreds of KB expected" for
content-scale worlds that are predominantly open.

---

## 7. The examples, walked (ask 2)

**A. Small castle courtyard.** One bespoke zone (no `ResidencyCellSize`),
graph-shaped region. Sampled shape captures the walls; arches constrict and
compile as crossings to the keep and the gatehouse. Preload is graph hops.
Identical machinery to a room, because its data is room-shaped. One
residency unit, as expected.

**B. Large palace garden.** One authored document, one region ("Palace
Garden", radius-shaped), `ResidencyCellSize` 64. Compiles to N children;
proximity streams them around the player. The hedge maze corner constricts
nowhere, so no internal edges exist and none are needed. The orangery and
the grotto are bespoke zones (own documents) inside the same region; their
door and cave-mouth crossings compile as edges from whichever garden child
they open into, so approaching the grotto mouth preloads it at one hop while
the garden itself streams by radius. One gameplay identity (the region),
many residency units, both demand sources cooperating.

**C. Field-scale region.** As B, larger pitch, `ResidencyCellSize` 128,
possibly a raised per-region cap and cost budget (docs 10 and 11). No
internal crossings at all. Its exits: the walled town gate and the mountain
pass constrict, compile as crossings, and carry the region-to-region
transitions that deserve prefetch, timing, and map meaning. An unwalled
frontier into a neighboring open region compiles as contiguity only, and
proximity streams across it seamlessly because radius demand never cared
about region boundaries. Multiple focuses and terrain assets remain the
recorded v3 items; nothing here blocks them.

**D. Outdoor village.** Streets and exteriors: one subdivided place in a
radius-shaped region. House interiors: bespoke zones in a graph-only region
(the per-target semantics from Section 3.2), so walls actually insulate them
from radius demand; each front door compiles as a crossing from the street
child it opens onto. Exterior demand is spatial, interior demand is
topological, and they combine in one policy pass with no special case: that
sentence is the whole point of this review.

**E. Cliffside into a cave.** The cliff path: bespoke zone if compact (two
crossings at its ends), or a child-bearing place if it is a mountainside.
The cave: bespoke, sampled, its mouth constricts against the cliff's space
and compiles as the crossing that preloads the cave on approach. Radius
carries the exterior, the graph carries the mouth, the frontier band is the
only place sampling had to run outdoors.

---

## 8. Revised stages (ask 10)

Deleted from doc 12's plan: whole-envelope sampling (E3), crossings as sole
adjacency (E2), the interior region graph stage (deferred to the planner's
item), aperture polygons (already deferred there, unchanged). Reordered: the
open-space path is proven before crossings exist at all, so outdoor support
is structurally incapable of becoming the exception.

- **Stage 0: fixtures.** Doc 12's indoor set plus authored versions of
  examples A through E. The fixture list is the acceptance vocabulary for
  every later stage.
- **Stage 1: subdivision compilation.** `ResidencyCellSize`, cook
  re-bucketing, child headers, deterministic ids, analytic containment,
  provenance sidecar, preview grouping. Gate: example C streams by radius in
  the template game with zero transitions in the world; focus moves
  cell-to-cell with margin hysteresis; the traversal harness reports no
  misses. No sampling exists yet, and outdoor streaming already works.
- **Stage 2: per-target-region demand semantics.** The doc 10 recorded
  trade, implemented and tested (village-interior fixture stays cold from
  the street until its door edge exists in stage 5). Gate: doc 10's S-D3
  amendment recorded; preview shows why each zone is or is not demanded.
- **Stage 3: shared evidence passes, scoped.** Doc 12 stage 1 (occupancy,
  clearance, support, traversability, triangle-box primitive), domain
  limited to bespoke envelopes and frontier bands. Same gates, plus a
  domain-size assertion (the field fixture for example C samples only its
  exits).
- **Stage 4: labeling and contiguity.** Ownership growth between bespoke
  places, frontier bands against grids, contact records, ambiguity as
  low-stakes diagnostics. Gate: doc 12's labeling fixtures plus
  grid-versus-bespoke layering (the grotto wins its own shape).
- **Stage 5: crossings.** Constriction classification over contiguity, the
  doc 12 extraction/id/gate machinery unchanged, manifest compilation,
  migration diff, validation swap (contiguity extends reachability; open
  frontiers stop warning). Gate: doc 12 stage 3 gates plus: example C's
  frontier yields one contact record and zero edges; its town gate yields
  one crossing.
- **Stage 6: runtime containment and focus.** Layered `LabelAt` (bricks,
  grid, recovery), label-depth and grid-margin hysteresis, world-start
  load, preview parity. Gate: doc 12 stage 4 gates rerun across examples A
  through E; artifact sizes within the reduced budget.
- **Stage 7: editor surface.** Doc 12 stage 5 plus region-grouped demand
  records, contiguity and constriction overlays, subdivision preview.
- **Stage 8: streaming facts.** Crossing metrics, approach distances to
  crossings and region exits, cost records; policy consumption per doc 11.
- **Stage 9: gates.** Unchanged from doc 12 stage 7.
- **Stage 10: incremental bake.** Unchanged from doc 12 stage 8, gated on
  measured pain; the scoped domain makes it less likely to be needed.

---

## 9. Risks and open questions (asks 11, 12 posture included)

1. **Constriction classification is a judgment encoded as thresholds.** A
   6m arch and an 8m canyon neck must both read as crossings while a 40m
   tree line does not. Mitigations: ratio-plus-band config, fixtures on both
   sides of the line, the contact record as the safe fallback (a
   misclassified frontier still streams correctly by radius; the cost of a
   false negative is a missing prefetch edge, not a hole in the world).
   This is the review's weakest new joint, called out as such.
2. **Roaming entities versus child residency** (Section 2.3): open question
   for the owner; candidate answers are `Global()` residency for true
   roamers now, migration via stateful detach later. Must be answered
   before any game ships wandering NPCs in subdivided places, not before
   this design lands.
3. **Pitch changes invalidate child ids** (save-state keys, annotations on
   child-referencing edges). Mitigation: rebucket-by-position migration
   tooling when Track C item 5 lands; until then, pitch is treated as a
   content-stability decision like renaming a zone.
4. **Demand-scan scaling**: linear scans over cooked zones are fine at
   hundreds, indexed at v3 scale (recorded trigger, do not build).
5. **Terrain remains brush-shaped in v1.x.** Honest scope: gardens,
   villages, fields built from brushes work now; true terrain assets, HLOD,
   impostors are Track C item 9 and this design only reserves their slots.
6. **The two-identity model asks gameplay to choose zone or region ids.**
   Right answer per consumer, but it is a documentation duty: one page in
   the gameplay docs when the first consumer (music, map) lands, stating
   which id means what.

## 10. Non-goals

- No `StreamingCell` or any third runtime tier; no hierarchical
  participation (2.1 stands).
- No indoor/outdoor mode enum anywhere; shape is data, constriction is
  measured, subdivision is a value.
- No terrain system, HLOD, impostors, or multi-focus demand in these
  stages (recorded homes exist for all four).
- No whole-world sampling; no runtime spatial-query facade; no navmesh; no
  map meshes (unchanged deferrals from doc 12).
- No silent supersession: doc 12's header gains an amendment note naming
  this document, and both stay in the map.

## 11. Open questions for the owner

1. **Region as compound identity**: comfortable making the region the
   recommended identity level for large places (music, map, weather), with
   zone names carrying small-place identity? The alternative (a new Place
   concept) was evaluated and rejected as a third name for existing
   machinery.
2. **Per-target-region semantics shape** (stage 2): the policy signature
   grows (region table as input). Doc 10 recorded the trade; confirm paying
   it now.
3. **Default pitch and the multiple-of-cook-cell rule**: 64m default
   (4 cook cells) proposed; confirm against intended content scale.
4. **Constriction band defaults**: proposed start, width under 8m absolute
   or ratio under 0.25 against the joined bodies, tuned on fixtures A
   through E; confirm the fixtures are the right arbiter.
5. **Doc 12 disposition**: this review amends rather than replaces (its
   Sections 4, 5, 7, 8 machinery survives verbatim). Confirm that
   split-document form, or request a merged rewrite once both are accepted.
