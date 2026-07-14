# Zone Runtime Model: Zones, Regions, Topology, Demand, and Containment

Status: proposed design (2026-07-13), owner review before any stage starts.
Canonical: this document and `12-spatial-compilation.md` are the current zone
architecture (reasoning history lives in git). This document owns the runtime
model: what zones and regions are, the world topology store and its
evaluation under world state, mutation rules, the query surface, demand
composition, the reconfiguration lifecycle, containment, and the honest
runtime boundaries. Doc 12 owns everything compiled: subdivision, spatial
evidence, contact records, capability compilation, spatial-configuration
cooking, artifacts, the editor surface, and the stage plan.

## Why

Sencha's world must serve enclosed, bottlenecked space and open,
distance-streamed space with one architecture, and it must keep working when
the world itself moves: doors, drawbridges, elevators, rotating halls,
floodgates, destructible walls. Two ideas organize the model:

1. **The compiled world graph is runtime data with a query surface**,
   philosophically aligned with the ECS: the ECS is the queryable dataset of
   things and their properties; the world topology is the queryable dataset
   of places and their relationships. Streaming is its first consumer, not
   its owner.
2. **Topology is evaluated, not fixed.** The cook compiles potential
   physical relationships from geometry; runtime world state (tags and
   declared spatial configurations) decides what each relationship currently
   permits, per consumer. Nothing rebakes at runtime, and nothing invents
   physical edges at runtime.

The scope discipline that keeps this from becoming a speculative universal
world simulator: the store ships with exactly the consumers that exist
(streaming demand, editor preview and validation), exactly two evaluated
capabilities (Section 3.3), one graph algorithm, and no caches. Every other
consumer named in this document is a recorded attachment point, added when
its system lands, never before.

---

## 1. The model

| Concept | Meaning | Status |
| --- | --- | --- |
| **Zone** | The residency and containment atom, exactly as built: one `Registry`, one `ZoneParticipation`, loaded and unloaded as a unit, flat in `ZoneRuntime`. | exists, unchanged |
| **Authored zone (source)** | What a designer authors: one document, one identity, one place. A large place declares `ResidencyCellSize` and compiles to many resident child zones (doc 12 Section 3). | exists + one optional field |
| **Region** | A named group of zones carrying demand configuration: streaming shape (doc 10) plus `JoinsSpatialDemand` (Section 5.3). A demand-policy grouping; it may coincide with a semantic place but is not required to (a village legitimately splits into a radius street region and a graph-only interior region). Place identity is `SourceZone`, not region. | exists + one optional field |
| **Contact** | A relationship between two zones: either compiled by the spatial compiler where their traversable free space meets (one kind, from a narrow doorway to a wide-open frontier; the difference is metrics, not type), or an authored logical link (teleport, elevator route). Carries potential capabilities, an optional controller, and predicates (doc 12 Section 6). | new (replaces the transition array) |
| **World topology store** | The runtime dataset of zones, regions, sources, and contacts, with indexes, an evaluated state array, and a revision counter. Owned by the world runtime, read by systems, mutated only through declared state inputs. | new |

Identity remains three-fold and honest: compiled zone id (residency,
containment), `SourceZone` (authored place), region (demand grouping).
Gameplay consumers key on whichever answers their question; the first to
land documents its choice.

Invariants, restated because they force the design: zones are flat (no
hierarchical participation; `world-partition-authoring.md` Section 2.1);
every entity lives in exactly one registry; the manifest stays
O(zones + contacts); the editor mints authored ids randomly, the cook mints
compiled ids as deterministic content hashes (the D4 amendment).

---

## 2. What the runtime keeps, verbatim

`ZoneRuntime` (registries, participation, frame view), `AsyncZoneLoader` and
the recipe seam (D10), participation spans and every span consumer, pins,
linger, the resident cap, per-region streaming shapes (doc 10), the template
game's world load path, and the pawn-in-`Global()` model. The pure-policy
pattern (plain-data functions shared by runtime and editor preview, D18)
extends to topology evaluation. `WorldPartitionIndex` retires: its sorted
arrays, offset tables, and span lookups are the storage pattern the topology
store generalizes, and the store's incident-contact index replaces it.

---

## 3. The world topology store

### 3.1 Shape

