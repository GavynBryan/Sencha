# Sencha

Sencha is a C++20 game engine project built around explicit systems, explicit services, and a flat engine tree organized by domain.

It is not trying to hide the machinery. Services are registered by hand, systems receive what they need through visible construction paths, and backend implementation code stays behind opt-in interfaces. If a piece of code depends on something, the goal is that you can trace the relationship directly.

Sencha is best enjoyed without added sweeteners.

### ECS Stance

Sencha is a **hybrid, data-oriented engine, not a purist ECS**. There is no god-world, no `EntityId`, no `IComponent` base class, no virtual `Update()`, and no central registry that owns all gameplay state. Contributors should not add any of those.

The primitives are:

- **`DataBatch<T>`** — a dense, cache-friendly store for one component type. Anyone can own one.
- **`DataBatchKey`** — a stable key that cross-references slots between batches.
- **`DataBatchHandle<T>`** — RAII ownership of a single batch slot. When it drops, the slot frees.
- **`TransformSpace<T>`** — a self-contained transform space (batches + hierarchy + propagation cache). `World` owns one; UI, editor, or any other subsystem can own its own.
- **Systems** — plain types that implement `Update(float)`, `Tick(float)`, and/or `Render(float)`. `SystemHost` detects capabilities via concepts and dispatches without a base class or vtable.

Gameplay authoring happens in two modes, both built on the same primitives:

1. **Programmer-authored:** plain C++ structs compose `TransformNode` and other handles by value, hold their own state, and expose their own non-virtual `Update(dt)` called by whoever owns the container. No registry, no framework.
2. **Data-authored (editor/prefab):** a component-type registry maps serialized blobs into batch slots. A spawned prefab is a RAII handle bundle that owns one slot per component. The same batches and systems back both paths.

Ticking happens in systems that sweep batches — never through virtual dispatch on individual game objects. Cold components (e.g. a `PlayerCharacterController` with one instance) are just small batches with their own small system; the engine does not distinguish "hot" and "cold" components structurally.

When in doubt: **compose, don't inherit. Own by value, not by pointer. Sweep batches, don't dispatch virtuals.** If a change would require adding a virtual method to an engine primitive, it's solving the wrong problem.

## Repository Layout
```text
app/       Minimal application target.
docs/      Project notes and secondary documentation.
engine/ Shared engine library: core services, math, transform, assets, window/input, graphics.
Sencha.2D/   2D engine library: 2D physics, sprite/tilemap rendering, tilemap data.
engine/   3D engine library scaffold.
example/   Small executable examples.
test/      GoogleTest-based engine tests.
```

## Documentation

| Document | Description |
|---|---|
| [docs/shaders.md](shaders.md) | Shader authoring, build pipeline, metadata format, hot-reload, and planned tiers. |
| [docs/grid.md](grid.md) | `Grid2d<T>` setup, storage model, idiomatic usage, constraints, and its role in the tilemap pipeline. |
| [docs/transform.md](transform.md) | Transform value types (`Transform2d`, `Transform3d`), hierarchy propagation, transform systems, and domain structure |
| [docs/render.md](render.md) | `Renderer` architecture, render feature system, Vulkan graphics integration, and rendering pipeline overview |
| [docs/data.md](data.md) | Data containers, batch storage, serialization, and memory management |
| [docs/audio.md](audio.md) | Audio system design, integration, and usage examples |
| [docs/entity.md](entity.md) | `EntityBatch<T>`, `EntityRegistry`, `EntityKey`, subtree destruction, and the `IsEntity` concept. |
| [docs/physics.md](physics.md) | 2D physics: `PhysicsDomain2D`, `ColliderSyncSystem2D`, quadtree broadphase, spatial queries, and move-and-slide. |
| [docs/time.md](time.md) | `TimeService` timing model, per-frame timing, timescale, delta clamping, and constraints. |

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
