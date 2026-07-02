# Sencha Action-Adventure Core Runtime Plan

Status: **proposed working plan** (2026-06-12). Version assignments and gates live in `docs/plans/engine-roadmap.md` (Track C).

This document captures the core-engine work that would make Sencha feel
purpose-built for large authored action-adventure games: interconnected worlds,
doorways and elevators, dense indoor spaces, outdoor vistas, backtracking,
checkpointing, boss transitions, scan-heavy rooms, and traversal paths that must
never hitch.

This is not a gameplay-systems plan. Combat, lock-on, character movement,
prefabs, animation playback, quests, inventory, and UI are adjacent consumers.
The goal here is deeper: engine-level guarantees, data contracts, telemetry, and
tooling that make those games hard to accidentally break.

The north star:

> Authored interconnected worlds should be cheap to load, cheap to remember,
> cheap to inspect, and hard to hitch.

## Product Frame

Sencha already targets Metroidvania/Zelda-like and survival-horror worlds:
room-sized zones, 2-4 live, dormant preloaded neighbors, and constant
backtracking churn. This plan keeps that target, then stretches it toward
larger action-adventure production: Dark Souls-scale layered spaces,
Metroid Prime-style room/portal visibility, outdoor approach paths, vista
proxies, boss arenas, elevators, shortcuts, checkpoints, and eventual
open-world-like cell streaming.

The distinction from a generic engine is the set of promises:

- continuous traversal must not hitch
- streaming is budgeted and measurable
- participation is more expressive than loaded/unloaded
- world state survives unload/reload without bespoke game code per feature
- transitions carry timing semantics into camera, audio, input, simulation, and
  render history
- spatial, visibility, and content-risk data are first-class runtime assets

## Current Foundation

What already exists and should be reused:

- `ZoneRuntime`: global registry plus loaded zone registries, each with
  `ZoneParticipation` flags for `Visible`, `Physics`, `Logic`, and `Audio`.
- `FrameRegistryView`: per-phase spans built from participation.
- `AsyncZoneLoader`: detached registry build on the async lane, attach/finalize
  at the owner-thread drain point.
- `AssetPreloader`: manifest-sized async residency batches, cache/in-flight
  dedup, staged load on task threads, commit at `DrainAsyncTasks`.
- `AsyncCommitBudgetMs`: the first owner-thread budget knob for streaming.
- `RuntimeFrameLoop` discontinuity flags: already names frame-history reset as
  a runtime concern.
- `JobSystem` and `ForEachRegistryParallel`: zone-axis work for many live heavy
  registries.
- `SceneSerializer`, `TypeSchema`, and schema-driven component registration:
  the base for scene, template, state overlay, and budget introspection.
- `QuadTree` and `Grid2d`: small spatial data structures, not yet promoted to
  engine-level world query or partition services.

## Non-Goals

- No gameplay lock-on, combat, character controller, item, quest, or puzzle
  systems in this plan.
- No duplicate animation or prefab plan. Those systems are expected consumers,
  but their runtime designs live elsewhere.
- No hardcoded "Dark Souls mode" or "Metroid mode" config profile. Genre enters
  as budgets, metadata, and defaults on shape-neutral mechanisms.
- No general editor database. Author-time feedback can be emitted as structured
  reports first; editor UI can consume those reports later.
- No MMO/server-streaming design. This plan serves single-player authored
  traversal first.

## Vocabulary

`Zone`
: The runtime residency atom: one `Registry` plus participation state.

`Partition cell`
: Metadata that describes where a zone belongs spatially and how it should
  stream. A cell may represent a room, hallway, terrain tile, vista proxy,
  encounter pocket, boss arena, or interior.

`Residency tier`
: A named level of runtime cost and capability, richer than loaded/unloaded.

`Traversal corridor`
: A path segment the player can move through continuously, with an expected
  minimum traversal time. Used to judge whether the next required content can
  load without a hitch.

`Transition`
: A runtime mode change such as door, elevator, respawn, fast travel, cutscene
  takeover, boss fog, camera cut, or seamless traversal.

`Content risk`
: A structured warning that authored content may violate runtime budgets or
  streaming assumptions.

## Decisions

