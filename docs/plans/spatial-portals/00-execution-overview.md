# Spatial Portal Execution Suite

Status: approved execution plan, revision 2 (2026-11-27), written against `main` at
`fbe262a3`. The owner approved implementation as written on 2026-11-27, which rules D2,
D14 and D15 as recommended. Stages 1 and 2 are in progress.

This revision supersedes the suite on `origin/agent/spatial-portals-plan` (`7c47ea8c`,
2026-07-22). That suite was written before the unified runtime merged and assumed
contracts by name. This one is written against what merged. Section 3 lists what
changed and why.

A spatial portal is a pair of linked planar apertures. Looking into one shows the space
in front of the other. Moving through one continues in front of the other with position,
facing and velocity carried through one rigid mapping. Rooms that could not coexist in
ordinary space are authored at separate coordinates and joined this way. The suite adds
no dimension, space id, alternate world or second simulation.

"Portal" is the designer-facing word and the name of this folder. Engine identifiers use
the mechanism names in section 4, because "portal" already means a navmesh polygon edge
in `engine/include/navigation/tooling/NavMeshSnapshot.h` and was retired as a
world-partition term.

`CLAUDE.md` is binding. Where this suite and `CLAUDE.md` disagree, `CLAUDE.md` wins and
the suite is corrected in the same change.

## 1. Product boundary

### 1.1 v0.1: walk through, see through

1. Two linked, static, rectangular apertures, in one zone or two.
2. A nonrecursive view of the linked side through each aperture, correct at any angle
   and distance, clipped at the exit plane.
3. Character traversal on upright links (section 5, D4): position, facing and velocity
   carried through, straddling, stopping halfway and backing out.
4. The same traversal predicted on the owning client and replicated to other clients
   without a visible path between the apertures.
5. First-person and orbit cameras behave correctly at and across an aperture.
6. Particles are visible through an aperture and continue through it.
7. Traces and simulated projectiles continue through an aperture for one hop.
8. Same-zone navigation routes through a link.
9. A destination that is not ready refuses traversal safely and shows a fallback surface.
10. Counters, debug draw, deterministic tests, golden images and benchmark runs.

### 1.2 v0.2: things pass through

1. Dynamic rigid bodies cross by centre of mass, with pose and both velocities carried.
   No coupling: a body is wholly on one side for collision.
2. A renderable straddling an aperture draws clipped on both sides, skinned meshes
   included.
3. Zone-owned entities change zone when they cross and stay there across save and load.

### 1.3 v1.0: one body in two places, and light

1. A rigid body straddling an aperture collides on both sides as one mass.
2. Baked direct light and probe rays cross static links.
3. Dynamic point and spot lights, shadowed, cross visible links.

### 1.4 Not in this suite

- Recursive views. A portal seen through a portal shows the fallback surface.
- Moving endpoints, scaling links, mirrored links.
- Runtime cutting of wall geometry. The opening is authored (D6).
- Character traversal of links that change which way is up (deferred, section 9).
- Sight and hearing through a link for AI (deferred, section 9).
- Cross-zone navigation through a link. It waits on the cross-zone planner
  (`docs/deferred.md`, "Cross-zone orders").

## 2. Owning invariant

> One link owns two endpoint references. Every consumer of a link reads one resolved
> record: one rigid mapping in each direction, one aperture rectangle per end, one
> plane-side convention and one stable link identity. No consumer rebuilds any of it,
> and no operation follows more than one link.

A second invariant carries the transfer:

> Moving an entity discontinuously is one operation with one owner. It transforms or
> resets every piece of spatial state the entity holds, advances the entity's
> relocation serial, and records the mapping. Link traversal, level carry and any later
> teleport call that operation.

## 3. What changed from revision 1

