# Zone Runtime Model: Zones, Regions, Demand, and Containment

Status: proposed design (2026-07-13), owner review before any stage starts.
Canonical: this document and `12-spatial-compilation.md` together replace the
earlier 11 through 13 review chain (reasoning history lives in git). This
document owns the runtime model: what a zone and a region are, how demand
composes, how focus and containment resolve, and which runtime systems remain
unchanged. Doc 12 owns everything compiled: subdivision, spatial evidence,
contacts, transition promotion, and the containment artifact.

## Why

Sencha's world must serve enclosed, bottlenecked space (rooms, caves,
courtyards, stairwells) and open, distance-streamed space (fields, gardens,
cliffsides, villages) with one architecture, where the difference between them
is data, never a mode, and where no place category is the exception. The model
below does that with the two concepts the engine already has, one new authored
value, and one recorded policy extension. It requires no new runtime tier and
no rewrite of the streaming machinery; the substantial new work is
compilational and lives in doc 12.

---

## 1. The model

| Concept | Meaning | Status |
| --- | --- | --- |
| **Zone** | The residency atom, exactly as built: one `Registry`, one `ZoneParticipation`, loaded and unloaded as a unit, flat in `ZoneRuntime`, compiled into `FrameRegistryView` spans. Also the containment answer: every playable position resolves to one zone. | exists, unchanged |
| **Authored zone (source)** | What a designer authors: one document, one identity, one place. A small place (room, cave, courtyard) compiles to one resident zone. A large place declares `ResidencyCellSize` and compiles to many resident child zones (doc 12 Section 3). | exists + one new optional field |
| **Compiled zone** | A cooked `ZoneHeader` the runtime streams. For a subdivided place, children carry `SourceZone` provenance back to their authored source. For everything else the compiled zone is the authored zone. | new (cook output) |
| **Region** | A named group of zones carrying demand configuration: streaming shape (doc 10) plus spatial-demand eligibility (Section 3.3). A demand-policy grouping first; it may coincide with a semantic place but is not required to. | exists + one new optional field |

### 1.1 Identity, honestly

Three identities exist and they answer different questions:

- **Residency and containment identity**: the compiled zone id. What is
  loaded, what participates, what contains this position.
- **Place identity**: the authored source. For compiled children this is
  `SourceZone`; for everything else it is the zone itself. This is the id
  that means "the Palace Garden" regardless of how many pieces stream it, and
  it is what map discovery, music, or telemetry grouping should key on when
  they mean the authored place.
- **Demand grouping**: the region. Regions configure how residency is
  requested and may legitimately cut across places: the canonical example is
  a village whose exterior streets sit in a radius-shaped region while its
  house interiors sit in a graph-only region, all semantically one village.
  Because that example is real, this design does not claim region equals
  place. Region is policy; place is `SourceZone`; gameplay consumers pick the
  id that answers their question, and the first gameplay consumer to land
  documents which one it reads.

`SourceZone` lives directly on the cooked `ZoneHeader` (cooked-only field,
invalid means "authored directly"), not in a side table: it is one id, every
consumer that groups children wants it (demand records, preview, telemetry,
future save state), and a provenance table would be indirection with no
second use. Child display names derive from the source name plus cell
coordinates.

### 1.2 Invariants (unchanged, restated because they force the design)

- Zones are flat. No hierarchical participation, no parent registries, no
  runtime nesting (`world-partition-authoring.md` Section 2.1). A place
  needing independently resident pieces therefore compiles into sibling
  zones, with the place identity carried as data, and this is forced by the
  span model, not chosen for convenience.
- Every entity lives in exactly one registry.
- The manifest is O(zones + transitions). Compiled children multiply the
  zone count (bounded by content, hundreds at v1 scale); volumetric data
  lives in cooked artifacts beside the manifest, never in it.
- The engine mints no random ids. The editor mints authored ids randomly
  (D4); the cook mints compiled ids (children, contacts) as deterministic
  content-derived hashes, re-salted deterministically on collision. This is
  a recorded amendment to D4's wording, not to its intent.

---

## 2. What the runtime keeps, verbatim

`ZoneRuntime` (registries, participation, frame view), `AsyncZoneLoader` and
the detached-build recipe seam (`ZoneLoadRecipeFn`, D10), participation spans
and every span consumer (render, physics, logic, audio), pins, linger, the
resident cap, tag gating (`RequiredTags` + `SetWorldTags`), per-region
streaming shape resolution (doc 10), the template game's world load path and
the pawn-in-`Global()` model. None of these change shape in this design.
Compiled children are ordinary `ZoneHeader`s to all of them.