### A. Traversal-Hitch Contract

**Proposed.**

Sencha should define a measurable contract for continuous traversal:

> During a declared continuous traversal corridor, required content must become
> resident without missed fixed ticks, without owner-thread drain overruns, and
> without synchronous fallback loads on activation.

The contract is enforced by data and tests, not by hope:

- Each partition cell emits a `ZoneBudgetRecord`: asset bytes, decoded bytes,
  GPU upload bytes, build time, finalize time, commit time, entity count,
  render item count, audio clip bytes, and worst asset contributors.
- Each traversal edge declares an expected minimum traversal time, or derives
  one from authored distance and configured player speed.
- A preflight pass estimates whether the load path has enough time under the
  configured async thread count and commit budget.
- Runtime telemetry records actual load/commit timings, missed fixed ticks,
  sync fallback loads, and activation wait time.
- A test harness can replay a traversal script and assert the contract.

This makes `AsyncZoneLoader`, `AssetPreloader`, and `AsyncCommitBudgetMs` into
a product guarantee: "walking through the world does not hitch."

Deferred until needed: predictive IO scheduling, platform-specific bandwidth
models, and transfer-queue upload. The first version can be conservative and
measurement-driven.

### B. World Partition As Core Runtime

**Proposed.**

Add a runtime layer above `ZoneRuntime`, tentatively `WorldPartitionRuntime`.
It owns metadata and policy; `ZoneRuntime` remains the registry owner.

The partition layer should know:

- stable partition cell ids
- zone ids and scene/manifest refs
- local bounds and optional world bounds
- adjacency edges
- portal links
- traversal corridors
- streaming priority hints
- indoor/outdoor tags for policy, not hardcoded behavior
- vista/proxy/full-zone relationships
- authored preload requirements

It decides what should be loaded, dormant, visible, simulated, audible, or
unloaded. It does not deserialize scenes itself; it issues requests to the
existing async zone and asset surfaces.

The key rule: partition policy is game-extensible, but the data model is engine
standard. Dark corridors, doors, elevators, terrain grids, shortcuts, and boss
fog all become different metadata over the same residency machinery.

### C. Participation LOD

**Proposed.**

`ZoneParticipation` is already the seed. Grow it into a richer residency model
without losing the simple per-phase spans.

Candidate tiers:

- `Unloaded`: no registry, metadata only
- `Requested`: load queued, no runtime data yet
- `AssetsResident`: manifest assets retained, registry not attached
- `Dormant`: attached, absent from all frame spans
- `VisibleProxy`: cheap render proxy only
- `VisibleFull`: full render extraction
- `PhysicsShell`: static collision/query surfaces active
- `LogicCold`: low-frequency or event-only logic eligible
- `LogicHot`: normal fixed/update logic active
- `AudioAmbient`: room ambience active
- `AudioFull`: emitters and captions active
- `Critical`: transition/boss/cutscene content pinned against eviction

Implementation should preserve the current fast frame shape: each frame builds
spans for systems to consume. Rich tiers compile down to spans and flags, not
branchy string policies inside hot systems.

This is how spacious outdoor scenes stay affordable: nearby terrain can be
visible, collision can exist only around the player, distant landmarks can be
proxies, and encounter logic can stay cold until activation.

### D. Author-Time Budget Feedback

**Proposed.**

Budget feedback should be generated from the same data runtime uses. A command
line or build step can produce structured reports before an editor UI exists.

First reports:

- per-zone entity, component, render-item, material, texture, mesh, audio, and
  caption counts
- asset byte totals and worst contributors
- estimated staged-load time and commit time
- traversal-corridor pass/fail against configured budgets
- sync-fallback risk: refs present in scene but missing from manifest
- resident memory for rings of adjacent cells
- proxy coverage: zones visible from afar with no proxy/HLOD
- state overlay size estimate
- per-transition activation set

Output format should be machine-readable JSON plus a human-readable summary.
The future editor and debug UI consume the same records.

The rule: content authors should learn about a hitch risk when they save or
cook content, not when a playtester crosses a doorway.

### E. Stateful Streaming

**Proposed.**

Zone loading without zone memory is not enough for action-adventure worlds.
Backtracking means the world remembers.