| Revision 1 assumed | What is on `main` | Consequence |
|---|---|---|
| `StableEntityRef { ZoneId, PersistentEntityId }` with resolution epochs | `PersistableRef { EntityId, PersistentEntityId }` (`engine/include/world/identity/PersistableRef.h`), ids unique across the world, one `PersistentEntityIndex` per `World` | Use `PersistableRef`. No epochs: a dead generational `EntityId` is the staleness check. Partition-slot aliasing cannot happen and its tests are dropped. |
| A new relationship entity type | `WorldLink` with `LinkKind::Teleport` (`engine/include/zone/WorldConnectionComponents.h`) already lives in the world scene and already feeds streaming demand | The link is a `WorldLink` that gains two endpoint references (D2). |
| Portal demand built on participation leases | Links are demand edges in `ComputeZoneDemand`; the linked zone is a one-hop neighbour. Leases exist in `WorldPartitionRuntime` but have no production holder, no denial signal and no forced-teardown wiring | Demand comes from the existing edge. Leases are used only to pin a straddle, and their gaps are closed as general work when first used. |
| A portal-specific transfer | `LevelCarry::Arrive` (`engine/src/app/LevelCarry.cpp`) already places, turns velocity, rezones, snaps history, clears perception memory and reissues navigation orders | Extract one relocation operation and make both call it (D10). |
| A migration journal read by physics | `World::MoveEntityToPartition` records `EntityPartitionMove` for registered followers (attachments, held records). Physics reconciles by partition set and keeps a body moved between two active zones | No journal work. One missing test: a character mover moved between active zones. |
| `PhysicsScene` with record families, angular impulses, constraints, contact hooks | `PhysicsStepSystem` owns `PhysicsWorld`, `RigidBodyBinding`, `CharacterMoverPool`. The facade has velocity get and set, `TeleportBody`, `WakeBody` and a one-way driven-pose drive. No impulses, mass, inertia, centre of mass, motion quality, contact listener or per-pair filter | Coupled bodies need a facade stage first (`04`, Stage 9). v0.2 adds an uncoupled tier that needs none of it. |
| Extraction and one forward pass per frame extended in place | The runtime frame has one camera, one `RenderQueue`, one light set (`DefaultRenderPipeline`). The editor already draws several views through `FrameComposition` and `RenderTargetStore` | The runtime adopts `FrameComposition`; a linked view is a second view (`03`). |
| A clip plane in shaders | No clip distance anywhere; `shaderClipDistance` is not enabled on the device | Views use an oblique near plane. Straddling meshes need the device feature (`04`). |
| Stable render keys from persistent identity | `RenderEntityKey { Scope, EntityId }`; runtime `Scope` is 0 | Synthetic representations set `Scope` from the link id and side. Keys are session-stable, not load-stable. |
| World cook resolves links before zone bakes | `CollectWorldCookInput` already loads every zone and exchanges "halo" triangles between zones. Lights never cross zones | Light transport extends the halo exchange (`05`). |
| Inspector pickers for endpoint references | No entity-reference property kind; the one hand-written picker lists the focused document only | An entity-reference property kind is a prerequisite general mechanism (`01`). |
| One undoable pair command | A focus change clears the command stack; a composite command can write two documents without changing focus (`MoveEntitiesToZoneCommand`) | The pair command runs from world-scene focus (`01`). |
| Not mentioned | Replication, prediction, the orbit camera, particles, projectiles, navigation links, perception, saves | New document `06`. |
| "No em dash" rule | Not in `CLAUDE.md` | Dropped as a rule. |

Facts in this table were read from source at `fbe262a3`. Each stage begins by rereading
the ones it depends on (stop condition 12).

## 4. Vocabulary

Names are provisional. Ownership is not.

