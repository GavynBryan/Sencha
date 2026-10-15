# Perception v1

Date: 2026-10-14. Status: stages A-D built; hearing (E) is next. Scope: sight,
the observation contract, diagnostics with world drawing, and the authored read
path. Hearing shapes the contract but is not built here.

Perception answers one question: what can this entity acquire from the world
right now? It does not remember, believe, judge hostility, or pick behavior.
Sensors detect, observations communicate, memory retains, cognition
interprets, and no entity has to take part in every stage.

## Ownership

| Piece | Owner | Layer |
| --- | --- | --- |
| Range, cone, ray (`TestSight`, `TestSightGeometry`) | `spatial/sight/SightTest` | spatial |
| Where an eye is and what it reaches (`SightSensor`, `SightTarget`) | `spatial/sight/SightComponents` | spatial |
| Eye and sample point in the world (`PlaceSightObserver`, `PlaceSightTarget`) | `spatial/sight/SightPlacement` | spatial |
| One tick's evidence (`Observation`, `PerceptionFrame`) | `perception/` | gameplay substrate |
| Running the senses each fixed tick (`PerceptionSystem`) | `perception/` | gameplay substrate |
| Authored questions about the frame (`perception.*` queries) | `perception/PerceptionQueries` | gameplay substrate |
| Lines a simulation system wants shown (`DebugLineChannels`) | `debug/DebugLines` | neutral, no render |
| Lines in render form, the pass, the feature | `render/debug`, `graphics/vulkan/DebugLinePass`, `render/feature/DebugLineRenderFeature` | render |

The sight components live beside the sight test, not in `perception/`,
because candidate evaluation reads them too. An entity's authored eye is one
fact with two readers. The `visibility` and `route_visibility` measures use an
observer's `SightSensor` and a target's `SightTarget` when the entity has
them. Their `eye_height`, `target_height`, `half_angle_degrees` and `range`
arguments only describe eyes that have no entity behind them, such as grid
points.

## Components

`SightSensor { Range, HalfAngle, EyeOffset }`. The half-angle is stored in
radians and authored in degrees. The eye faces the entity's world forward axis
(-Z). A sensor is usable when the range is positive and finite, the half-angle
is in (0, pi], and the offset is finite. An unusable sensor is counted in the
diagnostics and sees nothing.

`SightTarget { SampleOffset }`. A target needs a `WorldTransform` and nothing
else: no sensor and no body. An observer never sees itself.

Both are plain values, so removing either removes the capability on the next
scan. Neither implies hostility, importance, memory, or any AI component.

## Observation contract

```
Observation { Observer, Sense (GameplayTagId), Subject, Position, Tick, Confidence }
```

- `Subject` may be invalid for an anonymous stimulus (hearing).
- `Position` is where the subject was observed, captured at scan time.
  Consumers do not resolve `Subject` to a current transform and call that the
  observed position.
- `Confidence` is in [0, 1]. Sight reports 1. Hearing will report attenuation.
- Sense ids are gameplay tags (`perception.sense.sight`). Observations are
  never serialized, so registration-order tag ids are safe here.

`PerceptionFrame` is a World resource holding the current tick's
observations. Rows are grouped by observer and sense in ascending `EntityId`
order, so the result does not depend on chunk layout. Spans into it are valid
until the next tick's `Begin`; anything kept longer is copied out.

Coverage is per observer and sense: `Complete`, `Partial` (the work cap
stopped the scan) or `NotEvaluated` (no row for that pair). A missing
observation proves absence only when coverage is `Complete`.

## Scheduling and freshness

Each fixed tick runs FixedLogic, then the event drain, then Physics, then
transform propagation, then PostFixed. `PerceptionSystem` runs in FixedLogic,
so at tick N it reads `WorldTransform` and the physics broadphase as tick N-1
left them. The frame is stamped N, and it describes that completed snapshot.
Anything that reads observations in FixedLogic, or moves bodies in the physics
world directly, declares `After<X, PerceptionSystem>`.

The frame lives within one tick, so zone residency, which runs before ticks,
can never leave stale rows readable as current evidence. Events drained after
FixedLogic N become stimuli that perception reads at N+1. That fixes the
hearing tick without any same-tick ordering question.

A game opts in with `RegisterPerception(schedule, console)` after
`RegisterPhysics`; the FPS template does. Sight needs physics; without it there is no occlusion
answer, so registration asserts physics is present. A game that never
registers perception runs no perception work. A registered system with no
sensors finds an empty observer list and stops.