---

## 3. Demand

Two demand sources, OR'd, exactly as implemented today, with their inputs
sharpened:

### 3.1 Spatial demand

Radius from the focus position demands any zone whose **residency coverage**
falls inside the radius (`ZoneDemand.cpp:274-307` mechanics, with coverage
replacing derived content bounds as the tested box; doc 12 Section 3.4).
Spatial demand is the carrier for open space: subdivided children stream
around the player with zero edges, across region boundaries, because
point-to-box distance never cared about grouping. Per-region radius, cap,
and hop values resolve from the focus zone's region as shipped (doc 10).

### 3.2 Graph demand

Hop ranks BFS over `TransitionRecord`s: promoted contacts plus authored
logical links (Section 4). Because open frontiers no longer inject edges,
hop counts regain their honest meaning: rooms away through real openings.
`PreloadDepth`, `PreloadPriority`, and tag gates behave as shipped.

### 3.3 Spatial eligibility (the recorded doc 10 trade, pulled in)

`RegionStreamingConfig` gains one optional field:

```cpp
// Absent or true: zones of this region join spatial (radius) demand.
// False: they are demanded only by focus, graph edges, or pins. Interiors
// use this so proximity does not pull them through their walls.
std::optional<bool> JoinsSpatialDemand;
```

This is the escape hatch doc 10 recorded as a deliberate later trade
(`10-per-region-streaming-and-topology-labels.md:221`): a zone's own region
now governs whether that zone may be demanded spatially, which requires
`ComputeZoneDemand` to consume the region table as policy input rather than
one focus-resolved config. That amendment to S-D3's single-config signature
is accepted here on the record. Everything stays pure and preview-shared
(D18).

The composition this buys, with no special case anywhere: village streets in
a radius region stream by proximity; house interiors in a
`JoinsSpatialDemand = false` region stay cold behind their walls; each front
door's promoted contact preloads its house at one hop when the player nears
the right street cell. Exterior spatial and interior topological demand run
in the same policy pass.

### 3.4 Participation and cost

- Focus zone: full participation. Graph and spatial neighbors:
  `{NeighborVisible, NeighborPhysics}` per config, as shipped (D17).
- Neighbors reached only through a Teleport edge preload dormant: a
  discontinuous transition has no sightline or threshold a dormant attach
  could pop. This gives topology exactly one streaming behavior and changes
  doc 10's S4 label text in the same commit that changes the behavior
  (owner decision recorded in Section 7).
- Cost-aware residency: the world cook emits per-zone cost facts on Track C
  item 1's `ZoneBudgetRecord` (asset bytes, collision bytes, entity counts;
  the record family's planned shape already covers this). The demand policy
  gains an optional `CostBudget` (config base plus per-region override)
  enforced in the existing eviction pass beside the count cap: focus and
  pins never evict, absent cost data leaves behavior byte-identical.
  Hop horizons then become generous ceilings rather than hand-tuned
  constraints, and heavy zones preload shallower than cheap chains without
  per-edge authoring.

### 3.5 Recorded extensions, not built

Multiple simultaneous focuses (the pure policy generalizes to a focus span;
trigger: split-screen, multiplayer, or scripted cameras needing residency),
visibility-driven demand (v2.0 portals item), and a spatial index over zone
coverage when zone counts reach v3 open-field scale.

---

## 4. Transitions: what an edge is

A `TransitionRecord` exists for exactly two reasons:

1. **A logical link was authored** (`ConnectZones`): teleports, elevators,
   any connection geometry cannot witness. Unchanged.
2. **A physical contact was promoted.** The cook compiles contact records
   wherever traversable free space crosses a zone or place boundary (doc 12
   Section 6), and promotes a contact into transition records only when
   demand semantics require an edge there:
   - a gate controls the contact (the opening is conditional content:
     `Doorway`),
   - either side's region has `JoinsSpatialDemand = false` (an ineligible
     zone's only demand path is topological, so every contact into it must
     be an edge or it is unreachable by streaming),
   - an authored annotation promotes it (the designer wants topological
     preload or future transition timing across a specific contact:
     `Seam`).