| Term | Meaning |
|---|---|
| link | A `WorldLink` entity in the world scene with both endpoint references set. |
| aperture | An entity carrying `LinkAperture { Vec2d HalfExtents }`. Its transform is the frame. |
| mapping | The rigid transform from one aperture's space to the other's, with the half turn. |
| resolved link | The immutable record for one link while both apertures are resident. |
| `ResolvedLinks` | World resource publishing the ordered resolved records. |
| relocation | The one operation that moves an entity through a rigid mapping. |
| `RelocationSerial` | Per-entity counter advanced by every relocation, replicated. |
| linked view | A second scene view drawn from the main camera carried through a mapping. |
| crossing record | Transient state for one entity overlapping one aperture. Never serialized. |
| recess | The authored free volume behind an aperture that a capsule centre moves into. |

## 5. Pinned decisions

Decisions marked **owner** change scope or a persisted format. The owner accepted each
recommendation on 2026-11-27.

### D1. One world, disjoint coordinates

Linked rooms are authored at coordinates that do not overlap. There is one Jolt world
and no space id, so two co-resident zones whose geometry shares coordinates would
collide with each other. World validation rejects a link whose two zones have
overlapping content bounds unless the zones are also dock-adjacent.

After a traversal, the destination zone comes from the link, never from position
(`ResolveZoneAt` picks the smallest containing bounds and is ambiguous by design).

### D2. The link is a `WorldLink` (owner: additive format change)

`WorldLink` gains `EndpointA` and `EndpointB` as `PersistableRef`, null by default. A
link with neither set is today's demand-only link. A link with exactly one set is
invalid. `LinkKind` gains no value: a teleport link with endpoints is a spatial one.

The `WLNK` scene chunk and the cooked `LinkEndpoint` gain fields. The change is
additive, old data loads unchanged, and it gives links the positions that
`world.reachable` reports as missing today. Endpoints do not store a partner, a link
id, a matrix or a zone.

### D3. Endpoint uniqueness is validated globally

One aperture belongs to at most one link. Cook and runtime both check.

### D4. Rectangular rigid apertures; characters need upright links

Apertures are rectangles under translation and rotation at unit scale. A link is
*upright* when its mapping keeps world up. Any valid link carries views, particles,
traces, bodies and light. Only upright links carry characters, because
`LookOrientation` is yaw and pitch about world up and the character capsule never
tilts. A non-upright link is a solid boundary to a character and is reported at cook.

### D5. Static endpoints through v1.0

### D6. Authored openings with a recess

The wall opening is authored geometry. A capsule centre must pass the aperture plane, so
there must be free volume behind it: a recess at least one capsule radius deep plus a
margin. In the level spatial model the recess is an ordinary small space joined by an
ordinary opening, which keeps `opening.joins_one_space` satisfied and needs no change to
that model. The composite surface hides the recess from the front. Cook validates recess
depth against the registered character profiles.

### D7. One hop

Views, rays, traces, particles, lights and traversal follow at most one link per
operation. A particle or body may cross again on a later step.

### D8. One active link per body

### D9. No duplicate gameplay entities

Clipped render representations are render-domain data. Physics proxies are backend
records. Neither is an entity.

### D10. One relocation operation

Link traversal does not get its own transfer code. `LevelCarry`'s placement and turn
logic is extracted into the relocation operation in `02`, and `LevelCarry` becomes its
first caller. Each component that holds world-space state registers how it relocates,
so a later component adds an entry instead of editing the operation.

### D11. Each side lights its own half

### D12. Zone migration keeps identity and uses the existing keep-with-zone path

### D13. Budgets come from measurement, limits are stated up front

No millisecond budget is invented. Every cap this suite adds (linked views, target
bytes, crossing records, linked emitter states, transmitted lights) states its number,
its overload behaviour and its counter in the same change, has a boundary test, and is
recorded in `docs/renderer/constraints.md` or the owning subsystem document.

### D14. Replicated from the first traversal (owner)

Character traversal lands with its replicated behaviour, not after it. The crossing runs
inside the shared character tick so prediction replay reproduces it. Recommendation:
accept. Retrofitting touches the same step function, command format and interpolation.

### D15. An uncoupled body tier before coupling (owner)

