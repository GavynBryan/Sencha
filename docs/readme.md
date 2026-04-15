# Sencha

Sencha is a C++20 game engine project built around explicit systems, explicit services, and a flat engine tree organized by domain.

It is not trying to hide the machinery. Services are registered by hand, systems receive what they need through visible construction paths, and backend implementation code stays behind opt-in interfaces. If a piece of code depends on something, the goal is that you can trace the relationship directly.

Sencha is best enjoyed without added sweeteners.

## Current Status

Sencha is early and actively changing. The repository currently contains:

- Core service and system hosting.
- Logging with console and file sinks.
- RAII lifetime handles for attach/detach ownership patterns.
- Dense batch containers for referenced and owned data.
- Event buffers.
- A lightweight JSON DOM and parser for load-time config.
- Binary serialization helpers.
- An input mapping pipeline that compiles JSON config into runtime binding tables.
- Basic math, identity, and window abstraction types.
- SDL3-backed window, video, and input ingestion services.
- Vulkan bootstrap services for instance, device, queues, surface, swapchain, and frames.
- Vulkan resource caches and helpers: allocator, buffer, image, sampler, shader, pipeline, descriptor, upload context, per-frame scratch, and barrier utilities.
- A `Renderer` host with pluggable render features, starting with an immediate-mode `SpriteFeature` for batched sprite draws.
- A `World` domain covering transform hierarchy propagation, reusable transform nodes, a 2D tilemap, and an entity registry with typed `EntityBatch<T>` containers and subtree destruction.
- GoogleTest coverage across core, math/geometry, world, and render code.
- Examples for input mapping, SDL/Vulkan window startup, transform hierarchy stress testing, and a `JasmineGreen` sprite sample.

The engine is still more framework than finished product. APIs may move while the foundation settles.

## Architecture

Sencha is organized under one public include root and one source root:

```text
engine/
  include/
    core/
    input/
    math/
      geometry/
    render/
      backend/vulkan/
      features/
    window/
    world/
      entity/
      tilemap/
      transform/
  src/
    (mirrors include/)
```

Core contains bootstrap infrastructure. Math carries reusable value types and geometry. Input and window own their public API plus SDL-backed implementations. Render is split into `backend/` for backend-specific services (currently Vulkan) and `features/` for pluggable draw features driven by the `Renderer`. World holds engine-facing gameplay-adjacent systems: transform domains, hierarchy propagation, reusable transform nodes, and tilemaps.

### Core

Core contains:

- `ServiceHost`, `ServiceProvider`, and `IService` for explicit service registration and lookup.
- `SystemHost` and `ISystem` for ordered system execution.
- `LoggingProvider`, `Logger`, `ConsoleLogSink`, `FileLogSink`, and `LogLevel`.
- `LifetimeHandle<T, KeyT>`, `DataBatchHandle<T>`, `InstanceRegistryHandle<T>`, and `ILifetimeOwner` for typed RAII attach/detach lifetimes.
- `InstanceRegistry<T>` for tracking externally-owned instances of a type with O(1) swap-and-pop removal.
- `DataBatch<T>` for owning dense, cache-friendly vectors of data.
- `EventBuffer<T>` for frame-local event accumulation.
- `JsonParser` and `JsonValue` for load-time configuration.
- `BinaryReader`, `BinaryWriter`, and serialization helper functions.

Core is intentionally structural. It gives applications a small set of primitives, then gets out of the way.

### Math And Geometry

Math and geometry contain backend-agnostic game-facing value types such as vectors, transforms, matrices, quaternions, rays, bounds, and frustums.

### Input, Window, Render

Current integrations include:

- SDL3 video and window services.
- SDL3 input ingestion and control resolution.
- Vulkan bootstrap: instance, physical device, logical device, queues, surface, swapchain, and frame services.
- Vulkan resource layer: allocator, buffer, image, sampler cache, shader cache, pipeline cache, descriptor cache, upload context, per-frame scratch ring, and barrier helpers.
- A `Renderer` that owns render features and drives per-frame work. `SpriteFeature` provides immediate-mode batched sprite rendering on top of the Vulkan backend.

### World

World groups engine features that sit on top of the smaller domains:

- `world/transform` — `TransformDomain` (a self-contained transform space), `TransformStore`, `TransformHierarchyService`, propagation order/system, and `TransformNode` for rule-of-zero hierarchy participation.
- `world/entity` — `EntityRegistry`, `EntityBatch<T>`, `EntityKey`, and `EntityRecord` for stable entity identity, cross-type destroy routing, and subtree destruction.
- `world/tilemap` — `Tilemap2d` for 2D tile grids.