Geometry is evidence; demand semantics are the authority. Width and
constriction are compiled as contact metadata (useful for diagnostics,
prefetch weighting, and future timing volumes) but never decide topology:
a hundred-meter frontier between two radius regions stays a contact with
summary metrics and streams by proximity; a narrow canyon between two
radius regions also stays a contact, because an edge would add nothing
radius does not already do; a forty-meter temple mouth into a graph-only
interior is promoted despite its width, because nothing else would ever
demand the interior. Non-promoted contacts still serve reachability
validation (reachable = edges, contacts, and radius-region cliques),
overlays, and later nav and map products.

Consequences for the vocabulary: `Doorway` means gate-bound promoted
contact, `Seam` means gateless promoted contact, `Teleport` means authored
logical link. The topology labels finally describe provenance and behavior
truthfully, completing what doc 10's honest-labels pass started.

---

## 5. Containment and focus

### 5.1 The lookup contract

`LabelAt(position)` resolves through three layers, first hit wins
(mechanics and artifact format in doc 12 Sections 5 and 9):

1. **Sampled space**: bespoke place envelopes and place frontiers carry
   baked 3D labels. Where present and assigned, they are the answer; this
   is what makes caves under gardens, interiors inside village cells,
   bridges over roads, stacked floors, and rooftops resolve correctly in Y
   rather than pretending the world is two-dimensional.
2. **Analytic subdivision grids**: inside a subdivided place's residency
   coverage, the child is computed from the cell coordinate (zero storage,
   exact). Overlapping grid candidates (stacked or adjacent places whose
   coverage boxes intersect) tie-break deterministically: containing
   coverage, then nearest content bounds, then source id.
3. **Recovery**: nearest zone by residency coverage (the existing
   nearest-bounds rule re-pointed at coverage), for airborne, falling,
   spawning, and out-of-envelope positions.

### 5.2 Focus resolution

The game supplies the pawn's capsule-center position through
`SetFocus(Vec3d)` once per frame, exactly as today. Focus keeps the previous
zone unless the sample resolves to a different zone with confidence:
label-depth margin in sampled space, distance-to-cell-boundary margin in
grids (equivalent roles, both config). `SetFocus(ZoneId)` remains for
spawn, save restore, and scripted warps; teleport arrivals and
falling-out-of-world resolve through `LabelAt` plus recovery. The editor
preview resolves through the same pure functions against the last bake and
wears the existing stale-cook badge when hashes mismatch; the runtime
refuses a cooked world whose containment artifact is missing or stale,
the same contract as a missing `CookedSceneRef`.

---

## 6. Dynamic entities: the honest boundary of this design

No total runtime refactor is required to support **static** subdivided
residency with the current flat-zone model. That claim is deliberately
narrow. Entities that move across child boundaries at runtime are a real,
unsolved category: NPCs that chase, physics props, dropped items, corpses,
vehicles, projectiles, moving platforms, destructibles. There is no
cross-registry migration at runtime today; the working precedents are
anchored content owned by its child and the pawn living in `Global()`.
Putting every roamer in `Global()` works at small counts and degrades with
scale (its registry never unloads, so budgeting, lifetime, and
participation pressure accumulate).

Position, plainly: this design ships static subdivision and treats runtime
zone migration as owned future work, not an incidental edge case. Its
natural home is Track C item 5 (stateful detach and the serialized entity
identity scheme), which already owns entity state crossing the
loaded/unloaded boundary; runtime child-to-child handoff is the same
mechanism pointed sideways. A game shipping roaming NPCs inside subdivided
places needs that work first, and the fixture suite gains a scripted
roaming case the day it lands.

---

## 7. Owner decisions collected here

1. **Teleport dormant preload** (Section 3.4): accepts giving topology one
   real streaming behavior and re-teaching the S4 label text in the same
   commit.
2. **`JoinsSpatialDemand` shape** (Section 3.3): optional bool as proposed,
   or fold into the streaming-shape combo as a third derived character in
   the panel. Either way values, never a stored mode.
3. **Cost budget units** (Section 3.4): cooked bytes now, harness-measured
   milliseconds joining later on the same record.
4. **`PreloadDepth` retirement watch**: after budgets and honest hop
   meanings land, authored depth may have zero remaining uses; revisit
   then, no action now.
5. **Roaming-entity policy** (Section 6): confirm Global-for-few now plus
   Track C item 5 as the future home, or pull migration work forward.
6. **Multi-focus demand** (Section 3.5): confirm it stays recorded until a
   real consumer names itself.