Revision 1 refused transform snapping as rigid-body physics. That stands for v1.0. v0.2
adds bodies that cross whole, by centre of mass, as a named tier with a stated limit: a
body larger than the recess cannot cross until coupling exists. Recommendation: accept.
It delivers crates and props through portals without the physics facade work.

### D16. Stage-sized changes, no stacked branches

Each stage is one branch off `main`, buildable, tested and revertible. A stage that
depends on unmerged work waits for it or is folded into it.

## 6. Document map

| Document | Owns |
|---|---|
| `00-execution-overview.md` | Scope, invariants, decisions, stage order, gates, stop conditions. |
| `01-link-data-and-authoring.md` | `WorldLink` endpoints, `LinkAperture`, resolution, cook, validation, editor. |
| `02-relocation-and-character-traversal.md` | Geometry kernel, relocation operation, relocation serial, character crossing, demand. |
| `03-linked-views.md` | Runtime frame composition, linked view extraction, targets, clipping, composite. |
| `04-crossing-entities-and-rigid-bodies.md` | Uncoupled bodies, clipped representations, facade prerequisites, coupling. |
| `05-light-transport.md` | Baked direct, probes, dynamic lights and shadows through a link. |
| `06-replication-and-presentation.md` | Prediction, replication, cameras, particles, traces, navigation, perception, saves. |
| `07-validation-stress-and-profiling.md` | Fixtures, adversarial matrices, counters, benchmarks, evidence. |

## 7. Stage order

| Stage | Delivers | Document |
|---|---|---|
| 0 | Owner rulings on D2, D14, D15. Fixtures. Zero-link baselines. | `07` |
| 1 | Geometry kernel. `LinkAperture`, `WorldLink` endpoints, `ResolvedLinks`, cook, validation, authoring, debug draw. | `01`, `02` |
| 2 | Relocation operation extracted from `LevelCarry`. `RelocationSerial`. History that presents a relocation correctly. | `02` |
| 3 | Character traversal on one machine: crossing in the shared tick, straddle, refusal, focus, demand. | `02` |
| 4 | Linked views: runtime `FrameComposition`, targets, oblique clip, composite, sky per view. | `03` |
| 5 | Replicated traversal: serial on the wire, interpolation, command frames, replay. | `06` |
| 6 | Presentation and gameplay consumers: orbit camera, traces and projectiles, navigation links. | `06` |
| 6P | Particle compatibility: particles drawn per view, particle crossing, attached emitters. | `03` section 4.2, `06` section 4 |
| | **v0.1 complete** | |
| 7 | Uncoupled body transfer. Zone migration for zone-owned entities. | `04` |
| 8 | Clipped crossing representations, device clip distance. | `04` |
| | **v0.2 complete** | |
| 9 | Physics facade prerequisites. | `04` |
| 10 | Coupling investigation, two candidates, one deleted. | `04` |
| 11 | Coupled bodies. | `04` |
| 12 | Baked direct and probe transport. | `05` |
| 13 | Dynamic light and shadow transport. | `05` |
| 14 | Hardening: teardown, caps, sanitizers, sweeps, documentation. | `07` |
| | **v1.0 complete** | |

Stage 2 has no dependency on Stage 1 and may land first. It also resolves the
`docs/deferred.md` entry "Presentation consumers observing a teleport", which the next
particle phase is waiting on. Whichever of the two efforts reaches it first does it once.

The particle system is being built at the same time. Stages 1 to 6 are written to
avoid particle code, so they proceed now. Stage 6P is everything that touches it and
starts once the particle runtime is on `main`. Stage 4 leaves the particle draw bound
to the main view only, and says so in its handoff.

Stages 12 and 13 do not depend on 9 to 11 and may be reordered.

## 8. Merge gates

**Link.** A link holds exactly two references or none. An aperture has at most one link.
Unresolved because a zone is not resident is distinct from malformed. Nothing authored,
cooked or saved contains an `EntityId` or a storage partition id.