A dedicated indexed dataset, deliberately not ECS entities. The decisive
argument: topology must describe zones that are not resident, and an entity
lives in a registry that may be unloaded, so the graph cannot be made of
entities without inventing a never-unloaded meta-registry. The scale
argument seconds it: hundreds of zones and contacts want sorted arrays and
spans, not archetypes. What carries over from the ECS is the philosophy:
plain-data records, `StrongId` handles, allocation-free span iteration,
immutable views while systems run, one explicit mutation point per frame.

Contents (record shapes and the cooked artifact live in doc 12 Sections 6
and 9): zone and region tables from the cooked manifest, `SourceZone`
provenance, the contact array (physical and logical), predicate and
controller tables, the residency-dependency index (dependent child to
owner plus participation mask, Section 5.6), and indexes: zone to incident
contacts, zone pair to contacts, controller to controlled contacts, source
to children. Beside the static records sits one evaluated array
(Section 3.4) and a revision counter.

### 3.2 State inputs (engine language only)

Two declared inputs, both explicit, serializable, deterministic, cheap, and
shared verbatim by runtime and editor preview:

- **World tags**: the existing dotted-name set pushed by the game
  (`SetWorldTags`). Broad boolean facts: `power.on`,
  `security.lockdown`, `quest.east_wing`. The engine never interprets a
  name.
- **Spatial configurations**: a map from `ConfigurationSetId` to
  `ConfigurationStateId`, one entry per topology-relevant assembly
  (doc 12 Section 8): which arrangement a rotating hall, elevator,
  drawbridge, door, or destructible wall is currently in. Finite,
  enumerated, authored; never a transform stream. Local enumerated state
  lives here rather than exploding into pseudo-exclusive tags.

Gameplay logic of any complexity resolves into these two inputs before
evaluation. The evaluator never calls scripts, never reads components, and
never observes animation state; a mechanism's gameplay commits its declared
state when the arrangement is physically true (Section 7).

### 3.3 Capabilities (two now, grown by consumers)

Each contact compiles **potential capabilities** and evaluates **active
capabilities** under the current inputs:

```cpp
enum class ContactCapability : uint8_t
{
    Demand,     // may graph demand traverse this contact
    Traversal,  // can the declared profile physically cross it now
};
using CapabilityMask = uint8_t; // bit per capability
```

Exactly these two ship, because exactly these two have consumers today:
graph demand (streaming) and traversal truth (editor reachability
validation, the contact inspector's explanations, and the fixture suite;
physical blocking itself is enforced by collision, not by this bit). The
growth rule is an invariant: a capability is added in the same change that
lands its consuming system (Navigation with the navmesh links, Visibility
with the portal work, Audio and Map likewise), never speculatively. The
mask, the predicate table, and the artifact format are shaped so that
growth is additive data, not a format break.

The split earns itself immediately: a closed bedroom door keeps `Demand`
active (the room behind it preloads) while `Traversal` is inactive; a
quest-sealed wing authors a `Demand` predicate so it does not preload
until its tag arrives; a barred grate never compiles pawn `Traversal`
potential at all. One active bit could not express any of these without
lying to somebody.

### 3.4 Evaluation, revision, and deltas

`EvaluateWorldTopology(records, tags, configurations)` is pure: it produces
the active-capability array (one `CapabilityMask` per contact, SoA beside
the static records) plus a delta (contact ids whose mask changed). The
runtime evaluates at one point per frame, at the top of the world update,
before demand; consumers read one consistent evaluated view for the rest of
the frame. That is the existing frame discipline (mutations at drain
points, views stable within the frame) applied to topology; no snapshot
type, no structural sharing, no copy of the graph. A `uint64_t Revision`
increments when any mask changes, and the delta record
`{PreviousRevision, Revision, changed contact ids}` is retained for the
frame. Streaming does not need it (demand recomputes each update); it
exists for telemetry, the editor's change highlighting, and the future
consumers that cache (navigation links, route caches), whose invalidation
contract is "revision changed, diff the delta." If profiling ever shows
re-evaluation cost (it is a linear pass over hundreds of predicates),
evaluation goes incremental by indexing predicates by tag and controller;
that is an optimization with a recorded trigger, not a v1 structure.

---

## 4. Mutation rules (invariants)

1. **Potential physical topology is compiled.** The cook discovers every
   physical relationship, including every declared spatial configuration's
   relationships (doc 12 Section 8).