Only the authority senses (`IsSimulationAuthority`). A client's frame stays
empty, which reads as `NotEvaluated` everywhere and never as absence.

## Work limits

Range and cone are rejected before any ray. Rays are capped per observer
(`perception.sight.rays_per_observer`). A shared cap walked in id order would
starve the same high-id observers on every tick, permanently. A per-observer
cap needs no multi-tick scheduler. An observer that hits its cap is `Partial`,
and the candidates it never tested produce no observation.

Targets are gathered once per tick from the logic partitions and sorted by
id. Scratch storage is retained, so a steady state does not allocate. A
spatial index is not built until a measured encounter exceeds the ~1 ms gate.

## Authored read path

Engine-declared queries, installed with the rest of the engine's vocabulary so
editors validate them without a running game:

| Query | Arguments | Result |
| --- | --- | --- |
| `perception.sees` | observer, subject | bool |
| `perception.sees_tagged` | observer, tag | bool |
| `perception.nearest_seen_tagged` | observer, tag | entity |

They answer for sight alone: another sense's observations and coverage never
change them, and queries spanning senses will be separate names. "Seen"
answers true. It answers false only when the observer's sight scan this tick
is `Complete`. A partial or missing scan answers `Unavailable`,
which the authored contract already defines as "does not apply here", never
false. `nearest_seen_tagged` answers `Unavailable` when nothing matching was
seen. "Tagged" matches the subject's `GameplayTagContainer` hierarchically.
The queries are bound to the World's `PerceptionFrame`, which exists whether
or not a game registered perception. A game without perception therefore gets
`Unavailable`, not an unanswered query.

Candidate evaluation reaches the same answers through its `authored_query`
measure. That makes "of the things I can see, pick one" a definition, not
code.

## Diagnostics

`PerceptionStats` per tick: sensors, unusable sensors, candidates, rejected
by range, rejected by cone, blocked, seen, rays, partial scans.

World drawing goes through a general debug-line path that any simulation
system can feed:

1. A producer owns a named channel in the `DebugLineChannels` World resource.
   It clears and refills the channel when it has new state. Channels persist
   across frames, so a frame that runs no tick still shows the last tick, and
   a catch-up burst shows its final tick.
2. `DefaultRenderPipeline::ExtractRender` copies enabled channels into a
   render-domain `DebugLineFrame` (`debug.draw`, off by default).
3. `DebugLineRenderFeature` (MainColor, after meshes) records
   `DebugLinePass`. Depth-tested lines are drawn first, then on-top lines,
   both from per-frame scratch. The backend reads only the extracted frame.

The debug-line path is described in `docs/renderer/features-and-passes.md`
(`DebugLineRenderFeature`).

Perception's channel (`perception.debug.draw`) shows each sensor's eye,
facing and cone. For the observer selected in the Perception debug panel it
also draws a green line to each seen sample, a red line to each blocking hit
with the occluded remainder dimmed, and a marker at each observed position.
The Perception panel in the debug overlay lists the counters and every
observer with its coverage, and for the selected observer each candidate's
outcome. That is enough to explain why any target was or was not seen.

## Lifecycle and networking

- Observations only name entities alive at scan time, and the frame is
  rebuilt every tick.
- Removing a sensor or target, destroying an entity, or unloading its zone
  takes effect on the next scan.
- `PerceptionFrame` is not replicated. Decisions made on observations reach
  peers through movement, AbilityKit and replication, as any authoritative
  decision does.
- Durable memory, when it exists, keys on `PersistentEntityId` and stable
  vocabulary, never on runtime ids.

## Stages

A. Built. Sight primitives (from eqs-core), `EntityId` ordering, sight
   components and placement; candidate measures read the components.
B. Built. Observation runtime: `Observation`, `PerceptionFrame`,
   `PerceptionSystem`, `RegisterPerception`, sense tag, work cap, stats.
C. Built. Authored queries, declared through `InstallEngineVocabulary` and
   bound by the engine.
D. Built. Debug lines (channels, extraction, pass, feature) on the
   colored-vertex pipeline the editor shares, perception drawing, and the
   Perception panel.
E. Hearing: `HearingSensor`, stimuli staged from authored events by game
   adapters, observations through the same frame. Separate follow-up.

## Not in v1

Memory, appraisal, belief, alertness, behavior selection, communication,
prediction, multiple sample points, lighting-aware concealment, sound
propagation, parallel or multi-tick scanning, a sense registration framework,
and a spatial index. Each waits for a real consumer or a measurement. None is
excluded by the contract.