Add the inverse of attach:

```text
DetachZone(zone)
  remove from frame spans
  move Registry ownership out of ZoneRuntime
  hand sole ownership to async task
  serialize state overlay or full snapshot off-thread
  publish compact ZoneStateRecord
```

State should be represented as an overlay against authored scene data:

- stable entity identity, eventually coordinated with `AssetId`/scene ids
- created/destroyed entity records
- changed serialized component fields
- lightweight runtime state for systems that opt in
- reset-to-authored support
- checkpoint and rest-point grouping

The first consumer is simple: doors, pickups, puzzle flags, defeated enemies,
and destructibles. The engine-level requirement is broader: unload must not
mean forget, and saving state must not block traversal.

### F. Transition-Aware Frame Loop

**Proposed.**

`TemporalDiscontinuityReason` should become part of a broader transition model.
Different transitions have different rules for history, budgets, input, audio,
and camera behavior.

Candidate transition kinds:

- continuous traversal
- portal/door transition
- elevator or lift
- camera cut
- cutscene takeover
- boss fog entry
- respawn
- fast travel
- checkpoint restore
- debug teleport
- initial spawn

Each transition can declare:

- interpolation history reset policy
- camera history reset or blend policy
- async drain tolerance for this frame range
- required pinned zones
- audio fade/sweep policy
- input buffering behavior
- fixed-tick scheduling behavior
- telemetry label

This should integrate with `RuntimeFrameLoop`, not bypass it. The frame loop is
already the place that owns lifecycle, fixed ticks, presentation history, and
discontinuity records.

### G. Action-Grade Timing Infrastructure

**Proposed.**

Action-adventure feel depends on timing trust. The engine should provide the
substrate that lets animation, input, combat, and camera agree without each
system inventing its own clock rules.

Core timing surfaces:

- timestamped input edges
- fixed-tick input consumption with configurable buffering windows
- presentation-to-fixed mapping helpers
- deterministic event ordering inside a frame
- frame-accurate authored event queues
- late-input-to-next-fixed-tick policy
- transition-aware input suppression or carry-over
- per-zone event delivery rules when a zone activates mid-frame
- debug trace for "why did this input/event fire on this tick?"

This is not an animation event system by itself. It is the timing contract that
animation events, hit windows, interaction prompts, camera transitions, and
audio stingers should use.

### H. Spatial Query Layer As Core

**Proposed.**

Promote spatial queries to an engine-level service instead of leaving every
consumer to build its own broadphase.

The query layer should support:

- per-zone static query data
- per-zone dynamic broadphase
- cross-zone query stitching
- ray, overlap, shape cast, and nearest queries
- query layers and masks
- stable hit records that include registry, zone, entity, component, distance,
  normal, and material/surface metadata
- async-built static acceleration for loaded scenes
- debug drawing and trace logs

Consumers include camera obstruction, interactions, lock target visibility,
melee traces, projectile sweeps, audio obstruction, foot placement, trigger
volumes, streaming volumes, and editor selection.

The first implementation can be intentionally modest: AABB broadphase plus ray
helpers over static and dynamic query components. The important part is the
engine-level contract: spatial questions are zone-aware and uniform.

### I. Camera And Visibility Infrastructure

**Proposed.**

Do not put gameplay camera behaviors in this plan. Do add the runtime data that
camera-heavy authored worlds need.

Core visibility features:

- portal and sector metadata
- precomputed visibility sets per indoor cell
- outdoor visibility radii and vista proxies
- HLOD/impostor hooks
- per-zone render-proxy assets
- camera obstruction query acceleration
- visibility debug views
- "visible but not interactive" participation path

This is especially important for Metroid Prime-like spaces: authored rooms,
scan vistas, portals, expensive reveals, and strong sightline control. It also
helps Dark Souls-like outdoor layering, where distant landmarks sell the world
but should not imply full simulation.

Render extraction should continue to consume copied render-domain data. The
visibility layer decides which registries/proxies enter extraction; the renderer
should not reach back into live ECS state.

### J. Content Risk Dashboard

**Proposed.**

Sencha should have a single runtime/debug surface for content risk. It is not an
editor feature first; it is an engine observability feature.