2. **Runtime selects among compiled possibilities.** Systems change
   topology by setting tags, setting configuration states, or authored
   logical-link availability; there is no `AddEdge`, no runtime contact
   minting, no runtime geometry analysis. Destructibles fit as
   configuration states (intact and destroyed are compiled arrangements).
   Genuinely procedural or runtime-generated worlds would need a runtime
   compilation path (the bake kernels run against runtime geometry); that
   is a recorded future capability with its own design burden, not an
   escape hatch this plan opens.
3. **Topology state commits atomically** at the evaluation point; no
   consumer observes a half-applied change (Section 3.4).
4. **Game logic resolves into declared facts before evaluation**
   (Section 3.2).

---

## 5. Demand

Demand becomes the topology store's first consumer. The policy remains
pure, per-frame, and preview-shared; its edge set changes meaning.

### 5.1 Graph demand

`ComputeZoneHopRanks` BFS traverses contacts whose evaluated mask has
`Demand` active, in both compiled and authored (logical link) forms. The
former `RequiredTags` edge gating becomes a `Demand` predicate on the
contact with identical semantics (doc 12 Section 7.4 migrates it), so
"do not preload behind this quest seal" and "preload behind this ordinary
door" are both expressible (Section 3.3). `PreloadPriority` and
`PreloadDepth` remain contact annotations consumed by rank ordering.

### 5.2 Spatial demand

Unchanged: radius from the focus position over `ResidencyCoverage`
(doc 12 Section 3.3), OR'd with graph demand, per-region shapes resolved
from the focus region (doc 10).

### 5.3 Eligibility

`JoinsSpatialDemand` (optional bool on `RegionStreamingConfig`, absent =
true): a region whose zones never join spatial demand, so interiors are
not pulled through their walls by proximity. This is the escape hatch
doc 10 recorded as a deliberate later trade (`10-...md:221`); adopting it
amends S-D3's single-resolved-config signature on the record
(`ComputeZoneDemand` consumes the region table). Every contact into an
ineligible zone compiles `Demand` potential (doc 12 Section 7.1), because
topological demand is that zone's only path to residency. This covers a
subdivided graph-only place cleanly: its children are spatially
ineligible, but the sibling contacts between them (doc 12 Section 6.1) are
contacts into ineligible zones, so each compiles `Demand` potential and
graph demand walks child to child as focus moves through the place, the
same way a room-graph place streams through doorways. The sibling contacts
are the streaming mechanism; no special case is needed.

### 5.4 Participation and cost

Focus full; neighbors `{NeighborVisible, NeighborPhysics}` (D17); logical
links (teleport kind) preload their targets dormant, since a discontinuous
arrival has nothing a dormant attach can pop (this ends doc 10's
"topology labels never affect streaming" story honestly; the S4 label text
changes in the same commit). Cost-aware residency rides Track C item 1's
`ZoneBudgetRecord` with an optional `CostBudget` enforced beside the count
cap in the existing eviction pass; absent data leaves behavior
byte-identical.

### 5.5 Demand reasons

`ZoneDemandRecord` sources gain the topology vocabulary: demanded through
contact X, pinned by prepare handle Y (Section 7), retained as a residency
dependency of zone Z at a stated participation mask (Section 5.6),
spatial, focus, lingering. "Why is this zone resident" stays answerable
from a log and from the inspector without new machinery.

### 5.6 Residency-dependency closure

An object spanning several child zones is owned by one and retained by the
others (doc 12 Section 3.4); the cook emits a residency-dependency index,
`dependent -> (owner, participation mask)`, into the topology store. After
the base demand set is computed (focus, neighbors, spatial, pins, graph),
the policy runs **dependency closure** over that index:

- For each zone in the demand set, add every owner it depends on, `OR`-ing
  the edge's participation mask into that owner's desired participation
  (never below what the owner already has). The propagated participation
  is the spanning object's own need, not the dependent's: retaining a
  wall's owner for `{Visible, Physics}` never makes it `Logic`-hot.
- Iterate to a fixpoint: an owner pulled in this way is itself closed over
  its own dependencies. Termination is guaranteed because mask union is
  monotone over a finite zone set. A dependency **cycle** (A owns an
  object spanning into B, B owns one spanning into A) is valid and
  converges to both resident at the union of the two masks, in
  deterministic zone-id order.
- A zone present only as a dependency carries `Sources.Dependency` and the
  owner id in its record (Section 5.5).

