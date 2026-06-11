# Sencha Documentation

Sencha is a C++20 game engine built around explicit systems, explicit services,
and a flat domain tree. Dependencies are traced by hand, ownership is visible at
construction, and backends live behind opt-in interfaces.

This directory is meant to be useful to both humans and AI agents working in the
repo. Start with the current architecture notes, then use the decision records
when you need the "why" behind a rule.

## Repository Layout

```
app/        Minimal application target.
docs/       Architecture notes, subsystem guides, and decision records.
engine/     Shared engine library: core, math, world, render, graphics, audio, input, time, runtime.
example/    Small executable examples.
test/       GoogleTest-based engine tests.
```

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

## Requirements

- CMake 3.20+
- C++20 compiler
- SDL3 (via `find_package(SDL3 REQUIRED CONFIG)`)
- Vulkan SDK (when `SENCHA_ENABLE_VULKAN` is on — default)

Optional: `SENCHA_ENABLE_HOT_RELOAD` (glslang), `SENCHA_ENABLE_DEBUG_UI` (ImGui).

## Build

```powershell
cmake -S . -B build
cmake --build build
```

Without Vulkan:

```powershell
cmake -S . -B build -DSENCHA_ENABLE_VULKAN=OFF
cmake --build build
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

## Examples

After building, run the JasmineGreen sprite example (requires Vulkan):

```powershell
.\build\example\JasmineGreen\JasmineGreen.exe
```

## Design Notes

- Services are ordinary objects owned by a host — no service locator, no global singletons.
- Systems declare dependencies through construction, not through runtime lookup.
- Backends live behind interfaces rather than leaking upward through the engine.
- Load-time formats compile into runtime tables before hot-path use.
- ECS component data lives in archetype chunks. Structural changes are explicit
  and either direct outside query scope or deferred through `CommandBuffer`.
- Cross-frame work uses `AsyncTaskQueue`; in-frame fork/join work uses
  `JobSystem`.

That makes setup slightly more explicit but keeps ownership, dependency direction, and update order easy to inspect.