Minimum dashboard data:

```text
Current partition cell
Live zones by residency tier
Dormant zones
Pending loads
Pinned transition content
Async work queue depth
Commit budget used this frame
Worst recent commit
Sync fallback load count
Missed fixed ticks during traversal
Resident asset bytes by type
State overlay bytes
Visible proxy misses
Spatial query hot spots
Camera/visibility rejected zones
```

Every warning should link back to a structured content risk record. A future
editor panel can browse the same records, but the runtime must be useful on its
own.

## Rollout

Ordered by leverage and by how much each stage validates the next.

### Stage 1 - Metrics And Risk Records

Add the shared record types and collectors:

- `ZoneBudgetRecord`
- `TraversalBudgetRecord`
- `StreamingTelemetryRecord`
- `ContentRiskRecord`
- JSON summary writer

Gate: CubeDemo or a test scene emits a budget/risk report that names zone
entity counts, asset refs, preload status, and actual async commit timings.

### Stage 2 - Traversal-Hitch Harness

Add a headless or llvmpipe-friendly traversal test runner:

- scripted zone activation path
- async preload requests
- fixed-tick miss detection
- sync fallback load detection
- drain overrun detection

Gate: a multi-zone fixture proves dormant preload plus activation can run with
zero missed fixed ticks under the configured budget.

### Stage 3 - World Partition Metadata

Add the data model, loader, and runtime policy shell:

- partition cell ids and bounds
- zone/scene/manifest refs
- adjacency and traversal edges
- initial policy that maps distance/edge preload to zone requests

Gate: game code can ask the partition runtime to update around a player
position and it drives `AsyncZoneLoader` plus participation changes without
direct zone-id scripting.

### Stage 4 - Participation LOD

Extend participation into tiers while preserving the frame spans.

Gate: a fixture has full, proxy, dormant, physics-only, and logic-hot zones;
the built frame view includes exactly the registries expected for each phase.

### Stage 5 - Stateful Detach

Implement zone detach by ownership handoff and async state capture.

Gate: a zone changes state, detaches, serializes an overlay off-thread, reloads,
and restores the change without blocking the frame.

### Stage 6 - Transition Model

Replace ad hoc discontinuity calls with typed transition scopes.

Gate: camera cut, door transition, and debug teleport produce distinct runtime
records and apply different history/input/streaming policies in tests.

### Stage 7 - Spatial Query Runtime

Add the first zone-aware query service and debug traces.

Gate: cross-zone ray and overlap queries return stable hit records, and camera
obstruction/interactable-style tests can share the same API.

### Stage 8 - Visibility And Proxy Path

Add sector/portal/vista metadata and proxy extraction hooks.

Gate: a room/portal fixture and an outdoor-vista fixture prove visible proxy
content can render without activating full logic/physics/audio zones.

### Stage 9 - Dashboard

Expose the live risk and telemetry panel through debug UI and log snapshots.

Gate: during a streaming traversal, the dashboard shows live zones, dormant
zones, queue depth, commit budget use, worst assets, and active risk warnings.

## Open Questions

- Should partition metadata live in a new `.sworld` asset, beside scenes, or as
  a higher-level JSON that references scene assets?
- Should residency tiers be a single enum, a bitset of capabilities, or a small
  value type that compiles to current `FrameRegistryView` spans?
- What is the first stable entity identity scheme for state overlays before the
  editor/AssetId work lands?
- Does transition policy belong in `RuntimeFrameLoop`, a new runtime service, or
  a narrow layer that writes into the existing frame loop?
- Should static spatial query data be cooked from render meshes first, or wait
  for explicit collision-shape assets?
- How much author-time budget feedback should fail a build versus warn?

## Architectural Smells

Be suspicious when new code:

- puts streaming policy directly into `ZoneRuntime`
- treats loaded/unloaded as the only meaningful residency states
- resolves assets synchronously during zone activation without recording a risk
- serializes zone state on the owner thread during traversal
- builds camera, physics, interaction, and AI spatial queries independently
- hides transition semantics inside gameplay systems instead of frame/runtime
  records
- makes debug UI the only source of content-risk data
- adds genre-named strategy strings where budgets or metadata would be clearer