### ECS Stance

Sencha is a **hybrid, data-oriented engine, not a purist ECS**. There is no god-world, no `EntityId`, no `IComponent` base class, no virtual `Update()`, and no central registry that owns all gameplay state. Contributors should not add any of those.

The primitives are:

- **`DataBatch<T>`** — a dense, cache-friendly store for one component type. Anyone can own one.
- **`DataBatchKey`** — a stable key that cross-references slots between batches.
- **`DataBatchHandle<T>`** — RAII ownership of a single batch slot. When it drops, the slot frees.
- **`TransformDomain<T>`** — a self-contained transform space (batches + hierarchy + propagation cache). `World` owns one; UI, editor, or any other subsystem can own its own.
- **`ISystem`** — a function that sweeps one or more batches in a defined `SystemPhase`.

Gameplay authoring happens in two modes, both built on the same primitives:

1. **Programmer-authored:** plain C++ structs compose `TransformNode` and other handles by value, hold their own state, and expose their own non-virtual `Update(dt)` called by whoever owns the container. No registry, no framework.
2. **Data-authored (editor/prefab):** a component-type registry maps serialized blobs into batch slots. A spawned prefab is a RAII handle bundle that owns one slot per component. The same batches and systems back both paths.

Ticking happens in systems that sweep batches — never through virtual dispatch on individual game objects. Cold components (e.g. a `PlayerCharacterController` with one instance) are just small batches with their own small system; the engine does not distinguish "hot" and "cold" components structurally.

When in doubt: **compose, don't inherit. Own by value, not by pointer. Sweep batches, don't dispatch virtuals.** If a change would require adding a virtual method to an engine primitive, it's solving the wrong problem.

## Repository Layout
```text
app/       Minimal application target.
docs/      Project notes and secondary documentation.
engine/    Flat include/src engine tree.
example/   Small executable examples.
test/      GoogleTest-based engine tests.
```

## Documentation

| Document | Description |
|---|---|
| [docs/shaders.md](shaders.md) | Shader authoring, build pipeline, metadata format, hot-reload, and planned tiers. |
| [docs/grid.md](grid.md) | `Grid2d<T>` setup, storage model, idiomatic usage, constraints, and its role in the tilemap pipeline. |
| [docs/transform.md](transform.md) | Transform value types (`Transform2d`, `Transform3d`), hierarchy propagation, transform systems, and domain structure |
| [docs/render.md](render.md) | `Renderer` architecture, render feature system, Vulkan backend integration, and rendering pipeline overview |
| [docs/data.md](data.md) | Data containers, batch storage, serialization, and memory management |
| [docs/audio.md](audio.md) | Audio system design, integration, and usage examples |
| [docs/entity.md](entity.md) | `EntityBatch<T>`, `EntityRegistry`, `EntityKey`, subtree destruction, and the `IsEntity` concept. |

## Requirements

- CMake 3.20 or newer.
- A C++20 compiler.
- SDL3 with a CMake package available to `find_package(SDL3 REQUIRED CONFIG)`.
- Vulkan SDK when `SENCHA_ENABLE_VULKAN` is enabled.
- Git and network access on first configure so CMake can fetch GoogleTest.

Vulkan support is enabled by default. Disable it if you only want the non-Vulkan engine and input examples.

## Build

Configure and build:

```powershell
cmake -S . -B build
cmake --build build
```

Configure without Vulkan:

```powershell
cmake -S . -B build -DSENCHA_ENABLE_VULKAN=OFF
cmake --build build
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

## Examples

After building, the example executables are placed under the CMake build tree.

Input mapping example:

```powershell
.\build\example\SenchaInputSystem\SenchaInputSystem.exe
```

Transform hierarchy propagation stress test:

```powershell
.\build\example\TransformHierarchyStressTest\TransformHierarchyStressTest.exe
```

SDL/Vulkan window example, when Vulkan is enabled:

```powershell
.\build\example\SdlVulkanWindow\SdlVulkanWindow.exe
```

JasmineGreen sprite example, when Vulkan is enabled:

```powershell
.\build\example\JasmineGreen\JasmineGreen.exe
```

## Design Notes

Sencha favors visible wiring over hidden runtime behavior:

- Services are ordinary objects owned by a host.
- Systems declare dependencies through construction.
- Logging categories are tied to requesting types.
- Load-time formats compile into runtime tables before hot-path use.
- Backends live behind interfaces instead of leaking upward through the whole engine.

That makes the code a little more explicit at setup time, but it keeps ownership, dependency direction, and update order easy to inspect.