Closure runs before the cap and cost pass, so dependency residency
**counts** toward both. Dependency-pulled zones join focus and pins as
**non-evictable**: the cap's eviction candidates exclude them, because
evicting an owner while a dependent is resident would drop the spanning
object from a cell that still shows it. If focus, pins, and dependencies
together exceed the cap, the cap is honestly exceeded (the existing
focus-plus-pins overage rule). When the last dependent leaves the demand
set, the owner is no longer pulled and returns to ordinary linger and
eviction. This is ordinary demand closure over flat zones: no hierarchy,
no second streaming system, no new participation tier.

---

## 6. The query surface

Two API categories, deliberately separate, both over the store's spans and
the evaluated array, both allocation-free, both usable identically by
engine systems, the editor, and (when it lands) the scripting runtime as a
read-only declared seam (Track A's rule: scripts see declared seams only).

### 6.1 Selection

Filtered iteration over indexed records; O(incident contacts) or
O(contacts) with early filters, never hidden graph work:

```cpp
struct ContactFilter
{
    ZoneId               Touching;        // invalid = any
    ConfigurationSetId   Controller;      // invalid = any
    CapabilityMask       ActiveAll   = 0; // all listed bits active
    CapabilityMask       PotentialAll = 0;
    CapabilityMask       InactiveAll  = 0; // potential but not active
    ContactKindMask      Kinds = ContactKindMask::All;
};

// Iterate matching contacts; fn receives (const WorldContact&,
// CapabilityMask active). Deterministic order: ascending contact id.
void ForEachContact(const ContactFilter&, Fn&&) const;
std::span<const uint32_t> Incident(ZoneId) const; // indices, sorted
```

This answers the working set: exits of the focus zone traversable now,
contacts controlled by assembly X, contacts with `Demand` potential into a
graph-only region, contacts potentially traversable but currently blocked.

### 6.2 Algorithms

Explicit pure functions with visible cost, in the `ComputeZoneHopRanks`
family; v1 ships exactly one, shared by runtime queries and editor
validation:

```cpp
// Flood over contacts with `capability` active (or potential, by flag).
// O(zones + contacts). Ascending zone id.
[[nodiscard]] std::vector<ZoneId>
ComputeReachableZones(const WorldTopology&, ZoneId from,
                      ContactCapability capability, bool potential = false);

[[nodiscard]] bool CanReach(const WorldTopology&, ZoneId from, ZoneId to,
                            ContactCapability capability);
```

Routes, blocking-set analysis ("which contacts sever all paths"),
conditional planning ("what state change would connect these"), cost
models, caching, and asynchronous execution are recorded for the systems
that need them (the cross-zone planner, AI), keyed on the revision for
invalidation. They are not disguised as cheap iteration and they are not
built now.

### 6.3 Explanation

```cpp
// Why is `capability` inactive on this contact: the predicate view
// (required tags present and missing, required configuration and its
// current state) plus the controller id. Compact ids only; the editor
// renders rich text from indexed metadata.
[[nodiscard]] ContactExplanation
Explain(const WorldTopology&, ContactId, ContactCapability);
```

Runtime artifacts carry predicate and controller ids, never prose. The
inspector's "requires power.on; SecurityGate14 must be Open; blocked by
security.lockdown" renders editor-side from the same records
(doc 12 Section 10).

---

## 7. Reconfiguration lifecycle

A topology-changing assembly must not expose an unloaded destination: the
rotating hall may not dock against a west wing whose collision is not
resident. The engine exposes facts and a small lifecycle; gameplay owns
motion, animation, timing, and occupants.

```cpp
// On the world runtime, over existing pin machinery.
PrepareHandle PrepareConfiguration(ConfigurationSetId, ConfigurationStateId);
bool IsPrepared(PrepareHandle) const;   // all implied zones resident at
                                        // the prepared participation
void CommitConfiguration(PrepareHandle); // sets the input; evaluation
                                        // applies it at the next frame's
                                        // evaluation point
void ReleaseConfiguration(PrepareHandle); // also the cancel path
```

- **Prepare**: the runtime evaluates which zones the target state's
  `Demand`-active contacts expose (a pure what-if evaluation against the
  target configuration) and pins them at `{Visible, Physics}`: the dock
  must render and collide the moment it exists; logic and audio flip on
  focus as always.
- **Transit**: gameplay begins motion only after `IsPrepared`; the
  assembly's declared state during motion is its authored transit state
  (no dock contacts), so topology never claims a half-docked passage.
  Occupants ride as gameplay and physics concerns (the cab's contents are
  ordinary entities in the assembly's zone); the engine's only promise is
  that both docks' zones stay resident while pinned.
- **Commit**: gameplay declares the target state when physically docked;
  the declared state and therefore collision-relevant adjacency and
  demand all flip at the same evaluation point next frame.
- **Release**: pins drop; normal linger and eviction resume. Cancel at
  any point is `ReleaseConfiguration` plus gameplay returning to a safe
  declared state. A failed or cancelled load simply never reports
  prepared; gameplay decides whether to wait, abort, or play a stall.

Multiplayer synchronization of prepare and commit is recorded as future
work alongside whatever replication model Sencha eventually adopts;
nothing here presumes one.

---

## 8. Containment and focus

Unchanged from the accepted model, restated for self-containment.
`LabelAt(position)` resolves through three layers, first hit wins
(artifact mechanics in doc 12 Section 9): sampled bespoke and frontier
space (3D labels, correct for caves under gardens, interiors in village
cells, bridges, stacked floors), then analytic subdivision grids
(coverage cell arithmetic, deterministic tie-breaks), then recovery
(nearest by `ResidencyCoverage`). Focus keeps the previous zone unless
the pawn's capsule-center sample resolves elsewhere with margin
(label depth in sampled space, cell-edge distance in grids).
`SetFocus(ZoneId)` remains for spawn, restore, and scripted warps. The
editor preview resolves through the same functions against the last bake
with the stale-cook badge; the runtime refuses a cooked world with a
missing or stale containment artifact.

One topology note: containment is deliberately not state-dependent in
v1. A rotating hall's interior is one zone in every configuration; what
changes is its contacts. A mechanism whose containment itself must
change with state has no fixture yet and is recorded as a deferral.

---

## 9. Dynamic entities and controller residency

No total runtime refactor is required for **static** subdivided residency
and **declared** reconfiguration under this model, and that claim stays
deliberately narrow. Explicitly:

- the player can remain in `Global()` (the shipped model), so the focus
  pawn crosses child zones freely today;
- static subdivision can land before any entity-migration work;
- ordinary NPCs, props, projectiles, and dropped items **cannot** freely
  cross child registries: an entity lives in exactly one registry and
  there is no cross-registry migration yet, so the precedents are anchored
  content owned by its child and the pawn in `Global()`;
- a subdivided *populated* world is therefore not fully production-ready
  until entity migration exists.

That migration is owned future work homed at Track C item 5 (stateful
detach and serialized entity identity); these documents do not solve it.

Controller entities (the door, the hall machinery) sit between the zones
they connect. `Global()` residency is the accepted first implementation
(the doc 09 world-scene direction: resident whenever either side is).
The recorded successor, when door counts make `Global()` heavy, is
boundary residency: content resident whenever either endpoint zone is
resident, which is the same dependency-retention mechanism recorded for
large-influence entities (doc 12 Section 3.4). Controllers are discovered
by identity, not by entity reference: the authored component carries its
editor-minted `ConfigurationSetId`, contacts carry the controlling id,
and the runtime system that owns the entity pushes state by that id, so
no entity-identity scheme is required for binding (doc 12 Section 8.2).

---

## 10. Owner decisions collected here

1. **Capability set v1** (`Demand`, `Traversal`) and the
   consumer-lands-its-capability growth rule: confirm.
2. **Teleport dormant preload** (Section 5.4): accepts ending the
   "topology never affects streaming" label story; text changes in the
   same commit.
3. **`JoinsSpatialDemand` shape** (Section 5.3): optional bool as
   proposed, or a third derived character in the panel's shape combo.
4. **Cost budget units** (Section 5.4): cooked bytes now, measured
   milliseconds later on the same record.
5. **Prepare participation** (Section 7): `{Visible, Physics}` proposed
   for prepared destinations; confirm, or make it a parameter of
   `PrepareConfiguration`.
6. **Roaming-entity policy** (Section 9): confirm Global-for-few now
   plus Track C item 5 as the future home.
7. **Scripting exposure** (Section 6): the read API and the two state
   inputs as declared script seams when the scripting runtime lands;
   confirm that boundary (no script-visible mutation beyond tags,
   configurations, and pins).