**Relocation.** One operation. `LevelCarry` calls it. Every registered component
relocates or resets; an entity holding an unregistered world-space component is refused
with its name. The serial advances exactly once per relocation.

**Traversal.** Authority, owner replay and a recorded serial run produce the same tick
and the same pose. Backing out never relocates. A refused crossing leaves the character
outside solid geometry.

**View.** With no resolved link in the visible set there is no linked view, target,
pass or allocation. View count and target bytes are capped; the cap drops the lowest
ranked view to the fallback surface and counts it. The backend never queries ECS.

**Replication.** A remote entity is never drawn between two apertures. The owner's
facing matches the authority's after a mispredicted crossing.

**Physics.** Jolt stays behind `physics_isolation`. Coupled mass and inertia are counted
once. Migration between active zones never recreates a body.

**Lighting.** No light around the aperture. Either side's occluders block. Identical
input bakes identical bytes.

**Performance.** The zero-link path is inside measured noise for CPU, GPU and
allocations. Cost follows visible linked content and active crossings, never total
resident content. `scripts/bench_particles.sh` and the render A/B bench pass without
dropped work.

## 9. Deferred, with triggers

Each is added to `docs/deferred.md` by the stage that creates it.

| Deferred | Created by | Trigger |
|---|---|---|
| Characters through non-upright links | Stage 3 | A general up-axis facing model for `LookOrientation` and the character capsule. |
| Sight and hearing through a link | Stage 6 | The first authored encounter that needs an agent to perceive through one. |
| Runtime open and close of a link | Stage 1 | The first content that gates a link. Generalise `DockGateBinding` to links, not a second gate. |
| Cross-zone navigation through a link | Stage 6 | The cross-zone planner. Links have positions from Stage 1. |
| Linked views in Kyusu viewports | Stage 4 | The first level whose layout cannot be judged from the aperture overlay and PIE. |
| Recursive views | Stage 4 | Content with two links facing each other, and a measured budget for the depth. |
| Curved-path crossing within one step | Stage 3 | The existing "Continuous sensor trajectories" trigger; solve it once there. |

Resolved by this suite: "Presentation consumers observing a teleport" (Stage 2).
Unblocked by this suite: the `Unconsidered` world links in `world.reachable` (Stage 1).

## 10. Stop conditions

Stop and ask the owner when:

1. A public SDK or game-module ABI change becomes necessary.
2. A persisted or cooked format needs a change that is not additive.
3. The relocation operation cannot serve `LevelCarry` without changing what a level
   change does.
4. A link would need to become streaming policy inside `WorldPartitionRuntime` beyond
   the demand edge it already is.
5. Straddle pinning cannot be expressed with participation leases once their gaps are
   closed.
6. Runtime `FrameComposition` adoption changes a golden image with no link present.
7. Moving endpoints, scaling, or wall cutting becomes required.
8. Jolt cannot supply contact filtering or impulse information behind backend-free
   types.
9. Neither coupling candidate clears its rubric.
10. A stage needs a lock, raw thread, `std::async` or third worker lane.
11. A test is being rescued by a wider tolerance, a sleep or a replaced snapshot.
12. A fact this suite states about `main` is no longer true.

## 11. Definition of done

v0.1 is done when a player in a two-client session walks through a same-zone and a
cross-zone link in both directions, sees the far side correctly before and after, stops
halfway and backs out, is seen by the other client arriving without a streak, fires a
projectile and a particle burst through, and is followed through by a same-zone
navigating agent; and when the zero-link baselines are unchanged, every cap has a
boundary test, and `ctest --preset dev` passes.

v0.2 is done when a crate pushed off a ledge falls through a floor-facing aperture and
lands on the far side, draws on both sides while it straddles, belongs to the far zone
afterwards, and is still there after save and load.

v1.0 is done when a long pole jams diagonally in an aperture and can be pulled back out,
a baked room lights its neighbour through the opening and nowhere else, a moving
shadowed light does the same, and the adversarial matrix and sweeps in `07` pass.
