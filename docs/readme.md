# Sencha Documentation

Sencha is a C++20 3D game engine built on an Entity Component System (ECS),
aimed at action/adventure games — specifically 3D Metroidvanias, survival
horror, and soulslikes. The architecture favors explicit systems, explicit
ownership, and a flat domain tree: dependencies are traced by hand, ownership is
visible at construction, and backends (Vulkan, SDL3) live behind opt-in
interfaces rather than leaking upward through the engine.

This directory is meant to be useful to both humans and AI agents working in the
repo. Start with the current architecture notes, then use the decision records
when you need the "why" behind a rule.

## Repository Layout

```
app/        Minimal application target.
docs/       Architecture notes, subsystem guides, and decision records.
engine/     Shared engine library: core, math, ecs, world, zone, render, graphics,
            audio, input, anim, assets, jobs, time, runtime, platform, debug.
example/    Small executable examples.
test/       GoogleTest-based engine tests.
```

## Roadmap and Plans

- `docs/plans/engine-roadmap.md` is the standing master roadmap: the v1.0
  through v3.0 version arc, gates, tracks, and sequencing for the whole
  engine. It owns versions and priorities; the specialist docs below own
  execution detail. It supersedes the former `real-engine-roadmap.md`.
- `docs/action-adventure-core-runtime.md` is the execution spec for the
  streaming and traversal track (roadmap Track C).
- `docs/gameplay/abilitykit.md` is the execution spec for the gameplay
  framework items in roadmap Track A.
- `docs/architecture/hardening-and-consolidation.md` is the execution spec
  for the editor hardening and module ABI items (roadmap Tracks D and F).
- `docs/plans/sencha-level-editor/` is the shipped-branch record and the
  execution detail for the editor substrate roadmap Track D builds on.

## Core Architecture Map

- `docs/core-systems-map.md` is the first broad map of the engine: core
  systems, ownership, dependency directions, frame flow, and constraints.
  
## ECS Docs

The ECS docs describe the implementation currently in the tree:

- `docs/ecs/overview.md` is the first read for entities, archetypes, chunks,
  queries, resources, systems, and registration.
- `docs/ecs/queries.md` is the cookbook for `Read<T>`, `Write<T>`, `With<T>`,
  `Without<T>`, `Changed<T>`, entity access, and change detection.
- `docs/ecs/command-buffers.md` explains deferred structural mutation and flush
  semantics.
- `docs/ecs/component-traits.md` explains lifecycle hooks and when to avoid
  them.
- `docs/ecs/parallelization.md` describes the landed job/async lanes, current
  runtime knobs, and the deferred chunk-query design.
- `docs/ecs/MigrationPlan.md` is now a migration record: what replaced the old
  sparse-set ECS, what remains for compatibility, and where to look in code.
- `docs/ecs/decisions.md` is the historical decision log and benchmark record.

For new ECS code, prefer the overview and cookbooks. Use the migration and
decision records when you are changing core storage, scheduling, or lifecycle
behavior.

## Asset Docs

- `docs/assets/pipeline.md` is the working plan for the asset pipeline: the
  current-state inventory, the decisions deferred from the parallelization
  work, and the staged rollout for async asset streaming.

## Audio Docs

- `docs/audio/runtime.md` is the landed plan for scene-authored audio sources.
- `docs/audio/captions-and-dialogue.md` is the working plan for subtitles,
  closed captions, semantic audio cues, and dialogue-line routing.

## Gameplay Docs

- `docs/gameplay/abilitykit.md` is the working plan for **AbilityKit**, Sencha's
  data-driven gameplay framework: tags, attributes, effects, and abilities as POD
  components + data + uniform systems — no per-entity behavior object, tags for
  mutual exclusion, events as data. Public entry point:
  `engine/include/framework/AbilityKit.h`.
- AbilityKit lives under `engine/.../framework/`, kept decoupled from the renderer
  and scene data by the `framework_isolation` check
  (`cmake/CheckFrameworkIsolation.cmake`).

## Requirements

- CMake 3.20+ (3.23+ to use the presets below)
- C++20 compiler
- Ninja (used by the presets)
- SDL3 (via `find_package(SDL3 REQUIRED CONFIG)`)
- Vulkan SDK (required — the engine does not currently build without it)

Optional: `SENCHA_ENABLE_HOT_RELOAD` (glslang), `SENCHA_ENABLE_DEBUG_UI` (ImGui).

## Build

Presets are the canonical workflow (see [building.md](building.md) for the full
list and a from-scratch / dirty-tree recovery guide):

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

Without presets (CMake < 3.23):

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

> A no-Vulkan / headless build is **not** currently supported — the render layer
> includes Vulkan headers unconditionally. See the "Future work" section of
> [cmake-build-hygiene-plan.md](cmake-build-hygiene-plan.md).

## Examples

The `example/` directory holds small executables that exercise the engine.
`CubeDemo` is the main graphical sample (requires Vulkan); the rest —
`AudioTest`, `EcsBenchmark`, `JobSystemBenchmark`, and
`TransformHierarchyStressTest` — are non-graphical.

After building, run the CubeDemo:

```sh
./build/example/CubeDemo/CubeDemo
```

## Design Notes

- Services are named members owned directly by the `Engine` (grouped into
  `PlatformServices` / `GraphicsServices` where they share a backend) — no
  service locator, no global singletons.
- Systems declare dependencies through construction, not through runtime lookup.
- Backends live behind interfaces rather than leaking upward through the engine.
- Load-time formats compile into runtime tables before hot-path use.
- ECS component data lives in archetype chunks. Structural changes are explicit
  and either direct outside query scope or deferred through `CommandBuffer`.
- Cross-frame work uses `AsyncTaskQueue`; in-frame fork/join work uses
  `JobSystem`.

That makes setup slightly more explicit but keeps ownership, dependency direction, and update order easy to inspect.
